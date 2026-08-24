#include "bench.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

/// \file
/// Run deterministic m1 children, validate output, and report timing samples

extern char **environ;

namespace hpc::bench {

const Program *program(const std::string_view target,
                       const std::string_view case_name) noexcept {
    if (target != "m1") {
        return nullptr;
    }
    for (const Case &value : m1_cases()) {
        if (case_name == value.name) {
            return &value.program;
        }
    }
    return nullptr;
}

} // namespace hpc::bench

namespace {

// Command-line settings kept separate from each measured child process
struct Options {
    std::string binary;
    std::string case_name;
    std::size_t warmups = 1U;
    std::size_t samples = 3U;
    std::size_t minimum_case_ms = 100U;
    bool csv = false;
};

// Summary of one case's normalised timing samples
struct Statistics {
    double median;
    double cv;
    double ci_low;
    double ci_high;
};

// Bytes captured from one child alongside its peak resident memory
struct Measurement {
    std::string output;
    std::uint64_t peak_rss_bytes = 0U;
};

// Page-policy values reported by the m1 benchmark child
struct PageReport {
    std::string policy;
    std::uint64_t host_page_bytes = 0U;
    std::uint64_t advised_bytes = 0U;
    std::uint64_t anon_huge_bytes = 0U;
    bool backing_verified = false;
};

// Deterministic neighbour-list counters checked across samples
struct ChildMetrics {
    std::uint64_t state_bytes = 0U;
    std::uint64_t pair_evaluations = 0U;
    std::uint64_t pair_list_rebuilds = 0U;
    std::uint64_t pair_list_bytes = 0U;
};

// One accumulated sample after enough child runs reach the timing floor
struct Sample {
    double ns_per_unit = 0.0;
    std::uint64_t peak_rss_bytes = 0U;
    std::string checksum;
    PageReport pages;
    ChildMetrics metrics;
};

[[nodiscard]] std::size_t positive(const std::string &value,
                                   const std::string_view name) {
    std::size_t consumed = 0U;
    const unsigned long long parsed = std::stoull(value, &consumed);
    if (consumed != value.size() || parsed == 0U) {
        throw std::runtime_error(std::string(name) +
                                 " must be a positive integer");
    }
    return static_cast<std::size_t>(parsed);
}

// Convert a reported unsigned field and reject trailing input
[[nodiscard]] std::uint64_t number(const std::string_view value,
                                   const std::string_view name) {
    std::size_t consumed = 0U;
    const unsigned long long parsed =
        std::stoull(std::string(value), &consumed);
    if (consumed != value.size()) {
        throw std::runtime_error(std::string(name) + " must be an integer");
    }
    return parsed;
}

// Find the last field because elapsed output follows the deterministic summary
[[nodiscard]] std::string_view field(const std::string_view output,
                                     const std::string_view name) {
    const std::size_t begin = output.rfind(name);
    if (begin == std::string_view::npos) {
        throw std::runtime_error("benchmark did not report " +
                                 std::string(name));
    }
    const std::size_t value = begin + name.size();
    const std::size_t end = output.find_first_of(" \n", value);
    return output.substr(value, end - value);
}

// Parse the page-policy line emitted by m1 --benchmark
[[nodiscard]] PageReport pages(const std::string_view output) {
    const std::string_view verified = field(output, "backing_verified=");
    if (verified != "true" && verified != "false") {
        throw std::runtime_error("backing_verified must be true or false");
    }
    return {std::string(field(output, "page_policy=")),
            number(field(output, "host_page_bytes="), "host_page_bytes"),
            number(field(output, "advised_bytes="), "advised_bytes"),
            number(field(output, "anon_huge_bytes="), "anon_huge_bytes"),
            verified == "true"};
}

[[nodiscard]] bool same_pages(const PageReport &left,
                              const PageReport &right) noexcept {
    return left.policy == right.policy &&
           left.host_page_bytes == right.host_page_bytes &&
           left.advised_bytes == right.advised_bytes &&
           left.anon_huge_bytes == right.anon_huge_bytes &&
           left.backing_verified == right.backing_verified;
}

// Parse counters that must not change between deterministic child runs
[[nodiscard]] ChildMetrics child_metrics(const std::string_view output) {
    return {number(field(output, "state_bytes="), "state_bytes"),
            number(field(output, "pair_evaluations="), "pair_evaluations"),
            number(field(output, "pair_list_rebuilds="), "pair_list_rebuilds"),
            number(field(output, "pair_list_bytes="), "pair_list_bytes")};
}

[[nodiscard]] bool same_metrics(const ChildMetrics &left,
                                const ChildMetrics &right) noexcept {
    return left.state_bytes == right.state_bytes &&
           left.pair_evaluations == right.pair_evaluations &&
           left.pair_list_rebuilds == right.pair_list_rebuilds &&
           left.pair_list_bytes == right.pair_list_bytes;
}

[[nodiscard]] Options parse_options(const int argc, char *argv[]) {
    // Parse before spawning a child so option errors have no side effects
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string name = argv[index];
        if (name == "--csv") {
            options.csv = true;
            continue;
        }
        if (index + 1 >= argc) {
            throw std::runtime_error("missing value for " + name);
        }
        const std::string value = argv[++index];
        if (name == "--binary") {
            options.binary = value;
        } else if (name == "--case") {
            options.case_name = value;
        } else if (name == "--warmup") {
            options.warmups = positive(value, name);
        } else if (name == "--samples") {
            options.samples = positive(value, name);
        } else if (name == "--minimum-case-ms") {
            options.minimum_case_ms = positive(value, name);
        } else {
            throw std::runtime_error("unknown option: " + name);
        }
    }
    if (options.binary.empty()) {
        throw std::runtime_error("--binary is required");
    }
    return options;
}

