#ifndef MOLLY_M1_MODEL_HPP
#define MOLLY_M1_MODEL_HPP

#include "simulation/pde.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <limits>
#include <optional>
#include <string>
#include <vector>

/// \file
/// Scenario data shared by parsing, kernels, snapshots, and tests

namespace m1 {

/// Pick the update loop named by a scenario file
enum class Kernel : std::uint8_t {
    continuous,
    cellular,
    turn,
    timeline,
    pde,
};
/// Choose the coordinate system expected by the visualiser
enum class View : std::uint8_t { plane, grid };
/// Describe how one entity is drawn in a snapshot
enum class Shape : std::uint8_t { circle, cell, text, icon, sprite };
/// Describe how the renderer should place an entity between frames
enum class Motion : std::uint8_t {
    static_,
    grounded,
    ballistic,
    flight,
    water,
};
/// Name one behaviour in a compiled character plan
enum class BehaviourCode : std::uint8_t {
    idle,
    seek,
    flee,
    pursue,
    evade,
    consume,
    separate,
    align,
    cohere,
    avoid,
    wander,
};
/// Bit flags used by the reciprocal nearest-neighbour fast path
enum Behaviour : std::uint8_t {
    seek = 1U << 0U,
    flee = 1U << 1U,
    consume = 1U << 2U,
    sense = 1U << 3U,
};

/// Values shared by every kernel in one scenario run
struct WorldConfig {
    // Plane or grid extent, depending on the chosen kernel
    double width = 0.0;
    double height = 0.0;
    // Fixed simulation increment and the requested number of increments
    double time_step = 0.0;
    std::uint64_t steps = 0;
    // Reproducible source for simulation randomness
    std::uint64_t seed = 0;
    // Continuous kernels use this for boundary handling
    bool wraps = false;
};

/// One weighted action attached to a character group
struct BehaviourRecord {
    // Action, character type, and rule-specific tuning from the scenario file
    BehaviourCode code = BehaviourCode::idle;
    std::uint32_t target = std::numeric_limits<std::uint32_t>::max();
    double weight = 1.0;
    double parameter = 0.0;
};

/// A contiguous range of entities with one starting plan and behaviour list
struct CharacterPlan {
    // Entity range in State and the optional target character type
    std::size_t first = 0;
    std::size_t count = 0;
    std::uint32_t target = 0;
    std::uint8_t behaviours = 0;
    double step_distance = 0.0;
    // Pre-squared radii avoid square roots in neighbour searches
    double sensing_radius_squared = 0.0;
    double capture_radius_squared = 0.0;
    // Explicit starting coordinates override the generated placement
    double initial_x = 0.0;
    double initial_y = 0.0;
    bool positioned = false;
    bool initial_alive = true;
    // Range into Scenario::behaviour_plan for detailed rules
    std::size_t first_behaviour = 0;
    std::size_t behaviour_count = 0;
    std::size_t initial_count = 0;
    double max_steering = 0.0;
    double obstacle_radius = 0.0;
};

/// Presentation data kept separate from the simulation state
struct RenderStyle {
    // CSV appearance fields interpreted by the Python visualiser
    Shape shape = Shape::circle;
    std::string colour;
    std::string glyph;
    std::int32_t layer = 0;
    double size = 1.0;
    std::string label;
    // East is the default sprite while north and south track vertical travel
    std::uint32_t sprite = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t sprite_north = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t sprite_south = std::numeric_limits<std::uint32_t>::max();
    Motion motion = Motion::static_;
};

/// Grid dimensions and the initial byte state for a cellular kernel
struct CellularPlan {
    // Cells are row-major and use zero for an empty state
    std::size_t columns = 0;
    std::size_t rows = 0;
    std::uint8_t state_count = 2;
    bool wraps = true;
    std::vector<std::uint8_t> initial;
};

/// Board dimensions for a turn-based kernel
struct TurnPlan {
    std::size_t columns = 0;
    std::size_t rows = 0;
};

/// A local asset referenced by a render style
struct AssetPlan {
    std::string name;
    std::string path;
    std::string kind;
};

/// Parsed scenario data passed unchanged from loading through simulation
struct Scenario {
    // Parsed input is immutable after validation except for compiled Lua rules
    WorldConfig world{};
    // Each character type has matching entries in these parallel vectors
    std::vector<std::string> names;
    std::vector<CharacterPlan> characters;
    std::vector<BehaviourRecord> behaviour_plan;
    std::vector<AssetPlan> assets;
    // Output cadence and total entity count are resolved during parsing
    std::uint64_t snapshot_stride = 1;
    std::size_t entity_count = 0;
    Kernel kernel = Kernel::continuous;
    View view = View::plane;
    std::vector<RenderStyle> styles;
    // Kernel-specific plans are populated only for the selected kernel
    CellularPlan cellular{};
    TurnPlan turn{};
    PdePlan pde{};
    // Source locations make relative assets and Lua callbacks reproducible
    std::string lua_rules;
    std::string lua_directory;
    std::string source_directory;
};

/// Mutable arrays for one run, indexed by the entity ranges in CharacterPlan
struct State {
    // Separate fields keep particle loops contiguous
    std::vector<double> x;
    std::vector<double> y;
    std::vector<double> next_x;
    std::vector<double> next_y;
    std::vector<double> velocity_x;
    std::vector<double> velocity_y;
    // Timeline kernels use these to interpolate moving visual records
    std::vector<double> timeline_z;
    std::vector<std::uint32_t> timeline_state;
    std::vector<std::string> timeline_text;
    std::vector<double> timeline_start_x;
    std::vector<double> timeline_start_y;
    std::vector<double> timeline_start_z;
    std::vector<double> timeline_target_x;
    std::vector<double> timeline_target_y;
    std::vector<double> timeline_target_z;
    std::vector<double> timeline_arc_height;
    std::vector<std::uint64_t> timeline_start_step;
    std::vector<std::uint64_t> timeline_end_step;
    std::vector<double> next_velocity_x;
    std::vector<double> next_velocity_y;
    // Byte flags preserve compact storage and make staged updates explicit
    std::vector<std::uint8_t> alive;
    std::vector<std::uint8_t> next_alive;
    // Separate buffers remove cellular update-order dependence
    std::vector<std::uint8_t> cells;
    std::vector<std::uint8_t> next_cells;
    std::vector<std::uint32_t> board;
    // Turn kernels update board and result while PDE kernels fill pde
    std::uint32_t turn = 0;
    std::int32_t result = -1;
    std::uint64_t turn_duration_us = 0U;
    PdeResult pde{};
    std::uint64_t spatial_revision = 0U;
};

/// Counters emitted with every completed run and consumed by the benchmark
struct Metrics {
    // Work completed by all kernels
    std::uint64_t steps = 0;
    std::uint64_t entity_updates = 0;
    std::uint64_t candidate_checks = 0;
    // Continuous-kernel neighbour-list work
    std::uint64_t pair_evaluations = 0;
    std::uint64_t pair_list_rebuilds = 0;
    std::uint64_t pair_list_bytes = 0;
    // State changes visible in snapshots or scenario outcomes
    std::uint64_t sensed_interactions = 0;
    std::uint64_t captures = 0;
    std::uint64_t births = 0;
    std::uint64_t deaths = 0;
    // Grid, turn, and timeline kernel work
    std::uint64_t cell_updates = 0;
    std::uint64_t turns = 0;
    std::uint64_t timeline_events = 0;
};

/// Called when a frame is ready for a snapshot sink
using SnapshotObserver = void (*)(std::uint64_t, const Scenario &,
                                  const State &, void *);
/// Let stream mode decide whether the next simulation step may proceed
using StepController = bool (*)(std::uint64_t, const Scenario &, State &,
                                void *);

struct ScenarioRuntime;

/// Parse scenario text without opening any referenced files
/// Returns an empty optional and writes a reason to error on failure
[[nodiscard]] std::optional<Scenario> parse_scenario(std::istream &input,
                                                     std::string &error);

/// Compile the scenario's Lua hooks before allocating its runtime state
/// Returns false and writes a compiler failure to error for an invalid hook
[[nodiscard]] bool compile_rules(Scenario &scenario, std::string &error);
/// Allocate and populate the state described by a validated scenario
[[nodiscard]] State initialise(const Scenario &scenario);
/// Run the selected kernel and optionally report frames or wait for a
/// controller
/// The caller keeps state and any ScenarioRuntime alive for the whole run
[[nodiscard]] Metrics simulate(const Scenario &scenario, State &state,
                               SnapshotObserver observer = nullptr,
                               void *context = nullptr,
                               std::uint64_t snapshot_stride = 1U,
                               StepController controller = nullptr,
                               void *controller_context = nullptr,
                               ScenarioRuntime *program = nullptr);
/// Produce stable summaries for checks and benchmark output
[[nodiscard]] std::uint64_t checksum(const State &state) noexcept;
/// Count heap bytes held by the dynamic fields in state
[[nodiscard]] std::uint64_t state_bytes(const State &state) noexcept;
/// Count live entities after a continuous or timeline update
[[nodiscard]] std::size_t active_count(const State &state) noexcept;

} // namespace m1

#endif
