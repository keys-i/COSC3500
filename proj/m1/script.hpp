#ifndef COSC3500_PROJ_M1_SCRIPT_HPP
#define COSC3500_PROJ_M1_SCRIPT_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace m1 {

enum class ScriptCallback : std::uint8_t {
    setup,
    before_step,
    after_step,
    cellular_compile,
    turn,
    timeline,
};

enum class ScriptCommandKind : std::uint8_t {
    move,
    show,
    hide,
    kill,
    spawn,
    set,
    cue,
    board_set,
};

enum class ScriptNameKind : std::uint8_t {
    type,
    symbol,
    state,
    buffer,
    cue,
};

struct ScriptCommand {
    ScriptCommandKind kind = ScriptCommandKind::move;
    std::uint64_t entity = 0;
    std::uint32_t field = 0;
    std::uint32_t type = 0;
    double first = 0.0;
    double second = 0.0;
};

struct ScriptLimits {
    std::size_t bytes = std::size_t{16U} * 1024U * 1024U;
    std::size_t source_bytes = std::size_t{1024U} * 1024U;
    std::size_t commands = 4096U;
    std::uint64_t instructions = 1'000'000U;
};

struct ScriptError {
    std::string path;
    std::size_t line = 0;
    std::uint64_t step = 0;
    std::string message;
};

struct ScriptHost {
    void *context = nullptr;
    bool (*entity_value)(void *, std::uint64_t, std::uint32_t,
                         double &) noexcept = nullptr;
    bool (*state_value)(void *, std::uint32_t, double &) noexcept = nullptr;
    bool (*buffer_value)(void *, std::uint32_t, std::size_t,
                         double &) noexcept = nullptr;
    bool (*board_value)(void *, std::size_t, double &) noexcept = nullptr;
    bool (*entity_exists)(void *, std::uint64_t) noexcept = nullptr;
    bool (*type_exists)(void *, std::uint32_t) noexcept = nullptr;
    bool (*field_writable)(void *, std::uint32_t) noexcept = nullptr;
    bool (*cue_exists)(void *, std::uint32_t) noexcept = nullptr;
    bool (*board_cell)(void *, std::size_t, std::uint32_t) noexcept = nullptr;
    bool (*resolve_id)(void *, ScriptNameKind, const char *, std::size_t,
                       std::uint32_t &) noexcept = nullptr;
    std::uint64_t (*random)(void *) noexcept = nullptr;
};

class Script {
  public:
    struct Storage;

    explicit Script(const ScriptLimits &limits = {});
    ~Script();

    Script(const Script &) = delete;
    Script &operator=(const Script &) = delete;
    Script(Script &&) = delete;
    Script &operator=(Script &&) = delete;

    [[nodiscard]] bool load(const std::string &path, const ScriptHost &host,
                            ScriptError &error);
    [[nodiscard]] bool call(ScriptCallback callback, std::uint64_t step,
                            ScriptError &error);
    void clear_commands() noexcept;
    [[nodiscard]] const std::vector<ScriptCommand> &commands() const noexcept;

  private:
    Storage *storage_ = nullptr;
};

} // namespace m1

#endif
