#include "simulation/runtime/lua.hpp"
#include "model.hpp"

#include <clx.h>

#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <random>

/// \file
/// Open generated Lua modules, retain callbacks, and buffer engine calls
namespace m1 {
namespace {

// Runtime storage and callback scope
struct Storage {
    explicit Storage(const std::uint64_t seed) : random(seed) {}

    clx::LState *state = nullptr;
    ScriptHost host{};
    std::array<clx::LValue, 4U> callbacks{};
    clx::LValue cell_next{};
    // PDE callbacks share the same bound CLX state
    std::array<clx::LValue, 4U> pde_callbacks{};
    // Calls append commands here before C++ validates and applies them
    std::vector<ScriptCommand> commands;
    std::mt19937_64 random;
    bool allow_behaviour = false;
    bool in_cell_callback = false;
    std::array<std::uint8_t, 256U> cell_neighbours{};
};
thread_local Storage *active = nullptr;
struct Active {
    // CLX native calls recover the active runtime through this scope
    Storage *previous;
    explicit Active(Storage &s) noexcept : previous(active) { active = &s; }
    ~Active() { active = previous; }
};
[[noreturn]] void fail(clx::LState *s, const char *m) { clx::error(s, m); }
[[nodiscard]] Storage &host(clx::LState *s) {
    if (active == nullptr || active->state != s)
        fail(s, "compiled scenario has no host");
    return *active;
}
[[nodiscard]] std::uint64_t entity(clx::LState *s, const clx::LValue &v) {
    const auto n = clx::check_integer(s, v);
    if (n < 0)
        fail(s, "entity must be non-negative");
    return static_cast<std::uint64_t>(n);
}
[[nodiscard]] std::uint32_t id(clx::LState *s, const clx::LValue &v) {
    const auto n = clx::check_integer(s, v);
    if (n < 0 || static_cast<std::uint64_t>(n) >
                     std::numeric_limits<std::uint32_t>::max())
        fail(s, "ID must fit uint32");
    return static_cast<std::uint32_t>(n);
}
[[nodiscard]] double finite(clx::LState *s, const clx::LValue &v) {
    const double n = clx::check_number(s, v);
    if (!std::isfinite(n))
        fail(s, "number must be finite");
    return n;
}

// Lua API input checks and command buffering
// Delay mutations until the callback has completed successfully
void add(Storage &h, const ScriptCommand &c) { h.commands.push_back(c); }
[[nodiscard]] ScriptCommand entity_command(clx::LState *s, Storage &h,
                                           const ScriptCommandKind kind,
                                           const clx::LValue &value) {
    ScriptCommand c;
    c.kind = kind;
    c.entity = entity(s, value);
    if (h.host.entity_exists == nullptr ||
        !h.host.entity_exists(h.host.context, c.entity))
        fail(s, "unknown entity");
    return c;
}
[[nodiscard]] clx::MultiValue engine_id(clx::LState *s, const clx::LValue *a,
                                        const std::size_t n) {
    if (n != 2U)
        fail(s, "id needs kind and name");
    auto &h = host(s);
    const char *k = clx::check_string(s, a[0]);
    const char *name = clx::check_string(s, a[1]);
    std::uint32_t out{};
    const auto len = std::char_traits<char>::length(name);
    const bool ok =
        (std::string_view(k) == "type" && h.host.resolve_type != nullptr &&
         h.host.resolve_type(h.host.context, name, len, out)) ||
        (std::string_view(k) == "asset" && h.host.resolve_asset != nullptr &&
         h.host.resolve_asset(h.host.context, name, len, out));
    if (!ok)
        fail(s, "unknown ID");
    return clx::MultiValue(clx::integer(out));
}
[[nodiscard]] clx::MultiValue
engine_has_type(clx::LState *s, const clx::LValue *a, const std::size_t n) {
    if (n != 1U)
        fail(s, "has_type needs a name");
    auto &h = host(s);
    std::uint32_t ignored{};
    const char *name = clx::check_string(s, a[0]);
    return clx::MultiValue(clx::boolean(
        h.host.resolve_type != nullptr &&
        h.host.resolve_type(h.host.context, name,
                            std::char_traits<char>::length(name), ignored)));
}
[[nodiscard]] clx::MultiValue
engine_entity(clx::LState *s, const clx::LValue *a, const std::size_t n) {
    if (n != 2U)
        fail(s, "entity needs entity and field");
    auto &h = host(s);
    double out{};
    if (h.host.entity_value == nullptr ||
        !h.host.entity_value(h.host.context, entity(s, a[0]), id(s, a[1]), out))
        fail(s, "unknown entity or field");
    return clx::MultiValue(clx::number(out));
}
[[nodiscard]] clx::MultiValue engine_move(clx::LState *s, const clx::LValue *a,
                                          const std::size_t n) {
    if (n != 3U && n != 4U)
        fail(s, "move needs entity, x, y, optional z");
    auto &h = host(s);
    ScriptCommand c = entity_command(s, h, ScriptCommandKind::move, a[0]);
    c.first = finite(s, a[1]);
    c.second = finite(s, a[2]);
    c.has_third = n == 4U;
    if (c.has_third)
        c.third = finite(s, a[3]);
    add(h, c);
    return {};
}
[[nodiscard]] clx::MultiValue
engine_timed_move(clx::LState *s, const clx::LValue *a, const std::size_t n) {
    if (n < 4U || n > 6U)
        fail(s, "timed_move needs entity, x, y, duration, optional z and arc");
    auto &h = host(s);
    ScriptCommand c = entity_command(s, h, ScriptCommandKind::timed_move, a[0]);
    c.first = finite(s, a[1]);
    c.second = finite(s, a[2]);
    c.duration = static_cast<std::uint64_t>(id(s, a[3]));
    if (c.duration == 0U)
        fail(s, "timed_move duration must be positive");
    c.has_third = n >= 5U;
    if (c.has_third)
        c.third = finite(s, a[4]);
    if (n == 6U)
        c.arc_height = finite(s, a[5]);
    add(h, c);
    return {};
}
[[nodiscard]] clx::MultiValue
engine_visible(clx::LState *s, const clx::LValue *a, const std::size_t n,
               const ScriptCommandKind kind, const char *message) {
    if (n != 1U)
        fail(s, message);
    auto &h = host(s);
    ScriptCommand c = entity_command(s, h, kind, a[0]);
    add(h, c);
    return {};
}
[[nodiscard]] clx::MultiValue engine_hide(clx::LState *s, const clx::LValue *a,
                                          const std::size_t n) {
    return engine_visible(s, a, n, ScriptCommandKind::hide,
                          "hide needs entity");
}
[[nodiscard]] clx::MultiValue engine_show(clx::LState *s, const clx::LValue *a,
                                          const std::size_t n) {
    return engine_visible(s, a, n, ScriptCommandKind::show,
                          "show needs entity");
}
[[nodiscard]] clx::MultiValue
engine_velocity(clx::LState *s, const clx::LValue *a, const std::size_t n) {
    if (n != 3U)
        fail(s, "velocity needs entity, vx, vy");
    auto &h = host(s);
    ScriptCommand c = entity_command(s, h, ScriptCommandKind::velocity, a[0]);
    c.first = finite(s, a[1]);
    c.second = finite(s, a[2]);
    add(h, c);
    return {};
}
[[nodiscard]] clx::MultiValue
engine_behaviour(clx::LState *s, const clx::LValue *a, const std::size_t n) {
    if (n != 5U || !host(s).allow_behaviour)
        fail(s, "behaviour is only valid in on_setup");
    ScriptCommand c;
    c.kind = ScriptCommandKind::behaviour;
    c.entity = entity(s, a[0]);
    c.text = clx::check_string(s, a[1]);
    c.field = id(s, a[2]);
    c.first = finite(s, a[3]);
    c.second = finite(s, a[4]);
    add(host(s), c);
    return {};
}
[[nodiscard]] clx::MultiValue
engine_board_get(clx::LState *s, const clx::LValue *a, const std::size_t n) {
    if (n != 1U)
        fail(s, "board needs cell");
    auto &h = host(s);
    std::uint32_t value{};
    if (h.host.board_value == nullptr ||
        !h.host.board_value(h.host.context,
                            static_cast<std::size_t>(id(s, a[0])), value))
        fail(s, "invalid board cell");
    return clx::MultiValue(clx::integer(value));
}
[[nodiscard]] clx::MultiValue engine_board(clx::LState *s, const clx::LValue *a,
                                           const std::size_t n) {
    if (n != 2U)
        fail(s, "board_set needs cell and value");
    auto &h = host(s);
    ScriptCommand c;
    c.kind = ScriptCommandKind::board_set;
    c.entity = id(s, a[0]);
    c.field = id(s, a[1]);
    if (h.host.board_cell == nullptr ||
        !h.host.board_cell(h.host.context, static_cast<std::size_t>(c.entity),
                           c.field))
        fail(s, "invalid board cell");
    add(h, c);
    return {};
}
[[nodiscard]] clx::MultiValue
engine_board_size(clx::LState *s, const clx::LValue *, const std::size_t n) {
    auto &h = host(s);
    if (n != 0U || h.host.board_size == nullptr)
        fail(s, "board_size takes no arguments");
    return clx::MultiValue(clx::integer(
        static_cast<std::int64_t>(h.host.board_size(h.host.context))));
}
[[nodiscard]] clx::MultiValue engine_neighbour_count(clx::LState *s,
                                                     const clx::LValue *a,
                                                     const std::size_t n) {
    if (n != 1U || !host(s).in_cell_callback)
        fail(s, "neighbour_count is only valid in next_cell");
    return clx::MultiValue(clx::integer(host(s).cell_neighbours[id(s, a[0])]));
}
[[nodiscard]] clx::MultiValue engine_state(clx::LState *s, const clx::LValue *a,
                                           const std::size_t n) {
    if (n != 2U)
        fail(s, "state needs entity and asset");
    auto &h = host(s);
    ScriptCommand c = entity_command(s, h, ScriptCommandKind::state, a[0]);
    if (clx::is_string(a[1])) {
        const char *name = clx::check_string(s, a[1]);
        const auto length = std::char_traits<char>::length(name);
        if (h.host.resolve_asset == nullptr ||
            !h.host.resolve_asset(h.host.context, name, length, c.field))
            fail(s, "unknown asset");
    } else
        c.field = id(s, a[1]);
    add(h, c);
    return {};
}
[[nodiscard]] clx::MultiValue engine_text(clx::LState *s, const clx::LValue *a,
                                          const std::size_t n) {
    if (n != 2U)
        fail(s, "text needs entity and text");
    auto &h = host(s);
    ScriptCommand c = entity_command(s, h, ScriptCommandKind::text, a[0]);
    c.text = clx::check_string(s, a[1]);
    if (c.text.size() > 96U)
        fail(s, "invalid text");
    add(h, c);
    return {};
}
[[nodiscard]] clx::MultiValue engine_random(clx::LState *s, const clx::LValue *,
                                            const std::size_t n) {
    if (n != 0U)
        fail(s, "random takes no arguments");
    return clx::MultiValue(
        clx::number(std::generate_canonical<double, 53>(host(s).random)));
}
[[nodiscard]] clx::MultiValue
engine_result(clx::LState *s, const clx::LValue *a, const std::size_t n) {
    if (n != 1U)
        fail(s, "result needs one non-negative result code");
    ScriptCommand c;
    c.kind = ScriptCommandKind::result;
    c.field = id(s, a[0]);
    add(host(s), c);
    return {};
}
[[nodiscard]] clx::LValue module(clx::LState *s) {
    const auto m = clx::table(s);
    clx::set_function(s, m, "id", engine_id);
    clx::set_function(s, m, "has_type", engine_has_type);
    clx::set_function(s, m, "entity", engine_entity);
    clx::set_function(s, m, "move", engine_move);
    clx::set_function(s, m, "timed_move", engine_timed_move);
    clx::set_function(s, m, "show", engine_show);
    clx::set_function(s, m, "hide", engine_hide);
    clx::set_function(s, m, "velocity", engine_velocity);
    clx::set_function(s, m, "behaviour", engine_behaviour);
    clx::set_function(s, m, "board", engine_board_get);
    clx::set_function(s, m, "board_set", engine_board);
    clx::set_function(s, m, "board_size", engine_board_size);
    clx::set_function(s, m, "neighbour_count", engine_neighbour_count);
    clx::set_function(s, m, "state", engine_state);
    clx::set_function(s, m, "text", engine_text);
    clx::set_function(s, m, "random", engine_random);
    clx::set_function(s, m, "result", engine_result);
    return m;
}
void sandbox(clx::LState *s) {
    // AOT modules use the supplied engine API rather than file or GC access
    for (const char *name :
         {"print", "load", "loadfile", "dofile", "collectgarbage"})
        clx::set_global(s, name, clx::LValue());
    const auto math = clx::get_global(s, "math");
    if (clx::is_table(math)) {
        // Keep only deterministic numeric helpers and make the table read-only
        for (const char *name :
             {"abs",   "acos", "asin",  "atan", "ceil",      "cos",
              "deg",   "exp",  "floor", "fmod", "max",       "min",
              "rad",   "sin",  "sqrt",  "tan",  "tointeger", "frexp",
              "ldexp", "log",  "modf",  "type", "ult"})
            (void)clx::get_field(s, math, name);
        clx::setmetatable(s, math, clx::LValue());
    }
}

// AOT registry and runtime lifetime
[[nodiscard]] bool matches(std::string_view path,
                           const std::string_view bundle) noexcept {
    while (!path.empty() && path.back() == '/')
        path.remove_suffix(1U);
    if (!path.ends_with(bundle))
        return false;
    const std::size_t prefix = path.size() - bundle.size();
    return prefix == 0U || path[prefix - 1U] == '/';
}
void set_error(std::string &error, const char *message) noexcept {
    error.clear();
    try {
        error = message;
    } catch (...) {
        error.clear();
    }
}
[[nodiscard]] bool require_pde_callback(const Storage &storage,
                                        const std::size_t callback,
                                        std::string &error) noexcept {
    if (clx::is_function(storage.pde_callbacks[callback]))
        return true;
    set_error(error, "compiled scenario has no PDE callback");
    return false;
}
[[nodiscard]] bool exact_values(const clx::MultiValue &values,
                                const std::size_t count,
                                std::string &error) noexcept {
    if (values.count == count)
        return true;
    set_error(error, "PDE callback returned the wrong number of values");
    return false;
}
} // namespace

// Registry lookup
const ScenarioProgram *
find_scenario_program(const std::string_view path) noexcept {
    // Registry entries are generated with the executable
    std::size_t count{};
    const auto *programs = generated_scenario_programs(count);
    for (std::size_t i{}; i < count; ++i)
        if (matches(path, programs[i].bundle))
            return &programs[i];
    return nullptr;
}

// Binding and AOT module open
bool prepare_scenario_program(const Scenario &scenario, State &,
                              ScenarioRuntime &runtime,
                              std::string &) noexcept {
    runtime.program = find_scenario_program(scenario.lua_directory.empty()
                                                ? scenario.source_directory
                                                : scenario.lua_directory);
    runtime.seed = scenario.world.seed;
    return runtime.program != nullptr;
}
bool bind_scenario_program(ScenarioRuntime &runtime,
                           const ScriptHost &script_host, std::string &error) {
    if (runtime.program == nullptr)
        return false;
    auto *h = new (std::nothrow) Storage(runtime.seed);
    if (h == nullptr) {
        error = "cannot allocate CLX state";
        return false;
    }
    h->host = script_host;
    h->state = clx::open();
    if (h->state == nullptr) {
        delete h;
        error = "cannot allocate CLX state";
        return false;
    }
    try {
        clx::luastd_math(h->state);
        clx::luastd_string(h->state);
        clx::luastd_table(h->state);
        sandbox(h->state);
        Active scope(*h);
        clx::set_global(h->state, "engine", module(h->state));
        // Register dependencies before opening the generated entry module
        for (std::size_t i{}; i < runtime.program->module_count; ++i)
            if (i != runtime.program->entry)
                h->state->register_module(runtime.program->modules[i].name,
                                          runtime.program->modules[i].open);
        runtime.program->modules[runtime.program->entry].open(h->state);
        // Cache callbacks once so stepping performs no global-name lookups
        constexpr std::array<const char *, 4U> names{"on_setup", "on_turn",
                                                     "on_timeline", "on_tick"};
        for (std::size_t i{}; i < names.size(); ++i)
            h->callbacks[i] = clx::get_global(h->state, names[i]);
        h->cell_next = clx::get_global(h->state, "next_cell");
        constexpr std::array<const char *, 4U> pde_names{
            "pde_initial", "pde_coefficients", "pde_boundary", "pde_reference"};
        for (std::size_t i{}; i < pde_names.size(); ++i)
            h->pde_callbacks[i] = clx::get_global(h->state, pde_names[i]);
    } catch (const std::exception &e) {
        clx::close(h->state);
        delete h;
        error = e.what();
        return false;
    }
    runtime.context = h;
    return true;
}

// Host replacement and callback invocation
void update_scenario_program_host(ScenarioRuntime &runtime,
                                  const ScriptHost &script_host) noexcept {
    auto *h = static_cast<Storage *>(runtime.context);
    if (h != nullptr)
        h->host = script_host;
}
bool invoke_scenario_program(ScenarioRuntime &runtime,
                             const ScriptCallback callback,
                             const std::uint64_t step,
                             std::vector<ScriptCommand> &commands,
                             std::string &error) noexcept {
    auto *h = static_cast<Storage *>(runtime.context);
    if (h == nullptr) {
        set_error(error, "compiled scenario is not bound");
        return false;
    }
    // Each callback receives a fresh command buffer
    h->commands.clear();
    try {
        Active scope(*h);
        h->allow_behaviour = callback == ScriptCallback::setup;
        const auto fn = h->callbacks[static_cast<std::size_t>(callback)];
        if (clx::is_function(fn)) {
            const clx::LValue args[]{
                clx::integer(static_cast<std::int64_t>(step))};
            (void)clx::call_direct(h->state, fn, args, 1U, __FILE__, __LINE__);
        }
        // Copy only after a successful callback so partial output is discarded
        commands = h->commands;
        h->allow_behaviour = false;
        return true;
    } catch (const std::exception &e) {
        h->allow_behaviour = false;
        set_error(error, e.what());
        return false;
    }
}

// Cellular callback lifetime
bool invoke_cell_next(ScenarioRuntime &runtime, const std::uint8_t current,
                      const std::uint64_t generation, const std::size_t cell,
                      const std::array<std::uint8_t, 256U> &neighbours,
                      std::uint8_t &next, std::string &error) noexcept {
    auto *h = static_cast<Storage *>(runtime.context);
    if (h == nullptr || !clx::is_function(h->cell_next)) {
        set_error(error, "compiled scenario has no on_cell callback");
        return false;
    }
    try {
        Active scope(*h);
        h->cell_neighbours = neighbours;
        // neighbour_count is valid only while next_cell is on the stack
        h->in_cell_callback = true;
        const clx::LValue args[]{
            clx::integer(current),
            clx::integer(static_cast<std::int64_t>(generation)),
            clx::integer(static_cast<std::int64_t>(cell))};
        const clx::MultiValue value = clx::call_direct(
            h->state, h->cell_next, args, 3U, __FILE__, __LINE__);
        h->in_cell_callback = false;
        if (value.count == 0U) {
            set_error(error, "on_cell must return a state");
            return false;
        }
        const std::int64_t result = clx::check_integer(h->state, value[0]);
        if (result < 0 || result > 255) {
            set_error(error, "on_cell state is out of range");
            return false;
        }
        next = static_cast<std::uint8_t>(result);
        return true;
    } catch (const std::exception &e) {
        h->in_cell_callback = false;
        set_error(error, e.what());
        return false;
    }
}

// PDE callbacks
bool invoke_pde_initial(ScenarioRuntime &runtime, const std::size_t field,
                        const double x, const double y, double &value,
                        std::string &error) noexcept {
    auto *h = static_cast<Storage *>(runtime.context);
    if (h == nullptr) {
        set_error(error, "compiled scenario is not bound");
        return false;
    }
    if (!require_pde_callback(*h, 0U, error))
        return false;
    try {
        Active scope(*h);
        const clx::LValue args[]{clx::integer(static_cast<std::int64_t>(field)),
                                 clx::number(x), clx::number(y)};
        const auto values = clx::call_direct(h->state, h->pde_callbacks[0U],
                                             args, 3U, __FILE__, __LINE__);
        if (!exact_values(values, 1U, error))
            return false;
        value = finite(h->state, values[0U]);
        return true;
    } catch (const std::exception &e) {
        set_error(error, e.what());
        return false;
    }
}
bool invoke_pde_coefficients(ScenarioRuntime &runtime, const std::size_t field,
                             const double x, const double y,
                             PdeCoefficients &coefficients,
                             std::string &error) noexcept {
    auto *h = static_cast<Storage *>(runtime.context);
    if (h == nullptr) {
        set_error(error, "compiled scenario is not bound");
        return false;
    }
    if (!require_pde_callback(*h, 1U, error))
        return false;
    try {
        Active scope(*h);
        const clx::LValue args[]{clx::integer(static_cast<std::int64_t>(field)),
                                 clx::number(x), clx::number(y)};
        const auto values = clx::call_direct(h->state, h->pde_callbacks[1U],
                                             args, 3U, __FILE__, __LINE__);
        if (!exact_values(values, 7U, error))
            return false;
        coefficients.xx = finite(h->state, values[0U]);
        coefficients.xy = finite(h->state, values[1U]);
        coefficients.yy = finite(h->state, values[2U]);
        coefficients.x = finite(h->state, values[3U]);
        coefficients.y = finite(h->state, values[4U]);
        coefficients.value = finite(h->state, values[5U]);
        coefficients.source = finite(h->state, values[6U]);
        return true;
    } catch (const std::exception &e) {
        set_error(error, e.what());
        return false;
    }
}
bool invoke_pde_boundary(ScenarioRuntime &runtime, const std::size_t field,
                         const std::uint8_t side, const double coordinate,
                         const double tau, PdeBoundaryKind &kind, double &value,
                         std::string &error) noexcept {
    auto *h = static_cast<Storage *>(runtime.context);
    if (h == nullptr) {
        set_error(error, "compiled scenario is not bound");
        return false;
    }
    if (!require_pde_callback(*h, 2U, error))
        return false;
    try {
        Active scope(*h);
        const clx::LValue args[]{clx::integer(static_cast<std::int64_t>(field)),
                                 clx::integer(side), clx::number(coordinate),
                                 clx::number(tau)};
        const auto values = clx::call_direct(h->state, h->pde_callbacks[2U],
                                             args, 4U, __FILE__, __LINE__);
        if (!exact_values(values, 2U, error))
            return false;
        const auto raw_kind = clx::check_integer(h->state, values[0U]);
        if (raw_kind < 0 || raw_kind > 2) {
            set_error(error, "PDE boundary kind must be in 0..2");
            return false;
        }
        kind = static_cast<PdeBoundaryKind>(raw_kind);
        value = finite(h->state, values[1U]);
        return true;
    } catch (const std::exception &e) {
        set_error(error, e.what());
        return false;
    }
}
bool invoke_pde_reference(ScenarioRuntime &runtime, const std::size_t field,
                          double &value, bool &present,
                          std::string &error) noexcept {
    auto *h = static_cast<Storage *>(runtime.context);
    if (h == nullptr) {
        set_error(error, "compiled scenario is not bound");
        return false;
    }
    if (!clx::is_function(h->pde_callbacks[3U])) {
        present = false;
        return true;
    }
    try {
        Active scope(*h);
        const clx::LValue args[]{
            clx::integer(static_cast<std::int64_t>(field))};
        const auto values = clx::call_direct(h->state, h->pde_callbacks[3U],
                                             args, 1U, __FILE__, __LINE__);
        if (!exact_values(values, 1U, error))
            return false;
        value = finite(h->state, values[0U]);
        present = true;
        return true;
    } catch (const std::exception &e) {
        set_error(error, e.what());
        return false;
    }
}

// Teardown
void destroy_scenario_program(ScenarioRuntime &runtime) noexcept {
    auto *h = static_cast<Storage *>(runtime.context);
    if (h != nullptr) {
        // CLX owns values retained by the compiled module
        clx::close(h->state);
        delete h;
    }
    runtime = {};
}
} // namespace m1
