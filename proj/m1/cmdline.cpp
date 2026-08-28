#include "scene.hpp"
#include "simulation/runtime/lua.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <ostream>
#include <random>
#include <span>
#include <streambuf>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>
#if defined(__linux__)
#include <sys/mman.h>
#include <sys/prctl.h>
#endif

/// \file
/// Command-line boundary that loads a scenario, runs it, and publishes reports

namespace m1 {
namespace {

// Keep command-line failures on stderr without allocating a temporary string
void report_error(const char *const message) noexcept {
    static_cast<void>(std::fputs(message, stderr));
}

[[nodiscard]] bool regular_file(const std::string &path) noexcept {
    struct stat information = {};
    return lstat(path.c_str(), &information) == 0 &&
           S_ISREG(information.st_mode) && !S_ISLNK(information.st_mode);
}

[[nodiscard]] bool directory(const std::string &path) noexcept {
    struct stat information = {};
    return lstat(path.c_str(), &information) == 0 &&
           S_ISDIR(information.st_mode) && !S_ISLNK(information.st_mode);
}

class FileBuffer final : public std::streambuf {
  public:
    explicit FileBuffer(const int descriptor) : descriptor_{descriptor} {
        setp(buffer_, buffer_ + sizeof(buffer_));
    }

  private:
    // Retry interrupted writes so snapshot output is either complete or fails
    [[nodiscard]] bool flush_buffer() noexcept {
        const char *data = pbase();
        std::ptrdiff_t remaining = pptr() - pbase();
        while (remaining > 0) {
            const ssize_t written =
                write(descriptor_, data, static_cast<std::size_t>(remaining));
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0)
                return false;
            data += written;
            remaining -= written;
        }
        setp(buffer_, buffer_ + sizeof(buffer_));
        return true;
    }

    int_type overflow(const int_type value) override {
        if (!flush_buffer())
            return traits_type::eof();
        if (!traits_type::eq_int_type(value, traits_type::eof())) {
            *pptr() = traits_type::to_char_type(value);
            pbump(1);
        }
        return traits_type::not_eof(value);
    }

    std::streamsize xsputn(const char *data,
                           std::streamsize remaining) override {
        const std::streamsize requested = remaining;
        while (remaining > 0) {
            if (epptr() == pptr() && !flush_buffer())
                break;
            const std::streamsize space = epptr() - pptr();
            const std::streamsize count = remaining < space ? remaining : space;
            std::memcpy(pptr(), data, static_cast<std::size_t>(count));
            pbump(static_cast<int>(count));
            data += count;
            remaining -= count;
        }
        return requested - remaining;
    }

    int sync() override { return flush_buffer() ? 0 : -1; }

    int descriptor_;
    char buffer_[65536];
};

void validate_output(const std::string &path) {
    struct stat information = {};
    if (lstat(path.c_str(), &information) == 0) {
        if (S_ISREG(information.st_mode) && !S_ISLNK(information.st_mode))
            return;
    } else if (errno == ENOENT) {
        return;
    }
    throw std::ios_base::failure("unsafe output path");
}

class AtomicOutput final {
  public:
    explicit AtomicOutput(std::string path)
        : path_{std::move(path)}, temporary_{path_ + ".tmp.XXXXXX"},
          descriptor_{create(path_, temporary_)},
          buffer_{std::make_unique<FileBuffer>(descriptor_)},
          output_{std::make_unique<std::ostream>(buffer_.get())} {
        output_->exceptions(std::ios::badbit | std::ios::failbit);
    }

    AtomicOutput(const AtomicOutput &) = delete;
    AtomicOutput &operator=(const AtomicOutput &) = delete;

    ~AtomicOutput() {
        // A failed run leaves no partial CSV at the final destination
        if (descriptor_ >= 0)
            static_cast<void>(close(descriptor_));
        if (!published_)
            static_cast<void>(unlink(temporary_.c_str()));
    }

    [[nodiscard]] std::ostream &stream() noexcept { return *output_; }

    void publish() {
        // Flush and close before rename so readers never observe a partial file
        output_->flush();
        const int descriptor = descriptor_;
        descriptor_ = -1;
        if (close(descriptor) != 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "cannot close output");
        }
        validate_output(path_);
        if (rename(temporary_.c_str(), path_.c_str()) != 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "cannot publish output");
        }
        published_ = true;
    }

  private:
    // Refuse symlinks and non-files before creating a sibling temporary file
    [[nodiscard]] static int create(const std::string &final_path,
                                    std::string &temporary) {
        validate_output(final_path);
        const int descriptor = mkstemp(temporary.data());
        if (descriptor < 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "cannot create output");
        }
        return descriptor;
    }

    std::string path_;
    std::string temporary_;
    int descriptor_ = -1;
    std::unique_ptr<FileBuffer> buffer_;
    std::unique_ptr<std::ostream> output_;
    bool published_ = false;
};