// Interpolate a percentile after sorting a small sample set
[[nodiscard]] double percentile(const std::vector<double> &sorted,
                                const double probability) {
    const double position =
        static_cast<double>(sorted.size() - 1U) * probability;
    const auto low = static_cast<std::size_t>(std::floor(position));
    const auto high = static_cast<std::size_t>(std::ceil(position));
    return sorted[low] +
           (sorted[high] - sorted[low]) * (position - static_cast<double>(low));
}

[[nodiscard]] double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return percentile(values, 0.5);
}

// Resample medians with a fixed generator for a repeatable confidence interval
[[nodiscard]] std::pair<double, double>
median_interval(const std::vector<double> &values) {
    // Fix the bootstrap seed so reports can be compared
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp,bugprone-*)
    std::mt19937_64 random(0x35007502ULL);
    std::uniform_int_distribution<std::size_t> pick(0U, values.size() - 1U);
    std::vector<double> medians;
    std::vector<double> sample(values.size());
    medians.reserve(1'000U);
    for (std::size_t round = 0U; round < 1'000U; ++round) {
        for (double &value : sample) {
            value = values[pick(random)];
        }
        medians.push_back(median(sample));
    }
    std::sort(medians.begin(), medians.end());
    return {percentile(medians, 0.025), percentile(medians, 0.975)};
}

[[nodiscard]] Statistics statistics(const std::vector<double> &values) {
    if (values.empty()) {
        throw std::runtime_error("statistics require samples");
    }
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) /
                        static_cast<double>(values.size());
    double squared = 0.0;
    for (const double value : values) {
        squared += (value - mean) * (value - mean);
    }
    const double deviation =
        values.size() > 1U
            ? std::sqrt(squared / static_cast<double>(values.size() - 1U))
            : 0.0;
    const auto interval = median_interval(values);
    return {median(values), mean == 0.0 ? 0.0 : deviation / mean,
            interval.first, interval.second};
}

// macOS reports bytes while Linux reports kibibytes in ru_maxrss
[[nodiscard]] std::uint64_t rss_bytes(const rusage &usage) noexcept {
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024U;
#endif
}

// Spawn one m1 process and retain its stdout for validation
[[nodiscard]] Measurement run(const std::string &binary,
                              const hpc::bench::Program &program) {
    // Use a child so wait4 reports this sample's peak RSS
    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0) {
        throw std::system_error(errno, std::generic_category(), "pipe failed");
    }
    posix_spawn_file_actions_t actions{};
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        throw std::runtime_error("posix_spawn setup failed");
    }
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);
    char benchmark[] = "--benchmark";
    char seed[] = "--seed";
    // Use the same seed for every timing sample
    char seed_value[] = "31";
    char *arguments[] = {const_cast<char *>(binary.c_str()),
                         benchmark,
                         const_cast<char *>(program.argument.data()),
                         seed,
                         seed_value,
                         nullptr};
    pid_t child = 0;
    const int spawned = posix_spawn(&child, binary.c_str(), &actions, nullptr,
                                    arguments, environ);
    // Actions are process-local once posix_spawn returns
    posix_spawn_file_actions_destroy(&actions);
    close(pipefd[1]);
    if (spawned != 0) {
        close(pipefd[0]);
        throw std::system_error(spawned, std::generic_category(),
                                "posix_spawn failed");
    }
    std::string output;
    char buffer[4096];
    // Drain stdout before waiting so a verbose child cannot fill the pipe
    for (;;) {
        const ssize_t count = read(pipefd[0], buffer, sizeof(buffer));
        if (count > 0) {
            output.append(buffer, static_cast<std::size_t>(count));
        } else if (count == 0) {
            break;
        } else if (errno != EINTR) {
            close(pipefd[0]);
            throw std::system_error(errno, std::generic_category(),
                                    "read failed");
        }
    }
    close(pipefd[0]);
    int status = 0;
    rusage usage{};
    // wait4 couples the child's termination status with its maximum RSS
    while (wait4(child, &status, 0, &usage) == -1) {
        if (errno != EINTR) {
            throw std::system_error(errno, std::generic_category(),
                                    "waitpid failed");
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw std::runtime_error("benchmark child failed");
    }
    return {std::move(output), rss_bytes(usage)};
}

