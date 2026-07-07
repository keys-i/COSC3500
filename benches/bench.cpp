#include "bench.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utility>
#include <vector>

extern char **environ;

namespace {

constexpr std::string_view raw_csv_header =
    "timestamp,git_commit,binary_sha256,target,preset,compiler,compiler_"
    "version,flags,host,cpu,"
    "gpu,backend,input_size,seed,threads,ranks,process_run,sample,elapsed_ns,"
    "operations,bytes,"
    "ns_per_operation,operations_per_second,checksum,valid,note";
constexpr std::string_view summary_csv_header =
    "target,variant,input_size,count,minimum,median,mean,maximum,mad,standard_"
    "deviation,p5,p95,"
    "coefficient_of_variation,ci95_low,ci95_high,speedup,speedup_ci95_low,"
    "speedup_ci95_high";

struct Options {
    std::string binary;
    std::string target;
    std::string preset;
    std::string compiler;
    std::string compiler_version;
    std::string flags;
    std::string git_commit;
    std::string binary_sha256;
    std::string host;
    std::string cpu;
    std::string gpu = "none";
    std::string backend = "process-smoke";
    std::string timestamp;
    std::string output;
    std::string summary;
    std::string mode = "end-to-end";
    std::uint64_t input_size = 0;
    std::uint64_t seed = 0;
    std::size_t threads = 1;
    std::size_t ranks = 1;
    std::size_t warmups = 1;
    std::size_t samples = 3;
    std::size_t process_runs = 1;
    std::size_t minimum_case_ms = 100;
    std::size_t bootstrap_resamples = 10'000;
    bool verify = true;
};

struct Sample {
    std::size_t process_run;
    std::size_t sample;
    std::uint64_t elapsed_ns;
    std::uint64_t operations;
};

struct Statistics {
    double minimum;
    double median;
    double mean;
    double maximum;
    double mad;
    double standard_deviation;
    double p5;
    double p95;
    double coefficient_of_variation;
    double ci95_low;
    double ci95_high;
};

[[nodiscard]] std::uint64_t now_ns() {
#if defined(__linux__) && defined(CLOCK_MONOTONIC_RAW)
    timespec value{};
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &value) == 0) {
        return static_cast<std::uint64_t>(value.tv_sec) * 1'000'000'000ULL +
               static_cast<std::uint64_t>(value.tv_nsec);
    }
#endif
    const auto value = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(value).count());
}

[[nodiscard]] std::string csv_escape(const std::string &value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) {
        return value;
    }
    std::string escaped = "\"";
    for (const char character : value) {
        if (character == '\"') {
            escaped += "\"\"";
        } else {
            escaped += character;
        }
    }
    escaped += '\"';
    return escaped;
}

[[nodiscard]] std::vector<std::string> parse_csv_row(const std::string &row) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t index = 0; index < row.size(); ++index) {
        const char character = row[index];
        if (quoted && character == '\"' && index + 1 < row.size() &&
            row[index + 1] == '\"') {
            field += '\"';
            ++index;
        } else if (character == '\"') {
            quoted = !quoted;
        } else if (character == ',' && !quoted) {
            fields.push_back(field);
            field.clear();
        } else {
            field += character;
        }
    }
    if (quoted) {
        throw std::runtime_error("unterminated quoted CSV field");
    }
    fields.push_back(field);
    return fields;
}

[[nodiscard]] double percentile(const std::vector<double> &sorted,
                                const double probability) {
    if (sorted.empty()) {
        throw std::runtime_error("statistics require at least one sample");
    }
    const double position =
        static_cast<double>(sorted.size() - 1U) * probability;
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    const double fraction = position - static_cast<double>(lower);
    return sorted[lower] + (sorted[upper] - sorted[lower]) * fraction;
}

[[nodiscard]] double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return percentile(values, 0.5);
}