[[nodiscard]] bool ensure_directory(const char *const path) noexcept {
    struct stat information = {};
    if (lstat(path, &information) == 0) {
        return S_ISDIR(information.st_mode) && !S_ISLNK(information.st_mode);
    }
    return errno == ENOENT && mkdir(path, 0755) == 0;
}

// Values printed with a benchmark so page-size experiments can be checked later
struct PageReport {
    std::string_view policy{"none"};
    std::uint64_t host_page_bytes = 0U;
    std::uint64_t advised_bytes = 0U;
    std::uint64_t anon_huge_bytes = 0U;
    bool backing_verified = false;
};

// Only Linux can verify that each advised span received the requested backing
[[nodiscard]] constexpr bool page_backing_verified(
    const std::string_view policy, const std::uint64_t host_page_bytes,
    const std::uint64_t advised_bytes, const std::uint64_t anon_huge_bytes,
    const bool has_spans) noexcept {
    return host_page_bytes == 4096U && has_spans && advised_bytes != 0U &&
           (policy == "base" ? anon_huge_bytes == 0U
                             : anon_huge_bytes == advised_bytes);
}

constexpr std::uint64_t mib = 1024ULL * 1024ULL;
static_assert(page_backing_verified("base", 4096U, 2U * mib, 0U, true));
static_assert(page_backing_verified("huge", 4096U, 2U * mib, 2U * mib, true));
static_assert(!page_backing_verified("huge", 4096U, 4U * mib, 2U * mib, true));
static_assert(!page_backing_verified("huge", 4096U, 2U * mib, 4U * mib, true));

template <class Integer>
[[nodiscard]] bool parse_integer(const std::string_view text, Integer &value,
                                 const int base = 10) noexcept {
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), value, base);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

class PageProbe final {
  public:
    explicit PageProbe(State &state) {
        // The environment makes page experiments opt-in for ordinary runs
        const char *requested = std::getenv("M1_PAGE_POLICY");
        if (requested == nullptr || *requested == '\0')
            return;
        if (std::strcmp(requested, "base") != 0 &&
            std::strcmp(requested, "huge") != 0) {
            valid_ = false;
            return;
        }
        report_.policy = requested;
        const long host_page = sysconf(_SC_PAGESIZE);
        if (host_page <= 0)
            return;
        report_.host_page_bytes = static_cast<std::uint64_t>(host_page);
#if defined(__linux__)
        // Advise complete huge-page-sized interiors, never allocator edges
        const auto add = [this]<class T>(std::vector<T> &values) {
            constexpr std::size_t huge_page = std::size_t{2} * 1024U * 1024U;
            const auto storage = std::as_writable_bytes(std::span<T>{values});
            if (storage.size() < huge_page)
                return;
            void *aligned = storage.data();
            std::size_t remaining = storage.size();
            if (std::align(huge_page, huge_page, aligned, remaining) == nullptr)
                return;
            const std::size_t bytes = remaining - remaining % huge_page;
            const int advice =
                report_.policy == "base" ? MADV_NOHUGEPAGE : MADV_HUGEPAGE;
            if (madvise(aligned, bytes, advice) != 0)
                return;
#if defined(MADV_COLLAPSE)
            if (report_.policy == "huge") {
                static_cast<void>(madvise(aligned, bytes, MADV_COLLAPSE));
            }
#endif
            const auto begin = reinterpret_cast<std::uintptr_t>(aligned);
            spans_.emplace_back(begin, begin + bytes);
            report_.advised_bytes += bytes;
        };
        add(state.x);
        add(state.y);
        add(state.next_x);
        add(state.next_y);
        add(state.velocity_x);
        add(state.velocity_y);
        add(state.next_velocity_x);
        add(state.next_velocity_y);
        add(state.alive);
        add(state.next_alive);
        add(state.cells);
        add(state.next_cells);
#else
        static_cast<void>(state);
#endif
    }