[[nodiscard]] std::uint64_t elapsed(const std::string &output,
                                    const hpc::bench::Program &program) {
    // Validate only fields before elapsed_ns because timing varies by run
    const std::size_t marker = output.rfind("elapsed_ns=");
    const std::string_view deterministic =
        marker == std::string::npos
            ? std::string_view{}
            : std::string_view(output).substr(0U, marker);
    if ((!program.expected_output.empty() &&
         deterministic != program.expected_output) ||
        (!program.expected_prefix.empty() &&
         !deterministic.starts_with(program.expected_prefix)) ||
        (program.checksum != "invariant" &&
         field(deterministic, "checksum=") != program.checksum)) {
        throw std::runtime_error(
            "benchmark output differs from the expected output");
    }
    if (marker == std::string::npos) {
        throw std::runtime_error("benchmark did not report elapsed_ns");
    }
    const std::size_t first = marker + 11U;
    const std::size_t last = output.find_first_not_of("0123456789", first);
    return positive(output.substr(first, last - first), "elapsed_ns");
}

// Repeat a case until startup noise is small relative to timed work
[[nodiscard]] Sample sample(const Options &options,
                            const hpc::bench::Program &program) {
    // Accumulate child runs until the configured timing floor is reached
    const std::uint64_t minimum =
        static_cast<std::uint64_t>(options.minimum_case_ms) * 1'000'000U;
    std::uint64_t duration = 0U;
    std::uint64_t operations = 0U;
    std::uint64_t peak_rss = 0U;
    std::string checksum;
    PageReport page_report;
    ChildMetrics metrics;
    bool have_page_report = false;
    bool have_metrics = false;
    while (duration < minimum) {
        Measurement result = run(options.binary, program);
        duration += elapsed(result.output, program);
        operations += program.operations;
        peak_rss = std::max(peak_rss, result.peak_rss_bytes);
        // Deterministic fields must agree before their timings may be combined
        const std::string current_checksum{field(result.output, "checksum=")};
        if (!checksum.empty() && checksum != current_checksum) {
            throw std::runtime_error("checksum differs within sample");
        }
        checksum = current_checksum;
        const PageReport current = pages(result.output);
        if (have_page_report && !same_pages(page_report, current)) {
            throw std::runtime_error("page backing differs between samples");
        }
        page_report = current;
        have_page_report = true;
        const ChildMetrics current_metrics = child_metrics(result.output);
        if (have_metrics && !same_metrics(metrics, current_metrics)) {
            throw std::runtime_error(
                "simulation metrics differ between samples");
        }
        metrics = current_metrics;
        have_metrics = true;
    }
    return {static_cast<double>(duration) / static_cast<double>(operations),
            peak_rss, std::move(checksum), std::move(page_report), metrics};
}

