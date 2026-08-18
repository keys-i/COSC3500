#ifndef COSC3500_BENCHES_BENCH_HPP
#define COSC3500_BENCHES_BENCH_HPP

#include <array>
#include <cstdint>
#include <string_view>

namespace hpc::bench {

struct Program {
    std::string_view target;
    std::string_view backend;
    std::string_view mode;
    std::string_view argument;
    std::string_view expected_output;
    std::string_view note;
    std::uint64_t operations_per_process = 0;
};

struct Case {
    std::string_view name;
    std::string_view work_unit;
    std::string_view option;
    std::uint64_t input_size = 0;
    std::uint64_t seed = 0;
    Program program;
};

[[nodiscard]] const std::array<Case, 7U> &m1_cases() noexcept;
[[nodiscard]] const Program *program(std::string_view target,
                                     std::string_view case_name = {}) noexcept;
[[nodiscard]] const Case *program_case(const Program &program) noexcept;

} // namespace hpc::bench

#endif