[[nodiscard]] std::pair<double, double>
bootstrap_median_interval(const std::vector<double> &values,
                          const std::size_t resamples) {
    if (values.empty() || resamples < 100U) {
        throw std::runtime_error(
            "bootstrap requires samples and at least 100 resamples");
    }
    std::mt19937_64 generator(0x35007502ULL);
    std::uniform_int_distribution<std::size_t> pick(0U, values.size() - 1U);
    std::vector<double> medians;
    std::vector<double> sample(values.size());
    medians.reserve(resamples);
    for (std::size_t resample = 0; resample < resamples; ++resample) {
        for (double &value : sample) {
            value = values[pick(generator)];
        }
        medians.push_back(median(sample));
    }
    std::sort(medians.begin(), medians.end());
    return {percentile(medians, 0.025), percentile(medians, 0.975)};
}

[[nodiscard]] Statistics
calculate_statistics(const std::vector<double> &values,
                     const std::size_t bootstrap_resamples) {
    if (values.empty()) {
        throw std::runtime_error("statistics require samples");
    }
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const double centre = percentile(sorted, 0.5);
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) /
                        static_cast<double>(values.size());
    double squared_difference_sum = 0.0;
    std::vector<double> absolute_deviations;
    absolute_deviations.reserve(values.size());
    for (const double value : values) {
        const double difference = value - mean;
        squared_difference_sum += difference * difference;
        absolute_deviations.push_back(std::abs(value - centre));
    }
    const double standard_deviation =
        values.size() > 1U ? std::sqrt(squared_difference_sum /
                                       static_cast<double>(values.size() - 1U))
                           : 0.0;
    const auto interval =
        bootstrap_median_interval(values, bootstrap_resamples);
    return Statistics{
        sorted.front(),
        centre,
        mean,
        sorted.back(),
        median(absolute_deviations),
        standard_deviation,
        percentile(sorted, 0.05),
        percentile(sorted, 0.95),
        mean == 0.0 ? 0.0 : standard_deviation / mean,
        interval.first,
        interval.second,
    };
}

[[nodiscard]] int wait_for_child(const pid_t process) {
    int status = 0;
    while (waitpid(process, &status, 0) == -1) {
        if (errno != EINTR) {
            throw std::runtime_error(std::string("waitpid failed: ") +
                                     std::strerror(errno));
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 128;
}

[[nodiscard]] int spawn_silent(const std::string &binary) {
    const int null_file = open("/dev/null", O_WRONLY);
    if (null_file == -1) {
        throw std::runtime_error(std::string("open /dev/null failed: ") +
                                 std::strerror(errno));
    }

    posix_spawn_file_actions_t actions{};
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(null_file);
        throw std::runtime_error(
            "posix_spawn file-action initialization failed");
    }
    posix_spawn_file_actions_adddup2(&actions, null_file, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, null_file, STDERR_FILENO);
    char *arguments[] = {const_cast<char *>(binary.c_str()), nullptr};
    pid_t process = 0;
    const int spawn_result = posix_spawn(&process, binary.c_str(), &actions,
                                         nullptr, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(null_file);
    if (spawn_result != 0) {
        throw std::runtime_error(std::string("posix_spawn failed: ") +
                                 std::strerror(spawn_result));
    }
    return wait_for_child(process);
}

[[nodiscard]] std::pair<int, std::string>
spawn_capture(const std::string &binary) {
    int descriptors[2] = {-1, -1};
    if (pipe(descriptors) != 0) {
        throw std::runtime_error(std::string("pipe failed: ") +
                                 std::strerror(errno));
    }

    posix_spawn_file_actions_t actions{};
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(descriptors[0]);
        close(descriptors[1]);
        throw std::runtime_error(
            "posix_spawn file-action initialization failed");
    }
    posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, descriptors[0]);
    posix_spawn_file_actions_addclose(&actions, descriptors[1]);
    char *arguments[] = {const_cast<char *>(binary.c_str()), nullptr};
    pid_t process = 0;
    const int spawn_result = posix_spawn(&process, binary.c_str(), &actions,
                                         nullptr, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(descriptors[1]);
    if (spawn_result != 0) {
        close(descriptors[0]);
        throw std::runtime_error(std::string("posix_spawn failed: ") +
                                 std::strerror(spawn_result));
    }

    std::string output;
    char buffer[4096];
    while (true) {
        const ssize_t count = read(descriptors[0], buffer, sizeof(buffer));
        if (count > 0) {
            output.append(buffer, static_cast<std::size_t>(count));
        } else if (count == 0) {
            break;
        } else if (errno != EINTR) {
            close(descriptors[0]);
            static_cast<void>(wait_for_child(process));
            throw std::runtime_error(std::string("read failed: ") +
                                     std::strerror(errno));
        }
    }
    close(descriptors[0]);
    return {wait_for_child(process), output};
}

[[nodiscard]] std::uint64_t fnv1a(const std::string_view value) {
    std::uint64_t hash = 14'695'981'039'346'656'037ULL;
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        hash ^= byte;
        hash *= 1'099'511'628'211ULL;
    }
    return hash;
}