    void inspect() {
#if defined(__linux__)
        // smaps reports huge backing by mapped region rather than allocation
        std::ifstream smaps{"/proc/self/smaps"};
        std::string line;
        bool overlaps = false;
        while (std::getline(smaps, line)) {
            const std::string_view view{line};
            std::uintptr_t begin = 0U;
            std::uintptr_t end = 0U;
            const std::size_t dash = view.find('-');
            const std::size_t space = dash == std::string_view::npos
                                          ? std::string_view::npos
                                          : view.find(' ', dash + 1U);
            if (dash != std::string_view::npos &&
                space != std::string_view::npos &&
                parse_integer(view.substr(0U, dash), begin, 16) &&
                parse_integer(view.substr(dash + 1U, space - dash - 1U), end,
                              16)) {
                overlaps = std::any_of(spans_.begin(), spans_.end(),
                                       [begin, end](const auto span) {
                                           return begin < span.second &&
                                                  span.first < end;
                                       });
            } else if (overlaps && view.starts_with("AnonHugePages:")) {
                std::string_view amount = view.substr(14U);
                const std::size_t first = amount.find_first_not_of(' ');
                if (first == std::string_view::npos)
                    continue;
                amount.remove_prefix(first);
                const std::size_t last = amount.find(' ');
                if (last != std::string_view::npos)
                    amount = amount.substr(0U, last);
                std::uint64_t kib = 0U;
                if (parse_integer(amount, kib))
                    report_.anon_huge_bytes += kib * 1024ULL;
            }
        }
        report_.backing_verified = page_backing_verified(
            report_.policy, report_.host_page_bytes, report_.advised_bytes,
            report_.anon_huge_bytes, !spans_.empty());
#endif
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] const PageReport &report() const noexcept { return report_; }

  private:
    PageReport report_{};
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> spans_;
    bool valid_ = true;
};

struct ScenarioFile {
    // Resolved file plus directories used by scenario-relative resources
    std::string path;
    std::string source_directory;
    std::string name;
    std::string parent_directory;
};

// Selectors are constrained to repository scenario roots before path building
[[nodiscard]] bool valid_scenario_name(std::string_view name) {
    constexpr std::string_view templates_prefix{"templates/"};
    constexpr std::string_view test_prefix{"test/"};
    if (name.starts_with(templates_prefix)) {
        name.remove_prefix(templates_prefix.size());
    } else if (name.starts_with(test_prefix)) {
        name.remove_prefix(test_prefix.size());
    } else {
        return false;
    }
    if (name.empty() || name.front() == '/' || name.back() == '/' ||
        std::count(name.begin(), name.end(), '/') > 1) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](const char value) {
        return (value >= 'a' && value <= 'z') ||
               (value >= 'A' && value <= 'Z') ||
               (value >= '0' && value <= '9') || value == '_' || value == '-' ||
               value == '/';
    });
}

[[nodiscard]] std::optional<ScenarioFile>
find_scenario(const std::string_view selector) {
    // Flat fixtures and scenario directories share the public selector form
    constexpr std::string_view templates_prefix{"templates/"};
    constexpr std::string_view template_root{"proj/scenarios"};
    constexpr std::string_view test_root{"tests/scenarios/fixtures"};
    const bool is_template = selector.starts_with(templates_prefix);
    const std::string_view prefix =
        is_template ? templates_prefix : std::string_view{"test/"};
    const std::string_view requested = selector.substr(prefix.size());
    const std::string base{is_template ? template_root : test_root};
    const std::size_t slash = requested.find('/');
    const std::string parent =
        slash == std::string_view::npos
            ? base
            : base + '/' + std::string{requested.substr(0U, slash)};
    const std::string_view name = slash == std::string_view::npos
                                      ? requested
                                      : requested.substr(slash + 1U);
    const std::string source = parent + '/' + std::string{name};
    const std::string flat = source + ".sim";
    if (directory(parent) && regular_file(flat)) {
        return ScenarioFile{flat, parent, std::string{name}, parent};
    }
    const std::string bundled = source + "/scenario.sim";
    if (directory(parent) && directory(source) && regular_file(bundled)) {
        return ScenarioFile{bundled, source, std::string{name},
                            slash == std::string_view::npos ? "" : parent};
    }
    return std::nullopt;
}

[[nodiscard]] std::string asset_path(const Scenario &scenario,
                                     const std::string &path) {
    constexpr std::string_view shared{"shared/"};
    if (path.rfind(shared, 0U) == 0U) {
        return std::string{"proj/assets/"} + path.substr(shared.size());
    }
    return scenario.source_directory + '/' + path;
}

