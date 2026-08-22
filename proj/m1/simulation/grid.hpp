#include "model.hpp"
#include "simulation/runtime/commands.hpp"
#include "simulation/runtime/lua.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <random>
#include <stdexcept>
#include <string_view>
#include <utility>

/// \file
/// Build spatial indexes while callers retain the interaction rules
namespace m1 {
namespace { // NOLINT(cert-dcl59-cpp): included by one implementation unit

#ifndef M1_WIDE_GRID
#define M1_WIDE_GRID 0
#endif

#ifndef M1_OPT_LEVEL
#define M1_OPT_LEVEL 0
#endif

static_assert(M1_OPT_LEVEL >= 0 && M1_OPT_LEVEL <= 7,
              "M1_OPT_LEVEL must be in 0..7");

constexpr double unit_scale = 1.0 / 9'007'199'254'740'992.0;
#if M1_OPT_LEVEL == 0 || M1_WIDE_GRID
using GridIndex = std::uint64_t;
#else
using GridIndex = std::uint32_t;
#endif

template <class Index> struct BasicGrid {
    // Cells cover the world while members groups live entity IDs by cell
    std::size_t columns = 0;
    std::size_t rows = 0;
    double cell_width = 0.0;
    double cell_height = 0.0;
    std::vector<Index> counts;
    std::vector<Index> offsets;
    std::vector<Index> members;
    std::vector<Index> entity_cells;
#if M1_OPT_LEVEL == 0
    // Linked lists retain the simplest reference traversal
    std::vector<std::uint64_t> heads;
    std::vector<std::uint64_t> next;
#endif
#if M1_OPT_LEVEL >= 2
    // Sparse rebuilds reset only cells occupied in the previous frame
    std::vector<Index> cursors;
    std::vector<Index> occupied_cells;
#endif
#if M1_OPT_LEVEL >= 3
    // Split each cell when two contiguous populations permit it
    std::vector<Index> type_splits;
    std::size_t second_type_first = 0;
#endif
};

using Grid = BasicGrid<GridIndex>;

[[nodiscard]] double random_unit(std::mt19937_64 &generator) noexcept {
    return static_cast<double>(generator() >> 11U) * unit_scale;
}

// Position helpers
// Wrapped positions stay in the half-open world interval [0, extent)
[[nodiscard]] double wrapped_delta(double delta, const double extent) noexcept {
    const double half_extent = extent * 0.5;
    if (delta > half_extent) {
        delta -= extent;
    } else if (delta < -half_extent) {
        delta += extent;
    }
    return delta;
}

[[nodiscard]] double add_wrapped(const double position, const double delta,
                                 const double extent) noexcept {
    const double movement =
        delta >= extent || delta <= -extent ? std::fmod(delta, extent) : delta;
    double result = 0.0;
    if (movement >= 0.0) {
        const double remaining = extent - position;
        result =
            movement < remaining ? position + movement : movement - remaining;
    } else {
        const double distance = -movement;
        result = distance <= position ? position - distance
                                      : extent - (distance - position);
    }
    return result < extent ? result : std::nextafter(extent, 0.0);
}

[[nodiscard]] bool has_behaviour(const CharacterPlan &plan,
                                 const Behaviour behaviour) noexcept {
    return (plan.behaviours & static_cast<std::uint8_t>(behaviour)) != 0U;
}

[[nodiscard]] std::size_t simple_target(const CharacterPlan &plan,
                                        const Scenario &scenario) noexcept {
    if (plan.behaviour_count == 0U) {
        return plan.target;
    }
    if (plan.first_behaviour >= scenario.behaviour_plan.size()) {
        return scenario.characters.size();
    }
    const std::size_t target =
        scenario.behaviour_plan[plan.first_behaviour].target;
    const std::size_t end = plan.first_behaviour + plan.behaviour_count;
    for (std::size_t index = plan.first_behaviour; index < end; ++index) {
        if (index >= scenario.behaviour_plan.size() ||
            scenario.behaviour_plan[index].target != target) {
            return scenario.characters.size();
        }
    }
    return target;
}

[[nodiscard]] bool valid_coordinates(const double x, const double y,
                                     const WorldConfig &world) noexcept {
    return std::isfinite(x) && std::isfinite(y) && x >= 0.0 &&
           x < world.width && y >= 0.0 && y < world.height;
}

[[nodiscard]] std::size_t limited_cells(const double extent,
                                        const double radius,
                                        const std::size_t limit) noexcept {
    const double available = std::floor(extent / radius);
    if (available < 1.0) {
        return 1U;
    }
    if (available >= static_cast<double>(limit)) {
        return limit;
    }
    return static_cast<std::size_t>(available);
}

[[nodiscard]] double maximum_cutoff(const Scenario &scenario) noexcept {
    double radius_squared = 0.0;
    for (const CharacterPlan &plan : scenario.characters) {
        radius_squared =
            std::max(radius_squared, std::max(plan.sensing_radius_squared,
                                              plan.capture_radius_squared));
    }
    return std::sqrt(radius_squared);
}

#if M1_OPT_LEVEL >= 3
[[nodiscard]] bool
has_two_contiguous_populations(const Scenario &scenario) noexcept {
    return scenario.characters.size() == 2U &&
           scenario.characters[0].first == 0U &&
           scenario.characters[0].count == scenario.characters[1].first &&
           scenario.characters[1].first <= scenario.entity_count &&
           scenario.characters[1].count ==
               scenario.entity_count - scenario.characters[1].first;
}
#endif

// Grid allocation
// The chosen cell width bounds the neighbourhood search to adjacent cells
template <class Index = GridIndex>
[[nodiscard]] BasicGrid<Index> make_grid(const Scenario &scenario,
                                         const double requested_radius = 0.0) {
    // A cell spans at most one interaction cutoff in each dimension
    const double radius =
        requested_radius > 0.0 ? requested_radius : maximum_cutoff(scenario);
    const std::size_t maximum_cells =
        std::max(scenario.entity_count, std::size_t{1});
    const std::size_t columns =
        radius > 0.0
            ? limited_cells(scenario.world.width, radius, maximum_cells)
            : 1U;
    const std::size_t rows_limit =
        std::max(maximum_cells / columns, std::size_t{1});
    const std::size_t rows =
        radius > 0.0 ? limited_cells(scenario.world.height, radius, rows_limit)
                     : 1U;
    const std::size_t cells = columns * rows;
#if M1_OPT_LEVEL >= 3
    const bool two_types = has_two_contiguous_populations(scenario);
#endif
    if (scenario.entity_count >
            static_cast<std::size_t>(std::numeric_limits<Index>::max()) ||
        cells > static_cast<std::size_t>(std::numeric_limits<Index>::max())) {
        throw std::overflow_error("continuous grid index capacity exceeded");
    }
    BasicGrid<Index> grid;
    grid.columns = columns;
    grid.rows = rows;
    grid.cell_width = scenario.world.width / static_cast<double>(columns);
    grid.cell_height = scenario.world.height / static_cast<double>(rows);
    grid.counts.resize(cells);
    grid.offsets.resize(cells + 1U);
    grid.members.resize(scenario.entity_count);
    grid.entity_cells.resize(scenario.entity_count);
#if M1_OPT_LEVEL == 0
    grid.heads.resize(cells, std::numeric_limits<std::uint64_t>::max());
    grid.next.resize(scenario.entity_count,
                     std::numeric_limits<std::uint64_t>::max());
#endif
#if M1_OPT_LEVEL >= 2
    grid.cursors.resize(cells);
    grid.occupied_cells.reserve(std::min(cells, scenario.entity_count));
#endif
#if M1_OPT_LEVEL >= 3
    grid.type_splits.resize(two_types ? cells : 0U);
    grid.second_type_first = two_types ? scenario.characters[1].first : 0U;
#endif
    return grid;
}

// Grid indexing and rebuild
// State positions must be valid before calling these helpers
template <class Index>
[[nodiscard]] Index cell_index(const BasicGrid<Index> &grid, const double x,
                               const double y) noexcept {
    const std::size_t column = std::min(
        static_cast<std::size_t>(x / grid.cell_width), grid.columns - 1U);
    const std::size_t row = std::min(
        static_cast<std::size_t>(y / grid.cell_height), grid.rows - 1U);
    return static_cast<Index>(row * grid.columns + column);
}

template <class Index>
void build_grid(BasicGrid<Index> &grid, const State &state) {
#if M1_OPT_LEVEL == 0
    constexpr std::uint64_t missing = std::numeric_limits<std::uint64_t>::max();
    std::fill(grid.heads.begin(), grid.heads.end(), missing);
#endif
#if M1_OPT_LEVEL >= 2
    for (const Index cell : grid.occupied_cells) {
        grid.counts[cell] = 0U;
    }
    grid.occupied_cells.clear();
#else
    std::fill(grid.counts.begin(), grid.counts.end(), 0U);
#if M1_OPT_LEVEL >= 3
    std::fill(grid.type_splits.begin(), grid.type_splits.end(), 0U);
#endif
#endif
    for (std::size_t entity = 0; entity < state.x.size(); ++entity) {
        if (state.alive[entity] != 0U) {
            const Index cell =
                cell_index(grid, state.x[entity], state.y[entity]);
            grid.entity_cells[entity] = cell;
            ++grid.counts[cell];
        }
    }
#if M1_OPT_LEVEL == 0
    // Insert backwards to preserve ascending entity IDs
    for (std::size_t entity = state.x.size(); entity-- > 0U;) {
        if (state.alive[entity] == 0U)
            continue;
        const Index cell = grid.entity_cells[entity];
        grid.next[entity] = grid.heads[cell];
        grid.heads[cell] = static_cast<std::uint64_t>(entity);
    }
#endif
    grid.offsets[0] = 0U;
    for (std::size_t cell = 0; cell < grid.counts.size(); ++cell) {
        grid.offsets[cell + 1U] = grid.offsets[cell] + grid.counts[cell];
#if M1_OPT_LEVEL >= 2
        if (grid.counts[cell] != 0U) {
            grid.cursors[cell] = grid.offsets[cell];
            grid.occupied_cells.push_back(static_cast<Index>(cell));
        }
#else
        // Reuse prefix sums as insertion cursors
        grid.counts[cell] = grid.offsets[cell];
#endif
#if M1_OPT_LEVEL >= 3
        if (!grid.type_splits.empty()) {
            grid.type_splits[cell] = grid.offsets[cell];
        }
#endif
    }
    // Deterministic member order makes equal-distance ties reproducible
    for (std::size_t entity = 0; entity < state.x.size(); ++entity) {
        if (state.alive[entity] != 0U) {
            const Index cell = grid.entity_cells[entity];
#if M1_OPT_LEVEL >= 2
            Index &cursor = grid.cursors[cell];
            grid.members[cursor++] = static_cast<Index>(entity);
#if M1_OPT_LEVEL >= 3
            if (!grid.type_splits.empty() && entity < grid.second_type_first) {
                grid.type_splits[cell] = cursor;
            }
#endif
#else
            grid.members[grid.counts[cell]++] = static_cast<Index>(entity);
#endif
        }
    }
}

template <class Index, class Visit>
void visit_cell_members(const BasicGrid<Index> &grid, const std::size_t cell,
                        Visit &&visit) {
    // The visitor receives each live member exactly once in ID order
#if M1_OPT_LEVEL == 0
    constexpr std::uint64_t missing = std::numeric_limits<std::uint64_t>::max();
    for (std::uint64_t entity = grid.heads[cell]; entity != missing;
         entity = grid.next[entity]) {
        visit(static_cast<std::size_t>(entity));
    }
#else
    for (Index member = grid.offsets[cell]; member < grid.offsets[cell + 1U];
         ++member) {
        visit(static_cast<std::size_t>(grid.members[member]));
    }
#endif
}

// Neighbour selection
// The output buffer has room for a 3 by 3 stencil, including the source cell
[[nodiscard]] std::size_t adjacent(const std::size_t value, const int offset,
                                   const std::size_t extent) noexcept {
    if (offset < 0)
        return value == 0U ? extent - 1U : value - 1U;
    if (offset > 0)
        return value + 1U == extent ? 0U : value + 1U;
    return value;
}

template <class Index>
[[nodiscard]] std::size_t
neighbouring_cells(const BasicGrid<Index> &grid, const std::size_t source,
                   std::array<Index, 9U> &neighbours) noexcept {
    // Wrapped grids need each neighbouring cell once on thin dimensions
    const std::size_t source_row = source / grid.columns;
    const std::size_t source_column = source % grid.columns;
    if (grid.rows >= 3U && grid.columns >= 3U) {
#if M1_OPT_LEVEL >= 4
        if (source_row != 0U && source_row + 1U < grid.rows &&
            source_column != 0U && source_column + 1U < grid.columns) {
            const std::size_t first = source - grid.columns - 1U;
            for (std::size_t row = 0U; row < 3U; ++row) {
                for (std::size_t column = 0U; column < 3U; ++column) {
                    neighbours[row * 3U + column] =
                        static_cast<Index>(first + row * grid.columns + column);
                }
            }
            return neighbours.size();
        }
#endif
        const std::size_t previous_row =
            source_row == 0U ? grid.rows - 1U : source_row - 1U;
        const std::size_t next_row =
            source_row + 1U == grid.rows ? 0U : source_row + 1U;
        const std::size_t previous_column =
            source_column == 0U ? grid.columns - 1U : source_column - 1U;
        const std::size_t next_column =
            source_column + 1U == grid.columns ? 0U : source_column + 1U;
        const std::size_t rows[] = {previous_row, source_row, next_row};
        const std::size_t columns[] = {previous_column, source_column,
                                       next_column};
        for (std::size_t row = 0U; row < 3U; ++row) {
            for (std::size_t column = 0U; column < 3U; ++column) {
                neighbours[row * 3U + column] = static_cast<Index>(
                    rows[row] * grid.columns + columns[column]);
            }
        }
        return neighbours.size();
    }
    std::size_t count = 0;
    for (int row_offset = -1; row_offset <= 1; ++row_offset) {
        const std::size_t row = adjacent(source_row, row_offset, grid.rows);
        for (int column_offset = -1; column_offset <= 1; ++column_offset) {
            const std::size_t column =
                adjacent(source_column, column_offset, grid.columns);
            const std::size_t cell = row * grid.columns + column;
            bool duplicate = false;
            for (std::size_t index = 0; index < count; ++index) {
                duplicate = duplicate || neighbours[index] == cell;
            }
            if (!duplicate) {
                neighbours[count++] = static_cast<Index>(cell);
            }
        }
    }
    return count;
}

template <class Index>
[[nodiscard]] std::size_t
bounded_neighbouring_cells(const BasicGrid<Index> &grid,
                           const std::size_t source,
                           std::array<Index, 9U> &neighbours) noexcept {
    // Bounded worlds omit cells beyond an edge
    const std::size_t source_row = source / grid.columns;
    const std::size_t source_column = source % grid.columns;
    const std::size_t first_row = source_row == 0U ? 0U : source_row - 1U;
    const std::size_t first_column =
        source_column == 0U ? 0U : source_column - 1U;
    const std::size_t last_row =
        source_row + 1U < grid.rows ? source_row + 1U : source_row;
    const std::size_t last_column =
        source_column + 1U < grid.columns ? source_column + 1U : source_column;
#if M1_OPT_LEVEL >= 4
    if (source_row != 0U && source_row + 1U < grid.rows &&
        source_column != 0U && source_column + 1U < grid.columns) {
        const std::size_t first = source - grid.columns - 1U;
        for (std::size_t row = 0U; row < 3U; ++row) {
            for (std::size_t column = 0U; column < 3U; ++column) {
                neighbours[row * 3U + column] =
                    static_cast<Index>(first + row * grid.columns + column);
            }
        }
        return neighbours.size();
    }
#endif
    std::size_t count = 0U;
    for (std::size_t row = first_row; row <= last_row; ++row) {
        for (std::size_t column = first_column; column <= last_column;
             ++column) {
            neighbours[count++] =
                static_cast<Index>(row * grid.columns + column);
        }
    }
    return count;
}

#if M1_OPT_LEVEL >= 4
template <class Index>
[[nodiscard]] std::uint8_t
periodic_image(const BasicGrid<Index> &grid, const std::size_t source,
               const std::size_t target, const bool wraps) noexcept {
    if (!wraps || grid.columns < 3U || grid.rows < 3U) {
        return 9U;
    }
    const std::size_t source_column = source % grid.columns;
    const std::size_t target_column = target % grid.columns;
    const std::size_t source_row = source / grid.columns;
    const std::size_t target_row = target / grid.columns;
    const int x = source_column == 0U && target_column + 1U == grid.columns ? -1
                  : source_column + 1U == grid.columns && target_column == 0U
                      ? 1
                      : 0;
    const int y = source_row == 0U && target_row + 1U == grid.rows   ? -1
                  : source_row + 1U == grid.rows && target_row == 0U ? 1
                                                                     : 0;
    return static_cast<std::uint8_t>((x + 1) * 3 + y + 1);
}
#endif

template <class Index>
[[nodiscard]] std::size_t
load_neighbours(const BasicGrid<Index> &grid, const std::size_t source,
                const bool wraps, std::array<Index, 9U> &neighbours) noexcept {
    return wraps ? neighbouring_cells(grid, source, neighbours)
                 : bounded_neighbouring_cells(grid, source, neighbours);
}

#if M1_OPT_LEVEL >= 4
void apply_periodic_image(const std::uint8_t image, const bool wraps,
                          const double width, const double height,
                          double &delta_x, double &delta_y) noexcept {
    if (image == 9U) {
        if (wraps) {
            delta_x = wrapped_delta(delta_x, width);
            delta_y = wrapped_delta(delta_y, height);
        }
        return;
    }
    delta_x += static_cast<double>(static_cast<int>(image / 3U) - 1) * width;
    delta_y += static_cast<double>(static_cast<int>(image % 3U) - 1) * height;
}

template <class Index>
void classified_delta(const BasicGrid<Index> &grid,
                      const std::size_t source_cell,
                      const std::size_t target_cell, const bool wraps,
                      const double width, const double height,
                      const double source_x, const double source_y,
                      const double target_x, const double target_y,
                      double &delta_x, double &delta_y) noexcept {
    delta_x = target_x - source_x;
    delta_y = target_y - source_y;
    apply_periodic_image(periodic_image(grid, source_cell, target_cell, wraps),
                         wraps, width, height, delta_x, delta_y);
}
#endif

// Displacement and boundary handling
[[nodiscard]] double spatial_delta(const double delta, const double extent,
                                   const bool wraps) noexcept {
    return wraps ? wrapped_delta(delta, extent) : delta;
}

void advance_bounded(const double current, double &velocity,
                     const double extent, double &next) noexcept {
    // Reflect through repeated boundaries instead of losing long movement
    const double raw = current + velocity;
    const double period = extent * 2.0;
    const double edge = std::nextafter(extent, 0.0);
    if (!std::isfinite(raw)) {
        next = raw > 0.0 ? edge : 0.0;
        velocity = 0.0;
        return;
    }
    if (!std::isfinite(period)) {
        next = std::clamp(raw, 0.0, edge);
        velocity = 0.0;
        return;
    }
    double phase = std::fmod(raw, period);
    if (phase < 0.0) {
        phase += period;
    }
    if (phase >= extent) {
        phase = period - phase;
        velocity = -velocity;
    }
    next = std::min(phase, edge);
}

[[nodiscard]] bool
needs_extended_continuous(const Scenario &scenario) noexcept {
    for (const CharacterPlan &plan : scenario.characters) {
        if (plan.max_steering != 0.0) {
            return true;
        }
        if (plan.behaviour_count == 0U) {
            continue;
        }
        const std::size_t target = simple_target(plan, scenario);
        if (target >= scenario.characters.size()) {
            return true;
        }
        const std::size_t end = plan.first_behaviour + plan.behaviour_count;
        if (end > scenario.behaviour_plan.size()) {
            return true;
        }
        std::uint8_t seen = 0U;
        for (std::size_t index = plan.first_behaviour; index < end; ++index) {
            const BehaviourRecord &record = scenario.behaviour_plan[index];
            if ((record.code != BehaviourCode::seek &&
                 record.code != BehaviourCode::flee &&
                 record.code != BehaviourCode::consume) ||
                record.weight != 1.0 || record.parameter != 0.0 ||
                record.target != target) {
                return true;
            }
            const std::uint8_t flag = record.code == BehaviourCode::seek ? seek
                                      : record.code == BehaviourCode::flee
                                          ? flee
                                          : consume;
            if ((seen & flag) != 0U) {
                return true;
            }
            seen = static_cast<std::uint8_t>(seen | flag);
        }
        if ((seen & seek) != 0U && (seen & flee) != 0U) {
            return true;
        }
    }
    return false;
}

} // namespace
} // namespace m1
