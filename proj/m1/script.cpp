#include "script.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <sstream>
#include <utility>

#if defined(M1_HAVE_LUAJIT)
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#endif

namespace m1 {

struct Script::Storage {
    explicit Storage(const ScriptLimits &value) : limits(value) {
        commands.reserve(limits.commands);
    }

    ScriptLimits limits;
    ScriptHost host{};
    std::vector<ScriptCommand> commands;
    std::string path;
    std::uint64_t step = 0;
    std::uint64_t instructions = 0;
    std::size_t bytes = 0;
#if defined(M1_HAVE_LUAJIT)
    lua_State *state = nullptr;
    std::array<int, 6U> callbacks{};
#endif
};

namespace {

void set_error(ScriptError &error, const std::string &path,
               const std::uint64_t step, const std::string &message) {
    error.path = path;
    error.step = step;
    error.line = 0;
    error.message = message;
    const std::size_t first = message.find(':');
    if (first == std::string::npos) {
        return;
    }
    const std::size_t second = message.find(':', first + 1U);
    if (second == std::string::npos || second == first + 1U) {
        return;
    }
    const std::string digits = message.substr(first + 1U, second - first - 1U);
    char *end = nullptr;
    errno = 0;
    const unsigned long value = std::strtoul(digits.c_str(), &end, 10);
    if (errno == 0 && end != digits.c_str() && *end == '\0') {
        error.line = static_cast<std::size_t>(value);
    }
}

#if defined(M1_HAVE_LUAJIT)
[[nodiscard]] const char *lua_message(lua_State *state) noexcept {
    const char *const message = lua_tostring(state, -1);
    return message == nullptr ? "Lua failure" : message;
}
#endif

#if defined(M1_HAVE_LUAJIT)

constexpr std::array<const char *, 6U> callback_names{
    "on_setup", "on_before_step", "on_after_step", "on_cellular_compile",
    "on_turn",  "on_timeline",
};

[[nodiscard]] std::size_t callback_index(const ScriptCallback callback) {
    return static_cast<std::size_t>(callback);
}

[[nodiscard]] bool read_source(const std::string &path, const std::size_t limit,
                               std::string &source, ScriptError &error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        set_error(error, path, 0U, "cannot read Lua rules");
        return false;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0 || static_cast<std::uintmax_t>(size) > limit) {
        set_error(error, path, 0U, "Lua rules exceed the source limit");
        return false;
    }
    source.resize(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    input.read(source.data(), size);
    if (!input && !input.eof()) {
        set_error(error, path, 0U, "cannot read Lua rules");
        return false;
    }
    return true;
}

[[nodiscard]] Script::Storage *storage(lua_State *state) noexcept {
    void *context = nullptr;
    (void)lua_getallocf(state, &context);
    return static_cast<Script::Storage *>(context);
}

void *allocate(void *context, void *pointer, const size_t old_size,
               const size_t new_size) {
    auto *const value = static_cast<Script::Storage *>(context);
    if (new_size == 0U) {
        std::free(pointer);
        value->bytes = old_size <= value->bytes ? value->bytes - old_size : 0U;
        return nullptr;
    }
    const std::size_t retained =
        old_size <= value->bytes ? value->bytes - old_size : 0U;
    if (new_size > value->limits.bytes - retained) {
        return nullptr;
    }
    void *const result = std::realloc(pointer, new_size);
    if (result != nullptr) {
        value->bytes = retained + new_size;
    }
    return result;
}

[[noreturn]] void fail(lua_State *state, const char *message) {
    (void)luaL_error(state, "%s", message);
    std::abort();
}

struct TextArgument {
    const char *data = nullptr;
    std::size_t size = 0;
};

[[nodiscard]] TextArgument text_argument(lua_State *state, const int index,
                                         const char *label) {
    std::size_t size = 0;
    const char *const data = luaL_checklstring(state, index, &size);
    if (size == 0U || size > 128U || std::memchr(data, '\0', size) != nullptr) {
        fail(state, label);
    }
    return TextArgument{data, size};
}

[[nodiscard]] bool name_kind(const TextArgument text, ScriptNameKind &kind) {
    if (text.size == 4U && std::memcmp(text.data, "type", 4U) == 0) {
        kind = ScriptNameKind::type;
        return true;
    }
    if (text.size == 6U && std::memcmp(text.data, "symbol", 6U) == 0) {
        kind = ScriptNameKind::symbol;
        return true;
    }
    if (text.size == 5U && std::memcmp(text.data, "state", 5U) == 0) {
        kind = ScriptNameKind::state;
        return true;
    }
    if (text.size == 6U && std::memcmp(text.data, "buffer", 6U) == 0) {
        kind = ScriptNameKind::buffer;
        return true;
    }
    if (text.size == 3U && std::memcmp(text.data, "cue", 3U) == 0) {
        kind = ScriptNameKind::cue;
        return true;
    }
    return false;
}

[[nodiscard]] std::uint64_t entity_argument(lua_State *state, const int index) {
    const lua_Number value = luaL_checknumber(state, index);
    if (value < 0.0 || value > 9'007'199'254'740'991.0 ||
        value != static_cast<lua_Number>(static_cast<std::uint64_t>(value))) {
        fail(state, "entity must be a non-negative integer");
    }
    return static_cast<std::uint64_t>(value);
}

[[nodiscard]] std::uint32_t id_argument(lua_State *state, const int index,
                                        const char *label) {
    const lua_Number value = luaL_checknumber(state, index);
    if (value < 0.0 || value > 4'294'967'295.0 ||
        value != static_cast<lua_Number>(static_cast<std::uint32_t>(value))) {
        fail(state, label);
    }
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] double finite_argument(lua_State *state, const int index,
                                     const char *label) {
    const double value = static_cast<double>(luaL_checknumber(state, index));
    if (value != value || value == std::numeric_limits<double>::infinity() ||
        value == -std::numeric_limits<double>::infinity()) {
        fail(state, label);
    }
    return value;
}

void push_command(lua_State *state, const ScriptCommand &command) {
    Script::Storage *const value = storage(state);
    if (value == nullptr || value->commands.size() == value->limits.commands) {
        fail(state, "command limit reached");
    }
    value->commands.push_back(command);
}

void require_entity(lua_State *state, const std::uint64_t entity) {
    Script::Storage *const value = storage(state);
    if (value == nullptr || value->host.entity_exists == nullptr ||
        !value->host.entity_exists(value->host.context, entity)) {
        fail(state, "unknown entity");
    }
}

void require_type(lua_State *state, const std::uint32_t type) {
    Script::Storage *const value = storage(state);
    if (value == nullptr || value->host.type_exists == nullptr ||
        !value->host.type_exists(value->host.context, type)) {
        fail(state, "unknown type");
    }
}

void require_writable_field(lua_State *state, const std::uint32_t field) {
    Script::Storage *const value = storage(state);
    if (value == nullptr || value->host.field_writable == nullptr ||
        !value->host.field_writable(value->host.context, field)) {
        fail(state, "field is not writable");
    }
}

void require_cue(lua_State *state, const std::uint32_t cue) {
    Script::Storage *const value = storage(state);
    if (value == nullptr || value->host.cue_exists == nullptr ||
        !value->host.cue_exists(value->host.context, cue)) {
        fail(state, "unknown cue");
    }
}

void require_board_cell(lua_State *state, const std::size_t cell,
                        const std::uint32_t value) {
    Script::Storage *const script = storage(state);
    if (script == nullptr || script->host.board_cell == nullptr ||
        !script->host.board_cell(script->host.context, cell, value)) {
        fail(state, "invalid board cell or value");
    }
}

int engine_random(lua_State *state) {
    Script::Storage *const value = storage(state);
    if (value == nullptr || value->host.random == nullptr) {
        fail(state, "engine RNG is unavailable");
    }
    const std::uint64_t bits = value->host.random(value->host.context);
    constexpr double scale = 1.0 / 9'007'199'254'740'992.0;
    lua_pushnumber(state, static_cast<lua_Number>(bits >> 11U) * scale);
    return 1;
}

int engine_entity(lua_State *state) {
    Script::Storage *const value = storage(state);
    double result = 0.0;
    const std::uint64_t entity = entity_argument(state, 1);
    const std::uint32_t field = id_argument(state, 2, "field must be an ID");
    if (value == nullptr || value->host.entity_value == nullptr ||
        !value->host.entity_value(value->host.context, entity, field, result)) {
        fail(state, "unknown entity or field");
    }
    lua_pushnumber(state, result);
    return 1;
}

int engine_state(lua_State *state) {
    Script::Storage *const value = storage(state);
    double result = 0.0;
    const std::uint32_t field = id_argument(state, 1, "field must be an ID");
    if (value == nullptr || value->host.state_value == nullptr ||
        !value->host.state_value(value->host.context, field, result)) {
        fail(state, "unknown state field");
    }
    lua_pushnumber(state, result);
    return 1;
}

int engine_buffer(lua_State *state) {
    Script::Storage *const value = storage(state);
    double result = 0.0;
    const std::uint32_t buffer = id_argument(state, 1, "buffer must be an ID");
    const std::uint64_t index = entity_argument(state, 2);
    if (index > static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max()) ||
        value == nullptr || value->host.buffer_value == nullptr ||
        !value->host.buffer_value(value->host.context, buffer,
                                  static_cast<std::size_t>(index), result)) {
        fail(state, "unknown buffer or index");
    }
    lua_pushnumber(state, result);
    return 1;
}

int engine_board(lua_State *state) {
    Script::Storage *const value = storage(state);
    double result = 0.0;
    const std::uint64_t index = entity_argument(state, 1);
    if (index > static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max()) ||
        value == nullptr || value->host.board_value == nullptr ||
        !value->host.board_value(value->host.context,
                                 static_cast<std::size_t>(index), result)) {
        fail(state, "unknown board index");
    }
    lua_pushnumber(state, result);
    return 1;
}

