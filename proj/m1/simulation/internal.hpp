#ifndef MOLLY_M1_SIMULATION_KERNELS_HPP
#define MOLLY_M1_SIMULATION_KERNELS_HPP

#include "model.hpp"

/// \file
/// Native kernel entry points and their shared callback order
namespace m1 {

struct ScenarioRuntime;

#ifndef M1_WIDE_GRID
#define M1_WIDE_GRID 0
#endif
#ifndef M1_OPT_LEVEL
#define M1_OPT_LEVEL 0
#endif
#if M1_OPT_LEVEL == 0 || M1_WIDE_GRID
// Wide indices are the reference representation and cover large grids
using CellIndex = std::uint64_t;
#else
using CellIndex = std::uint32_t;
#endif

[[nodiscard]] inline std::size_t adjacent(const std::size_t value,
                                          const int offset,
                                          const std::size_t extent) noexcept {
    if (offset < 0)
        return value == 0U ? extent - 1U : value - 1U;
    if (offset > 0)
        return value + 1U == extent ? 0U : value + 1U;
    return value;
}

// Kernel entry points
// Controllers run before a frame; observers see the committed frame
/// Solve all configured PDE fields in one call
[[nodiscard]] Metrics simulate_pde(const Scenario &scenario, State &state,
                                   ScenarioRuntime *program);

/// Advance the reciprocal nearest-neighbour continuous path
[[nodiscard]] Metrics
simulate_continuous(const Scenario &scenario, State &state,
                    SnapshotObserver observer, void *context,
                    std::uint64_t snapshot_stride, StepController controller,
                    void *controller_context, std::uint64_t first_step,
                    std::uint64_t step_count);
/// Advance the full continuous behaviour path
[[nodiscard]] Metrics simulate_extended_continuous(
    const Scenario &scenario, State &state, SnapshotObserver observer,
    void *context, std::uint64_t snapshot_stride, StepController controller,
    void *controller_context, std::uint64_t first_step,
    std::uint64_t step_count, bool scripted_motion = false);
/// Advance interpolated timeline events written by Lua callbacks
[[nodiscard]] Metrics
simulate_timeline(const Scenario &scenario, State &state,
                  SnapshotObserver observer, void *context,
                  std::uint64_t snapshot_stride, StepController controller,
                  void *controller_context, std::uint64_t first_step,
                  std::uint64_t step_count);
/// Advance turn callbacks and record their elapsed time
[[nodiscard]] Metrics
simulate_turn(const Scenario &scenario, State &state, SnapshotObserver observer,
              void *context, std::uint64_t snapshot_stride,
              StepController controller, void *controller_context,
              std::uint64_t first_step, std::uint64_t step_count);
/// Advance cellular generations through the compiled next-cell callback
[[nodiscard]] Metrics simulate_scripted_cellular(
    const Scenario &scenario, State &state, SnapshotObserver observer,
    void *context, std::uint64_t snapshot_stride, StepController controller,
    void *controller_context, ScenarioRuntime &runtime,
    std::uint64_t first_step, std::uint64_t step_count);
/// Invoke and commit the compiled callback for one frame
[[nodiscard]] bool run_compiled_program(std::uint64_t frame,
                                        const Scenario &scenario, State &state,
                                        void *opaque);
/// Select the native kernel after scripted callbacks are ready
[[nodiscard]] Metrics simulate_native(const Scenario &scenario, State &state,
                                      SnapshotObserver observer, void *context,
                                      std::uint64_t snapshot_stride,
                                      StepController controller,
                                      void *controller_context,
                                      std::uint64_t first_step,
                                      std::uint64_t step_count);

} // namespace m1

#endif
