#include "model.hpp"
#include "script.hpp"
#include "search.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace m1 {
namespace {

constexpr double unit_scale = 1.0 / 9'007'199'254'740'992.0;
constexpr std::uint64_t fnv_offset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
// Validation caps grid and entity indices at 32 bits
using GridIndex = std::uint32_t;

struct Grid {
    std::size_t columns = 0;
    std::size_t rows = 0;
    double cell_width = 0.0;
    double cell_height = 0.0;
    std::vector<GridIndex> counts;
    std::vector<GridIndex> offsets;
    std::vector<GridIndex> members;
    std::vector<GridIndex> entity_cells;
    // Two-type worlds keep one boundary per cell to avoid non-target scans
    std::vector<GridIndex> type_splits;
    std::size_t second_type_first = 0;
};

[[nodiscard]] double random_unit(std::mt19937_64 &generator) noexcept {
    return static_cast<double>(generator() >> 11U) * unit_scale;
}

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
    // Movement is capped at half an extent, so it crosses at most one edge
    double result = 0.0;
    if (delta >= 0.0) {
        const double remaining = extent - position;
        result = delta < remaining ? position + delta : delta - remaining;
    } else {
        const double distance = -delta;
        result = distance <= position ? position - distance
                                      : extent - (distance - position);
    }
    return result < extent ? result : std::nextafter(extent, 0.0);
}

[[nodiscard]] bool has_behaviour(const CharacterPlan &plan,
                                 const Behaviour behaviour) noexcept {
    return (plan.behaviours & static_cast<std::uint8_t>(behaviour)) != 0U;
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

[[nodiscard]] Grid make_grid(const Scenario &scenario,
                             const bool split_by_type) {
    double radius_squared = 0.0;
    for (const CharacterPlan &plan : scenario.characters) {
        radius_squared = std::max(radius_squared, plan.sensing_radius_squared);
    }
    const double radius = std::sqrt(radius_squared);
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
    const bool canonical_two_type_layout =
        split_by_type && scenario.characters.size() == 2U &&
        scenario.characters[0U].first == 0U &&
        scenario.characters[0U].count == scenario.characters[1U].first &&
        scenario.characters[1U].first <= scenario.entity_count &&
        scenario.characters[1U].count ==
            scenario.entity_count - scenario.characters[1U].first;
    return Grid{
        columns,
        rows,
        scenario.world.width / static_cast<double>(columns),
        scenario.world.height / static_cast<double>(rows),
        std::vector<GridIndex>(cells),
        std::vector<GridIndex>(cells + 1U),
        std::vector<GridIndex>(scenario.entity_count),
        std::vector<GridIndex>(scenario.entity_count),
        std::vector<GridIndex>(canonical_two_type_layout ? cells : 0U),
        canonical_two_type_layout ? scenario.characters[1U].first : 0U,
    };
}

[[nodiscard]] GridIndex cell_index(const Grid &grid, const double x,
                                   const double y) noexcept {
    const std::size_t column = std::min(
        static_cast<std::size_t>(x / grid.cell_width), grid.columns - 1U);
    const std::size_t row = std::min(
        static_cast<std::size_t>(y / grid.cell_height), grid.rows - 1U);
    return static_cast<GridIndex>(row * grid.columns + column);
}

void build_grid(Grid &grid, const State &state) {
    std::fill(grid.counts.begin(), grid.counts.end(), 0U);
    std::fill(grid.type_splits.begin(), grid.type_splits.end(), 0U);
    for (std::size_t entity = 0; entity < state.x.size(); ++entity) {
        if (state.alive[entity] != 0U) {
            const GridIndex cell =
                cell_index(grid, state.x[entity], state.y[entity]);
            grid.entity_cells[entity] = cell;
            ++grid.counts[cell];
            if (!grid.type_splits.empty() && entity < grid.second_type_first) {
                ++grid.type_splits[cell];
            }
        }
    }
    grid.offsets[0] = 0U;
    for (std::size_t cell = 0; cell < grid.counts.size(); ++cell) {
        grid.offsets[cell + 1U] = grid.offsets[cell] + grid.counts[cell];
        // Reuse the histogram as insertion cursors after the prefix sum
        grid.counts[cell] = grid.offsets[cell];
        if (!grid.type_splits.empty()) {
            grid.type_splits[cell] += grid.offsets[cell];
        }
    }
    // Keep IDs ordered within each cell so target scans can stop early
    for (std::size_t entity = 0; entity < state.x.size(); ++entity) {
        if (state.alive[entity] != 0U) {
            const GridIndex cell = grid.entity_cells[entity];
            grid.members[grid.counts[cell]++] = static_cast<GridIndex>(entity);
        }
    }
}

[[nodiscard]] std::size_t adjacent(const std::size_t value, const int offset,
                                   const std::size_t extent) noexcept {
    if (offset < 0) {
        return value == 0U ? extent - 1U : value - 1U;
    }
    if (offset > 0) {
        return value + 1U == extent ? 0U : value + 1U;
    }
    return value;
}

[[nodiscard]] std::size_t
neighbouring_cells(const Grid &grid, const std::size_t source,
                   std::array<GridIndex, 9U> &neighbours) noexcept {
    const std::size_t source_row = source / grid.columns;
    const std::size_t source_column = source % grid.columns;
    if (grid.rows >= 3U && grid.columns >= 3U) {
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
                neighbours[row * 3U + column] = static_cast<GridIndex>(
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
                neighbours[count++] = static_cast<GridIndex>(cell);
            }
        }
    }
    return count;
}

void hash_u64(std::uint64_t &value, const std::uint64_t input) noexcept {
    for (unsigned int byte = 0; byte < 8U; ++byte) {
        value ^= (input >> (byte * 8U)) & 0xffU;
        value *= fnv_prime;
    }
}

void hash_double(std::uint64_t &value, const double input) noexcept {
    std::uint64_t bits = 0;
    static_assert(sizeof bits == sizeof input, "unexpected double width");
    std::memcpy(&bits, &input, sizeof bits);
    hash_u64(value, bits);
}

[[nodiscard]] Scenario test_scenario(const std::uint64_t steps) {
    Scenario scenario;
    scenario.world = {10.0, 10.0, 1.0, steps, 7U};
    return scenario;
}

[[nodiscard]] bool close(const double left, const double right) noexcept {
    return std::abs(left - right) <= 1.0e-12;
}

struct ControllerProbe {
    std::uint64_t calls = 0U;
};

[[nodiscard]] bool probe_controller(const std::uint64_t, const Scenario &,
                                    State &state, void *const context) {
    auto &probe = *static_cast<ControllerProbe *>(context);
    ++probe.calls;
    state.x[0U] = 1.0;
    return true;
}