[[nodiscard]] std::size_t parse_size(const std::string &value,
                                     const std::string &name) {
    std::size_t consumed = 0;
    const unsigned long long parsed = std::stoull(value, &consumed);
    if (consumed != value.size() || parsed == 0ULL) {
        throw std::runtime_error(name + " must be a positive integer");
    }
    return static_cast<std::size_t>(parsed);
}

[[nodiscard]] std::uint64_t parse_u64(const std::string &value,
                                      const std::string &name) {
    std::size_t consumed = 0;
    const unsigned long long parsed = std::stoull(value, &consumed);
    if (consumed != value.size()) {
        throw std::runtime_error(name + " must be an unsigned integer");
    }
    return static_cast<std::uint64_t>(parsed);
}

[[nodiscard]] Options parse_options(const int argc, char *argv[],
                                    const hpc::bench::Program &program) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string name = argv[index];
        if (name == "--verify") {
            options.verify = true;
            continue;
        }
        if (index + 1 >= argc) {
            throw std::runtime_error("missing value for " + name);
        }
        const std::string value = argv[++index];
        if (name == "--binary") {
            options.binary = value;
        } else if (name == "--target") {
            options.target = value;
        } else if (name == "--preset") {
            options.preset = value;
        } else if (name == "--compiler") {
            options.compiler = value;
        } else if (name == "--compiler-version") {
            options.compiler_version = value;
        } else if (name == "--flags") {
            options.flags = value;
        } else if (name == "--git-commit") {
            options.git_commit = value;
        } else if (name == "--binary-sha256") {
            options.binary_sha256 = value;
        } else if (name == "--host") {
            options.host = value;
        } else if (name == "--cpu") {
            options.cpu = value;
        } else if (name == "--gpu") {
            options.gpu = value;
        } else if (name == "--backend") {
            options.backend = value;
        } else if (name == "--timestamp") {
            options.timestamp = value;
        } else if (name == "--output") {
            options.output = value;
        } else if (name == "--summary") {
            options.summary = value;
        } else if (name == "--mode") {
            options.mode = value;
        } else if (name == "--size") {
            options.input_size = parse_u64(value, name);
        } else if (name == "--seed") {
            options.seed = parse_u64(value, name);
        } else if (name == "--threads") {
            options.threads = parse_size(value, name);
        } else if (name == "--ranks") {
            options.ranks = parse_size(value, name);
        } else if (name == "--warmup") {
            options.warmups = parse_size(value, name);
        } else if (name == "--samples") {
            options.samples = parse_size(value, name);
        } else if (name == "--process-run") {
            options.process_runs = parse_size(value, name);
        } else if (name == "--minimum-case-ms") {
            options.minimum_case_ms = parse_size(value, name);
        } else if (name == "--bootstrap-resamples") {
            options.bootstrap_resamples = parse_size(value, name);
        } else {
            throw std::runtime_error("unknown option: " + name);
        }
    }
    if (options.binary.empty() || options.target.empty() ||
        options.output.empty() || options.summary.empty()) {
        throw std::runtime_error(
            "--binary, --target, --output, and --summary are required");
    }
    if (options.target != program.target ||
        options.backend != program.backend || options.mode != program.mode) {
        throw std::runtime_error("benchmark options do not match the " +
                                 std::string(program.target) + " adapter");
    }
    return options;
}