// Confirm every declared visual asset is a regular file before Lua is prepared
[[nodiscard]] bool safe_inputs(const Scenario &scenario) {
    return std::all_of(scenario.assets.begin(), scenario.assets.end(),
                       [&scenario](const AssetPlan &asset) {
                           return regular_file(
                               asset_path(scenario, asset.path));
                       });
}

[[nodiscard]] std::optional<Scenario>
load_scenario(const std::string_view selector, std::string &name,
              std::string &error) {
    // Resolve and parse before accepting local paths or compiling Lua callbacks
    const std::optional<ScenarioFile> file = find_scenario(selector);
    if (!file) {
        error = " cannot open scenario";
        return std::nullopt;
    }
    std::ifstream input{file->path};
    if (!input) {
        error = " cannot open scenario file";
        return std::nullopt;
    }
    auto scenario = parse_scenario(input, error);
    if (!scenario)
        return std::nullopt;
    // Keep relative assets and Lua rules anchored beside the selected scenario
    scenario->source_directory = file->source_directory;
    scenario->lua_directory = file->source_directory;
    if (!scenario->lua_rules.empty() &&
        !regular_file(scenario->lua_directory + '/' + scenario->lua_rules) &&
        !file->parent_directory.empty() &&
        regular_file(file->parent_directory + '/' + scenario->lua_rules)) {
        scenario->lua_directory = file->parent_directory;
    }
    if (!safe_inputs(*scenario)) {
        error = " scenario contains a missing or unsafe local file";
        return std::nullopt;
    }
    // Compile here so run and stream share the same validation
    if (!compile_rules(*scenario, error))
        return std::nullopt;
    name = file->name;
    return scenario;
}

class PreparedProgram final {
  public:
    ~PreparedProgram() { destroy_scenario_program(runtime_); }

    [[nodiscard]] bool prepare(const Scenario &scenario, State &state,
                               std::string &error) noexcept {
        return prepare_scenario_program(scenario, state, runtime_, error);
    }

    [[nodiscard]] ScenarioRuntime *get() noexcept { return &runtime_; }

  private:
    // One RAII object keeps CLX cleanup on every exit path
    ScenarioRuntime runtime_{};
};

[[nodiscard]] bool parse_seed(const std::string_view text,
                              std::uint64_t &seed) noexcept {
    if (text.empty())
        return false;
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), seed);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

// Mix device entropy with a monotonic clock for interactive visual runs
[[nodiscard]] std::uint64_t fresh_seed() {
    std::random_device source;
    const auto now = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return (static_cast<std::uint64_t>(source()) << 32U) ^
           static_cast<std::uint64_t>(source()) ^ now;
}

[[nodiscard]] bool wait_for_tick(std::uint64_t, const Scenario &, State &,
                                 void *) {
    // The visualiser writes one complete line per requested simulation step
    std::string input;
    return std::getline(std::cin, input) && input == "tick";
}

// Stream one initial frame, then advance only after a client sends tick
int stream(Scenario scenario) {
    // Set up state and optional Lua runtime before emitting the initial frame
    State state = initialise(scenario);
    PreparedProgram program;
    std::string error;
    if (!scenario.lua_rules.empty() &&
        (!program.prepare(scenario, state, error) ||
         !setup_scenario_program(scenario, state, *program.get(), error))) {
        const std::string message = "m1: " + error + '\n';
        report_error(message.c_str());
        return 1;
    }
    // The stream uses the same records as file snapshots, framed by end records
    std::cout << snapshot_header << std::setprecision(17);
    write_stream_snapshot(0U, scenario, state, &std::cout);
    const Metrics metrics =
        simulate(scenario, state, write_stream_snapshot, &std::cout, 1U,
                 wait_for_tick, nullptr, program.get());
    return metrics.steps == scenario.world.steps || state.result >= 0 ? 0 : 1;
}

void report_pde(const Scenario &scenario, const State &state) {
    // PDE output is a compact numerical report rather than renderable snapshots
    const PdePlan &plan = scenario.pde;
    const PdeResult &result = state.pde;
    std::printf(
        "pde_grid=%zux%zu fields=%zu time_steps=%llu min_x_spacing=%.17g "
        "min_y_spacing=%.17g time_step=%.17g\n",
        plan.columns, plan.rows, plan.fields.size(),
        static_cast<unsigned long long>(result.steps), result.minimum_x_spacing,
        result.minimum_y_spacing, result.time_step);
    for (std::size_t field = 0; field < plan.fields.size(); ++field) {
        const double value =
            field < result.values.size() ? result.values[field] : 0.0;
        const double reference =
            field < result.references.size() ? result.references[field] : 0.0;
        const double error =
            field < result.errors.size() ? result.errors[field] : 0.0;
        std::printf("pde_field=%s value=%.17g reference=%.17g error=%.17g\n",
                    plan.fields[field].c_str(), value, reference, error);
    }
}

