#include "scene.hpp"

#include <algorithm>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string_view>

/// \file
/// Snapshot writer that maps Scenario and State into visualiser CSV records

namespace m1 {

const char snapshot_header[] =
    "frame,record,entity_id,type_id,type_name,x,y,world_width,world_height,"
    "view,shape,colour,glyph,layer,size,label,sprite,rotation,scale,opacity,"
    "velocity_x,velocity_y,title,subtitle,theme,duration_seconds,hud,labels,"
    "trails,vectors,kernel,z,state_id,motion,projection,format,focus_entity,"
    "focus_radius,run_seed,render_seed,result,turn_duration_us\n";

namespace {

// Derive visual randomness separately from simulation state
[[nodiscard]] constexpr std::uint64_t
render_seed_impl(const std::uint64_t run_seed) noexcept {
    std::uint64_t value = run_seed + 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

static_assert(render_seed_impl(0U) == 0xe220a8397b1dcdafULL);

// Keep CSV values aligned with the names consumed by the Python visualiser
[[nodiscard]] const char *view_name(const View view) noexcept {
    return view == View::grid ? "grid" : "plane";
}

[[nodiscard]] const char *shape_name(const Shape shape) noexcept {
    if (shape == Shape::cell)
        return "cell";
    if (shape == Shape::text)
        return "text";
    if (shape == Shape::icon)
        return "icon";
    if (shape == Shape::sprite)
        return "sprite";
    return "circle";
}

[[nodiscard]] const char *kernel_name(const Kernel kernel) noexcept {
    if (kernel == Kernel::cellular)
        return "cellular";
    if (kernel == Kernel::turn)
        return "turn";
    if (kernel == Kernel::timeline)
        return "timeline";
    if (kernel == Kernel::pde)
        return "pde";
    return "continuous";
}

[[nodiscard]] const char *motion_name(const Motion motion) noexcept {
    if (motion == Motion::grounded)
        return "grounded";
    if (motion == Motion::ballistic)
        return "ballistic";
    if (motion == Motion::flight)
        return "flight";
    return motion == Motion::water ? "water" : "static";
}

[[nodiscard]] constexpr std::uint32_t
directional_sprite(const std::uint32_t east, const std::uint32_t north,
                   const std::uint32_t south, const double velocity_x,
                   const double velocity_y) noexcept {
    const double absolute_x = velocity_x < 0.0 ? -velocity_x : velocity_x;
    const double absolute_y = velocity_y < 0.0 ? -velocity_y : velocity_y;
    if (north == std::numeric_limits<std::uint32_t>::max() ||
        absolute_y <= absolute_x)
        return east;
    return velocity_y < 0.0 ? north : south;
}

static_assert(directional_sprite(1U, 2U, 3U, 0.0, -1.0) == 2U);
static_assert(directional_sprite(1U, 2U, 3U, 0.0, 1.0) == 3U);
static_assert(directional_sprite(1U, 2U, 3U, -1.0, 0.0) == 1U);
static_assert(directional_sprite(1U, std::numeric_limits<std::uint32_t>::max(),
                                 3U, 0.0, -1.0) == 1U);

void quoted(std::ostream &output, const std::string_view text) {
    output << '"';
    for (const char value : text) {
        if (value == '"')
            output << '"';
        output << value;
    }
    output << '"';
}

// Append a quoted CSV field, doubling embedded quotes
void text(std::ostream &output, const std::string_view value) {
    output << ',';
    quoted(output, value);
}

// Reserve empty CSV columns when a record does not use them
void empty(std::ostream &output, const std::size_t count) {
    for (std::size_t field = 0; field < count; ++field)
        output << ',';
}

// Translate scenario-local and shared asset paths into visualiser paths
void asset(std::ostream &output, const Scenario &scenario,
           const std::uint32_t index) {
    output << ',';
    if (index >= scenario.assets.size())
        return;
    constexpr std::string_view shared{"shared/"};
    const std::string &path = scenario.assets[index].path;
    if (path.rfind(shared, 0U) == 0U) {
        quoted(output,
               std::string{"proj/assets/"} + path.substr(shared.size()));
        return;
    }
    quoted(output, scenario.source_directory + '/' + path);
}

// Fields repeated by frame and entity records for reproducible playback
void run_fields(std::ostream &output, const Scenario &scenario,
                const State &state) {
    output << ',' << scenario.world.seed << ','
           << render_seed_impl(scenario.world.seed) << ',';
    if (state.result >= 0)
        output << state.result;
    output << ',' << state.turn_duration_us << '\n';
}

// Write the metadata record that starts each snapshot frame
void frame(std::ostream &output, const std::uint64_t index,
           const Scenario &scenario, const State &state) {
    output << index << ",frame";
    empty(output, 5U);
    output << ',' << scenario.world.width << ',' << scenario.world.height << ','
           << view_name(scenario.view);
    empty(output, 12U);
    output << ",,,\"neutral\",20,true,\"auto\",0,false,"
           << kernel_name(scenario.kernel);
    empty(output, 3U);
    output << ",flat,demo,,";
    run_fields(output, scenario, state);
}

// Emit only occupied cells so sparse grids do not inflate snapshot files
void grid(std::ostream &output, const std::uint64_t frame,
          const Scenario &scenario, const State &state) {
    static const RenderStyle fallback{Shape::cell,
                                      "55AA55",
                                      "",
                                      0,
                                      1.0,
                                      "",
                                      std::numeric_limits<std::uint32_t>::max(),
                                      std::numeric_limits<std::uint32_t>::max(),
                                      std::numeric_limits<std::uint32_t>::max(),
                                      Motion::static_};
    const std::size_t columns = scenario.kernel == Kernel::cellular
                                    ? scenario.cellular.columns
                                    : scenario.turn.columns;
    const std::vector<std::uint8_t> *cells =
        scenario.kernel == Kernel::cellular ? &state.cells : nullptr;
    const std::size_t size =
        cells == nullptr ? state.board.size() : cells->size();
    for (std::size_t cell = 0; cell < size; ++cell) {
        const std::uint32_t value =
            cells == nullptr ? state.board[cell] : (*cells)[cell];
        if (value == 0U)
            continue;
        const std::size_t type = static_cast<std::size_t>(value - 1U);
        if (scenario.kernel == Kernel::turn &&
            (type >= scenario.styles.size() || type >= scenario.names.size())) {
            throw std::runtime_error(
                "turn board value is outside declared character range");
        }
        const RenderStyle &style =
            type < scenario.styles.size() ? scenario.styles[type] : fallback;
        const std::string_view name =
            type < scenario.names.size()
                ? std::string_view{scenario.names[type]}
                : std::string_view{"state"};
        output << frame << ",entity," << cell << ',' << value << ',' << name
               << ',' << cell % columns << ',' << cell / columns << ','
               << scenario.world.width << ',' << scenario.world.height
               << ",grid," << shape_name(style.shape) << ',' << style.colour
               << ',' << style.glyph << ',' << style.layer << ',' << style.size;
        text(output, style.label);
        asset(output, scenario, style.sprite);
        empty(output, 13U);
        output << ',' << kernel_name(scenario.kernel) << ",0," << value << ','
               << motion_name(style.motion) << ",flat,demo,,";
        run_fields(output, scenario, state);
    }
}

} // namespace

std::uint64_t render_seed(const std::uint64_t run_seed) noexcept {
    return render_seed_impl(run_seed);
}

std::string snapshot_path(const std::string_view scenario) {
    const std::string_view name =
        scenario.substr(scenario.find_last_of('/') + 1U);
    return std::string{"results/snapshots/"} + std::string{name} + ".csv";
}

void write_snapshot(const std::uint64_t index, const Scenario &scenario,
                    const State &state, void *const context) {
    auto &output = *static_cast<std::ostream *>(context);
    // Every frame starts with shared dimensions and run metadata
    frame(output, index, scenario, state);
    if (scenario.kernel == Kernel::cellular ||
        scenario.kernel == Kernel::turn) {
        // Grid kernels store occupancy rather than entity coordinate arrays
        grid(output, index, scenario, state);
        return;
    }
    // Character ranges preserve type order while State stays flat for kernels
    const char *const view = view_name(scenario.view);
    for (std::size_t type = 0; type < scenario.characters.size(); ++type) {
        const CharacterPlan &plan = scenario.characters[type];
        const RenderStyle &style = scenario.styles[type];
        const char *const shape = shape_name(style.shape);
        for (std::size_t entity = plan.first; entity < plan.first + plan.count;
             ++entity) {
            if (state.alive[entity] == 0U)
                continue;
            const std::string *glyph = &style.glyph;
            std::uint32_t sprite = style.sprite;
            // Velocity chooses a directional sprite when the style provides one
            if (style.shape == Shape::sprite &&
                entity < state.velocity_x.size() &&
                entity < state.velocity_y.size()) {
                sprite = directional_sprite(
                    style.sprite, style.sprite_north, style.sprite_south,
                    state.velocity_x[entity], state.velocity_y[entity]);
            }
            // Timeline events can replace the default sprite or text per entity
            if (entity < state.timeline_state.size() &&
                style.shape == Shape::sprite &&
                state.timeline_state[entity] != 0U)
                sprite = state.timeline_state[entity] - 1U;
            if (entity < state.timeline_text.size() &&
                style.shape == Shape::text)
                glyph = &state.timeline_text[entity];
            output << index << ",entity," << entity << ',' << type << ','
                   << scenario.names[type] << ',' << state.x[entity] << ','
                   << state.y[entity] << ',' << scenario.world.width << ','
                   << scenario.world.height << ',' << view << ',' << shape
                   << ',' << style.colour << ',' << *glyph << ',' << style.layer
                   << ',' << style.size;
            text(output, style.label);
            asset(output, scenario, sprite);
            empty(output, 3U);
            if (entity < state.velocity_x.size()) {
                output << ',' << state.velocity_x[entity] << ','
                       << state.velocity_y[entity];
            } else {
                empty(output, 2U);
            }
            empty(output, 8U);
            output << ',' << kernel_name(scenario.kernel) << ',';
            if (entity < state.timeline_z.size()) {
                output << state.timeline_z[entity] << ','
                       << state.timeline_state[entity];
            } else {
                output << "0,0";
            }
            output << ',' << motion_name(style.motion) << ",flat,demo,,";
            run_fields(output, scenario, state);
        }
    }
}

void write_stream_snapshot(const std::uint64_t index, const Scenario &scenario,
                           const State &state, void *const context) {
    write_snapshot(index, scenario, state, context);
    auto &output = *static_cast<std::ostream *>(context);
    // The end record lets readers present a complete frame as it arrives
    output << index << ",end";
    empty(output, 5U);
    output << ',' << scenario.world.width << ',' << scenario.world.height << ','
           << view_name(scenario.view);
    empty(output, 28U);
    run_fields(output, scenario, state);
    output.flush();
}

} // namespace m1
