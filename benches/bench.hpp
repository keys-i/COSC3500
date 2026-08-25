#ifndef COSC3500_BENCHES_BENCH_HPP
#define COSC3500_BENCHES_BENCH_HPP

#include <array>
#include <cstdint>
#include <string_view>

/// \file
/// Fixed benchmark cases and their expected deterministic output

namespace hpc::bench {

/// Expected deterministic output and the unit used to normalise elapsed time
struct Program {
    // Selector passed to m1 --benchmark
    std::string_view argument;
    // Exact output or required prefix before elapsed_ns
    std::string_view expected_output;
    // Completed work used to convert elapsed time into ns per unit
    std::uint64_t operations = 0;
    std::uint64_t state_bytes = 0;
    std::string_view unit;
    std::string_view checksum;
    std::string_view expected_prefix;
};

/// A label paired with the m1 invocation used for one benchmark row
struct Case {
    // Stable row name used by command-line selection and report scripts
    std::string_view name;
    Program program;
};

/// Return the fixed suite used by scripts and the benchmark report
[[nodiscard]] const std::array<Case, 11U> &m1_cases() noexcept;
/// Look up one program, or return null when the requested case is unsupported
[[nodiscard]] const Program *program(std::string_view target,
                                     std::string_view case_name = {}) noexcept;
#ifdef COSC3500_BENCH_EMBEDDED
/// Exercise deterministic benchmark helpers without spawning child processes
[[nodiscard]] bool self_test();
#endif

} // namespace hpc::bench

#endif
