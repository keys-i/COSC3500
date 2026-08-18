#include "model.hpp"

#include <cerrno>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <sys/stat.h>

namespace {

constexpr std::string_view scenario_root{"proj/m1/scenarios/"};

[[nodiscard]] bool valid_scenario_name(std::string_view name) {
    constexpr std::string_view prefixes[]{"templates/", "test/"};
    bool prefix_found = false;
    for (const std::string_view prefix : prefixes) {
        if (name.size() >= prefix.size() &&
            name.substr(0U, prefix.size()) == prefix) {
            name.remove_prefix(prefix.size());
            prefix_found = true;
            break;
        }
    }
    if (!prefix_found || name.empty()) {
        return false;
    }
    for (const char value : name) {
        const bool letter =
            (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
        const bool digit = value >= '0' && value <= '9';
        if (!letter && !digit && value != '_' && value != '-') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool regular_file(const std::string &path) noexcept {
    struct stat information{};
    return lstat(path.c_str(), &information) == 0 &&
           S_ISREG(information.st_mode) && !S_ISLNK(information.st_mode);
}

[[nodiscard]] bool directory(const std::string &path) noexcept {
    struct stat information{};
    return lstat(path.c_str(), &information) == 0 &&
           S_ISDIR(information.st_mode) && !S_ISLNK(information.st_mode);
}

[[nodiscard]] bool safe_inputs(const m1::Scenario &scenario) {
    if (!scenario.lua_rules.empty() &&
        !regular_file(scenario.source_directory + '/' + scenario.lua_rules)) {
        return false;
    }
    for (const m1::AssetPlan &asset : scenario.assets) {
        if (!regular_file(scenario.source_directory + '/' + asset.path)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] const char *view_name(const m1::View view) noexcept {
    return view == m1::View::grid ? "grid" : "plane";
}

[[nodiscard]] const char *shape_name(const m1::Shape shape) noexcept {
    if (shape == m1::Shape::cell) {
        return "cell";
    }
    if (shape == m1::Shape::text) {
        return "text";
    }
    if (shape == m1::Shape::sprite) {
        return "sprite";
    }
    return "circle";
}

void report_error(const char *const message) noexcept {
    static_cast<void>(std::fputs(message, stderr));
}

void write_frame(std::ostream &output, const std::uint64_t frame,
                 const m1::Scenario &scenario) {
    output << frame << ",frame,,,,,," << scenario.world.width << ','
           << scenario.world.height << ',' << view_name(scenario.view)
           << ",,,,\n";
}

void write_grid_snapshot(std::ostream &output, const std::uint64_t frame,
                         const m1::Scenario &scenario, const m1::State &state) {
    const std::size_t columns = scenario.kernel == m1::Kernel::cellular
                                    ? scenario.cellular.columns
                                    : scenario.turn.columns;
    const std::vector<std::uint8_t> *cells = nullptr;
    if (scenario.kernel == m1::Kernel::cellular) {
        cells = &state.cells;
    }
    const std::size_t size =
        cells == nullptr ? state.board.size() : cells->size();
    for (std::size_t cell = 0; cell < size; ++cell) {
        const std::uint32_t value =
            cells == nullptr ? state.board[cell] : (*cells)[cell];
        if (value == 0U) {
            continue;
        }
        const std::size_t type = static_cast<std::size_t>(value - 1U);
        const bool styled = type < scenario.styles.size();
        const m1::RenderStyle fallback{m1::Shape::cell, "55AA55", "", 0};
        const m1::RenderStyle &style =
            styled ? scenario.styles[type] : fallback;
        const std::string_view name =
            type < scenario.names.size()
                ? std::string_view{scenario.names[type]}
                : std::string_view{"state"};
        output << frame << ",entity," << cell << ',' << value << ',' << name
               << ',' << cell % columns << ',' << cell / columns << ','
               << scenario.world.width << ',' << scenario.world.height
               << ",grid," << shape_name(style.shape) << ',' << style.colour
               << ',' << style.glyph << ',' << style.layer << '\n';
    }
}

void write_snapshot(const std::uint64_t frame, const m1::Scenario &scenario,
                    const m1::State &state, void *const context) {
    auto &output = *static_cast<std::ostream *>(context);
    write_frame(output, frame, scenario);
    if (scenario.kernel == m1::Kernel::cellular ||
        scenario.kernel == m1::Kernel::turn) {
        write_grid_snapshot(output, frame, scenario, state);
        return;
    }
    for (std::size_t type = 0; type < scenario.characters.size(); ++type) {
        const m1::CharacterPlan &plan = scenario.characters[type];
        const m1::RenderStyle &style = scenario.styles[type];
        const std::size_t end = plan.first + plan.count;
        for (std::size_t entity = plan.first; entity < end; ++entity) {
            if (state.alive[entity] == 0U) {
                continue;
            }
            output << frame << ",entity," << entity << ',' << type << ','
                   << scenario.names[type] << ',' << state.x[entity] << ','
                   << state.y[entity] << ',' << scenario.world.width << ','
                   << scenario.world.height << ',' << view_name(scenario.view)
                   << ',' << shape_name(style.shape) << ',' << style.colour
                   << ',' << style.glyph << ',' << style.layer << '\n';
        }
    }
}

[[nodiscard]] std::optional<m1::Scenario>
load_scenario(const std::string_view name, std::string &error) {
    const std::string root = std::string{scenario_root} + std::string{name};
    std::string source_directory = root;
    std::string path = root + ".sim";
    const bool flat = regular_file(path);
    if (!flat) {
        path = root + "/scenario.sim";
    } else {
        source_directory.resize(source_directory.find_last_of('/'));
    }
    if ((!flat && !regular_file(path)) || !directory(source_directory)) {
        error = " cannot open scenario";
        return std::nullopt;
    }
    std::ifstream input{path};
    if (!input) {
        error = " cannot open scenario file";
        return std::nullopt;
    }
    auto scenario = m1::parse_scenario(input, error);
    if (!scenario) {
        return std::nullopt;
    }
    scenario->source_directory = source_directory;
    if (!safe_inputs(*scenario)) {
        error = " scenario contains a missing or unsafe local file";
        return std::nullopt;
    }
    return scenario;
}

[[nodiscard]] std::string snapshot_path(const std::string_view scenario) {
    const std::string_view name =
        scenario.substr(scenario.find_last_of('/') + 1U);
    return std::string{"results/snapshots/"} + std::string{name} + ".csv";
}

[[nodiscard]] std::string cue_path(const std::string_view scenario) {
    const std::string_view name =
        scenario.substr(scenario.find_last_of('/') + 1U);
    return std::string{"results/snapshots/"} + std::string{name} + ".cues.csv";
}

void write_csv_text(std::ostream &output, const std::string_view text) {
    output << '"';
    for (const char value : text) {
        if (value == '"') {
            output << '"';
        }
        output << value;
    }
    output << '"';
}

[[nodiscard]] std::string write_cues(const m1::Scenario &scenario,
                                     const std::string_view name) {
    if (scenario.cues.empty()) {
        return {};
    }
    const std::string path = cue_path(name);
    std::ofstream output{path};
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output << "frame,kind,asset,text,x,y,width,height,rotation,scale,opacity,"
              "duration,volume,layer\n"
           << std::setprecision(17);
    for (const m1::CuePlan &cue : scenario.cues) {
        output << cue.frame << ',';
        write_csv_text(output, scenario.symbols[cue.kind]);
        output << ',';
        if (cue.asset < scenario.assets.size()) {
            write_csv_text(output, scenario.source_directory + '/' +
                                       scenario.assets[cue.asset].path);
        } else {
            write_csv_text(output, "");
        }
        output << ',';
        write_csv_text(output, cue.text);
        output << ',' << cue.x << ',' << cue.y << ',' << cue.width << ','
               << cue.height << ',' << cue.rotation << ',' << cue.scale << ','
               << cue.opacity << ',' << cue.duration << ',' << cue.volume << ','
               << cue.layer << '\n';
    }
    return path;
}

[[nodiscard]] bool ensure_directory(const char *const path) noexcept {
    struct stat information{};
    if (lstat(path, &information) == 0) {
        return S_ISDIR(information.st_mode) && !S_ISLNK(information.st_mode);
    }
    return errno == ENOENT && mkdir(path, 0755) == 0;
}

void write_stream_snapshot(const std::uint64_t frame,
                           const m1::Scenario &scenario, const m1::State &state,
                           void *const context) {
    write_snapshot(frame, scenario, state, context);
    auto &output = *static_cast<std::ostream *>(context);
    output << frame << ",end,,,,,," << scenario.world.width << ','
           << scenario.world.height << ',' << view_name(scenario.view)
           << ",,,,\n";
    output.flush();
}

[[nodiscard]] bool wait_for_tick(std::uint64_t, const m1::Scenario &,
                                 m1::State &, void *) {
    std::string input;
    return std::getline(std::cin, input) && input == "tick";
}

int stream(const m1::Scenario &scenario) {
    m1::State state = m1::initialise(scenario);
    std::cout << "frame,record,entity_id,type_id,type_name,x,y,world_width,"
                 "world_height,view,shape,colour,glyph,layer\n"
              << std::setprecision(17);
    write_stream_snapshot(0U, scenario, state, &std::cout);
    const m1::Metrics metrics =
        m1::simulate(scenario, state, write_stream_snapshot, &std::cout, 1U,
                     wait_for_tick, nullptr);
    return metrics.steps == scenario.world.steps ? 0 : 1;
}

int run(const m1::Scenario &scenario, const std::string_view name,
        const bool snapshots) {
    m1::State state = m1::initialise(scenario);
    const std::size_t initial = m1::active_count(state);
    m1::Metrics metrics;
    std::string output_path;
    std::string cues_path;
    if (snapshots) {
        if (!ensure_directory("results") ||
            !ensure_directory("results/snapshots")) {
            report_error("m1: cannot create snapshot directory\n");
            return 1;
        }
        output_path = snapshot_path(name);
        std::ofstream output{output_path};
        if (!output) {
            report_error("m1: cannot open snapshot file\n");
            return 1;
        }
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output << "frame,record,entity_id,type_id,type_name,x,y,world_width,"
                  "world_height,view,shape,colour,glyph,layer\n"
               << std::setprecision(17);
        write_snapshot(0U, scenario, state, &output);
        metrics = m1::simulate(scenario, state, write_snapshot, &output,
                               scenario.snapshot_stride);
        if (metrics.steps % scenario.snapshot_stride != 0U) {
            write_snapshot(metrics.steps, scenario, state, &output);
        }
        cues_path = write_cues(scenario, name);
    } else {
        metrics = m1::simulate(scenario, state);
    }
    std::printf("steps=%llu characters=%zu initial=%zu active=%zu "
                "entity_updates=%llu candidate_checks=%llu "
                "sensed_interactions=%llu births=%llu deaths=%llu "
                "cell_updates=%llu turns=%llu search_nodes=%llu "
                "path_expansions=%llu timeline_events=%llu checksum=%016llx\n",
                static_cast<unsigned long long>(metrics.steps),
                scenario.names.size(), initial, m1::active_count(state),
                static_cast<unsigned long long>(metrics.entity_updates),
                static_cast<unsigned long long>(metrics.candidate_checks),
                static_cast<unsigned long long>(metrics.sensed_interactions),
                static_cast<unsigned long long>(metrics.births),
                static_cast<unsigned long long>(metrics.deaths),
                static_cast<unsigned long long>(metrics.cell_updates),
                static_cast<unsigned long long>(metrics.turns),
                static_cast<unsigned long long>(metrics.search_nodes),
                static_cast<unsigned long long>(metrics.path_expansions),
                static_cast<unsigned long long>(metrics.timeline_events),
                static_cast<unsigned long long>(m1::checksum(state)));
    if (snapshots) {
        std::printf("snapshots=%s\n", output_path.c_str());
        if (!cues_path.empty()) {
            std::printf("cues=%s\n", cues_path.c_str());
        }
    }
    return metrics.steps == scenario.world.steps ? 0 : 1;
}

} // namespace

int main(const int argc, char *argv[]) {
    try {
        constexpr char usage[] = "usage: m1 <templates|test>/<name> "
                                 "[--snapshots|--stream]\n";
        if (argc == 2 && std::string_view{argv[1]} == "--self-test") {
            return m1::self_test();
        }
        if (argc < 2 || argc > 3) {
            report_error(usage);
            return 2;
        }
        const std::string_view option = argc == 3 ? argv[2] : "";
        const bool snapshots = option == "--snapshots";
        const bool streaming = option == "--stream";
        if ((argc == 3 && !snapshots && !streaming) ||
            !valid_scenario_name(std::string_view{argv[1]})) {
            report_error(usage);
            return 2;
        }
        std::string error;
        const auto scenario = load_scenario(argv[1], error);
        if (!scenario) {
            static_cast<void>(std::fprintf(stderr, "m1:%s\n", error.c_str()));
            return 1;
        }
        return streaming ? stream(*scenario)
                         : run(*scenario, argv[1], snapshots);
    } catch (const std::bad_alloc &) {
        report_error("m1: insufficient memory for scenario\n");
        return 1;
    } catch (const std::ios_base::failure &) {
        report_error("m1: snapshot write failed\n");
        return 1;
    } catch (const std::exception &error) {
        static_cast<void>(std::fprintf(stderr, "m1: %s\n", error.what()));
        return 1;
    }
}
