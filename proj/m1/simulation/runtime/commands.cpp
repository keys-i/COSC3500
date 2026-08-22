#include "simulation/runtime/commands.hpp"
#include "model.hpp"
#include "simulation/internal.hpp"
#include "simulation/runtime/lua.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

/// \file
/// Validate and commit operations buffered through ScriptHost
namespace m1 {
namespace {

// Host ABI
struct ScriptContext {
    // Points to state kept alive by setup_scenario_program or simulate
    const Scenario *scenario = nullptr;
    State *state = nullptr;
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

[[nodiscard]] bool script_entity_exists(void *const context,
                                        const std::uint64_t entity) noexcept {
    const auto &script = *static_cast<ScriptContext *>(context);
    return entity < script.state->x.size();
}

[[nodiscard]] bool script_board_cell(void *const context,
                                     const std::size_t cell,
                                     const std::uint32_t value) noexcept {
    const auto &script = *static_cast<ScriptContext *>(context);
    if (script.scenario->kernel == Kernel::cellular) {
        return cell < script.state->cells.size() &&
               value < script.scenario->cellular.state_count;
    }
    return cell < script.state->board.size() &&
           value <= script.scenario->names.size() &&
           value <= script.scenario->styles.size();
}

[[nodiscard]] bool script_board_value(void *const context,
                                      const std::size_t cell,
                                      std::uint32_t &value) noexcept {
    const auto &script = *static_cast<ScriptContext *>(context);
    if (script.scenario->kernel == Kernel::cellular) {
        if (cell >= script.state->cells.size()) {
            return false;
        }
        value = script.state->cells[cell];
        return true;
    }
    if (cell >= script.state->board.size()) {
        return false;
    }
    value = script.state->board[cell];
    return true;
}

[[nodiscard]] std::size_t script_board_size(void *const context) noexcept {
    const auto &script = *static_cast<ScriptContext *>(context);
    return script.scenario->kernel == Kernel::cellular
               ? script.state->cells.size()
               : script.state->board.size();
}

[[nodiscard]] bool script_resolve_type(void *const context,
                                       const char *const text,
                                       const std::size_t size,
                                       std::uint32_t &id) noexcept {
    const auto &script = *static_cast<ScriptContext *>(context);
    const std::string_view name{text, size};
    std::size_t found = std::numeric_limits<std::size_t>::max();
    for (std::size_t index = 0U; index < script.scenario->names.size();
         ++index) {
        if (script.scenario->names[index] == name) {
            if (found != std::numeric_limits<std::size_t>::max()) {
                return false;
            }
            found = index;
        }
    }
    if (found == std::numeric_limits<std::size_t>::max() ||
        found > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    id = static_cast<std::uint32_t>(found);
    return true;
}

[[nodiscard]] bool script_resolve_asset(void *const context,
                                        const char *const text,
                                        const std::size_t size,
                                        std::uint32_t &id) noexcept {
    const auto &script = *static_cast<ScriptContext *>(context);
    const std::string_view name{text, size};
    for (std::size_t index = 0U; index < script.scenario->assets.size();
         ++index) {
        const AssetPlan &asset = script.scenario->assets[index];
        if (asset.name == name && asset.kind == "image" &&
            index <= std::numeric_limits<std::uint32_t>::max()) {
            id = static_cast<std::uint32_t>(index);
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::size_t
timeline_character(const Scenario &scenario,
                   const std::size_t entity) noexcept {
    for (std::size_t type = 0U; type < scenario.characters.size(); ++type) {
        const CharacterPlan &character = scenario.characters[type];
        if (entity >= character.first &&
            entity < character.first + character.count) {
            return type;
        }
    }
    return scenario.characters.size();
}

[[nodiscard]] std::optional<BehaviourCode>
script_behaviour_code(const std::string_view value) noexcept {
    static constexpr std::array<std::string_view, 11U> codes{
        "idle",     "seek",  "flee",   "pursue", "evade", "consume",
        "separate", "align", "cohere", "avoid",  "wander"};
    for (std::size_t index{}; index < codes.size(); ++index)
        if (value == codes[index])
            return static_cast<BehaviourCode>(index);
    return std::nullopt;
}

// Setup validation and commit
[[nodiscard]] bool
register_script_behaviours(const std::vector<ScriptCommand> &commands,
                           Scenario &scenario, ScenarioRuntime &runtime) {
    for (const ScriptCommand &command : commands) {
        if (command.kind != ScriptCommandKind::behaviour)
            continue;
        const auto code = script_behaviour_code(command.text);
        if (!code || scenario.kernel != Kernel::continuous ||
            command.entity >= scenario.characters.size() ||
            command.field >= scenario.characters.size() ||
            !std::isfinite(command.first) || !std::isfinite(command.second) ||
            command.first < 0.0) {
            return false;
        }
        CharacterPlan &plan = scenario.characters[command.entity];
        if (runtime.behaviour_types.empty()) {
            runtime.behaviour_types.resize(scenario.characters.size());
        }
        if (!runtime.behaviour_types[command.entity] &&
            plan.behaviour_count != 0U) {
            return false;
        }
        if (plan.behaviour_count == 0U) {
            // Records for one type stay contiguous for the native kernel
            plan.first_behaviour = scenario.behaviour_plan.size();
            plan.target = command.field;
        } else if (plan.target != command.field) {
            plan.target = std::numeric_limits<std::uint32_t>::max();
        }
        scenario.behaviour_plan.push_back(BehaviourRecord{
            *code, command.field, command.first, command.second});
        ++plan.behaviour_count;
        if (*code == BehaviourCode::seek || *code == BehaviourCode::pursue)
            plan.behaviours |= seek | sense;
        else if (*code == BehaviourCode::flee || *code == BehaviourCode::evade)
            plan.behaviours |= flee | sense;
        else if (*code == BehaviourCode::consume)
            plan.behaviours |= consume | sense;
        else if (*code == BehaviourCode::separate ||
                 *code == BehaviourCode::align ||
                 *code == BehaviourCode::cohere ||
                 *code == BehaviourCode::avoid)
            plan.behaviours |= sense;
        runtime.behaviour_types[command.entity] = true;
        runtime.native_behaviour = true;
    }
    return true;
}

// Per-callback validation and commit
[[nodiscard]] bool
compile_script_commands(const std::vector<ScriptCommand> &commands,
                        ScriptContext &context, const std::uint64_t step,
                        Metrics *const metrics) {
    State &state = *context.state;
    for (const ScriptCommand &command : commands) {
        if (command.kind == ScriptCommandKind::behaviour) {
            continue;
        }
        if (command.kind == ScriptCommandKind::board_set) {
            if (context.scenario->kernel == Kernel::cellular) {
                // Initial cell contents are fixed once stepping begins
                if (step != 0U) {
                    return false;
                }
                if (command.entity >= state.cells.size() ||
                    command.field >= context.scenario->cellular.state_count)
                    return false;
                state.cells[static_cast<std::size_t>(command.entity)] =
                    static_cast<std::uint8_t>(command.field);
                continue;
            }
            if (command.entity >= state.board.size() ||
                command.entity > std::numeric_limits<std::uint32_t>::max() ||
                command.field > context.scenario->names.size() ||
                command.field > context.scenario->styles.size())
                return false;
            state.board[static_cast<std::size_t>(command.entity)] =
                command.field;
            if (metrics != nullptr) {
                ++metrics->entity_updates;
            }
            continue;
        }
        if (command.kind == ScriptCommandKind::result) {
            const auto result = static_cast<std::int32_t>(command.field);
            if (state.result != -1 && state.result != result) {
                return false;
            }
            state.result = result;
            continue;
        }
        if (command.entity >= state.x.size())
            return false;
        const std::size_t entity = static_cast<std::size_t>(command.entity);
        const Scenario &scenario = *context.scenario;
        const bool was_alive = state.alive[entity] != 0U;
        if ((command.kind == ScriptCommandKind::state ||
             command.kind == ScriptCommandKind::text || command.has_third) &&
            scenario.kernel != Kernel::timeline) {
            return false;
        }
        if (command.kind == ScriptCommandKind::timed_move) {
            if (scenario.kernel != Kernel::timeline || command.duration == 0U ||
                command.first < 0.0 || command.first >= scenario.world.width ||
                command.second < 0.0 ||
                command.second >= scenario.world.height) {
                return false;
            }
            state.timeline_start_x[entity] = state.x[entity];
            state.timeline_start_y[entity] = state.y[entity];
            state.timeline_start_z[entity] = state.timeline_z[entity];
            state.timeline_target_x[entity] = command.first;
            state.timeline_target_y[entity] = command.second;
            state.timeline_target_z[entity] =
                command.has_third ? command.third : state.timeline_z[entity];
            state.timeline_arc_height[entity] = command.arc_height;
            state.timeline_start_step[entity] = step;
            state.timeline_end_step[entity] = step + command.duration;
        } else if (command.kind == ScriptCommandKind::move) {
            if (command.first < 0.0 || command.first >= scenario.world.width ||
                command.second < 0.0 ||
                command.second >= scenario.world.height) {
                return false;
            }
            state.x[entity] = command.first;
            state.y[entity] = command.second;
            ++state.spatial_revision;
            if (command.has_third) {
                state.timeline_z[entity] = command.third;
            }
        } else if (command.kind == ScriptCommandKind::show) {
            state.spatial_revision +=
                static_cast<std::uint64_t>(state.alive[entity] == 0U);
            state.alive[entity] = 1U;
        } else if (command.kind == ScriptCommandKind::hide) {
            state.spatial_revision +=
                static_cast<std::uint64_t>(state.alive[entity] != 0U);
            state.alive[entity] = 0U;
        } else if (command.kind == ScriptCommandKind::velocity) {
            if (entity >= state.velocity_x.size() ||
                entity >= state.velocity_y.size()) {
                return false;
            }
            state.velocity_x[entity] = command.first;
            state.velocity_y[entity] = command.second;
        } else if (command.kind == ScriptCommandKind::state) {
            const std::size_t type = timeline_character(scenario, entity);
            if (type == scenario.characters.size() ||
                scenario.characters[type].count != 1U ||
                scenario.styles[type].shape != Shape::sprite ||
                command.field >= scenario.assets.size() ||
                scenario.assets[command.field].kind != "image") {
                return false;
            }
            state.timeline_state[entity] = command.field + 1U;
        } else if (command.kind == ScriptCommandKind::text) {
            const std::size_t type = timeline_character(scenario, entity);
            if (type == scenario.characters.size() ||
                scenario.characters[type].count != 1U ||
                scenario.styles[type].shape != Shape::text ||
                command.text.empty() || command.text.size() > 96U ||
                command.text.find_first_of("\r\n") != std::string::npos) {
                return false;
            }
            state.timeline_text[entity] = command.text;
        }
        if (metrics != nullptr && scenario.kernel == Kernel::timeline &&
            command.kind != ScriptCommandKind::velocity) {
            metrics->births += static_cast<std::uint64_t>(
                command.kind == ScriptCommandKind::show && !was_alive);
            metrics->deaths += static_cast<std::uint64_t>(
                command.kind == ScriptCommandKind::hide && was_alive);
            ++metrics->entity_updates;
            ++metrics->timeline_events;
        }
    }
    return true;
}

} // namespace

// Setup phase
bool setup_scenario_program(Scenario &scenario, State &state,
                            ScenarioRuntime &runtime, std::string &error) {
    if (runtime.program == nullptr) {
        return true;
    }
    // The temporary host is cleared after setup so it cannot outlive this call
    ScriptContext context{&scenario, &state};
    const ScriptHost host{&context,
                          script_entity_value,
                          script_entity_exists,
                          script_board_cell,
                          script_board_value,
                          script_board_size,
                          script_resolve_type,
                          script_resolve_asset};
    if (!bind_scenario_program(runtime, host, error)) {
        return false;
    }
    std::vector<ScriptCommand> commands;
    if (!invoke_scenario_program(runtime, ScriptCallback::setup, 0U, commands,
                                 error)) {
        return false;
    }
    if (!register_script_behaviours(commands, scenario, runtime)) {
        error = "compiled setup behaviour rejected";
        return false;
    }
    if (!compile_script_commands(commands, context, 0U, nullptr)) {
        error = "compiled setup command rejected";
        return false;
    }
    update_scenario_program_host(runtime, {});
    return true;
}

// Configuration check
bool compile_rules(Scenario &scenario, std::string &error) {
    if (find_scenario_program(scenario.lua_directory.empty()
                                  ? scenario.source_directory
                                  : scenario.lua_directory) == nullptr &&
        !scenario.lua_rules.empty()) {
        error = (scenario.lua_directory.empty() ? scenario.source_directory
                                                : scenario.lua_directory) +
                '/' + scenario.lua_rules + ": no compiled scenario program";
        return false;
    }
    return true;
}

// Stepping phase
struct LiveProgramContext {
    // References belong to simulate and remain valid for the stepping call
    ScenarioRuntime *runtime = nullptr;
    ScriptContext *context = nullptr;
    ScriptCallback callback = ScriptCallback::turn;
    StepController controller = nullptr;
    void *controller_context = nullptr;
    std::string path;
    std::string error;
    std::vector<ScriptCommand> commands;
    Metrics metrics;
};

[[nodiscard]] bool run_compiled_program(const std::uint64_t frame,
                                        const Scenario &scenario, State &state,
                                        void *const opaque) {
    auto &live = *static_cast<LiveProgramContext *>(opaque);
    live.context->state = &state;
    live.commands.clear();
    const auto started = std::chrono::steady_clock::now();
    // Run script code first so the controller observes committed commands
    if (!invoke_scenario_program(*live.runtime, live.callback, frame,
                                 live.commands, live.error)) {
        live.error = live.path + ": " + live.error;
        return false;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started);
    if (!compile_script_commands(live.commands, *live.context, frame,
                                 &live.metrics)) {
        live.error = live.path + ": compiled script command rejected";
        return false;
    }
    if (live.callback == ScriptCallback::turn) {
        state.turn_duration_us = std::max<std::uint64_t>(
            1U, static_cast<std::uint64_t>(elapsed.count()));
    }
    if (live.controller == nullptr) {
        return true;
    }
    const bool accepted =
        live.controller(frame, scenario, state, live.controller_context);
    ++state.spatial_revision;
    return accepted;
}

Metrics simulate(const Scenario &scenario, State &state,
                 const SnapshotObserver observer, void *const context,
                 const std::uint64_t snapshot_stride,
                 const StepController controller,
                 void *const controller_context,
                 ScenarioRuntime *const program) {
    if (scenario.kernel == Kernel::pde) {
        return simulate_pde(scenario, state, program);
    }
    if (program != nullptr && program->program != nullptr) {
        // This host references the live state for the duration of simulate
        ScriptContext script_context{&scenario, &state};
        const ScriptHost host{&script_context,      script_entity_value,
                              script_entity_exists, script_board_cell,
                              script_board_value,   script_board_size,
                              script_resolve_type,  script_resolve_asset};
        update_scenario_program_host(*program, host);
        if (scenario.kernel == Kernel::cellular) {
            LiveProgramContext live{program,
                                    &script_context,
                                    ScriptCallback::tick,
                                    controller,
                                    controller_context,
                                    scenario.source_directory + '/' +
                                        scenario.lua_rules,
                                    {},
                                    {},
                                    {}};
            Metrics metrics = simulate_scripted_cellular(
                scenario, state, observer, context, snapshot_stride,
                run_compiled_program, &live, *program, 0U,
                scenario.world.steps);
            if (!live.error.empty()) {
                throw std::runtime_error(live.error);
            }
            metrics.entity_updates += live.metrics.entity_updates;
            metrics.births += live.metrics.births;
            metrics.deaths += live.metrics.deaths;
            metrics.timeline_events += live.metrics.timeline_events;
            return metrics;
        }
        const ScriptCallback callback =
            scenario.kernel == Kernel::turn       ? ScriptCallback::turn
            : scenario.kernel == Kernel::timeline ? ScriptCallback::timeline
                                                  : ScriptCallback::tick;
        LiveProgramContext live{program,
                                &script_context,
                                callback,
                                controller,
                                controller_context,
                                scenario.source_directory + '/' +
                                    scenario.lua_rules,
                                {},
                                {},
                                {}};
        Metrics metrics =
            scenario.kernel == Kernel::continuous && !program->native_behaviour
                ? simulate_extended_continuous(scenario, state, observer,
                                               context, snapshot_stride,
                                               run_compiled_program, &live, 0U,
                                               scenario.world.steps, true)
                : simulate_native(scenario, state, observer, context,
                                  snapshot_stride, run_compiled_program, &live,
                                  0U, scenario.world.steps);
        if (!live.error.empty()) {
            throw std::runtime_error(live.error);
        }
        metrics.entity_updates += live.metrics.entity_updates;
        metrics.births += live.metrics.births;
        metrics.deaths += live.metrics.deaths;
        metrics.timeline_events += live.metrics.timeline_events;
        return metrics;
    }
    if (scenario.lua_rules.empty()) {
        return simulate_native(scenario, state, observer, context,
                               snapshot_stride, controller, controller_context,
                               0U, scenario.world.steps);
    }
    throw std::runtime_error(scenario.source_directory + '/' +
                             scenario.lua_rules +
                             ": no compiled scenario program");
}

} // namespace m1
