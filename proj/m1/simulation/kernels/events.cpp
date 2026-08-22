#include "model.hpp"
#include "simulation/internal.hpp"

/// \file
/// Advance timeline and turn state written by compiled callbacks
namespace m1 {
// Timeline frames
// Commands schedule paths by filling parallel fields in State
[[nodiscard]] Metrics simulate_timeline(
    const Scenario &scenario, State &state, const SnapshotObserver observer,
    void *const context, const std::uint64_t snapshot_stride,
    const StepController controller, void *const controller_context,
    const std::uint64_t first_step, const std::uint64_t step_count) {
    Metrics metrics;
    const std::size_t size = scenario.entity_count;
    if (state.x.size() != size || state.y.size() != size ||
        state.velocity_x.size() != size || state.velocity_y.size() != size ||
        state.timeline_z.size() != size ||
        state.timeline_state.size() != size ||
        state.timeline_text.size() != size ||
        state.timeline_start_x.size() != size ||
        state.timeline_start_y.size() != size ||
        state.timeline_start_z.size() != size ||
        state.timeline_target_x.size() != size ||
        state.timeline_target_y.size() != size ||
        state.timeline_target_z.size() != size ||
        state.timeline_arc_height.size() != size ||
        state.timeline_start_step.size() != size ||
        state.timeline_end_step.size() != size || state.alive.size() != size) {
        return metrics;
    }
    // Timeline commands fill per-entity start and target fields before ticking
    for (std::uint64_t step = 0U; step < step_count; ++step) {
        const std::uint64_t frame = first_step + step + 1U;
        // Commands can schedule or cancel events before interpolation
        if (controller != nullptr &&
            !controller(frame, scenario, state, controller_context)) {
            return metrics;
        }
        std::fill(state.velocity_x.begin(), state.velocity_x.end(), 0.0);
        std::fill(state.velocity_y.begin(), state.velocity_y.end(), 0.0);
        for (std::size_t entity = 0; entity < state.x.size(); ++entity) {
            const std::uint64_t end = state.timeline_end_step[entity];
            if (end == 0U || frame > end) {
                continue;
            }
            const std::uint64_t start = state.timeline_start_step[entity];
            const double amount = static_cast<double>(frame - start) /
                                  static_cast<double>(end - start);
            // Smoothstep gives zero velocity at the endpoints
            const double eased = amount * amount * (3.0 - 2.0 * amount);
            const double old_x = state.x[entity];
            const double old_y = state.y[entity];
            state.x[entity] = state.timeline_start_x[entity] +
                              (state.timeline_target_x[entity] -
                               state.timeline_start_x[entity]) *
                                  eased;
            state.y[entity] = state.timeline_start_y[entity] +
                              (state.timeline_target_y[entity] -
                               state.timeline_start_y[entity]) *
                                  eased;
            state.timeline_z[entity] = state.timeline_start_z[entity] +
                                       (state.timeline_target_z[entity] -
                                        state.timeline_start_z[entity]) *
                                           eased +
                                       4.0 * state.timeline_arc_height[entity] *
                                           amount * (1.0 - amount);
            state.velocity_x[entity] = state.x[entity] - old_x;
            state.velocity_y[entity] = state.y[entity] - old_y;
            ++metrics.entity_updates;
            if (frame == end) {
                // Zero marks a consumed timeline event
                state.timeline_end_step[entity] = 0U;
            }
        }
        ++metrics.steps;
        if (observer != nullptr && snapshot_stride != 0U &&
            frame % snapshot_stride == 0U) {
            // Report positions only after every active event has advanced
            observer(frame, scenario, state, context);
        }
        if (state.result >= 0) {
            break;
        }
    }
    return metrics;
}

// Turn frames
// The controller changes board state before the shared clock records a turn
[[nodiscard]] Metrics
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
    // Lua produces moves; this loop handles timing and snapshots
    for (std::uint64_t step = 0U; step < step_count; ++step) {
        const std::uint64_t frame = first_step + step + 1U;
        if (controller != nullptr &&
            !controller(frame, scenario, state, controller_context)) {
            return metrics;
        }
        ++state.turn;
        ++metrics.steps;
        ++metrics.turns;
        if (observer != nullptr && snapshot_stride != 0U &&
            frame % snapshot_stride == 0U) {
            // A turn snapshot includes the move submitted for this frame
            observer(frame, scenario, state, context);
        }
        if (state.result >= 0) {
            break;
        }
    }
    return metrics;
}

} // namespace m1
