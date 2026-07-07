#pragma once

#include <string_view>

namespace hpc::bench {

struct Program {
    std::string_view target;
    std::string_view backend;
    std::string_view mode;
    std::string_view expected_output;
    std::string_view note;
};

[[nodiscard]] const Program &program() noexcept;

} // namespace hpc::bench