int engine_id(lua_State *state) {
    const TextArgument category =
        text_argument(state, 1, "ID kind must be a short string");
    const TextArgument name =
        text_argument(state, 2, "ID name must be a short string");
    ScriptNameKind kind = ScriptNameKind::type;
    if (!name_kind(category, kind)) {
        fail(state, "unknown ID kind");
    }
    Script::Storage *const value = storage(state);
    std::uint32_t result = 0;
    if (value == nullptr || value->host.resolve_id == nullptr ||
        !value->host.resolve_id(value->host.context, kind, name.data, name.size,
                                result)) {
        fail(state, "unknown or ambiguous ID name");
    }
    lua_pushnumber(state, static_cast<lua_Number>(result));
    return 1;
}

int engine_move(lua_State *state) {
    const std::uint64_t entity = entity_argument(state, 1);
    require_entity(state, entity);
    push_command(state,
                 ScriptCommand{ScriptCommandKind::move, entity, 0U, 0U,
                               finite_argument(state, 2, "x must be finite"),
                               finite_argument(state, 3, "y must be finite")});
    return 0;
}

int engine_show(lua_State *state) {
    const std::uint64_t entity = entity_argument(state, 1);
    require_entity(state, entity);
    push_command(state, ScriptCommand{ScriptCommandKind::show, entity});
    return 0;
}