int run(Scenario scenario, const std::string_view name, const bool snapshots,
        const bool benchmark = false) {
    // Fault benchmark state into base pages before retaining NOHUGEPAGE on it
#if defined(__linux__) && defined(PR_SET_THP_DISABLE)
    const char *requested_page_policy = std::getenv("M1_PAGE_POLICY");
    const bool base_page_allocation =
        requested_page_policy != nullptr &&
        std::strcmp(requested_page_policy, "base") == 0;
    if (base_page_allocation &&
        prctl(PR_SET_THP_DISABLE, 1L, 0L, 0L, 0L) != 0) {
        report_error("m1: cannot disable transparent huge pages\n");
        return 2;
    }
#endif
    State state = initialise(scenario);
    PageProbe page_policy{state};
#if defined(__linux__) && defined(PR_SET_THP_DISABLE)
    // NOHUGEPAGE now pins the state policy; restore defaults for later storage
    if (base_page_allocation &&
        prctl(PR_SET_THP_DISABLE, 0L, 0L, 0L, 0L) != 0) {
        report_error("m1: cannot restore transparent huge pages\n");
        return 2;
    }
#endif
    if (!page_policy.valid()) {
        report_error("m1: M1_PAGE_POLICY must be base or huge\n");
        return 2;
    }
    // Prepare per-run Lua state after the C++ state exists
    PreparedProgram program;
    std::string program_error;
    if (!scenario.lua_rules.empty() &&
        (!program.prepare(scenario, state, program_error) ||
         !setup_scenario_program(scenario, state, *program.get(),
                                 program_error))) {
        const std::string message = "m1: " + program_error + '\n';
        report_error(message.c_str());
        return 1;
    }
    // Keep the initial count for the final summary before kernels mutate state
    const std::size_t initial = active_count(state);
    Metrics metrics;
    std::uint64_t elapsed_ns = 0U;
    std::string output_path;
    if (snapshots) {
        if (!ensure_directory("results") ||
            !ensure_directory("results/snapshots")) {
            report_error("m1: cannot create snapshot directory\n");
            return 1;
        }
        output_path = snapshot_path(name);
        // Publish the CSV only after simulation and final-frame writing succeed
        AtomicOutput snapshot{output_path};
        std::ostream &output = snapshot.stream();
        output << snapshot_header << std::setprecision(17);
        if (scenario.kernel != Kernel::turn)
            write_snapshot(0U, scenario, state, &output);
        metrics =
            simulate(scenario, state, write_snapshot, &output,
                     scenario.snapshot_stride, nullptr, nullptr, program.get());
        if (metrics.steps % scenario.snapshot_stride != 0U)
            write_snapshot(metrics.steps, scenario, state, &output);
        if (metrics.steps != scenario.world.steps && state.result < 0) {
            report_error("m1: simulation stopped before snapshot completion\n");
            return 1;
        }
        snapshot.publish();
    } else {
        // Non-snapshot runs keep output out of the timed simulation path
        const auto started = std::chrono::steady_clock::now();
        metrics = simulate(scenario, state, nullptr, nullptr, 1U, nullptr,
                           nullptr, program.get());
        if (benchmark) {
            const auto elapsed = std::chrono::steady_clock::now() - started;
            elapsed_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed)
                    .count());
        }
    }
    // Keep this single-line summary machine-readable for scripts and benchmarks
    std::printf(
        "steps=%llu characters=%zu initial=%zu active=%zu "
        "entity_updates=%llu candidate_checks=%llu "
        "sensed_interactions=%llu captures=%llu births=%llu deaths=%llu "
        "cell_updates=%llu turns=%llu "
        "timeline_events=%llu run_seed=%llu render_seed=%llu result=%d "
        "state_bytes=%llu checksum=%016llx\n",
        static_cast<unsigned long long>(metrics.steps), scenario.names.size(),
        initial, active_count(state),
        static_cast<unsigned long long>(metrics.entity_updates),
        static_cast<unsigned long long>(metrics.candidate_checks),
        static_cast<unsigned long long>(metrics.sensed_interactions),
        static_cast<unsigned long long>(metrics.captures),
        static_cast<unsigned long long>(metrics.births),
        static_cast<unsigned long long>(metrics.deaths),
        static_cast<unsigned long long>(metrics.cell_updates),
        static_cast<unsigned long long>(metrics.turns),
        static_cast<unsigned long long>(metrics.timeline_events),
        static_cast<unsigned long long>(scenario.world.seed),
        static_cast<unsigned long long>(render_seed(scenario.world.seed)),
        state.result, static_cast<unsigned long long>(state_bytes(state)),
        static_cast<unsigned long long>(checksum(state)));
    if (scenario.kernel == Kernel::pde)
        report_pde(scenario, state);
    if (benchmark) {
        // Timing details follow the deterministic summary for validation
        std::printf("elapsed_ns=%llu\n",
                    static_cast<unsigned long long>(elapsed_ns));
        std::printf("pair_evaluations=%llu pair_list_rebuilds=%llu "
                    "pair_list_bytes=%llu\n",
                    static_cast<unsigned long long>(metrics.pair_evaluations),
                    static_cast<unsigned long long>(metrics.pair_list_rebuilds),
                    static_cast<unsigned long long>(metrics.pair_list_bytes));
        page_policy.inspect();
        const PageReport &pages = page_policy.report();
        std::printf("page_policy=%.*s host_page_bytes=%llu advised_bytes=%llu "
                    "anon_huge_bytes=%llu backing_verified=%s\n",
                    static_cast<int>(pages.policy.size()), pages.policy.data(),
                    static_cast<unsigned long long>(pages.host_page_bytes),
                    static_cast<unsigned long long>(pages.advised_bytes),
                    static_cast<unsigned long long>(pages.anon_huge_bytes),
                    pages.backing_verified ? "true" : "false");
    }
    if (snapshots)
        std::printf("snapshots=%s\n", output_path.c_str());
    return metrics.steps == scenario.world.steps || state.result >= 0 ? 0 : 1;
}