[[nodiscard]] bool
needs_extended_continuous(const Scenario &scenario) noexcept {
    for (const CharacterPlan &plan : scenario.characters) {
        if (plan.behaviour_count == 0U) {
            continue;
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
                record.target != plan.target) {
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

[[nodiscard]] Metrics simulate_extended_continuous(
    const Scenario &scenario, State &state, SnapshotObserver observer,
    void *context, std::uint64_t snapshot_stride, StepController controller,
    void *controller_context, std::uint64_t first_step,
    std::uint64_t step_count);

} // namespace

State initialise(const Scenario &scenario) {
    State state;
    state.scalars = scenario.scalars;
    state.buffers.reserve(scenario.buffers.size());
    for (const BufferPlan &plan : scenario.buffers) {
        BufferState buffer;
        buffer.kind = plan.kind;
        if (plan.kind == ScalarKind::boolean) {
            buffer.booleans.resize(plan.capacity);
        } else if (plan.kind == ScalarKind::integer) {
            buffer.integers.resize(plan.capacity);
        } else if (plan.kind == ScalarKind::number) {
            buffer.numbers.resize(plan.capacity);
        } else {
            buffer.identifiers.resize(plan.capacity);
        }
        state.buffers.push_back(std::move(buffer));
    }
    if (scenario.kernel == Kernel::cellular) {
        state.cells = scenario.cellular.initial;
        state.next_cells.resize(state.cells.size());
        return state;
    }
    if (scenario.kernel == Kernel::turn) {
        state.board.resize(scenario.turn.columns * scenario.turn.rows);
    }
    state.x.resize(scenario.entity_count);
    state.y.resize(scenario.entity_count);
    state.alive.assign(scenario.entity_count, 1U);
    if (scenario.kernel == Kernel::continuous) {
        state.next_x.resize(scenario.entity_count);
        state.next_y.resize(scenario.entity_count);
        state.velocity_x.assign(scenario.entity_count, 0.0);
        state.velocity_y.assign(scenario.entity_count, 0.0);
        state.next_velocity_x.resize(scenario.entity_count);
        state.next_velocity_y.resize(scenario.entity_count);
        state.next_alive.resize(scenario.entity_count);
        std::mt19937_64 generator{scenario.world.seed};
        for (std::size_t index = 0; index < scenario.entity_count; ++index) {
            state.x[index] = random_unit(generator) * scenario.world.width;
            state.y[index] = random_unit(generator) * scenario.world.height;
        }
    }
    for (const CharacterPlan &plan : scenario.characters) {
        if (plan.positioned) {
            state.x[plan.first] = plan.initial_x;
            state.y[plan.first] = plan.initial_y;
        }
        if (!plan.initial_alive) {
            std::fill_n(state.alive.begin() +
                            static_cast<std::ptrdiff_t>(plan.first),
                        plan.count, 0U);
        }
    }
    return state;
}

[[nodiscard]] static Metrics simulate_continuous(
    const Scenario &scenario, State &state, const SnapshotObserver observer,
    void *const context, const std::uint64_t snapshot_stride,
    const StepController controller, void *const controller_context,
    const std::uint64_t first_step, const std::uint64_t step_count) {
    if (needs_extended_continuous(scenario)) {
        return simulate_extended_continuous(
            scenario, state, observer, context, snapshot_stride, controller,
            controller_context, first_step, step_count);
    }
    Metrics metrics;
    const std::size_t count = scenario.entity_count;
    if (state.x.size() != count || state.y.size() != count ||
        state.next_x.size() != count || state.next_y.size() != count ||
        state.velocity_x.size() != count || state.velocity_y.size() != count ||
        state.next_velocity_x.size() != count ||
        state.next_velocity_y.size() != count || state.alive.size() != count ||
        state.next_alive.size() != count) {
        return metrics;
    }
    for (std::size_t entity = 0; entity < count; ++entity) {
        if (!std::isfinite(state.x[entity]) ||
            !std::isfinite(state.y[entity]) || state.x[entity] < 0.0 ||
            state.x[entity] >= scenario.world.width || state.y[entity] < 0.0 ||
            state.y[entity] >= scenario.world.height) {
            return metrics;
        }
    }
    Grid grid = make_grid(scenario, true);

    for (std::uint64_t step = 0; step < step_count; ++step) {
        const std::uint64_t frame = first_step + step + 1U;
        if (controller != nullptr &&
            !controller(frame, scenario, state, controller_context)) {
            return metrics;
        }
        // Read current buffers, then publish movement and deaths together
        std::copy(state.x.begin(), state.x.end(), state.next_x.begin());
        std::copy(state.y.begin(), state.y.end(), state.next_y.begin());
        std::copy(state.velocity_x.begin(), state.velocity_x.end(),
                  state.next_velocity_x.begin());
        std::copy(state.velocity_y.begin(), state.velocity_y.end(),
                  state.next_velocity_y.begin());
        std::copy(state.alive.begin(), state.alive.end(),
                  state.next_alive.begin());
        build_grid(grid, state);

        for (std::size_t character_index = 0;
             character_index < scenario.characters.size(); ++character_index) {
            const CharacterPlan &plan = scenario.characters[character_index];
            if (plan.target >= scenario.characters.size()) {
                continue;
            }

            const CharacterPlan &target = scenario.characters[plan.target];
            const bool seeking = has_behaviour(plan, seek);
            const bool fleeing = has_behaviour(plan, flee);
            const bool consuming = has_behaviour(plan, consume);
            if ((!seeking && !fleeing && !consuming) || (seeking && fleeing)) {
                continue;
            }

            const std::size_t end = plan.first + plan.count;
            const std::size_t target_end = target.first + target.count;
            for (std::size_t entity = plan.first; entity < end; ++entity) {
                if (state.alive[entity] == 0U) {
                    continue;
                }
                std::size_t nearest = count;
                double nearest_squared = plan.sensing_radius_squared;
                double nearest_x = 0.0;
                double nearest_y = 0.0;
                const std::size_t cell = grid.entity_cells[entity];
                std::array<GridIndex, 9U> neighbours{};
                const std::size_t neighbour_count =
                    neighbouring_cells(grid, cell, neighbours);
                for (std::size_t neighbour = 0; neighbour < neighbour_count;
                     ++neighbour) {
                    const std::size_t neighbour_cell = neighbours[neighbour];
                    std::size_t begin = grid.offsets[neighbour_cell];
                    std::size_t finish = grid.offsets[neighbour_cell + 1U];
                    if (!grid.type_splits.empty()) {
                        if (plan.target == 0U) {
                            finish = grid.type_splits[neighbour_cell];
                        } else {
                            begin = grid.type_splits[neighbour_cell];
                        }
                    }
                    for (std::size_t member = begin; member < finish;
                         ++member) {
                        const std::size_t candidate = grid.members[member];
                        if (grid.type_splits.empty()) {
                            if (candidate < target.first) {
                                continue;
                            }
                            if (candidate >= target_end) {
                                break;
                            }
                        }
                        if (candidate == entity) {
                            continue;
                        }
                        const double delta_x =
                            wrapped_delta(state.x[candidate] - state.x[entity],
                                          scenario.world.width);
                        const double delta_y =
                            wrapped_delta(state.y[candidate] - state.y[entity],
                                          scenario.world.height);
                        const double distance_squared =
                            delta_x * delta_x + delta_y * delta_y;
                        ++metrics.candidate_checks;
                        if (distance_squared > plan.sensing_radius_squared) {
                            continue;
                        }
                        ++metrics.sensed_interactions;
                        const bool nearer =
                            distance_squared < nearest_squared ||
                            (distance_squared == nearest_squared &&
                             candidate < nearest);
                        if (nearer) {
                            nearest = candidate;
                            nearest_squared = distance_squared;
                            nearest_x = delta_x;
                            nearest_y = delta_y;
                        }
                    }
                }

                if (nearest == count) {
                    continue;
                }
                if (consuming &&
                    nearest_squared <= plan.capture_radius_squared) {
                    state.next_alive[nearest] = 0U;
                }
                if (nearest_squared == 0.0 || (!seeking && !fleeing)) {
                    continue;
                }
                const double scale = std::min(
                    plan.step_distance / std::sqrt(nearest_squared), 1.0);
                const double direction = seeking ? 1.0 : -1.0;
                const double velocity_x = direction * nearest_x * scale;
                const double velocity_y = direction * nearest_y * scale;
                state.next_velocity_x[entity] = velocity_x;
                state.next_velocity_y[entity] = velocity_y;
                state.next_x[entity] = add_wrapped(state.x[entity], velocity_x,
                                                   scenario.world.width);
                state.next_y[entity] = add_wrapped(state.y[entity], velocity_y,
                                                   scenario.world.height);
            }
        }

        for (std::size_t entity = 0; entity < count; ++entity) {
            const bool alive = state.alive[entity] != 0U;
            metrics.entity_updates += static_cast<std::uint64_t>(alive);
            metrics.deaths += static_cast<std::uint64_t>(
                alive && state.next_alive[entity] == 0U);
        }
        state.x.swap(state.next_x);
        state.y.swap(state.next_y);
        state.velocity_x.swap(state.next_velocity_x);
        state.velocity_y.swap(state.next_velocity_y);
        state.alive.swap(state.next_alive);
        ++metrics.steps;
        if (observer != nullptr && snapshot_stride != 0U &&
            frame % snapshot_stride == 0U) {
            observer(frame, scenario, state, context);
        }
    }
    return metrics;
}

namespace {

[[nodiscard]] std::uint64_t mix_bits(std::uint64_t value) noexcept {
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] Metrics simulate_extended_continuous(
    const Scenario &scenario, State &state, const SnapshotObserver observer,
    void *context, const std::uint64_t snapshot_stride,
    const StepController controller, void *const controller_context,
    const std::uint64_t first_step, const std::uint64_t step_count) {
    Metrics metrics;
    const std::size_t count = scenario.entity_count;
    if (state.x.size() != count || state.y.size() != count ||
        state.next_x.size() != count || state.next_y.size() != count ||
        state.velocity_x.size() != count || state.velocity_y.size() != count ||
        state.next_velocity_x.size() != count ||
        state.next_velocity_y.size() != count || state.alive.size() != count ||
        state.next_alive.size() != count) {
        return metrics;
    }
    Grid grid = make_grid(scenario, false);
    for (std::uint64_t step = 0U; step < step_count; ++step) {
        const std::uint64_t frame = first_step + step + 1U;
        if (controller != nullptr &&
            !controller(frame, scenario, state, controller_context)) {
            return metrics;
        }
        std::copy(state.x.begin(), state.x.end(), state.next_x.begin());
        std::copy(state.y.begin(), state.y.end(), state.next_y.begin());
        std::copy(state.velocity_x.begin(), state.velocity_x.end(),
                  state.next_velocity_x.begin());
        std::copy(state.velocity_y.begin(), state.velocity_y.end(),
                  state.next_velocity_y.begin());
        std::copy(state.alive.begin(), state.alive.end(),
                  state.next_alive.begin());
        build_grid(grid, state);
        for (const CharacterPlan &plan : scenario.characters) {
            const std::size_t behaviour_end =
                plan.first_behaviour + plan.behaviour_count;
            if (behaviour_end > scenario.behaviour_plan.size()) {
                return Metrics{};
            }
            const std::size_t entity_end = plan.first + plan.count;
            for (std::size_t entity = plan.first; entity < entity_end;
                 ++entity) {
                if (state.alive[entity] == 0U) {
                    continue;
                }
                double force_x = 0.0;
                double force_y = 0.0;
                const std::size_t cell = grid.entity_cells[entity];
                std::array<GridIndex, 9U> nearby{};
                const std::size_t nearby_count =
                    neighbouring_cells(grid, cell, nearby);
                for (std::size_t neighbour = 0U; neighbour < nearby_count;
                     ++neighbour) {
                    const std::size_t neighbour_cell = nearby[neighbour];
                    for (std::size_t member = grid.offsets[neighbour_cell];
                         member < grid.offsets[neighbour_cell + 1U]; ++member) {
                        const std::size_t candidate = grid.members[member];
                        if (candidate == entity) {
                            continue;
                        }
                        const double delta_x =
                            wrapped_delta(state.x[candidate] - state.x[entity],
                                          scenario.world.width);
                        const double delta_y =
                            wrapped_delta(state.y[candidate] - state.y[entity],
                                          scenario.world.height);
                        const double distance_squared =
                            delta_x * delta_x + delta_y * delta_y;
                        ++metrics.candidate_checks;
                        if (distance_squared == 0.0 ||
                            distance_squared > plan.sensing_radius_squared) {
                            continue;
                        }
                        ++metrics.sensed_interactions;
                        const double inverse_distance =
                            1.0 / std::sqrt(distance_squared);
                        for (std::size_t index = plan.first_behaviour;
                             index < behaviour_end; ++index) {
                            const BehaviourRecord &record =
                                scenario.behaviour_plan[index];
                            if (record.target >= scenario.characters.size()) {
                                continue;
                            }
                            const CharacterPlan &target =
                                scenario.characters[record.target];
                            if (candidate < target.first ||
                                candidate >= target.first + target.count) {
                                continue;
                            }
                            const double weight = record.weight;
                            const double unit_x = delta_x * inverse_distance;
                            const double unit_y = delta_y * inverse_distance;
                            if (record.code == BehaviourCode::seek ||
                                record.code == BehaviourCode::cohere) {
                                force_x += unit_x * weight;
                                force_y += unit_y * weight;
                            } else if (record.code == BehaviourCode::flee ||
                                       record.code == BehaviourCode::avoid ||
                                       record.code == BehaviourCode::separate) {
                                force_x -= unit_x * weight;
                                force_y -= unit_y * weight;
                            } else if (record.code == BehaviourCode::align) {
                                force_x += (state.velocity_x[candidate] -
                                            state.velocity_x[entity]) *
                                           weight;
                                force_y += (state.velocity_y[candidate] -
                                            state.velocity_y[entity]) *
                                           weight;
                            } else if (record.code == BehaviourCode::consume &&
                                       distance_squared <=
                                           plan.capture_radius_squared) {
                                state.next_alive[candidate] = 0U;
                            }
                        }
                    }
                }
                for (std::size_t index = plan.first_behaviour;
                     index < behaviour_end; ++index) {
                    const BehaviourRecord &record =
                        scenario.behaviour_plan[index];
                    if (record.code != BehaviourCode::wander) {
                        continue;
                    }
                    const std::uint64_t bits = mix_bits(
                        scenario.world.seed ^ (step * 0x9e3779b97f4a7c15ULL) ^
                        static_cast<std::uint64_t>(entity));
                    const double weight = record.weight;
                    if ((bits & 1U) == 0U) {
                        force_x += weight;
                    } else {
                        force_x -= weight;
                    }
                    if ((bits & 2U) == 0U) {
                        force_y += weight;
                    } else {
                        force_y -= weight;
                    }
                }
                const double magnitude_squared =
                    force_x * force_x + force_y * force_y;
                if (magnitude_squared == 0.0) {
                    continue;
                }
                const double scale = std::min(
                    plan.step_distance / std::sqrt(magnitude_squared), 1.0);
                const double velocity_x = force_x * scale;
                const double velocity_y = force_y * scale;
                state.next_velocity_x[entity] = velocity_x;
                state.next_velocity_y[entity] = velocity_y;
                state.next_x[entity] = add_wrapped(state.x[entity], velocity_x,
                                                   scenario.world.width);
                state.next_y[entity] = add_wrapped(state.y[entity], velocity_y,
                                                   scenario.world.height);
            }
        }
        for (std::size_t entity = 0U; entity < count; ++entity) {
            const bool alive = state.alive[entity] != 0U;
            metrics.entity_updates += static_cast<std::uint64_t>(alive);
            metrics.deaths += static_cast<std::uint64_t>(
                alive && state.next_alive[entity] == 0U);
        }
        state.x.swap(state.next_x);
        state.y.swap(state.next_y);
        state.velocity_x.swap(state.next_velocity_x);
        state.velocity_y.swap(state.next_velocity_y);
        state.alive.swap(state.next_alive);
        ++metrics.steps;
        if (observer != nullptr && snapshot_stride != 0U &&
            frame % snapshot_stride == 0U) {
            observer(frame, scenario, state, context);
        }
    }
    return metrics;
}

} // namespace

[[nodiscard]] static Metrics simulate_timeline(
    const Scenario &scenario, State &state, const SnapshotObserver observer,
    void *const context, const std::uint64_t snapshot_stride,
    const StepController controller, void *const controller_context,
    const std::uint64_t first_step, const std::uint64_t step_count) {
    Metrics metrics;
    if (state.x.size() != scenario.entity_count ||
        state.y.size() != scenario.entity_count ||
        state.alive.size() != scenario.entity_count) {
        return metrics;
    }
    const auto first = scenario.events.begin();
    std::size_t event_index = static_cast<std::size_t>(
        std::lower_bound(
            first, scenario.events.end(), first_step + 1U,
            [](const TimelineEvent &event, const std::uint64_t frame) {
                return event.step < frame;
            }) -
        first);
    for (std::uint64_t step = 0U; step < step_count; ++step) {
        const std::uint64_t frame = first_step + step + 1U;
        if (controller != nullptr &&
            !controller(frame, scenario, state, controller_context)) {
            return metrics;
        }
        // Config order breaks ties between events on the same frame
        while (event_index < scenario.events.size() &&
               scenario.events[event_index].step == frame) {
            const TimelineEvent &event = scenario.events[event_index++];
            const bool was_alive = state.alive[event.entity] != 0U;
            if (event.action == EventAction::move) {
                state.x[event.entity] = event.x;
                state.y[event.entity] = event.y;
            } else {
                const bool alive = event.action == EventAction::show;
                state.alive[event.entity] = static_cast<std::uint8_t>(alive);
                metrics.births +=
                    static_cast<std::uint64_t>(alive && !was_alive);
                metrics.deaths +=
                    static_cast<std::uint64_t>(!alive && was_alive);
            }
            ++metrics.entity_updates;
            ++metrics.timeline_events;
        }
        ++metrics.steps;
        if (observer != nullptr && snapshot_stride != 0U &&
            frame % snapshot_stride == 0U) {
            observer(frame, scenario, state, context);
        }
    }
    return metrics;
}

[[nodiscard]] Graph make_turn_graph(const TurnPlan &plan) {
    const std::size_t nodes = plan.columns * plan.rows;
    Graph graph;
    graph.offsets.resize(nodes + 1U);
    std::vector<std::size_t> degree(nodes);
    if (plan.edges.empty()) {
        for (std::size_t row = 0U; row < plan.rows; ++row) {
            for (std::size_t column = 0U; column < plan.columns; ++column) {
                const std::size_t node = row * plan.columns + column;
                degree[node] =
                    static_cast<std::size_t>(column != 0U) +
                    static_cast<std::size_t>(column + 1U < plan.columns) +
                    static_cast<std::size_t>(row != 0U) +
                    static_cast<std::size_t>(row + 1U < plan.rows);
            }
        }
    } else {
        for (std::size_t index = 0U; index < plan.edges.size(); index += 2U) {
            ++degree[plan.edges[index]];
        }
    }
    for (std::size_t node = 0U; node < nodes; ++node) {
        graph.offsets[node + 1U] = graph.offsets[node] + degree[node];
    }
    graph.destinations.resize(graph.offsets.back());
    graph.costs.assign(graph.destinations.size(), 1U);
    std::vector<std::size_t> cursor = graph.offsets;
    const auto add_edge = [&graph, &cursor](const std::size_t source,
                                            const std::size_t destination) {
        graph.destinations[cursor[source]++] =
            static_cast<std::uint32_t>(destination);
    };
    if (plan.edges.empty()) {
        for (std::size_t row = 0U; row < plan.rows; ++row) {
            for (std::size_t column = 0U; column < plan.columns; ++column) {
                const std::size_t node = row * plan.columns + column;
                if (column != 0U)
                    add_edge(node, node - 1U);
                if (column + 1U < plan.columns)
                    add_edge(node, node + 1U);
                if (row != 0U)
                    add_edge(node, node - plan.columns);
                if (row + 1U < plan.rows)
                    add_edge(node, node + plan.columns);
            }
        }
    } else {
        for (std::size_t index = 0U; index < plan.edges.size(); index += 2U) {
            add_edge(plan.edges[index], plan.edges[index + 1U]);
        }
    }
    return graph;
}

struct GridDistance {
    std::size_t columns = 0U;
};

[[nodiscard]] std::uint64_t grid_distance(const std::uint32_t source,
                                          const std::uint32_t destination,
                                          void *const context) noexcept {
    const std::size_t columns = static_cast<GridDistance *>(context)->columns;
    const std::size_t source_row = source / columns;
    const std::size_t source_column = source % columns;
    const std::size_t destination_row = destination / columns;
    const std::size_t destination_column = destination % columns;
    const std::size_t rows = source_row > destination_row
                                 ? source_row - destination_row
                                 : destination_row - source_row;
    const std::size_t columns_delta = source_column > destination_column
                                          ? source_column - destination_column
                                          : destination_column - source_column;
    return static_cast<std::uint64_t>(rows) +
           static_cast<std::uint64_t>(columns_delta);
}

[[nodiscard]] static Metrics
simulate_turn(const Scenario &scenario, State &state,
              const SnapshotObserver observer, void *const context,
              const std::uint64_t snapshot_stride,
              const StepController controller, void *const controller_context,
              const std::uint64_t first_step, const std::uint64_t step_count) {
    Metrics metrics;
    const std::size_t cells = scenario.turn.columns * scenario.turn.rows;
    if (cells == 0U || state.board.size() != cells) {
        return metrics;
    }
    const Graph graph = make_turn_graph(scenario.turn);
    GridDistance distance{scenario.turn.columns};
    const std::size_t max_expansions =
        scenario.turn.search_budget >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max())
            ? std::numeric_limits<std::size_t>::max()
            : static_cast<std::size_t>(scenario.turn.search_budget);
    std::uint32_t set_verb = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t search_verb = std::numeric_limits<std::uint32_t>::max();
    for (std::size_t index = 0U; index < scenario.symbols.size(); ++index) {
        if (scenario.symbols[index] == "set") {
            set_verb = static_cast<std::uint32_t>(index);
        } else if (scenario.symbols[index] == "search") {
            search_verb = static_cast<std::uint32_t>(index);
        }
    }
    const auto first = scenario.actions.begin();
    std::size_t action_index = static_cast<std::size_t>(
        std::lower_bound(
            first, scenario.actions.end(), first_step + 1U,
            [](const ActionPlan &action, const std::uint64_t frame) {
                return action.step < frame;
            }) -
        first);
    for (std::uint64_t step = 0U; step < step_count; ++step) {
        const std::uint64_t frame = first_step + step + 1U;
        if (controller != nullptr &&
            !controller(frame, scenario, state, controller_context)) {
            return metrics;
        }
        while (action_index < scenario.actions.size() &&
               scenario.actions[action_index].step == frame) {
            const ActionPlan &action = scenario.actions[action_index++];
            const double cell = action.arguments[0];
            const double value = action.arguments[1];
            const bool valid_cell = std::isfinite(cell) && cell >= 0.0 &&
                                    std::floor(cell) == cell &&
                                    cell < static_cast<double>(cells);
            const bool valid_value =
                std::isfinite(value) && value >= 0.0 &&
                std::floor(value) == value &&
                value <= static_cast<double>(
                             std::numeric_limits<std::uint32_t>::max());
            if (action.verb == set_verb && valid_cell && valid_value) {
                state.board[static_cast<std::size_t>(cell)] =
                    static_cast<std::uint32_t>(value);
                ++metrics.entity_updates;
            } else if (action.verb == search_verb && valid_cell &&
                       valid_value) {
                const std::uint32_t start = static_cast<std::uint32_t>(cell);
                const std::uint32_t goal = static_cast<std::uint32_t>(value);
                std::optional<Path> path;
                std::size_t expanded = 0U;
                if (scenario.turn.search == SearchAlgorithm::bfs) {
                    path = breadth_first_path(graph, start, goal,
                                              max_expansions, &expanded);
                } else if (scenario.turn.search == SearchAlgorithm::dijkstra) {
                    path = dijkstra_path(graph, start, goal, max_expansions,
                                         &expanded);
                } else if (scenario.turn.search == SearchAlgorithm::astar) {
                    path = a_star_path(
                        graph, start, goal,
                        scenario.turn.edges.empty() ? grid_distance : nullptr,
                        scenario.turn.edges.empty()
                            ? static_cast<void *>(&distance)
                            : nullptr,
                        max_expansions, &expanded);
                }
                static_cast<void>(path);
                metrics.path_expansions += expanded;
            }
        }
        ++state.turn;
        ++metrics.steps;
        ++metrics.turns;
        if (observer != nullptr && snapshot_stride != 0U &&
            frame % snapshot_stride == 0U) {
            observer(frame, scenario, state, context);
        }
    }
    return metrics;
}

[[nodiscard]] static Metrics simulate_cellular(
    const Scenario &scenario, State &state, const SnapshotObserver observer,
    void *const context, const std::uint64_t snapshot_stride,
    const StepController controller, void *const controller_context,
    const std::uint64_t first_step, const std::uint64_t step_count) {
    Metrics metrics;
    const std::size_t columns = scenario.cellular.columns;
    const std::size_t rows = scenario.cellular.rows;
    const std::uint8_t state_count = scenario.cellular.state_count;
    if (columns == 0U || rows == 0U || state.cells.size() != columns * rows ||
        state.next_cells.size() != state.cells.size() || state_count == 0U) {
        return metrics;
    }
    struct Neighbours {
        std::array<GridIndex, 8U> indices{};
        std::uint8_t count = 0U;
    };
    std::vector<Neighbours> neighbours(state.cells.size());
    std::uint64_t candidate_checks = 0U;
    for (std::size_t row = 0U; row < rows; ++row) {
        for (std::size_t column = 0U; column < columns; ++column) {
            Neighbours &list = neighbours[row * columns + column];
            for (int row_offset = -1; row_offset <= 1; ++row_offset) {
                for (int column_offset = -1; column_offset <= 1;
                     ++column_offset) {
                    if (row_offset == 0 && column_offset == 0) {
                        continue;
                    }
                    const bool outside_row =
                        (row_offset < 0 && row == 0U) ||
                        (row_offset > 0 && row + 1U == rows);
                    const bool outside_column =
                        (column_offset < 0 && column == 0U) ||
                        (column_offset > 0 && column + 1U == columns);
                    if (!scenario.cellular.wraps &&
                        (outside_row || outside_column)) {
                        continue;
                    }
                    const std::size_t neighbour_row =
                        adjacent(row, row_offset, rows);
                    const std::size_t neighbour_column =
                        adjacent(column, column_offset, columns);
                    list.indices[list.count++] = static_cast<GridIndex>(
                        neighbour_row * columns + neighbour_column);
                }
            }
            candidate_checks += list.count;
        }
    }
    std::array<std::uint8_t, std::size_t{256U} * 9U> transition{};
    for (std::size_t current = 0U; current < state_count; ++current) {
        for (std::size_t count = 0U; count <= 8U; ++count) {
            if (state_count == 2U) {
                const std::uint16_t mask = current == 0U
                                               ? scenario.cellular.birth_mask
                                               : scenario.cellular.survive_mask;
                transition[current * 9U + count] = static_cast<std::uint8_t>(
                    (mask & (std::uint16_t{1U} << count)) != 0U);
            } else {
                transition[current * 9U + count] =
                    static_cast<std::uint8_t>(current);
            }
        }
    }
    if (scenario.cellular.transition.size() ==
        static_cast<std::size_t>(state_count) * 9U) {
        std::copy(scenario.cellular.transition.begin(),
                  scenario.cellular.transition.end(), transition.begin());
    }
    for (std::uint64_t step = 0U; step < step_count; ++step) {
        const std::uint64_t frame = first_step + step + 1U;
        if (controller != nullptr &&
            !controller(frame, scenario, state, controller_context)) {
            return metrics;
        }
        // Count neighbours, then use the prebuilt transition table
        for (std::size_t cell = 0U; cell < state.cells.size(); ++cell) {
            const Neighbours &list = neighbours[cell];
            std::uint8_t active = 0U;
            for (std::size_t index = 0U; index < list.count; ++index) {
                active += static_cast<std::uint8_t>(
                    state.cells[list.indices[index]] ==
                    scenario.cellular.count_state);
            }
            const std::uint8_t current = state.cells[cell];
            if (current >= state_count) {
                return Metrics{};
            }
            const std::uint8_t next = transition[current * 9U + active];
            state.next_cells[cell] = next;
            metrics.births +=
                static_cast<std::uint64_t>(next != 0U && current == 0U);
            metrics.deaths +=
                static_cast<std::uint64_t>(next == 0U && current != 0U);
        }
        metrics.entity_updates +=
            static_cast<std::uint64_t>(state.cells.size());
        metrics.cell_updates += static_cast<std::uint64_t>(state.cells.size());
        metrics.candidate_checks += candidate_checks;
        state.cells.swap(state.next_cells);
        ++metrics.steps;
        if (observer != nullptr && snapshot_stride != 0U &&
            frame % snapshot_stride == 0U) {
            observer(frame, scenario, state, context);
        }
    }
    return metrics;
}

[[nodiscard]] Metrics simulate_native(
    const Scenario &scenario, State &state, const SnapshotObserver observer,
    void *const context, const std::uint64_t snapshot_stride,
    const StepController controller, void *const controller_context,
    const std::uint64_t first_step, const std::uint64_t step_count) {
    if (scenario.kernel == Kernel::turn) {
        return simulate_turn(scenario, state, observer, context,
                             snapshot_stride, controller, controller_context,
                             first_step, step_count);
    }
    if (scenario.kernel == Kernel::timeline) {
        return simulate_timeline(scenario, state, observer, context,
                                 snapshot_stride, controller,
                                 controller_context, first_step, step_count);
    }
    if (scenario.kernel == Kernel::cellular) {
        return simulate_cellular(scenario, state, observer, context,
                                 snapshot_stride, controller,
                                 controller_context, first_step, step_count);
    }
    return simulate_continuous(scenario, state, observer, context,
                               snapshot_stride, controller, controller_context,
                               first_step, step_count);
}

namespace {

struct ScriptContext {
    const Scenario *scenario = nullptr;
    State *state = nullptr;
    std::mt19937_64 random;
};

[[nodiscard]] bool script_entity_value(void *const context,
                                       const std::uint64_t entity,
                                       const std::uint32_t field,
                                       double &value) noexcept {
    const auto &script = *static_cast<ScriptContext *>(context);
    const State &state = *script.state;
    if (entity >= state.x.size()) {
        return false;
    }
    if (field == 0U)
        value = state.x[entity];
    else if (field == 1U)
        value = state.y[entity];
    else if (field == 2U && entity < state.velocity_x.size())
        value = state.velocity_x[entity];
    else if (field == 3U && entity < state.velocity_y.size())
        value = state.velocity_y[entity];
    else if (field == 4U)
        value = state.alive[entity];
    else
        return false;
    return true;
}

[[nodiscard]] bool script_state_value(void *const context,
                                      const std::uint32_t field,
                                      double &value) noexcept {
    const auto &script = *static_cast<ScriptContext *>(context);
    if (field >= script.state->scalars.size()) {
        return false;
    }
    const ScalarPlan &scalar = script.state->scalars[field];
    if (scalar.kind == ScalarKind::boolean)
        value = scalar.boolean ? 1.0 : 0.0;
    else if (scalar.kind == ScalarKind::integer)
        value = static_cast<double>(scalar.integer);
    else if (scalar.kind == ScalarKind::number)
        value = scalar.number;
    else
        value = scalar.identifier;
    return true;
}

[[nodiscard]] bool script_buffer_value(void *const context,
                                       const std::uint32_t buffer,
                                       const std::size_t index,
                                       double &value) noexcept {
    const auto &script = *static_cast<ScriptContext *>(context);
    if (buffer >= script.state->buffers.size()) {
        return false;
    }
    const BufferState &source = script.state->buffers[buffer];
    if (source.kind == ScalarKind::boolean && index < source.booleans.size()) {
        value = source.booleans[index] == 0U ? 0.0 : 1.0;
    } else if (source.kind == ScalarKind::integer &&
               index < source.integers.size()) {
        value = static_cast<double>(source.integers[index]);
    } else if (source.kind == ScalarKind::number &&
               index < source.numbers.size()) {
        value = source.numbers[index];
    } else if (source.kind == ScalarKind::identifier &&
               index < source.identifiers.size()) {
        value = static_cast<double>(source.identifiers[index]);
    } else {
        return false;
    }
    return true;
}

[[nodiscard]] bool script_board_value(void *const context,
                                      const std::size_t cell,
                                      double &value) noexcept {
    const auto &script = *static_cast<ScriptContext *>(context);
    if (cell >= script.state->board.size()) {
        return false;
    }
    value = static_cast<double>(script.state->board[cell]);
    return true;
}

[[nodiscard]] bool script_entity_exists(void *const context,
                                        const std::uint64_t entity) noexcept {
    const auto &script = *static_cast<ScriptContext *>(context);
    return entity < script.state->x.size();
}

[[nodiscard]] bool script_type_exists(void *const context,
                                      const std::uint32_t type) noexcept {
    const auto &script = *static_cast<ScriptContext *>(context);
    return type < script.scenario->characters.size();
}

[[nodiscard]] bool script_field_writable(void *const context,
                                         const std::uint32_t field) noexcept {
    const auto &script = *static_cast<ScriptContext *>(context);
    return field <= 1U || field == 4U ||
           ((field == 2U || field == 3U) &&
            script.state->velocity_x.size() == script.state->x.size());
}

[[nodiscard]] bool script_cue_exists(void *const context,
                                     const std::uint32_t cue) noexcept {
    const auto &script = *static_cast<ScriptContext *>(context);
    return cue < script.scenario->cues.size();
}

[[nodiscard]] bool script_board_cell(void *const context,
                                     const std::size_t cell,
                                     const std::uint32_t) noexcept {
    const auto &script = *static_cast<ScriptContext *>(context);
    return cell < script.state->board.size();
}

[[nodiscard]] bool script_resolve_id(void *const context,
                                     const ScriptNameKind kind,
                                     const char *const text,
                                     const std::size_t size,
                                     std::uint32_t &id) noexcept {
    const auto &script = *static_cast<ScriptContext *>(context);
    const std::string_view name{text, size};
    std::size_t found = std::numeric_limits<std::size_t>::max();
    const auto match_names = [&name,
                              &found](const std::vector<std::string> &items) {
        for (std::size_t index = 0U; index < items.size(); ++index) {
            if (items[index] == name) {
                if (found != std::numeric_limits<std::size_t>::max()) {
                    return false;
                }
                found = index;
            }
        }
        return true;
    };
    const auto match_plans = [&name, &found](const auto &items) {
        for (std::size_t index = 0U; index < items.size(); ++index) {
            if (items[index].name == name) {
                if (found != std::numeric_limits<std::size_t>::max()) {
                    return false;
                }
                found = index;
            }
        }
        return true;
    };
    if (kind == ScriptNameKind::type) {
        if (!match_names(script.scenario->names))
            return false;
    } else if (kind == ScriptNameKind::symbol) {
        if (!match_names(script.scenario->symbols))
            return false;
    } else if (kind == ScriptNameKind::state) {
        if (!match_plans(script.state->scalars))
            return false;
    } else if (kind == ScriptNameKind::buffer) {
        if (!match_plans(script.scenario->buffers))
            return false;
    } else if (kind == ScriptNameKind::cue) {
        if (!match_names(script.scenario->cue_names))
            return false;
    } else {
        return false;
    }
    if (found == std::numeric_limits<std::size_t>::max() ||
        found > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    id = static_cast<std::uint32_t>(found);
    return true;
}

[[nodiscard]] std::uint64_t script_random(void *const context) noexcept {
    return static_cast<ScriptContext *>(context)->random();
}

[[nodiscard]] bool apply_script_commands(const Script &script,
                                         ScriptContext &context,
                                         Metrics &metrics) noexcept {
    State &state = *context.state;
    const Scenario &scenario = *context.scenario;
    for (const ScriptCommand &command : script.commands()) {
        if (command.kind == ScriptCommandKind::cue) {
            ++metrics.timeline_events;
            continue;
        }
        if (command.kind == ScriptCommandKind::board_set) {
            if (command.entity >= state.board.size())
                return false;
            state.board[static_cast<std::size_t>(command.entity)] =
                command.field;
            ++metrics.entity_updates;
            continue;
        }
        if (command.kind == ScriptCommandKind::spawn) {
            if (command.type >= scenario.characters.size() ||
                command.first < 0.0 || command.first >= scenario.world.width ||
                command.second < 0.0 ||
                command.second >= scenario.world.height) {
                return false;
            }
            const CharacterPlan &plan = scenario.characters[command.type];
            const std::size_t end = plan.first + plan.count;
            std::size_t entity = plan.first;
            while (entity < end && state.alive[entity] != 0U)
                ++entity;
            if (entity == end)
                return false;
            state.x[entity] = command.first;
            state.y[entity] = command.second;
            if (entity < state.velocity_x.size()) {
                state.velocity_x[entity] = 0.0;
                state.velocity_y[entity] = 0.0;
            }
            state.alive[entity] = 1U;
            ++metrics.births;
            continue;
        }
        if (command.entity >= state.x.size())
            return false;
        const std::size_t entity = static_cast<std::size_t>(command.entity);
        if (command.kind == ScriptCommandKind::move) {
            if (command.first < 0.0 || command.first >= scenario.world.width ||
                command.second < 0.0 ||
                command.second >= scenario.world.height) {
                return false;
            }
            state.x[entity] = command.first;
            state.y[entity] = command.second;
            ++metrics.entity_updates;
        } else if (command.kind == ScriptCommandKind::show) {
            metrics.births +=
                static_cast<std::uint64_t>(state.alive[entity] == 0U);
            state.alive[entity] = 1U;
        } else if (command.kind == ScriptCommandKind::hide ||
                   command.kind == ScriptCommandKind::kill) {
            metrics.deaths +=
                static_cast<std::uint64_t>(state.alive[entity] != 0U);
            state.alive[entity] = 0U;
        } else if (command.kind == ScriptCommandKind::set) {
            if (command.field == 0U && command.first >= 0.0 &&
                command.first < scenario.world.width)
                state.x[entity] = command.first;
            else if (command.field == 1U && command.first >= 0.0 &&
                     command.first < scenario.world.height)
                state.y[entity] = command.first;
            else if (command.field == 2U && entity < state.velocity_x.size())
                state.velocity_x[entity] = command.first;
            else if (command.field == 3U && entity < state.velocity_y.size())
                state.velocity_y[entity] = command.first;
            else if (command.field == 4U)
                state.alive[entity] =
                    static_cast<std::uint8_t>(command.first != 0.0);
            else
                return false;
            ++metrics.entity_updates;
        }
    }
    return true;
}

void add_metrics(Metrics &total, const Metrics &step) noexcept {
    total.steps += step.steps;
    total.entity_updates += step.entity_updates;
    total.candidate_checks += step.candidate_checks;
    total.sensed_interactions += step.sensed_interactions;
    total.births += step.births;
    total.deaths += step.deaths;
    total.cell_updates += step.cell_updates;
    total.turns += step.turns;
    total.search_nodes += step.search_nodes;
    total.path_expansions += step.path_expansions;
    total.timeline_events += step.timeline_events;
}

void call_script(Script &script, const ScriptCallback callback,
                 const std::uint64_t step, ScriptContext &context,
                 Metrics &metrics, const std::string &path) {
    ScriptError error;
    if (!script.call(callback, step, error)) {
        const std::string source = error.path.empty() ? path : error.path;
        const std::string line =
            error.line == 0U ? "" : ":" + std::to_string(error.line);
        throw std::runtime_error(source + line + ": step " +
                                 std::to_string(step) + ": " + error.message);
    }
    if (!apply_script_commands(script, context, metrics)) {
        throw std::runtime_error(path + ": step " + std::to_string(step) +
                                 ": script command rejected");
    }
}

[[nodiscard]] Metrics simulate_scripted(const Scenario &scenario, State &state,
                                        const SnapshotObserver observer,
                                        void *const context,
                                        const std::uint64_t snapshot_stride,
                                        const StepController controller,
                                        void *const controller_context) {
    ScriptContext script_context{&scenario, &state,
                                 std::mt19937_64{scenario.world.seed}};
    const ScriptHost host{
        &script_context,     script_entity_value,   script_state_value,
        script_buffer_value, script_board_value,    script_entity_exists,
        script_type_exists,  script_field_writable, script_cue_exists,
        script_board_cell,   script_resolve_id,     script_random};
    Script script;
    ScriptError error;
    const std::string path =
        scenario.source_directory + '/' + scenario.lua_rules;
    if (!script.load(path, host, error)) {
        const std::string source = error.path.empty() ? path : error.path;
        const std::string line =
            error.line == 0U ? "" : ":" + std::to_string(error.line);
        throw std::runtime_error(source + line + ": " + error.message);
    }
    Metrics total;
    call_script(script, ScriptCallback::setup, 0U, script_context, total, path);
    if (scenario.kernel == Kernel::cellular) {
        call_script(script, ScriptCallback::cellular_compile, 0U,
                    script_context, total, path);
    }
    for (std::uint64_t step = 0U; step < scenario.world.steps; ++step) {
        const std::uint64_t frame = step + 1U;
        if (controller != nullptr &&
            !controller(frame, scenario, state, controller_context)) {
            return total;
        }
        call_script(script, ScriptCallback::before_step, frame, script_context,
                    total, path);
        const ScriptCallback mode_callback =
            scenario.kernel == Kernel::turn       ? ScriptCallback::turn
            : scenario.kernel == Kernel::timeline ? ScriptCallback::timeline
                                                  : ScriptCallback::before_step;
        if (mode_callback != ScriptCallback::before_step) {
            call_script(script, mode_callback, frame, script_context, total,
                        path);
        }
        const bool emit = observer != nullptr && snapshot_stride != 0U &&
                          frame % snapshot_stride == 0U;
        const Metrics result =
            simulate_native(scenario, state, emit ? observer : nullptr, context,
                            snapshot_stride, nullptr, nullptr, step, 1U);
        if (result.steps != 1U) {
            throw std::runtime_error(path + ": step " + std::to_string(frame) +
                                     ": native step failed");
        }
        add_metrics(total, result);
        call_script(script, ScriptCallback::after_step, frame, script_context,
                    total, path);
    }
    return total;
}

} // namespace

Metrics simulate(const Scenario &scenario, State &state,
                 const SnapshotObserver observer, void *const context,
                 const std::uint64_t snapshot_stride,
                 const StepController controller,
                 void *const controller_context) {
    if (scenario.lua_rules.empty()) {
        return simulate_native(scenario, state, observer, context,
                               snapshot_stride, controller, controller_context,
                               0U, scenario.world.steps);
    }
    return simulate_scripted(scenario, state, observer, context,
                             snapshot_stride, controller, controller_context);
}

std::uint64_t checksum(const State &state) noexcept {
    std::uint64_t value = fnv_offset;
    hash_u64(value, static_cast<std::uint64_t>(state.x.size()));
    for (std::size_t index = 0; index < state.x.size(); ++index) {
        hash_double(value, state.x[index]);
        hash_double(value, state.y[index]);
        hash_u64(value, state.alive[index]);
    }
    if (!state.cells.empty()) {
        hash_u64(value, static_cast<std::uint64_t>(state.cells.size()));
        for (const std::uint8_t cell : state.cells) {
            hash_u64(value, cell);
        }
    }
    return value;
}

std::size_t active_count(const State &state) noexcept {
    if (!state.cells.empty()) {
        return static_cast<std::size_t>(
            std::count(state.cells.begin(), state.cells.end(), 1U));
    }
    return static_cast<std::size_t>(
        std::count(state.alive.begin(), state.alive.end(), 1U));
}

int self_test() {
    if (search_self_test() != 0) {
        return 1;
    }
    {
        std::istringstream input;
        input.setstate(std::ios::badbit);
        std::string error;
        if (parse_scenario(input, error) ||
            error != "1: cannot read scenario") {
            return 1;
        }
    }
    {
        const double extent = std::numeric_limits<double>::max();
        const double positive =
            add_wrapped(extent * 0.75, extent * 0.5, extent);
        const double negative =
            add_wrapped(extent * 0.25, -extent * 0.5, extent);
        if (!std::isfinite(positive) || !std::isfinite(negative) ||
            positive < 0.0 || positive >= extent || negative < 0.0 ||
            negative >= extent) {
            return 1;
        }
        const double edge = std::nextafter(10.0, 0.0);
        if (add_wrapped(edge, (10.0 - edge) * 0.75, 10.0) >= 10.0) {
            return 1;
        }
    }

    {
        Scenario scenario = test_scenario(1U);
        const double fast = std::numeric_limits<double>::max();
        scenario.characters = {
            {0U, 1U, 1U, seek, fast, 100.0, 0.0},
            {1U, 1U, 0U, flee, fast, 100.0, 0.0},
        };
        scenario.entity_count = 2U;
        State state = initialise(scenario);
        state.x = {9.5, 0.5};
        state.y = {5.0, 5.0};
        const Metrics metrics = simulate(scenario, state);
        if (metrics.candidate_checks != 2U || !close(state.x[0], 0.5) ||
            !close(state.x[1], 1.5)) {
            return 1;
        }
    }

    {
        Scenario scenario = test_scenario(1U);
        scenario.characters = {
            {0U, 1U, 1U, consume, 0.0, 1.0, 0.01},
            {1U, 1U, 0U, flee, 1.0, 1.0, 0.0},
        };
        scenario.entity_count = 2U;
        State state = initialise(scenario);
        state.x = {2.0, 2.05};
        state.y = {2.0, 2.0};
        const Metrics metrics = simulate(scenario, state);
        if (metrics.deaths != 1U || state.alive[0] == 0U ||
            state.alive[1] != 0U || !close(state.x[1], 2.1)) {
            return 1;
        }
    }

    {
        Scenario scenario = test_scenario(1U);
        scenario.characters = {
            {0U, 1U, 1U, seek, 1.0, 4.0, 0.0},
            {1U, 2U, std::numeric_limits<std::uint32_t>::max(), 0U, 0.0, 0.0,
             0.0},
        };
        scenario.entity_count = 3U;
        State state = initialise(scenario);
        state.x = {5.0, 6.0, 4.0};
        state.y = {5.0, 5.0, 5.0};
        static_cast<void>(simulate(scenario, state));
        if (!close(state.x[0], 6.0)) {
            return 1;
        }
    }

    {
        Scenario scenario = test_scenario(1U);
        scenario.characters = {
            {1U, 1U, 1U, 0U, 0.0, 0.0, 0.0},
            {0U, 1U, 0U, seek, 1.0, 4.0, 0.0},
        };
        scenario.entity_count = 2U;
        State state = initialise(scenario);
        state.x = {5.0, 6.0};
        state.y = {5.0, 5.0};
        static_cast<void>(simulate(scenario, state));
        if (!close(state.x[0], 6.0)) {
            return 1;
        }
    }

    {
        Scenario scenario = test_scenario(0U);
        scenario.entity_count = 3U;
        const State first = initialise(scenario);
        const State second = initialise(scenario);
        if (checksum(first) != checksum(second)) {
            return 1;
        }
    }

    {
        Scenario scenario = test_scenario(1U);
        CharacterPlan left{};
        left.first = 0U;
        left.count = 1U;
        left.target = 1U;
        left.first_behaviour = 0U;
        left.behaviour_count = 1U;
        left.step_distance = 1.0;
        left.sensing_radius_squared = 4.0;
        CharacterPlan right{};
        right.first = 1U;
        right.count = 1U;
        right.target = 0U;
        scenario.characters = {left, right};
        scenario.behaviour_plan = {
            {BehaviourCode::cohere, 1U, 1.0, 0.0},
        };
        scenario.entity_count = 2U;
        State state = initialise(scenario);
        state.x = {5.0, 6.0};
        state.y = {5.0, 5.0};
        static_cast<void>(simulate(scenario, state));
        if (!close(state.x[0], 6.0) || !close(state.velocity_x[0], 1.0)) {
            return 1;
        }
    }

    {
        Scenario scenario = test_scenario(1U);
        CharacterPlan left{};
        left.first = 0U;
        left.count = 1U;
        left.target = 1U;
        left.first_behaviour = 0U;
        left.behaviour_count = 2U;
        left.step_distance = 1.0;
        left.sensing_radius_squared = 4.0;
        CharacterPlan right{};
        right.first = 1U;
        right.count = 1U;
        scenario.characters = {left, right};
        scenario.behaviour_plan = {
            {BehaviourCode::seek, 1U, 2.0, 0.0},
            {BehaviourCode::flee, 1U, 1.0, 0.0},
        };
        scenario.entity_count = 2U;
        State state = initialise(scenario);
        state.x = {5.0, 6.0};
        state.y = {5.0, 5.0};
        static_cast<void>(simulate(scenario, state));
        if (!close(state.x[0], 6.0) || !close(state.velocity_x[0], 1.0)) {
            return 1;
        }
    }

    {
        Scenario scenario = test_scenario(2U);
        scenario.entity_count = 1U;
        State state = initialise(scenario);
        ControllerProbe probe;
        const Metrics metrics = simulate(scenario, state, nullptr, nullptr, 1U,
                                         probe_controller, &probe);
        if (metrics.steps != 2U || probe.calls != 2U ||
            !close(state.x[0U], 1.0)) {
            return 1;
        }
    }

    {
        Scenario scenario = test_scenario(2U);
        scenario.kernel = Kernel::timeline;
        scenario.characters = {{
            0U,
            1U,
            std::numeric_limits<std::uint32_t>::max(),
            0U,
            0.0,
            0.0,
            0.0,
            1.0,
            2.0,
            true,
            true,
        }};
        scenario.entity_count = 1U;
        scenario.events = {
            {1U, 0U, EventAction::move, 3.0, 4.0},
            {1U, 0U, EventAction::move, 4.0, 5.0},
            {2U, 0U, EventAction::hide, 0.0, 0.0},
        };
        State state = initialise(scenario);
        const Metrics metrics = simulate(scenario, state);
        if (metrics.steps != 2U || metrics.entity_updates != 3U ||
            metrics.timeline_events != 3U || metrics.deaths != 1U ||
            state.alive[0] != 0U || !close(state.x[0], 4.0) ||
            !close(state.y[0], 5.0)) {
            return 1;
        }
    }

    {
        Scenario scenario = test_scenario(2U);
        scenario.kernel = Kernel::cellular;
        scenario.world.width = 5.0;
        scenario.world.height = 5.0;
        scenario.entity_count = 25U;
        scenario.cellular.columns = 5U;
        scenario.cellular.rows = 5U;
        scenario.cellular.initial.assign(25U, 0U);
        scenario.cellular.initial[11U] = 1U;
        scenario.cellular.initial[12U] = 1U;
        scenario.cellular.initial[13U] = 1U;
        scenario.cellular.birth_mask = std::uint16_t{1U} << 3U;
        scenario.cellular.survive_mask =
            (std::uint16_t{1U} << 2U) | (std::uint16_t{1U} << 3U);
        State state = initialise(scenario);
        const Metrics metrics = simulate(scenario, state);
        if (metrics.steps != 2U || metrics.entity_updates != 50U ||
            metrics.cell_updates != 50U || metrics.candidate_checks != 400U ||
            metrics.births != 4U || metrics.deaths != 4U ||
            state.cells != scenario.cellular.initial) {
            return 1;
        }
    }

    {
        Scenario scenario = test_scenario(1U);
        scenario.kernel = Kernel::turn;
        scenario.world.width = 2.0;
        scenario.world.height = 2.0;
        scenario.turn.columns = 2U;
        scenario.turn.rows = 2U;
        scenario.turn.search = SearchAlgorithm::astar;
        scenario.turn.search_budget = 16U;
        scenario.symbols = {"set", "search"};
        ActionPlan set{};
        set.step = 1U;
        set.verb = 0U;
        set.arguments = {3.0, 7.0, 0.0, 0.0};
        ActionPlan search{};
        search.step = 1U;
        search.verb = 1U;
        search.arguments = {0.0, 3.0, 0.0, 0.0};
        scenario.actions = {set, search};
        State state = initialise(scenario);
        const Metrics metrics = simulate(scenario, state);
        if (metrics.turns != 1U || state.board[3U] != 7U ||
            metrics.path_expansions != 4U) {
            return 1;
        }
    }

    {
        Scenario scenario = test_scenario(1U);
        scenario.characters = {
            {0U, 1U, std::numeric_limits<std::uint32_t>::max(), 0U, 0.0, 0.0,
             0.0},
        };
        scenario.entity_count = 1U;
        State state = initialise(scenario);
        const Metrics metrics = simulate(scenario, state);
        if (metrics.candidate_checks != 0U || metrics.entity_updates != 1U ||
            active_count(state) != 1U) {
            return 1;
        }
    }

    std::puts("m1 self-test: PASS");
    return 0;
}

} // namespace m1
