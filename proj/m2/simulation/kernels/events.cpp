#include "model.hpp"
#include "simulation/internal.hpp"

/// \file
/// Advance timeline and turn state written by compiled callbacks
namespace m2 {
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
        const bool transforms_present = !state.timeline_rotation.empty();
        if (transforms_present &&
            (state.timeline_rotation.size() != size ||
             state.timeline_scale.size() != size ||
             state.timeline_opacity.size() != size ||
             state.timeline_start_rotation.size() != size ||
             state.timeline_start_scale.size() != size ||
             state.timeline_start_opacity.size() != size ||
             state.timeline_target_rotation.size() != size ||
             state.timeline_target_scale.size() != size ||
             state.timeline_target_opacity.size() != size ||
             state.timeline_transform_start_step.size() != size ||
             state.timeline_transform_end_step.size() != size)) {
            return metrics;
        }
        std::fill(state.velocity_x.begin(), state.velocity_x.end(), 0.0);
        std::fill(state.velocity_y.begin(), state.velocity_y.end(), 0.0);
        for (std::size_t entity = 0; entity < state.x.size(); ++entity) {
            const std::uint64_t end = state.timeline_end_step[entity];
            bool advanced = false;
            if (end != 0U && frame <= end) {
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
                advanced = true;
                if (frame == end) {
                    // Zero marks a consumed timeline event
                    state.timeline_end_step[entity] = 0U;
                }
            }
            if (transforms_present) {
                const std::uint64_t transform_end =
                    state.timeline_transform_end_step[entity];
                if (transform_end != 0U && frame <= transform_end) {
                    const std::uint64_t transform_start =
                        state.timeline_transform_start_step[entity];
                    const double transform_amount =
                        static_cast<double>(frame - transform_start) /
                        static_cast<double>(transform_end - transform_start);
                    const double transform_eased =
                        transform_amount * transform_amount *
                        (3.0 - 2.0 * transform_amount);
                    state.timeline_rotation[entity] =
                        state.timeline_start_rotation[entity] +
                        (state.timeline_target_rotation[entity] -
                         state.timeline_start_rotation[entity]) * transform_eased;
                    state.timeline_scale[entity] =
                        state.timeline_start_scale[entity] +
                        (state.timeline_target_scale[entity] -
                         state.timeline_start_scale[entity]) * transform_eased;
                    state.timeline_opacity[entity] =
                        state.timeline_start_opacity[entity] +
                        (state.timeline_target_opacity[entity] -
                         state.timeline_start_opacity[entity]) * transform_eased;
                    advanced = true;
                    if (frame == transform_end)
                        state.timeline_transform_end_step[entity] = 0U;
                }
            }
            metrics.entity_updates += static_cast<std::uint64_t>(advanced);
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

} // namespace m2
