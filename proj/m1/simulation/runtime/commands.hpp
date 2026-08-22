#ifndef MOLLY_M1_SIMULATION_RUNTIME_COMMANDS_HPP
#define MOLLY_M1_SIMULATION_RUNTIME_COMMANDS_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// \file
/// Buffered Lua mutations and read-only host queries
namespace m1 {

/// Callback stages that a compiled Lua program may implement
enum class ScriptCallback : std::uint8_t {
    setup,
    turn,
    timeline,
    tick,
};

/// Mutations that Lua may request through the host API
enum class ScriptCommandKind : std::uint8_t {
    move,
    timed_move,
    show,
    hide,
    velocity,
    board_set,
    state,
    text,
    behaviour,
    result,
};

/// One buffered mutation ready for validation and commit
struct ScriptCommand {
    // Lua calls are buffered here and applied after the callback returns
    ScriptCommandKind kind = ScriptCommandKind::move;
    // Entity or board cell, selected by kind
    std::uint64_t entity = 0;
    // Type, asset, state, or result value, selected by kind
    std::uint32_t field = 0;
    // Numeric command payload
    double first = 0.0;
    double second = 0.0;
    double third = 0.0;
    bool has_third = false;
    std::uint64_t duration = 0U;
    double arc_height = 0.0;
    std::string text;
};

/// Read-only queries exposed to the active Lua callback
struct ScriptHost {
    // Caller data passed unchanged to every callback
    void *context = nullptr;
    // The ABI between generic Lua bindings and the simulation state
    bool (*entity_value)(void *, std::uint64_t, std::uint32_t,
                         double &) noexcept = nullptr;
    bool (*entity_exists)(void *, std::uint64_t) noexcept = nullptr;
    bool (*board_cell)(void *, std::size_t, std::uint32_t) noexcept = nullptr;
    bool (*board_value)(void *, std::size_t,
                        std::uint32_t &) noexcept = nullptr;
    std::size_t (*board_size)(void *) noexcept = nullptr;
    bool (*resolve_type)(void *, const char *, std::size_t,
                         std::uint32_t &) noexcept = nullptr;
    bool (*resolve_asset)(void *, const char *, std::size_t,
                          std::uint32_t &) noexcept = nullptr;
};

} // namespace m1

#endif
