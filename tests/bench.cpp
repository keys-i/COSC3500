#ifdef NDEBUG
#undef NDEBUG
#endif
#define COSC3500_BENCH_EMBEDDED
// NOLINTNEXTLINE(bugprone-suspicious-include)
#include "../benches/bench.cpp"

#include <cassert>

// Exercise benchmark helpers in-process so the test is fast and deterministic
int main() try {
    // The embedded runner exposes helpers without spawning a child process
    static_cast<void>(&benchmark);
    const std::vector<double> values{1.0, 2.0, 3.0, 4.0, 5.0};
    const Statistics result = statistics(values);
    const Statistics again = statistics(values);
    assert(result.median == 3.0);
    assert(std::abs(result.cv - 0.5270462766947299) < 1e-12);
    assert(result.ci_low == again.ci_low);
    assert(result.ci_high == again.ci_high);

    char executable[] = "hpc_bench";
    char command[] = "--case";
    char *invalid[] = {executable, command, nullptr};
    bool rejected = false;
    try {
        static_cast<void>(parse_options(2, invalid));
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    assert(rejected);
    assert(hpc::bench::program("m1", "nope") == nullptr);

    // A representative child report exercises the line-oriented parsers
    const PageReport report =
        pages("elapsed_ns=1\npair_evaluations=2 pair_list_rebuilds=3 "
              "pair_list_bytes=4\npage_policy=huge host_page_bytes=4096 "
              "advised_bytes=2097152 anon_huge_bytes=2097152 "
              "backing_verified=true\n");
    const ChildMetrics metrics = child_metrics(
        "state_bytes=1 pair_evaluations=2 pair_list_rebuilds=3 "
        "pair_list_bytes=4\n");
    assert(report.policy == "huge");
    assert(report.host_page_bytes == 4096U);
    assert(report.backing_verified);
    assert(metrics.state_bytes == 1U);
    assert(metrics.pair_evaluations == 2U);
    assert(metrics.pair_list_rebuilds == 3U);
    assert(metrics.pair_list_bytes == 4U);
    assert(field("checksum=abc\n", "checksum=") == "abc");
} catch (...) {
    return 1;
}
