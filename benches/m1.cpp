#include "bench.hpp"

#include <array>

/// \file
/// Define the m1 benchmark suite and expected output for each case

namespace hpc::bench {

const std::array<Case, 12U> &m1_cases() noexcept {
    // Keep scale cases here so the benchmark runner stays scenario-agnostic
    static constexpr std::array<Case, 12U> values{{
        {"cellular/conway/1k",
         {"templates/conway/1k", "", 10'000U, 2'045U, "cell_updates",
          "invariant", "steps=10 "}},
        {"cellular/conway/10k",
         {"templates/conway/10k", "", 100'000U, 20'045U, "cell_updates",
          "invariant", "steps=10 "}},
        {"cellular/conway/100k",
         {"templates/conway/100k", "", 1'000'000U, 200'045U, "cell_updates",
          "invariant", "steps=10 "}},
        {"cellular/conway/1m",
         {"templates/conway/1m", "", 10'000'000U, 2'000'045U, "cell_updates",
          "invariant", "steps=10 "}},
        {"cellular/conway/10m",
         {"templates/conway/10m", "", 100'000'000U, 20'000'045U, "cell_updates",
          "invariant", "steps=10 "}},
        {"cellular/conway/100m",
         {"templates/conway/100m", "", 100'000'000U, 200'000'045U,
          "cell_updates", "invariant", "steps=1 "}},
        {"cellular/conway/1b",
         {"templates/conway/1b", "", 1'000'000'000U, 2'000'000'045U,
          "cell_updates", "invariant", "steps=1 "}},
        {"continuous/predator-prey/100k",
         {"test/continuous", "", 1'000'000U, 6'600'012U, "entity_updates",
          "cf299cf42034d25c",
          "steps=10 characters=2 initial=100000 active=100000 "
          "entity_updates=1000000 "}},
        {"cellular/conway",
         {"templates/conway",
          "steps=4320 characters=12 initial=540 active=13747 "
          "entity_updates=120960000 "
          "candidate_checks=958884480 sensed_interactions=0 captures=0 "
          "births=0 "
          "deaths=0 cell_updates=120960000 "
          "turns=0 "
          "timeline_events=0 run_seed=31 render_seed=15517599431202433770 "
          "result=1 state_bytes=56012 "
          "checksum=6ce54b0339ca49ad\n",
          120'960'000U, 56'012U, "cell_updates", "6ce54b0339ca49ad", ""}},
        {"pde/heat",
         {"test/pde-heat", "", 139'392U, 181U, "cell_updates", "invariant",
          "steps=128 characters=0 initial=0 active=0 entity_updates=0 "
          "candidate_checks=0 sensed_interactions=0 captures=0 births=0 "
          "deaths=0 cell_updates=139392 "}},
        {"turn/chess",
         {"templates/chess",
          "steps=385 characters=13 initial=32 active=4 entity_updates=24640 "
          "candidate_checks=0 sensed_interactions=0 captures=0 births=0 "
          "deaths=0 cell_updates=0 turns=385 "
          "timeline_events=0 run_seed=31 render_seed=15517599431202433770 "
          "result=0 state_bytes=489 "
          "checksum=e80bb057e0ee7f1d\n",
          385U, 489U, "turns", "e80bb057e0ee7f1d", ""}},
        {"timeline/carrom",
         {"templates/carrom", "", 356'906U, 3'062U, "timeline_events",
          "8969cbd07edbc5ce",
          "steps=3405 characters=21 initial=21 active=4 entity_updates=356906 "
          "candidate_checks=0 sensed_interactions=0 captures=0 births=261 "
          "deaths=278 cell_updates=0 turns=0 "
          "timeline_events=356906 run_seed=31 render_seed=15517599431202433770 "
          "result=2 "}},
    }};
    return values;
}

} // namespace hpc::bench
