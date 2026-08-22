#include "model.hpp"
#include "simulation/internal.hpp"
#include "simulation/runtime/lua.hpp"

#include <array>
#include <string>

/// \file
/// Run Lua cell rules with C++ topology, buffering, and generation timing
namespace m1 {
struct CellNeighbours {
    // Eight neighbours at most, stored once for every immutable cell topology
    std::array<CellIndex, 8U> indices{};
    std::uint8_t count = 0U;
};

[[nodiscard]] std::vector<CellNeighbours>
cell_neighbours(const std::size_t rows, const std::size_t columns,
                const bool wraps, std::uint64_t &checks) {
    // Build topology once because cell adjacency never changes during a run
    std::vector<CellNeighbours> neighbours(rows * columns);
    for (std::size_t row = 0U; row < rows; ++row) {
        for (std::size_t column = 0U; column < columns; ++column) {
            CellNeighbours &list = neighbours[row * columns + column];
            for (int y = -1; y <= 1; ++y) {
                for (int x = -1; x <= 1; ++x) {
                    if ((x == 0 && y == 0) ||
                        (!wraps &&
                         ((y < 0 && row == 0U) || (y > 0 && row + 1U == rows) ||
                          (x < 0 && column == 0U) ||
                          (x > 0 && column + 1U == columns)))) {
                        continue;
                    }
                    list.indices[list.count++] = static_cast<CellIndex>(
                        adjacent(row, y, rows) * columns +
                        adjacent(column, x, columns));
                }
            }
            checks += list.count;
        }
    }
    return neighbours;
}

// Frame setup and topology
[[nodiscard]] Metrics simulate_scripted_cellular(
    const Scenario &scenario, State &state, const SnapshotObserver observer,
    void *const context, const std::uint64_t snapshot_stride,
    const StepController controller, void *const controller_context,
    ScenarioRuntime &runtime, const std::uint64_t first_step,
    const std::uint64_t step_count) {
    Metrics metrics;
    const std::size_t columns = scenario.cellular.columns;
    const std::size_t rows = scenario.cellular.rows;
    if (columns == 0U || rows == 0U || state.cells.size() != columns * rows ||
        state.next_cells.size() != state.cells.size()) {
        return metrics;
    }
    // Lua receives state counts rather than neighbour positions
    std::uint64_t candidate_checks = 0U;
    const std::vector<CellNeighbours> neighbours = cell_neighbours(
        rows, columns, scenario.cellular.wraps, candidate_checks);
    for (std::uint64_t step = 0U; step < step_count; ++step) {
        const std::uint64_t frame = first_step + step + 1U;
        // Callbacks may stop a run before the next generation is evaluated
        if (controller != nullptr &&
            !controller(frame, scenario, state, controller_context)) {
            return metrics;
        }
        // Evaluate from the current buffer only
        for (std::size_t cell = 0U; cell < state.cells.size(); ++cell) {
            // The callback sees a histogram of every byte-sized state
            std::array<std::uint8_t, 256U> counts{};
            for (std::size_t index = 0U; index < neighbours[cell].count;
                 ++index) {
                ++counts[state.cells[neighbours[cell].indices[index]]];
            }
            std::string error;
            std::uint8_t next = 0U;
            if (!invoke_cell_next(runtime, state.cells[cell], frame, cell,
                                  counts, next, error) ||
                next >= scenario.cellular.state_count) {
                return {};
            }
            state.next_cells[cell] = next;
        }
        metrics.entity_updates +=
            static_cast<std::uint64_t>(state.cells.size());
        metrics.cell_updates += static_cast<std::uint64_t>(state.cells.size());
        metrics.candidate_checks += candidate_checks;
        // Commit one generation together so update order cannot affect rules
        state.cells.swap(state.next_cells);
        ++metrics.steps;
        if (frame == scenario.world.steps && state.result < 0) {
            state.result = 0;
        }
        if (observer != nullptr && snapshot_stride != 0U &&
            frame % snapshot_stride == 0U) {
            // Snapshots always see the newly committed generation
            observer(frame, scenario, state, context);
        }
        if (state.result >= 0) {
            break;
        }
    }
    return metrics;
}

// Native dispatch
// Scripted cellular work enters through the runtime bridge, not this fallback
[[nodiscard]] Metrics simulate_native(
    const Scenario &scenario, State &state, const SnapshotObserver observer,
    void *const context, const std::uint64_t snapshot_stride,
    const StepController controller, void *const controller_context,
    const std::uint64_t first_step, const std::uint64_t step_count) {
    if (scenario.kernel == Kernel::turn) {
        // Board updates are driven through scripted turn callbacks
        return simulate_turn(scenario, state, observer, context,
                             snapshot_stride, controller, controller_context,
                             first_step, step_count);
    }
    if (scenario.kernel == Kernel::timeline) {
        // Timeline commands populate paths which this kernel interpolates
        return simulate_timeline(scenario, state, observer, context,
                                 snapshot_stride, controller,
                                 controller_context, first_step, step_count);
    }
    if (scenario.kernel == Kernel::cellular) {
        // Cellular rules need their compiled Lua runtime
        return {};
    }
    return simulate_continuous(scenario, state, observer, context,
                               snapshot_stride, controller, controller_context,
                               first_step, step_count);
}

} // namespace m1