void require_valid_program(const Options &options,
                           const hpc::bench::Program &program) {
    if (!options.verify) {
        throw std::runtime_error(std::string(program.target) +
                                 " timing requires output verification");
    }
    const auto result = spawn_capture(options.binary);
    if (result.first != 0) {
        throw std::runtime_error(std::string(program.target) +
                                 " check run failed");
    }
    if (result.second != program.expected_output) {
        throw std::runtime_error(std::string(program.target) +
                                 " output differs from the expected output");
    }
}

[[nodiscard]] Sample measure_sample(const Options &options,
                                    const std::size_t process_run,
                                    const std::size_t sample_number) {
    const std::uint64_t minimum_ns =
        static_cast<std::uint64_t>(options.minimum_case_ms) * 1'000'000ULL;
    const std::uint64_t start = now_ns();
    std::uint64_t operations = 0;
    std::uint64_t finish = start;
    do {
        if (spawn_silent(options.binary) != 0) {
            throw std::runtime_error("timed " + options.target +
                                     " execution failed");
        }
        ++operations;
        finish = now_ns();
    } while (finish - start < minimum_ns);
    return Sample{process_run, sample_number, finish - start, operations};
}

void write_raw(const Options &options, const std::vector<Sample> &samples,
               const hpc::bench::Program &program) {
    std::ofstream output(options.output);
    if (!output) {
        throw std::runtime_error("cannot open raw CSV: " + options.output);
    }
    output << raw_csv_header << '\n';
    const std::uint64_t checksum = fnv1a(program.expected_output);
    output << std::setprecision(17);
    for (const Sample &sample : samples) {
        const double ns_per_operation = static_cast<double>(sample.elapsed_ns) /
                                        static_cast<double>(sample.operations);
        const double operations_per_second =
            static_cast<double>(sample.operations) * 1'000'000'000.0 /
            static_cast<double>(sample.elapsed_ns);
        output << csv_escape(options.timestamp) << ','
               << csv_escape(options.git_commit) << ','
               << csv_escape(options.binary_sha256) << ',' << options.target
               << ',' << options.preset << ',' << csv_escape(options.compiler)
               << ',' << csv_escape(options.compiler_version) << ','
               << csv_escape(options.flags) << ',' << csv_escape(options.host)
               << ',' << csv_escape(options.cpu) << ','
               << csv_escape(options.gpu) << ',' << options.backend << ','
               << options.input_size << ',' << options.seed << ','
               << options.threads << ',' << options.ranks << ','
               << sample.process_run << ',' << sample.sample << ','
               << sample.elapsed_ns << ',' << sample.operations << ",0,"
               << ns_per_operation << ',' << operations_per_second << ','
               << checksum << ",true," << csv_escape(std::string(program.note))
               << '\n';
    }
}

void write_summary(const Options &options, const std::vector<Sample> &samples) {
    std::vector<double> values;
    values.reserve(samples.size());
    for (const Sample &sample : samples) {
        values.push_back(static_cast<double>(sample.elapsed_ns) /
                         static_cast<double>(sample.operations));
    }
    const Statistics statistics =
        calculate_statistics(values, options.bootstrap_resamples);
    std::ofstream output(options.summary);
    if (!output) {
        throw std::runtime_error("cannot open summary CSV: " + options.summary);
    }
    output << summary_csv_header << '\n';
    output << std::setprecision(17) << options.target << ',' << options.backend
           << ',' << options.input_size << ',' << values.size() << ','
           << statistics.minimum << ',' << statistics.median << ','
           << statistics.mean << ',' << statistics.maximum << ','
           << statistics.mad << ',' << statistics.standard_deviation << ','
           << statistics.p5 << ',' << statistics.p95 << ','
           << statistics.coefficient_of_variation << ',' << statistics.ci95_low
           << ',' << statistics.ci95_high << ",,,\n";
}