void benchmark_case(const Options &options, const hpc::bench::Case &value) {
    // Discard warmups so setup work stays out of the timing data
    for (std::size_t warmup = 0U; warmup < options.warmups; ++warmup) {
        static_cast<void>(sample(options, value.program));
    }
    std::vector<double> values;
    std::uint64_t peak_rss = 0U;
    std::string checksum;
    PageReport page_report;
    ChildMetrics metrics;
    bool have_page_report = false;
    bool have_metrics = false;
    values.reserve(options.samples);
    for (std::size_t count = 0U; count < options.samples; ++count) {
        const auto sample_result = sample(options, value.program);
        values.push_back(sample_result.ns_per_unit);
        peak_rss = std::max(peak_rss, sample_result.peak_rss_bytes);
        if (!checksum.empty() && checksum != sample_result.checksum) {
            throw std::runtime_error("checksum differs between samples");
        }
        checksum = sample_result.checksum;
        if (have_page_report && !same_pages(page_report, sample_result.pages)) {
            throw std::runtime_error("page backing differs between samples");
        }
        page_report = sample_result.pages;
        have_page_report = true;
        if (have_metrics && !same_metrics(metrics, sample_result.metrics)) {
            throw std::runtime_error(
                "simulation metrics differ between benchmark samples");
        }
        metrics = sample_result.metrics;
        have_metrics = true;
    }
    const Statistics result = statistics(values);
    const double throughput = 1'000.0 / result.median;
    // CSV is consumed by report.py while the table is intended for a terminal
    if (options.csv) {
        std::cout << value.name << ',' << values.size() << ',' << std::fixed
                  << std::setprecision(6) << result.median << ','
                  << result.ci_low << ',' << result.ci_high << ','
                  << result.cv * 100.0 << ',' << value.program.unit << ','
                  << metrics.state_bytes << ','
                  << metrics.pair_evaluations << ','
                  << metrics.pair_list_rebuilds << ','
                  << metrics.pair_list_bytes << ',' << peak_rss << ','
                  << throughput << ',' << checksum << ",true,"
                  << page_report.policy << ',' << page_report.host_page_bytes
                  << ',' << page_report.advised_bytes << ','
                  << page_report.anon_huge_bytes << ','
                  << (page_report.backing_verified ? "true" : "false") << '\n';
        return;
    }
    std::ostringstream interval;
    interval << std::fixed << std::setprecision(2) << '[' << result.ci_low
             << ", " << result.ci_high << ']';
    std::cout << std::left << std::setw(18) << value.name << std::right
              << std::setw(9) << values.size() << std::setw(15) << std::fixed
              << std::setprecision(2) << result.median << ' ' << std::setw(17)
              << interval.str() << ' ' << std::setw(8) << std::setprecision(2)
              << result.cv * 100.0 << '%' << std::setw(14)
              << std::setprecision(2) << throughput << ' ' << value.program.unit
              << ' ' << std::setw(10) << metrics.state_bytes
              << " B rss=" << peak_rss
              << " B pairs=" << metrics.pair_evaluations
              << " rebuilds=" << metrics.pair_list_rebuilds
              << " pair-list=" << metrics.pair_list_bytes
              << " B pages=" << page_report.policy << '/'
              << page_report.host_page_bytes
              << " advised=" << page_report.advised_bytes
              << " huge=" << page_report.anon_huge_bytes << " verified="
              << (page_report.backing_verified ? "true" : "false") << '\n';
}

[[nodiscard]] int benchmark(const int argc, char *argv[]) {
    const Options options = parse_options(argc, argv);
    const auto &cases = hpc::bench::m1_cases();
    // The header describes either the machine-readable or terminal output path
    if (options.csv) {
        std::cout
            << "case,samples,median_ns_per_unit,ci_low_ns_per_unit,"
               "ci_high_ns_per_unit,cv_percent,unit,state_bytes,"
               "pair_evaluations,pair_list_rebuilds,pair_list_bytes,peak_rss_"
               "bytes,"
               "throughput_munits_per_s,checksum,verified,"
               "page_policy,host_page_bytes,advised_bytes,anon_huge_bytes,"
               "page_backing_verified\n";
    } else {
        std::cout << std::left << std::setw(18) << "case" << std::right
                  << std::setw(9) << "samples" << std::setw(15)
                  << "median ns/op" << ' ' << std::setw(17) << "95% CI" << ' '
                  << std::setw(8) << "CV" << std::setw(14) << "Munit/s"
                  << " unit state\n";
    }
    // Default runs omit 10M+ and capacity cases; --case selects one exactly
    for (const hpc::bench::Case &value : cases) {
        if ((!options.case_name.empty() && options.case_name != value.name) ||
            (options.case_name.empty() &&
             (value.name.ends_with("/10m") ||
              value.name.ends_with("/100m") ||
              value.name.ends_with("/1b") ||
              value.name.starts_with("capacity/")))) {
            continue;
        }
        benchmark_case(options, value);
    }
    if (!options.case_name.empty() &&
        hpc::bench::program("m1", options.case_name) == nullptr) {
        throw std::runtime_error("unknown benchmark case: " +
                                 options.case_name);
    }
    return 0;
}

} // namespace

#ifndef COSC3500_BENCH_EMBEDDED
int main(const int argc, char *argv[]) {
    try {
        return benchmark(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << "hpc_bench: " << error.what() << '\n';
        return 2;
    }
}
#endif