int engine_hide(lua_State *state) {
    const std::uint64_t entity = entity_argument(state, 1);
    require_entity(state, entity);
    push_command(state, ScriptCommand{ScriptCommandKind::hide, entity});
    return 0;
}

int engine_kill(lua_State *state) {
    const std::uint64_t entity = entity_argument(state, 1);
    require_entity(state, entity);
    push_command(state, ScriptCommand{ScriptCommandKind::kill, entity});
    return 0;
}

int engine_spawn(lua_State *state) {
    const std::uint32_t type = id_argument(state, 1, "type must be an ID");
    require_type(state, type);
    push_command(state, ScriptCommand{
                            ScriptCommandKind::spawn,
                            0U,
                            0U,
                            type,
                            finite_argument(state, 2, "x must be finite"),
                            finite_argument(state, 3, "y must be finite"),
                        });
    return 0;
}

int engine_set(lua_State *state) {
    const std::uint64_t entity = entity_argument(state, 1);
    const std::uint32_t field = id_argument(state, 2, "field must be an ID");
    require_entity(state, entity);
    require_writable_field(state, field);
    push_command(state, ScriptCommand{
                            ScriptCommandKind::set,
                            entity,
                            field,
                            0U,
                            finite_argument(state, 3, "value must be finite"),
                            0.0,
                        });
    return 0;
}

