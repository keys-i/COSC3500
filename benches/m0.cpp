#include "bench.hpp"

namespace hpc::bench {

const Program &program() noexcept {
    static constexpr Program value{
        "m0",
        "process-smoke",
        "end-to-end",
        "My topic is a config-based, multi-behaviour predator–prey "
        "simulation, "
        "and it’s about "
        "using configurable behaviours with a custom rendering module\n",
        "diagnostic process-launch smoke; not an algorithm benchmark",
    };
    return value;
}

} // namespace hpc::bench
