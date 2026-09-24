#include "bench.hpp"

#include <array>

/// \file
/// Define the narrow M2 benchmark suite independently from the M1 ladder

namespace hpc::bench {

const std::array<Case, 4U> &m2_cases() noexcept {
    static constexpr std::array<Case, 4U> values{{
        {"continuous/predator-prey/100k",
         {"test/continuous", "", 1'000'000U, 6'600'012U, "entity_updates",
          "invariant",
          "steps=10 characters=2 initial=100000 active=100000 "
          "entity_updates=1000000 "}},
        {"continuous/predator-prey/200k",
         {"test/continuous-200k", "", 2'000'000U, 13'200'012U,
          "entity_updates", "invariant",
          "steps=10 characters=2 initial=200000 active=200000 "
          "entity_updates=2000000 "}},
        {"continuous/predator-prey/400k",
         {"test/continuous-400k", "", 4'000'000U, 26'400'012U,
          "entity_updates", "invariant",
          "steps=10 characters=2 initial=400000 active=400000 "
          "entity_updates=4000000 "}},
        {"continuous/predator-prey/800k",
         {"test/continuous-800k", "", 8'000'000U, 52'800'012U,
          "entity_updates", "invariant",
          "steps=10 characters=2 initial=800000 active=800000 "
          "entity_updates=8000000 "}},
    }};
    return values;
}

} // namespace hpc::bench