int engine_cue(lua_State *state) {
    const std::uint32_t cue = id_argument(state, 1, "cue must be an ID");
    require_cue(state, cue);
    push_command(state,
                 ScriptCommand{
                     ScriptCommandKind::cue,
                     0U,
                     cue,
                     0U,
                     finite_argument(state, 2, "first value must be finite"),
                     finite_argument(state, 3, "second value must be finite"),
                 });
    return 0;
}

int engine_board_set(lua_State *state) {
    const std::uint64_t index = entity_argument(state, 1);
    if (index >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        fail(state, "board index is out of range");
    }
    const std::uint32_t value =
        id_argument(state, 2, "board value must be an ID");
    require_board_cell(state, static_cast<std::size_t>(index), value);
    push_command(state,
                 ScriptCommand{ScriptCommandKind::board_set, index, value});
    return 0;
}

void instruction_hook(lua_State *state, lua_Debug *) {
    Script::Storage *const value = storage(state);
    if (value == nullptr) {
        fail(state, "Lua context is unavailable");
    }
    value->instructions += 1000U;
    if (value->instructions > value->limits.instructions) {
        fail(state, "Lua instruction limit reached");
    }
}

void hide_global(lua_State *state, const char *name) {
    lua_pushnil(state);
    lua_setglobal(state, name);
}

void add_engine_function(lua_State *state, const char *name,
                         lua_CFunction function) {
    lua_pushcfunction(state, function);
    lua_setfield(state, -2, name);
}

void install_engine(lua_State *state) {
    lua_newtable(state);
    add_engine_function(state, "random", engine_random);
    add_engine_function(state, "entity", engine_entity);
    add_engine_function(state, "state", engine_state);
    add_engine_function(state, "buffer", engine_buffer);
    add_engine_function(state, "board", engine_board);
    add_engine_function(state, "id", engine_id);
    add_engine_function(state, "move", engine_move);
    add_engine_function(state, "show", engine_show);
    add_engine_function(state, "hide", engine_hide);
    add_engine_function(state, "kill", engine_kill);
    add_engine_function(state, "spawn", engine_spawn);
    add_engine_function(state, "set", engine_set);
    add_engine_function(state, "cue", engine_cue);
    add_engine_function(state, "board_set", engine_board_set);
    lua_setglobal(state, "engine");
}

void sandbox(lua_State *state) {
    luaL_openlibs(state);
    constexpr std::array<const char *, 15U> hidden{
        "os",     "io",         "package",        "debug",     "ffi",
        "jit",    "require",    "module",         "load",      "loadfile",
        "dofile", "loadstring", "collectgarbage", "coroutine", "print",
    };
    for (const char *const name : hidden) {
        hide_global(state, name);
    }
    lua_getglobal(state, "math");
    lua_pushnil(state);
    lua_setfield(state, -2, "random");
    lua_pushnil(state);
    lua_setfield(state, -2, "randomseed");
    lua_pop(state, 1);
    install_engine(state);
}

void release_callbacks(Script::Storage &value) {
    if (value.state == nullptr) {
        return;
    }
    for (int &reference : value.callbacks) {
        if (reference != LUA_NOREF) {
            luaL_unref(value.state, LUA_REGISTRYINDEX, reference);
            reference = LUA_NOREF;
        }
    }
}

void close_state(Script::Storage &value) noexcept {
    if (value.state != nullptr) {
        release_callbacks(value);
        lua_close(value.state);
        value.state = nullptr;
    }
}