int run_cli(const int argc, char *argv[]) {
    try {
        constexpr char usage[] =
            "usage: m1 <templates|test>/<name> [--snapshots|--stream] "
            "[--seed auto|UINT64]\n"
            "       m1 --benchmark <templates|test>/<name> [--seed UINT64]\n";
        if (argc < 2) {
            report_error(usage);
            return 2;
        }
        // Benchmark mode accepts the same selector but suppresses snapshot work
        const bool benchmark = std::string_view{argv[1]} == "--benchmark";
        int argument = benchmark ? 2 : 1;
        if (argument >= argc ||
            !valid_scenario_name(std::string_view{argv[argument]})) {
            report_error(usage);
            return 2;
        }
        // Parse all switches before opening the selected scenario
        const std::string_view selector{argv[argument++]};
        bool snapshots = false;
        bool streaming = false;
        bool seed_seen = false;
        bool automatic_seed = false;
        std::uint64_t seed = 0U;
        while (argument < argc) {
            const std::string_view option{argv[argument++]};
            if (!benchmark && option == "--snapshots" && !snapshots &&
                !streaming) {
                snapshots = true;
            } else if (!benchmark && option == "--stream" && !snapshots &&
                       !streaming) {
                streaming = true;
            } else if (option == "--seed" && !seed_seen && argument < argc) {
                const std::string_view value{argv[argument++]};
                seed_seen = true;
                automatic_seed = value == "auto";
                if ((!automatic_seed && !parse_seed(value, seed)) ||
                    (benchmark && automatic_seed)) {
                    report_error(usage);
                    return 2;
                }
            } else {
                report_error(usage);
                return 2;
            }
        }
        // Loading checks the scenario, its local files, and its Lua hooks
        std::string error;
        std::string scenario_name;
        auto scenario = load_scenario(selector, scenario_name, error);
        if (!scenario) {
            static_cast<void>(std::fprintf(stderr, "m1:%s\n", error.c_str()));
            return 1;
        }
        // PDE experiments report numerical values and have no visual frames
        if (scenario->kernel == Kernel::pde && (snapshots || streaming)) {
            report_error("m1: pde is a non-rendered experiment\n");
            return 2;
        }
        if (!seed_seen && (snapshots || streaming))
            automatic_seed = true;
        if (automatic_seed || seed_seen)
            scenario->world.seed = automatic_seed ? fresh_seed() : seed;
        // Dispatch only after all input and mode constraints have been checked
        return streaming ? stream(*scenario)
                         : run(*scenario, scenario_name, snapshots, benchmark);
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

} // namespace
} // namespace m1

int main(const int argc, char *argv[]) { return m1::run_cli(argc, argv); }
