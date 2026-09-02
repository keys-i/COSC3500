#include "bench.hpp"

#include <array>

/// \file
/// Define the m1 benchmark suite and expected output for each case

namespace hpc::bench {

const std::array<Case, 11U> &m1_cases() noexcept {
    // Keep scale cases here so the benchmark runner stays scenario-agnostic
    static constexpr std::array<Case, 11U> values{{
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
        {"cellular/conway",
         {"templates/conway",
          "steps=720 characters=12 initial=1761 active=1493 "
          "entity_updates=20160000 "
          "candidate_checks=159814080 sensed_interactions=0 captures=0 "
          "births=443439 "
          "deaths=447056 cell_updates=20160000 "
          "turns=0 "
          "timeline_events=0 run_seed=31 render_seed=15517599431202433770 "
          "result=1 state_bytes=56045 "
          "checksum=550ff6f8d497dcc7\n",
          20'160'000U, 56'045U, "cell_updates", "550ff6f8d497dcc7", ""}},
        {"pde/heat",
         {"test/pde-heat", "", 139'392U, 181U, "cell_updates",
          "222460008c024d44",
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
         {"templates/carrom",
          "steps=3405 characters=21 initial=21 active=4 entity_updates=356906 "
          "candidate_checks=0 sensed_interactions=0 captures=0 births=261 "
          "deaths=278 cell_updates=0 turns=0 "
          "timeline_events=356906 run_seed=31 render_seed=15517599431202433770 "
          "result=2 state_bytes=3062 "
          "checksum=8969cbd07edbc5ce\n",
          356'906U, 3'062U, "timeline_events", "8969cbd07edbc5ce", ""}},
    }};
    return values;
}

} // namespace hpc::bench