bool collect_callbacks(Script::Storage &value, ScriptError &error) {
    for (std::size_t index = 0; index < callback_names.size(); ++index) {
        lua_getglobal(value.state, callback_names[index]);
        if (lua_isnil(value.state, -1)) {
            lua_pop(value.state, 1);
            value.callbacks[index] = LUA_NOREF;
            continue;
        }
        if (!lua_isfunction(value.state, -1)) {
            lua_pop(value.state, 1);
            std::ostringstream message;
            message << callback_names[index] << " must be a function";
            set_error(error, value.path, value.step, message.str());
            return false;
        }
        value.callbacks[index] = luaL_ref(value.state, LUA_REGISTRYINDEX);
    }
    return true;
}

#endif

} // namespace

Script::Script(const ScriptLimits &limits) : storage_(new Storage(limits)) {}

Script::~Script() {
    if (storage_ == nullptr) {
        return;
    }
#if defined(M1_HAVE_LUAJIT)
    close_state(*storage_);
#endif
    delete storage_;
}

bool Script::load(const std::string &path, const ScriptHost &host,
                  ScriptError &error) {
    clear_commands();
    storage_->host = host;
    storage_->path = path;
    storage_->step = 0U;
    storage_->instructions = 0U;
    error = {};
#if !defined(M1_HAVE_LUAJIT)
    set_error(error, path, 0U, "LuaJIT support is not built");
    return false;
#else
    close_state(*storage_);
    std::string source;
    if (!read_source(path, storage_->limits.source_bytes, source, error)) {
        return false;
    }
    storage_->state = lua_newstate(allocate, storage_);
    if (storage_->state == nullptr) {
        set_error(error, path, 0U, "cannot allocate Lua state");
        return false;
    }
    storage_->callbacks.fill(LUA_NOREF);
    sandbox(storage_->state);
    if (luaL_loadbuffer(storage_->state, source.data(), source.size(),
                        path.c_str()) != 0) {
        set_error(error, path, 0U, lua_message(storage_->state));
        lua_pop(storage_->state, 1);
        close_state(*storage_);
        return false;
    }
    storage_->instructions = 0U;
    lua_sethook(storage_->state, instruction_hook, LUA_MASKCOUNT, 1000);
    if (lua_pcall(storage_->state, 0, 0, 0) != 0) {
        set_error(error, path, 0U, lua_message(storage_->state));
        lua_pop(storage_->state, 1);
        close_state(*storage_);
        return false;
    }
    if (!collect_callbacks(*storage_, error)) {
        close_state(*storage_);
        return false;
    }
    return true;
#endif
}

bool Script::call(const ScriptCallback callback, const std::uint64_t step,
                  ScriptError &error) {
    clear_commands();
    storage_->step = step;
    error = {};
#if !defined(M1_HAVE_LUAJIT)
    (void)callback;
    set_error(error, storage_->path, step, "LuaJIT support is not built");
    return false;
#else
    if (storage_->state == nullptr) {
        set_error(error, storage_->path, step, "Lua rules are not loaded");
        return false;
    }
    const std::size_t index = callback_index(callback);
    if (index >= storage_->callbacks.size()) {
        set_error(error, storage_->path, step, "unknown Lua callback");
        return false;
    }
    const int reference = storage_->callbacks[index];
    if (reference == LUA_NOREF) {
        return true;
    }
    storage_->instructions = 0U;
    lua_rawgeti(storage_->state, LUA_REGISTRYINDEX, reference);
    lua_pushnumber(storage_->state, static_cast<lua_Number>(step));
    if (lua_pcall(storage_->state, 1, 0, 0) != 0) {
        set_error(error, storage_->path, step, lua_message(storage_->state));
        lua_pop(storage_->state, 1);
        clear_commands();
        return false;
    }
    return true;
#endif
}

void Script::clear_commands() noexcept { storage_->commands.clear(); }

const std::vector<ScriptCommand> &Script::commands() const noexcept {
    return storage_->commands;
}

} // namespace m1
