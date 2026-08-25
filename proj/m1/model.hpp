#ifndef COSC3500_PROJ_M1_MODEL_HPP
#define COSC3500_PROJ_M1_MODEL_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace m1 {

enum class Kernel : std::uint8_t { continuous, cellular, turn, timeline };
enum class View : std::uint8_t { plane, grid };
enum class Shape : std::uint8_t { circle, cell, text, sprite };
enum class EventAction : std::uint8_t { move, show, hide };
enum class BehaviourCode : std::uint8_t {
    idle,
    seek,
    flee,
    consume,
    separate,
    align,
    cohere,
    avoid,
    wander,
    lua,
};
enum class ScalarKind : std::uint8_t { boolean, integer, number, identifier };
enum class SearchAlgorithm : std::uint8_t {
    none,
    bfs,
    dijkstra,
    astar,
    alphabeta,
    mcts,
    lua,
};

enum Behaviour : std::uint8_t {
    seek = 1U << 0U,
    flee = 1U << 1U,
    consume = 1U << 2U,
};

struct WorldConfig {
    double width = 0.0;
    double height = 0.0;
    double time_step = 0.0;
    std::uint64_t steps = 0;
    std::uint64_t seed = 0;
};

struct BehaviourRecord {
    BehaviourCode code = BehaviourCode::idle;
    std::uint32_t target = std::numeric_limits<std::uint32_t>::max();
    double weight = 1.0;
    double parameter = 0.0;
};

struct CharacterPlan {
    std::size_t first = 0;
    std::size_t count = 0;
    std::uint32_t target = 0;
    std::uint8_t behaviours = 0;
    double step_distance = 0.0;
    double sensing_radius_squared = 0.0;
    double capture_radius_squared = 0.0;
    double initial_x = 0.0;
    double initial_y = 0.0;
    bool positioned = false;
    bool initial_alive = true;
    std::size_t first_behaviour = 0;
    std::size_t behaviour_count = 0;
};

struct RenderStyle {
    Shape shape = Shape::circle;
    std::string colour;
    std::string glyph;
    std::int32_t layer = 0;
};

struct TimelineEvent {
    std::uint64_t step = 0;
    std::size_t entity = 0;
    EventAction action = EventAction::move;
    double x = 0.0;
    double y = 0.0;
    std::uint32_t value = 0;
};

struct CellularPlan {
    std::size_t columns = 0;
    std::size_t rows = 0;
    std::uint8_t state_count = 2;
    std::uint8_t count_state = 1;
    bool wraps = true;
    std::uint16_t birth_mask = 0;
    std::uint16_t survive_mask = 0;
    std::vector<std::uint8_t> initial;
    std::vector<std::uint8_t> transition;
};

struct TurnPlan {
    std::size_t columns = 0;
    std::size_t rows = 0;
    std::vector<std::uint32_t> edges;
    SearchAlgorithm search = SearchAlgorithm::none;
    std::uint64_t search_budget = 0;
};

struct ScalarPlan {
    std::string name;
    ScalarKind kind = ScalarKind::integer;
    std::int64_t integer = 0;
    double number = 0.0;
    bool boolean = false;
    std::uint32_t identifier = 0;
};

struct BufferPlan {
    std::string name;
    ScalarKind kind = ScalarKind::integer;
    std::size_t capacity = 0;
};

struct AssetPlan {
    std::string name;
    std::string path;
    std::string kind;
};

struct ActionPlan {
    std::uint64_t step = 0;
    std::uint32_t actor = 0;
    std::uint32_t verb = 0;
    std::array<double, 4U> arguments{};
};

struct CuePlan {
    std::uint64_t frame = 0;
    std::uint32_t kind = 0;
    std::uint32_t asset = std::numeric_limits<std::uint32_t>::max();
    std::string text;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    double rotation = 0.0;
    double scale = 1.0;
    double opacity = 1.0;
    double duration = 0.0;
    double volume = 1.0;
    std::int32_t layer = 0;
};

struct BufferState {
    ScalarKind kind = ScalarKind::integer;
    std::vector<std::uint8_t> booleans;
    std::vector<std::int64_t> integers;
    std::vector<double> numbers;
    std::vector<std::uint32_t> identifiers;
};

struct Scenario {
    WorldConfig world{};
    std::vector<std::string> names;
    std::vector<CharacterPlan> characters;
    std::vector<std::string> symbols;
    std::vector<BehaviourRecord> behaviour_plan;
    std::vector<ScalarPlan> scalars;
    std::vector<BufferPlan> buffers;
    std::vector<AssetPlan> assets;
    std::vector<ActionPlan> actions;
    std::vector<std::string> cue_names;
    std::vector<CuePlan> cues;
    std::uint64_t snapshot_stride = 1;
    std::size_t entity_count = 0;
    Kernel kernel = Kernel::continuous;
    View view = View::plane;
    std::vector<RenderStyle> styles;
    std::vector<TimelineEvent> events;
    CellularPlan cellular{};
    TurnPlan turn{};
    std::string lua_rules;
    std::string source_directory;
};

struct State {
    std::vector<double> x;
    std::vector<double> y;
    std::vector<double> next_x;
    std::vector<double> next_y;
    std::vector<double> velocity_x;
    std::vector<double> velocity_y;
    std::vector<double> next_velocity_x;
    std::vector<double> next_velocity_y;
    std::vector<std::uint8_t> alive;
    std::vector<std::uint8_t> next_alive;
    std::vector<std::uint8_t> cells;
    std::vector<std::uint8_t> next_cells;
    std::vector<ScalarPlan> scalars;
    std::vector<BufferState> buffers;
    std::vector<std::uint32_t> board;
    std::uint32_t turn = 0;
};

struct Metrics {
    std::uint64_t steps = 0;
    std::uint64_t entity_updates = 0;
    std::uint64_t candidate_checks = 0;
    std::uint64_t sensed_interactions = 0;
    std::uint64_t births = 0;
    std::uint64_t deaths = 0;
    std::uint64_t cell_updates = 0;
    std::uint64_t turns = 0;
    std::uint64_t search_nodes = 0;
    std::uint64_t path_expansions = 0;
    std::uint64_t timeline_events = 0;
};

using SnapshotObserver = void (*)(std::uint64_t, const Scenario &,
                                  const State &, void *);
using StepController = bool (*)(std::uint64_t, const Scenario &, State &,
                                void *);

[[nodiscard]] std::optional<Scenario> parse_scenario(std::istream &input,
                                                     std::string &error);

[[nodiscard]] State initialise(const Scenario &scenario);
[[nodiscard]] Metrics simulate(const Scenario &scenario, State &state,
                               SnapshotObserver observer = nullptr,
                               void *context = nullptr,
                               std::uint64_t snapshot_stride = 1U,
                               StepController controller = nullptr,
                               void *controller_context = nullptr);
[[nodiscard]] std::uint64_t checksum(const State &state) noexcept;
[[nodiscard]] std::size_t active_count(const State &state) noexcept;
[[nodiscard]] int self_test();

} // namespace m1

#endif