[[nodiscard]] int run_benchmark(const int argc, char *argv[],
                                const hpc::bench::Program &program) {
    const Options options = parse_options(argc, argv, program);
    require_valid_program(options, program);

    for (std::size_t warmup = 0; warmup < options.warmups; ++warmup) {
        static_cast<void>(measure_sample(options, 0U, warmup + 1U));
    }

    std::vector<Sample> samples;
    samples.reserve(options.samples * options.process_runs);
    for (std::size_t run = 1; run <= options.process_runs; ++run) {
        for (std::size_t sample = 1; sample <= options.samples; ++sample) {
            samples.push_back(measure_sample(options, run, sample));
        }
    }
    write_raw(options, samples, program);
    write_summary(options, samples);
    std::cout << "raw=" << options.output << '\n'
              << "summary=" << options.summary << '\n';
    return 0;
}

[[nodiscard]] int self_test() {
    const std::vector<double> values{1.0, 2.0, 3.0, 4.0, 5.0};
    const Statistics statistics = calculate_statistics(values, 1'000U);
    if (statistics.minimum != 1.0 || statistics.median != 3.0 ||
        statistics.mean != 3.0 || statistics.maximum != 5.0 ||
        statistics.mad != 1.0 || std::abs(statistics.p5 - 1.2) > 1.0e-12 ||
        std::abs(statistics.p95 - 4.8) > 1.0e-12) {
        throw std::runtime_error("statistics self-test failed");
    }
    const auto first = bootstrap_median_interval(values, 1'000U);
    const auto second = bootstrap_median_interval(values, 1'000U);
    if (first != second) {
        throw std::runtime_error("bootstrap determinism self-test failed");
    }
    const std::string escaped = csv_escape("a,\"b\"");
    const auto parsed = parse_csv_row(escaped + ",tail");
    if (parsed.size() != 2U || parsed[0] != "a,\"b\"" || parsed[1] != "tail") {
        throw std::runtime_error("CSV self-test failed");
    }
    if (parse_csv_row(std::string(raw_csv_header)).size() != 26U ||
        parse_csv_row(std::string(summary_csv_header)).size() != 18U) {
        throw std::runtime_error("CSV header self-test failed");
    }
    if (spawn_silent("/usr/bin/true") != 0) {
        throw std::runtime_error("process-launch self-test failed");
    }
    std::cout
        << "PASS: benchmark statistics, CSV, bootstrap, and process launch\n";
    return 0;
}

void validate_csv_file(const std::string &path,
                       const std::string_view expected_header,
                       const std::size_t expected_fields) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open CSV for validation: " + path);
    }
    std::string row;
    std::size_t rows = 0;
    while (std::getline(input, row)) {
        if (rows == 0U && row != expected_header) {
            throw std::runtime_error("unexpected CSV header in " + path);
        }
        if (parse_csv_row(row).size() != expected_fields) {
            throw std::runtime_error("unexpected CSV field count in " + path);
        }
        ++rows;
    }
    if (rows < 2U) {
        throw std::runtime_error("CSV contains no data rows: " + path);
    }
}

[[nodiscard]] int validate_csv(const char *raw, const char *summary) {
    validate_csv_file(raw, raw_csv_header, 26U);
    validate_csv_file(summary, summary_csv_header, 18U);
    std::cout << "PASS: raw and summary CSV structure\n";
    return 0;
}

} // namespace

int main(const int argc, char *argv[]) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
            return self_test();
        }
        if (argc == 4 && std::string_view(argv[1]) == "--validate-csv") {
            return validate_csv(argv[2], argv[3]);
        }
        return run_benchmark(argc, argv, hpc::bench::program());
    } catch (const std::exception &error) {
        std::cerr << "hpc_bench: " << error.what() << '\n';
        return 2;
    }
}
