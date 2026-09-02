#include "pde.hpp"
#include "../internal.hpp"
#include "../runtime/lua.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

/// \file
/// Solve scalar parabolic PDEs with model functions supplied by Lua
/// C++ builds the grid, advances each field, and samples the requested point
namespace m1 {
namespace {

struct Axis {
    // Node coordinates and non-uniform three-point derivative stencils
    std::vector<double> point;
    std::vector<double> lower_first;
    std::vector<double> middle_first;
    std::vector<double> upper_first;
    std::vector<double> lower_second;
    std::vector<double> middle_second;
    std::vector<double> upper_second;
    double minimum_spacing = 0.0;
};

struct Boundary {
    // Sides are left, right, lower, upper and values are indexed along each
    // side
    std::array<std::vector<PdeBoundaryKind>, 4U> kind;
    std::array<std::vector<double>, 4U> value;
};

struct DirectionFactors {
    // Reuse these factors until any boundary kind changes
    std::vector<double> lower;
    std::vector<double> diagonal;
    std::vector<double> upper;
    std::vector<PdeBoundaryKind> boundary_kind;
};

struct Field {
    // Stage buffers keep the Craig-Sneyd update allocation-free inside the time
    // loop
    std::vector<PdeCoefficients> coefficient;
    std::vector<double> value;
    std::vector<double> stage_a;
    std::vector<double> stage_b;
    std::vector<double> stage_c;
    std::vector<double> scratch;
    std::vector<double> mixed_base;
    Boundary boundary;
    std::array<DirectionFactors, 2U> factor;
};

[[nodiscard]] std::size_t checked_product(const std::size_t left,
                                          const std::size_t right,
                                          const char *what) {
    // All grid-sized allocations pass here so a wrapped size cannot reach
    // vector::resize
    if (left == 0U || right > std::numeric_limits<std::size_t>::max() / left) {
        throw std::runtime_error(std::string("PDE ") + what + " is too large");
    }
    return left * right;
}

[[nodiscard]] Axis make_axis(const std::size_t count, const double minimum,
                             const double maximum, const double focus,
                             const double scale, const char *name) {
    if (count < 3U || !std::isfinite(minimum) || !std::isfinite(maximum) ||
        !(maximum > minimum) || !std::isfinite(focus) ||
        !std::isfinite(scale) || scale < 0.0) {
        throw std::runtime_error(std::string("invalid PDE ") + name + " grid");
    }

    // A zero scale selects a uniform grid, otherwise asinh clusters nodes at
    // focus
    Axis axis;
    axis.point.resize(count);
    if (scale == 0.0) {
        const double spacing =
            (maximum - minimum) / static_cast<double>(count - 1U);
        for (std::size_t i = 0U; i < count; ++i) {
            axis.point[i] = minimum + spacing * static_cast<double>(i);
        }
    } else {
        const double left = std::asinh((minimum - focus) / scale);
        const double right = std::asinh((maximum - focus) / scale);
        for (std::size_t i = 0U; i < count; ++i) {
            const double fraction =
                static_cast<double>(i) / static_cast<double>(count - 1U);
            axis.point[i] =
                focus + scale * std::sinh(left + fraction * (right - left));
        }
    }

    axis.minimum_spacing = std::numeric_limits<double>::infinity();
    for (std::size_t i = 1U; i < count; ++i) {
        const double spacing = axis.point[i] - axis.point[i - 1U];
        if (!std::isfinite(spacing) || !(spacing > 0.0)) {
            throw std::runtime_error(std::string("PDE ") + name +
                                     " grid is not increasing");
        }
        axis.minimum_spacing = std::min(axis.minimum_spacing, spacing);
    }

    // Interior weights differentiate on the actual node spacing
    axis.lower_first.assign(count, 0.0);
    axis.middle_first.assign(count, 0.0);
    axis.upper_first.assign(count, 0.0);
    axis.lower_second.assign(count, 0.0);
    axis.middle_second.assign(count, 0.0);
    axis.upper_second.assign(count, 0.0);
    for (std::size_t i = 1U; i + 1U < count; ++i) {
        const double lower = axis.point[i] - axis.point[i - 1U];
        const double upper = axis.point[i + 1U] - axis.point[i];
        axis.lower_first[i] = -upper / (lower * (lower + upper));
        axis.middle_first[i] = (upper - lower) / (lower * upper);
        axis.upper_first[i] = lower / (upper * (lower + upper));
        axis.lower_second[i] = 2.0 / (lower * (lower + upper));
        axis.middle_second[i] = -2.0 / (lower * upper);
        axis.upper_second[i] = 2.0 / (upper * (lower + upper));
    }

    return axis;
}

[[nodiscard]] std::size_t index_of(const std::size_t columns,
                                   const std::size_t x, const std::size_t y) {
    // Field storage is row-major, making each x-direction line contiguous
    return y * columns + x;
}

[[nodiscard]] double first(const Axis &axis, const std::vector<double> &value,
                           const std::size_t stride, const std::size_t base,
                           const std::size_t position) {
    // Callers pass an interior position because this stencil reads both
    // neighbours
    return axis.lower_first[position] * value[base + (position - 1U) * stride] +
           axis.middle_first[position] * value[base + position * stride] +
           axis.upper_first[position] * value[base + (position + 1U) * stride];
}

[[nodiscard]] double second(const Axis &axis, const std::vector<double> &value,
                            const std::size_t stride, const std::size_t base,
                            const std::size_t position) {
    // Boundary rows are supplied by apply_boundary or direction_matrix
    if (position == 0U || position + 1U == axis.point.size()) {
        return 0.0;
    }
    return axis.lower_second[position] *
               value[base + (position - 1U) * stride] +
           axis.middle_second[position] * value[base + position * stride] +
           axis.upper_second[position] * value[base + (position + 1U) * stride];
}

[[nodiscard]] bool natural_x(const Boundary &boundary, const std::size_t y,
                             const bool upper) {
    return boundary.kind[upper ? 1U : 0U][y] == PdeBoundaryKind::natural;
}

[[nodiscard]] bool natural_y(const Boundary &boundary, const std::size_t x,
                             const bool upper) {
    return boundary.kind[upper ? 3U : 2U][x] == PdeBoundaryKind::natural;
}

void apply_boundary(std::vector<double> &value, const Axis &x_axis,
                    const Axis &y_axis, const std::size_t columns,
                    const std::size_t rows, const Boundary &boundary) {
    // Apply x sides first; y sides write the four corners last
    for (std::size_t y = 0U; y < rows; ++y) {
        const std::size_t left = index_of(columns, 0U, y);
        const std::size_t right = index_of(columns, columns - 1U, y);
        if (boundary.kind[0U][y] == PdeBoundaryKind::dirichlet) {
            value[left] = boundary.value[0U][y];
        } else if (boundary.kind[0U][y] == PdeBoundaryKind::neumann) {
            value[left] =
                value[left + 1U] +
                boundary.value[0U][y] * (x_axis.point[1U] - x_axis.point[0U]);
        }
        if (boundary.kind[1U][y] == PdeBoundaryKind::dirichlet) {
            value[right] = boundary.value[1U][y];
        } else if (boundary.kind[1U][y] == PdeBoundaryKind::neumann) {
            value[right] = value[right - 1U] +
                           boundary.value[1U][y] * (x_axis.point.back() -
                                                    x_axis.point[columns - 2U]);
        }
    }
    for (std::size_t x = 0U; x < columns; ++x) {
        const std::size_t lower = index_of(columns, x, 0U);
        const std::size_t upper = index_of(columns, x, rows - 1U);
        if (boundary.kind[2U][x] == PdeBoundaryKind::dirichlet) {
            value[lower] = boundary.value[2U][x];
        } else if (boundary.kind[2U][x] == PdeBoundaryKind::neumann) {
            value[lower] =
                value[lower + columns] +
                boundary.value[2U][x] * (y_axis.point[1U] - y_axis.point[0U]);
        }
        if (boundary.kind[3U][x] == PdeBoundaryKind::dirichlet) {
            value[upper] = boundary.value[3U][x];
        } else if (boundary.kind[3U][x] == PdeBoundaryKind::neumann) {
            value[upper] = value[upper - columns] +
                           boundary.value[3U][x] *
                               (y_axis.point.back() - y_axis.point[rows - 2U]);
        }
    }
}

void tabulate_boundary(Boundary &boundary, ScenarioRuntime &runtime,
                       const std::size_t field, const Axis &x_axis,
                       const Axis &y_axis,
                       const std::vector<PdeCoefficients> &coefficient,
                       const std::size_t columns, const double tau) {
    // Lua supplies values at the current time, while C++ preserves contiguous
    // lines
    const std::array<std::size_t, 4U> count = {
        y_axis.point.size(), y_axis.point.size(), x_axis.point.size(),
        x_axis.point.size()};
    for (std::size_t side = 0U; side < 4U; ++side) {
        boundary.kind[side].resize(count[side]);
        boundary.value[side].resize(count[side]);
        for (std::size_t i = 0U; i < count[side]; ++i) {
            const double coordinate =
                side < 2U ? y_axis.point[i] : x_axis.point[i];
            std::string error;
            if (!invoke_pde_boundary(runtime, field,
                                     static_cast<std::uint8_t>(side),
                                     coordinate, tau, boundary.kind[side][i],
                                     boundary.value[side][i], error)) {
                throw std::runtime_error("PDE boundary callback failed: " +
                                         error);
            }
            if (!std::isfinite(boundary.value[side][i])) {
                throw std::runtime_error(
                    "PDE boundary callback returned a non-finite value");
            }
            // Natural rows only represent drift and reaction, never normal
            // diffusion
            if (boundary.kind[side][i] == PdeBoundaryKind::natural) {
                const std::size_t x = side == 0U   ? 0U
                                      : side == 1U ? columns - 1U
                                                   : i;
                const std::size_t y = side < 2U    ? i
                                      : side == 2U ? 0U
                                                   : y_axis.point.size() - 1U;
                const PdeCoefficients &c = coefficient[index_of(columns, x, y)];
                const double normal_diffusion = side < 2U ? c.xx : c.yy;
                if (normal_diffusion >
                    64.0 * std::numeric_limits<double>::epsilon()) {
                    throw std::runtime_error(
                        "PDE natural boundary requires zero normal diffusion");
                }
            }
        }
    }
}

[[nodiscard]] double directional(const Field &field, const Axis &x_axis,
                                 const Axis &y_axis, const std::size_t columns,
                                 const std::size_t x, const std::size_t y,
                                 const bool x_direction,
                                 const std::vector<double> &value) {
    // Split the reaction term evenly between the x and y implicit operators
    const std::size_t cell = index_of(columns, x, y);
    const PdeCoefficients &c = field.coefficient[cell];
    if (x_direction) {
        if (x == 0U && natural_x(field.boundary, y, false)) {
            return c.x * (value[cell + 1U] - value[cell]) /
                       (x_axis.point[1U] - x_axis.point[0U]) +
                   0.5 * c.value * value[cell];
        }
        if (x + 1U == x_axis.point.size() &&
            natural_x(field.boundary, y, true)) {
            return c.x * (value[cell] - value[cell - 1U]) /
                       (x_axis.point.back() -
                        x_axis.point[x_axis.point.size() - 2U]) +
                   0.5 * c.value * value[cell];
        }
        if (x == 0U || x + 1U == x_axis.point.size())
            return 0.0;
        return c.xx * second(x_axis, value, 1U, index_of(columns, 0U, y), x) +
               c.x * first(x_axis, value, 1U, index_of(columns, 0U, y), x) +
               0.5 * c.value * value[cell];
    }
    if (y == 0U && natural_y(field.boundary, x, false)) {
        return c.y * (value[cell + columns] - value[cell]) /
                   (y_axis.point[1U] - y_axis.point[0U]) +
               0.5 * c.value * value[cell];
    }
    if (y + 1U == y_axis.point.size() && natural_y(field.boundary, x, true)) {
        return c.y * (value[cell] - value[cell - columns]) /
                   (y_axis.point.back() -
                    y_axis.point[y_axis.point.size() - 2U]) +
               0.5 * c.value * value[cell];
    }
    if (y == 0U || y + 1U == y_axis.point.size())
        return 0.0;
    return c.yy * second(y_axis, value, columns, x, y) +
           c.y * first(y_axis, value, columns, x, y) +
           0.5 * c.value * value[cell];
}

[[nodiscard]] double mixed(const Field &field, const Axis &x_axis,
                           const Axis &y_axis, const std::size_t columns,
                           const std::size_t x, const std::size_t y,
                           const std::vector<double> &value) {
    // The mixed derivative is explicit and has no stencil on the outer ring
    if (x == 0U || y == 0U || x + 1U == x_axis.point.size() ||
        y + 1U == y_axis.point.size())
        return 0.0;
    const std::size_t cell = index_of(columns, x, y);
    if (field.coefficient[cell].xy == 0.0)
        return 0.0;
    const auto dx = [&x_axis, &value, columns, x](const std::size_t yy) {
        return first(x_axis, value, 1U, index_of(columns, 0U, yy), x);
    };
    return field.coefficient[cell].xy * (y_axis.lower_first[y] * dx(y - 1U) +
                                         y_axis.middle_first[y] * dx(y) +
                                         y_axis.upper_first[y] * dx(y + 1U));
}

void full_operator(const Field &field, const Axis &x_axis, const Axis &y_axis,
                   const std::size_t columns, const std::size_t rows,
                   const std::vector<double> &value, std::vector<double> &out,
                   std::vector<double> &mixed_out) {
    // Keep the mixed term for the later Craig-Sneyd correction
    for (std::size_t y = 0U; y < rows; ++y) {
        for (std::size_t x = 0U; x < columns; ++x) {
            const std::size_t cell = index_of(columns, x, y);
            const double cross =
                mixed(field, x_axis, y_axis, columns, x, y, value);
            out[cell] =
                directional(field, x_axis, y_axis, columns, x, y, true, value) +
                directional(field, x_axis, y_axis, columns, x, y, false,
                            value) +
                cross + field.coefficient[cell].source;
            mixed_out[cell] = cross;
        }
    }
}

void solve_tridiagonal(std::vector<double> &lower,
                       std::vector<double> &diagonal,
                       std::vector<double> &upper, std::vector<double> &right,
                       const std::size_t length) {
    // Eliminate in place because these scratch arrays are rebuilt for every
    // uncached line
    for (std::size_t i = 1U; i < length; ++i) {
        if (!std::isfinite(diagonal[i - 1U]) ||
            std::abs(diagonal[i - 1U]) < 1e-15) {
            throw std::runtime_error("singular PDE implicit line");
        }
        const double ratio = lower[i] / diagonal[i - 1U];
        diagonal[i] -= ratio * upper[i - 1U];
        right[i] -= ratio * right[i - 1U];
    }
    if (!std::isfinite(diagonal[length - 1U]) ||
        std::abs(diagonal[length - 1U]) < 1e-15) {
        throw std::runtime_error("singular PDE implicit line");
    }
    right[length - 1U] /= diagonal[length - 1U];
    for (std::size_t i = length - 1U; i-- > 0U;) {
        right[i] = (right[i] - upper[i] * right[i + 1U]) / diagonal[i];
    }
}

[[nodiscard]] PdeBoundaryKind direction_kind(const Field &field,
                                             const bool x_direction,
                                             const std::size_t line,
                                             const bool upper) {
    return x_direction ? field.boundary.kind[upper ? 1U : 0U][line]
                       : field.boundary.kind[upper ? 3U : 2U][line];
}

void direction_matrix(const Field &field, const Axis &x_axis,
                      const Axis &y_axis, const std::size_t columns,
                      const std::size_t rows, const bool x_direction,
                      const double factor, const std::size_t line,
                      const std::size_t p, double &lower, double &diagonal,
                      double &upper) {
    const std::size_t length = x_direction ? columns : rows;
    const std::size_t x = x_direction ? p : line;
    const std::size_t y = x_direction ? line : p;
    const PdeCoefficients &c = field.coefficient[index_of(columns, x, y)];
    const bool at_lower = p == 0U;
    const bool at_upper = p + 1U == length;
    lower = 0.0;
    diagonal = 1.0;
    upper = 0.0;
    // Boundary rows encode the constraint instead of the differential operator
    if (at_lower || at_upper) {
        const PdeBoundaryKind kind =
            direction_kind(field, x_direction, line, at_upper);
        if (kind == PdeBoundaryKind::dirichlet)
            return;
        if (kind == PdeBoundaryKind::neumann) {
            if (at_lower)
                upper = -1.0;
            else
                lower = -1.0;
            return;
        }
        const double drift = x_direction ? c.x : c.y;
        const double reaction = 0.5 * c.value;
        const double spacing =
            x_direction
                ? (at_lower ? x_axis.point[1U] - x_axis.point[0U]
                            : x_axis.point.back() - x_axis.point[columns - 2U])
                : (at_lower ? y_axis.point[1U] - y_axis.point[0U]
                            : y_axis.point.back() - y_axis.point[rows - 2U]);
        if (at_lower) {
            diagonal -= factor * (-drift / spacing + reaction);
            upper -= factor * (drift / spacing);
        } else {
            diagonal -= factor * (drift / spacing + reaction);
            lower -= factor * (-drift / spacing);
        }
        return;
    }
    const Axis &axis = x_direction ? x_axis : y_axis;
    const double diffusion = x_direction ? c.xx : c.yy;
    const double drift = x_direction ? c.x : c.y;
    const double a =
        diffusion * axis.lower_second[p] + drift * axis.lower_first[p];
    const double b = diffusion * axis.middle_second[p] +
                     drift * axis.middle_first[p] + 0.5 * c.value;
    const double d =
        diffusion * axis.upper_second[p] + drift * axis.upper_first[p];
    lower = -factor * a;
    diagonal -= factor * b;
    upper = -factor * d;
}

[[nodiscard]] bool cached_kinds_match(const DirectionFactors &cached,
                                      const Field &field,
                                      const bool x_direction,
                                      const std::size_t lines) {
    // Values may change every stage, but a changed row kind needs a new matrix
    if (cached.boundary_kind.size() != 2U * lines)
        return false;
    for (std::size_t line = 0U; line < lines; ++line) {
        if (cached.boundary_kind[2U * line] !=
                direction_kind(field, x_direction, line, false) ||
            cached.boundary_kind[2U * line + 1U] !=
                direction_kind(field, x_direction, line, true))
            return false;
    }
    return true;
}

void cache_direction(DirectionFactors &cached, const Field &field,
                     const Axis &x_axis, const Axis &y_axis,
                     const std::size_t columns, const std::size_t rows,
                     const bool x_direction, const double factor) {
    // Coefficients and timestep are fixed for one solve, so factorize each line
    // once
    const std::size_t length = x_direction ? columns : rows;
    const std::size_t lines = x_direction ? rows : columns;
    const std::size_t entries = length * lines;
    cached.lower.resize(entries);
    cached.diagonal.resize(entries);
    cached.upper.resize(entries);
    cached.boundary_kind.resize(2U * lines);
    for (std::size_t line = 0U; line < lines; ++line) {
        cached.boundary_kind[2U * line] =
            direction_kind(field, x_direction, line, false);
        cached.boundary_kind[2U * line + 1U] =
            direction_kind(field, x_direction, line, true);
        const std::size_t offset = line * length;
        for (std::size_t p = 0U; p < length; ++p) {
            direction_matrix(field, x_axis, y_axis, columns, rows, x_direction,
                             factor, line, p, cached.lower[offset + p],
                             cached.diagonal[offset + p],
                             cached.upper[offset + p]);
        }
        for (std::size_t p = 1U; p < length; ++p) {
            const std::size_t cell = offset + p;
            const double previous = cached.diagonal[cell - 1U];
            if (!std::isfinite(previous) || std::abs(previous) < 1e-15) {
                throw std::runtime_error("singular PDE implicit line");
            }
            cached.lower[cell] /= previous;
            cached.diagonal[cell] -=
                cached.lower[cell] * cached.upper[cell - 1U];
        }
        if (!std::isfinite(cached.diagonal[offset + length - 1U]) ||
            std::abs(cached.diagonal[offset + length - 1U]) < 1e-15) {
            throw std::runtime_error("singular PDE implicit line");
        }
    }
}

void solve_direction(Field &field, const Axis &x_axis, const Axis &y_axis,
                     const std::size_t columns, const std::size_t rows,
                     const bool x_direction, const double factor,
                     const std::vector<double> &current,
                     const std::vector<double> *base, const bool include_source,
                     std::vector<double> &out, std::vector<double> &lower,
                     std::vector<double> &diagonal, std::vector<double> &upper,
                     std::vector<double> &right,
                     DirectionFactors *cached = nullptr) {
    const std::size_t length = x_direction ? columns : rows;
    const std::size_t lines = x_direction ? rows : columns;
    // Boundary values vary with time, but unchanged boundary kinds retain the
    // factorization
    if (cached != nullptr && cached->boundary_kind.empty()) {
        cache_direction(*cached, field, x_axis, y_axis, columns, rows,
                        x_direction, factor);
    }
    const bool reuse = cached != nullptr &&
                       cached_kinds_match(*cached, field, x_direction, lines);
    for (std::size_t line = 0U; line < lines; ++line) {
        for (std::size_t p = 0U; p < length; ++p) {
            const std::size_t x = x_direction ? p : line;
            const std::size_t y = x_direction ? line : p;
            const std::size_t cell = index_of(columns, x, y);
            const bool at_lower = p == 0U;
            const bool at_upper = p + 1U == length;
            right[p] = current[cell];
            const PdeBoundaryKind kind =
                direction_kind(field, x_direction, line, at_upper);
            if (!reuse) {
                direction_matrix(field, x_axis, y_axis, columns, rows,
                                 x_direction, factor, line, p, lower[p],
                                 diagonal[p], upper[p]);
            }
            if (at_lower || at_upper) {
                if (kind == PdeBoundaryKind::dirichlet) {
                    right[p] =
                        x_direction
                            ? field.boundary.value[at_upper ? 1U : 0U][y]
                            : field.boundary.value[at_upper ? 3U : 2U][x];
                    continue;
                }
                if (kind == PdeBoundaryKind::neumann) {
                    const double h =
                        x_direction
                            ? (at_upper ? x_axis.point.back() -
                                              x_axis.point[columns - 2U]
                                        : x_axis.point[1U] - x_axis.point[0U])
                            : (at_upper ? y_axis.point.back() -
                                              y_axis.point[rows - 2U]
                                        : y_axis.point[1U] - y_axis.point[0U]);
                    right[p] =
                        (x_direction
                             ? field.boundary.value[at_upper ? 1U : 0U][y]
                             : field.boundary.value[at_upper ? 3U : 2U][x]) *
                        h;
                    continue;
                }
                // Natural rows use the one-sided drift and reaction operator
                if (base != nullptr) {
                    right[p] -=
                        factor * directional(field, x_axis, y_axis, columns, x,
                                             y, x_direction, *base);
                }
                if (include_source)
                    right[p] += factor * 0.5 * field.coefficient[cell].source;
                continue;
            }
            if (base != nullptr) {
                right[p] -= factor * directional(field, x_axis, y_axis, columns,
                                                 x, y, x_direction, *base);
            }
            if (include_source)
                right[p] += factor * 0.5 * field.coefficient[cell].source;
        }
        if (reuse) {
            // Cached lower entries already contain the forward-elimination
            // ratios
            const std::size_t offset = line * length;
            for (std::size_t p = 1U; p < length; ++p) {
                right[p] -= cached->lower[offset + p] * right[p - 1U];
            }
            right[length - 1U] /= cached->diagonal[offset + length - 1U];
            for (std::size_t p = length - 1U; p-- > 0U;) {
                right[p] =
                    (right[p] - cached->upper[offset + p] * right[p + 1U]) /
                    cached->diagonal[offset + p];
            }
        } else {
            solve_tridiagonal(lower, diagonal, upper, right, length);
        }
        for (std::size_t p = 0U; p < length; ++p) {
            out[index_of(columns, x_direction ? p : line,
                         x_direction ? line : p)] = right[p];
        }
    }
}

[[nodiscard]] double interpolate(const std::vector<double> &value,
                                 const Axis &x_axis, const Axis &y_axis,
                                 const std::size_t columns, const double x,
                                 const double y) {
    // Bilinear sampling rejects points outside the solved domain
    if (!std::isfinite(x) || !std::isfinite(y) || x < x_axis.point.front() ||
        x > x_axis.point.back() || y < y_axis.point.front() ||
        y > y_axis.point.back()) {
        throw std::runtime_error("PDE sample point lies outside the grid");
    }
    const auto xi =
        std::upper_bound(x_axis.point.begin(), x_axis.point.end(), x);
    const auto yi =
        std::upper_bound(y_axis.point.begin(), y_axis.point.end(), y);
    const std::size_t x1 = std::min<std::size_t>(
        static_cast<std::size_t>(xi - x_axis.point.begin()),
        x_axis.point.size() - 1U);
    const std::size_t y1 = std::min<std::size_t>(
        static_cast<std::size_t>(yi - y_axis.point.begin()),
        y_axis.point.size() - 1U);
    const std::size_t x0 = x1 == 0U ? 0U : x1 - 1U;
    const std::size_t y0 = y1 == 0U ? 0U : y1 - 1U;
    const double fx = x0 == x1 ? 0.0
                               : (x - x_axis.point[x0]) /
                                     (x_axis.point[x1] - x_axis.point[x0]);
    const double fy = y0 == y1 ? 0.0
                               : (y - y_axis.point[y0]) /
                                     (y_axis.point[y1] - y_axis.point[y0]);
    const double lower = value[index_of(columns, x0, y0)] * (1.0 - fx) +
                         value[index_of(columns, x1, y0)] * fx;
    const double upper = value[index_of(columns, x0, y1)] * (1.0 - fx) +
                         value[index_of(columns, x1, y1)] * fx;
    return lower * (1.0 - fy) + upper * fy;
}

PdeResult solve_pde(const PdePlan &plan, ScenarioRuntime &runtime) {
    // Validate the scalar plan before deriving allocation sizes or a timestep
    if (plan.fields.empty() || !std::isfinite(plan.final_time) ||
        !(plan.final_time > 0.0) || !std::isfinite(plan.theta) ||
        !(plan.theta > 0.0) || plan.steps == 0U) {
        throw std::runtime_error("invalid PDE time plan");
    }
    const std::size_t cells = checked_product(plan.columns, plan.rows, "grid");
    const std::size_t field_cells =
        checked_product(cells, plan.fields.size(), "field grid");
    (void)field_cells;
    // Construct the grid once because the Lua model only supplies coefficients
    // on it
    const Axis x_axis = make_axis(plan.columns, plan.x_min, plan.x_max,
                                  plan.x_focus, plan.x_scale, "x");
    const Axis y_axis = make_axis(plan.rows, plan.y_min, plan.y_max,
                                  plan.y_focus, plan.y_scale, "y");
    const double dt = plan.final_time / static_cast<double>(plan.steps);
    if (!std::isfinite(dt) || !(dt > 0.0))
        throw std::runtime_error("invalid PDE time step");

    // Report grid and work metadata once because every field shares the same
    // plan
    PdeResult result;
    result.values.resize(plan.fields.size());
    result.references.assign(plan.fields.size(),
                             std::numeric_limits<double>::quiet_NaN());
    result.errors.assign(plan.fields.size(),
                         std::numeric_limits<double>::quiet_NaN());
    result.minimum_x_spacing = x_axis.minimum_spacing;
    result.minimum_y_spacing = y_axis.minimum_spacing;
    result.time_step = dt;
    result.steps = plan.steps;
    const std::uint64_t cells64 = static_cast<std::uint64_t>(cells);
    if (cells64 > std::numeric_limits<std::uint64_t>::max() / plan.steps ||
        cells64 * plan.steps >
            std::numeric_limits<std::uint64_t>::max() / plan.fields.size()) {
        throw std::runtime_error("PDE cell-update count overflows");
    }
    result.cell_updates =
        cells64 * plan.steps * static_cast<std::uint64_t>(plan.fields.size());

    // One reusable line workspace serves uncached x and y factorizations
    std::vector<double> lower(std::max(plan.columns, plan.rows));
    std::vector<double> diagonal(lower.size());
    std::vector<double> upper(lower.size());
    std::vector<double> right(lower.size());

    // Advance fields independently so one Lua field cannot alter another
    // field's state
    for (std::size_t f = 0U; f < plan.fields.size(); ++f) {
        // Lua is called during setup for initial data and coefficients, never
        // per stencil
        Field field;
        field.coefficient.resize(cells);
        field.value.resize(cells);
        field.stage_a.resize(cells);
        field.stage_b.resize(cells);
        field.stage_c.resize(cells);
        field.scratch.resize(cells);
        field.mixed_base.resize(cells);
        // Sample initial values and fixed operator coefficients across the
        // completed grid
        for (std::size_t y = 0U; y < plan.rows; ++y) {
            for (std::size_t x = 0U; x < plan.columns; ++x) {
                const std::size_t cell = index_of(plan.columns, x, y);
                std::string error;
                if (!invoke_pde_initial(runtime, f, x_axis.point[x],
                                        y_axis.point[y], field.value[cell],
                                        error)) {
                    throw std::runtime_error("PDE initial callback failed: " +
                                             error);
                }
                if (!invoke_pde_coefficients(runtime, f, x_axis.point[x],
                                             y_axis.point[y],
                                             field.coefficient[cell], error)) {
                    throw std::runtime_error(
                        "PDE coefficient callback failed: " + error);
                }
                const PdeCoefficients &c = field.coefficient[cell];
                // Require a positive semidefinite diffusion tensor before
                // forming stencils
                const double tolerance =
                    64.0 * std::numeric_limits<double>::epsilon() *
                    std::max({1.0, std::abs(c.xy * c.xy),
                              std::abs(4.0 * c.xx * c.yy)});
                if (!std::isfinite(field.value[cell]) || !std::isfinite(c.xx) ||
                    !std::isfinite(c.xy) || !std::isfinite(c.yy) ||
                    !std::isfinite(c.x) || !std::isfinite(c.y) ||
                    !std::isfinite(c.value) || !std::isfinite(c.source) ||
                    c.xx < 0.0 || c.yy < 0.0 ||
                    c.xy * c.xy > 4.0 * c.xx * c.yy + tolerance) {
                    throw std::runtime_error(
                        "PDE callback returned non-parabolic coefficients");
                }
            }
        }
        // Boundary callbacks remain at the language boundary and run once per
        // stage time
        tabulate_boundary(field.boundary, runtime, f, x_axis, y_axis,
                          field.coefficient, plan.columns, 0.0);
        apply_boundary(field.value, x_axis, y_axis, plan.columns, plan.rows,
                       field.boundary);

        for (std::uint64_t step = 0U; step < plan.steps; ++step) {
            const double next_tau = dt * static_cast<double>(step + 1U);
            if (step == 0U) {
                // Two implicit half-steps damp the initial discontinuity before
                // Craig-Sneyd
                const double half = 0.5 * dt;
                for (unsigned part = 0U; part < 2U; ++part) {
                    tabulate_boundary(field.boundary, runtime, f, x_axis,
                                      y_axis, field.coefficient, plan.columns,
                                      half * static_cast<double>(part + 1U));
                    for (std::size_t y = 0U; y < plan.rows; ++y) {
                        for (std::size_t x = 0U; x < plan.columns; ++x) {
                            const std::size_t cell =
                                index_of(plan.columns, x, y);
                            field.stage_a[cell] =
                                field.value[cell] +
                                half * mixed(field, x_axis, y_axis,
                                             plan.columns, x, y, field.value);
                        }
                    }
                    solve_direction(field, x_axis, y_axis, plan.columns,
                                    plan.rows, true, half, field.stage_a,
                                    nullptr, true, field.stage_b, lower,
                                    diagonal, upper, right);
                    solve_direction(field, x_axis, y_axis, plan.columns,
                                    plan.rows, false, half, field.stage_b,
                                    nullptr, true, field.value, lower, diagonal,
                                    upper, right);
                    apply_boundary(field.value, x_axis, y_axis, plan.columns,
                                   plan.rows, field.boundary);
                }
                continue;
            }
            // Refresh boundary data at the new timestep before the next stages
            tabulate_boundary(field.boundary, runtime, f, x_axis, y_axis,
                              field.coefficient, plan.columns, next_tau);

            // Explicit predictor and directional implicit stages
            full_operator(field, x_axis, y_axis, plan.columns, plan.rows,
                          field.value, field.scratch, field.mixed_base);
            for (std::size_t cell = 0U; cell < cells; ++cell) {
                field.stage_a[cell] =
                    field.value[cell] + dt * field.scratch[cell];
            }
            // Apply x then y corrections to the explicit predictor
            solve_direction(field, x_axis, y_axis, plan.columns, plan.rows,
                            true, plan.theta * dt, field.stage_a, &field.value,
                            false, field.stage_b, lower, diagonal, upper, right,
                            &field.factor[0U]);
            solve_direction(field, x_axis, y_axis, plan.columns, plan.rows,
                            false, plan.theta * dt, field.stage_b, &field.value,
                            false, field.stage_c, lower, diagonal, upper, right,
                            &field.factor[1U]);
            apply_boundary(field.stage_c, x_axis, y_axis, plan.columns,
                           plan.rows, field.boundary);

            full_operator(field, x_axis, y_axis, plan.columns, plan.rows,
                          field.stage_c, field.scratch, field.stage_b);
            for (std::size_t cell = 0U; cell < cells; ++cell) {
                field.stage_a[cell] +=
                    plan.theta * dt *
                        (field.stage_b[cell] - field.mixed_base[cell]) +
                    (0.5 - plan.theta) * dt *
                        (field.scratch[cell] -
                         (field.stage_a[cell] - field.value[cell]) / dt);
            }
            // The mixed-derivative correction restores Craig-Sneyd second-order
            // accuracy
            solve_direction(field, x_axis, y_axis, plan.columns, plan.rows,
                            true, plan.theta * dt, field.stage_a, &field.value,
                            false, field.stage_b, lower, diagonal, upper, right,
                            &field.factor[0U]);
            solve_direction(field, x_axis, y_axis, plan.columns, plan.rows,
                            false, plan.theta * dt, field.stage_b, &field.value,
                            false, field.value, lower, diagonal, upper, right,
                            &field.factor[1U]);
            apply_boundary(field.value, x_axis, y_axis, plan.columns, plan.rows,
                           field.boundary);
        }

        // Reject unstable output before exposing a sample or reference error
        for (const double value : field.value) {
            if (!std::isfinite(value))
                throw std::runtime_error(
                    "PDE solver produced a non-finite value");
        }
        // Sample on the physical grid, then compare only when Lua exposes a
        // reference
        result.values[f] =
            interpolate(field.value, x_axis, y_axis, plan.columns,
                        plan.sample_x, plan.sample_y);
        bool present = false;
        std::string error;
        if (!invoke_pde_reference(runtime, f, result.references[f], present,
                                  error)) {
            throw std::runtime_error("PDE reference callback failed: " + error);
        }
        if (present) {
            if (!std::isfinite(result.references[f])) {
                throw std::runtime_error(
                    "PDE reference callback returned a non-finite value");
            }
            result.errors[f] = result.values[f] - result.references[f];
        }
    }
    return result;
}

} // namespace

Metrics simulate_pde(const Scenario &scenario, State &state,
                     ScenarioRuntime *const program) {
    if (program == nullptr || program->program == nullptr ||
        program->context == nullptr) {
        throw std::runtime_error(scenario.source_directory + '/' +
                                 scenario.lua_rules +
                                 ": no compiled scenario program");
    }
    // Keep the interpreter boundary outside the numerical loop
    state.pde = solve_pde(scenario.pde, *program);
    state.result = 0;
    Metrics metrics;
    metrics.steps = state.pde.steps;
    metrics.cell_updates = state.pde.cell_updates;
    return metrics;
}

} // namespace m1
