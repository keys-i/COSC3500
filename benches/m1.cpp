#include "bench.hpp"

#include <array>

namespace hpc::bench {

const std::array<Case, 7U> &m1_cases() noexcept {
    static constexpr std::array<Case, 7U> values{{
        {
            "continuous",
            "entity_updates",
            "",
            256U,
            7U,
            {
                "m1",
                "serial-continuous",
                "end-to-end",
                "templates/predator-prey",
                "steps=20000 characters=2 initial=256 active=256 "
                "entity_updates=5120000 candidate_checks=44332462 "
                "sensed_interactions=15891770 births=0 deaths=0 "
                "cell_updates=0 turns=0 search_nodes=0 path_expansions=0 "
                "timeline_events=0 "
                "checksum=3f79b5ca16157376\n",
                "steady-state serial predator-prey throughput",
                5'120'000U,
            },
        },
        {
            "continuous-large",
            "entity_updates",
            "",
            100'000U,
            7U,
            {
                "m1",
                "serial-continuous",
                "end-to-end",
                "templates/predator-prey-large",
                "steps=10 characters=2 initial=100000 active=100000 "
                "entity_updates=1000000 candidate_checks=7371470 "
                "sensed_interactions=2572548 births=0 deaths=0 "
                "cell_updates=0 turns=0 search_nodes=0 path_expansions=0 "
                "timeline_events=0 checksum=6ad13acc54283f93\n",
                "100,000-agent constant-density continuous workload",
                1'000'000U,
            },
        },
        {
            "continuous-million",
            "entity_updates",
            "",
            1'000'000U,
            7U,
            {
                "m1",
                "serial-continuous",
                "end-to-end",
                "templates/predator-prey-million",
                "steps=10 characters=2 initial=1000000 active=1000000 "
                "entity_updates=10000000 candidate_checks=73716414 "
                "sensed_interactions=25717368 births=0 deaths=0 "
                "cell_updates=0 turns=0 search_nodes=0 path_expansions=0 "
                "timeline_events=0 checksum=a066c6cd48ee70b3\n",
                "million-agent constant-density continuous workload",
                10'000'000U,
            },
        },
        {
            "cellular",
            "cell_updates",
            "",
            25U,
            0U,
            {
                "m1",
                "serial-cellular",
                "end-to-end",
                "templates/conway",
                "steps=12 characters=0 initial=5 active=5 entity_updates=300 "
                "candidate_checks=2400 sensed_interactions=0 births=24 "
                "deaths=24 "
                "cell_updates=300 turns=0 search_nodes=0 path_expansions=0 "
                "timeline_events=0 checksum=52d10c7ec4cfd5bd\n",
                "cellular automaton; operations are cell updates",
                300U,
            },
        },
        {
            "turn-search",
            "path_expansions",
            "",
            12U,
            0U,
            {
                "m1",
                "serial-turn-search",
                "end-to-end",
                "templates/colour-number",
                "steps=6 characters=5 initial=5 active=5 entity_updates=4 "
                "candidate_checks=0 sensed_interactions=0 births=0 deaths=0 "
                "cell_updates=0 turns=6 search_nodes=0 path_expansions=6 "
                "timeline_events=0 checksum=e24c06cffae6aa01\n",
                "turn and Dijkstra scenario; operations are path expansions",
                6U,
            },
        },
        {
            "timeline",
            "timeline_events",
            "",
            8U,
            0U,
            {
                "m1",
                "serial-timeline",
                "end-to-end",
                "templates/niu-niu-lai",
                "steps=8 characters=3 initial=3 active=2 entity_updates=5 "
                "candidate_checks=0 sensed_interactions=0 births=0 deaths=1 "
                "cell_updates=0 turns=0 search_nodes=0 path_expansions=0 "
                "timeline_events=5 checksum=c0454854619de409\n",
                "scripted timeline; operations are processed events",
                5U,
            },
        },
        {
            "compositor-gate",
            "snapshot_frames",
            "--snapshots",
            9U,
            0U,
            {
                "m1",
                "serial-timeline-snapshot",
                "end-to-end",
                "templates/niu-niu-lai",
                "steps=8 characters=3 initial=3 active=2 entity_updates=5 "
                "candidate_checks=0 sensed_interactions=0 births=0 deaths=1 "
                "cell_updates=0 turns=0 search_nodes=0 path_expansions=0 "
                "timeline_events=5 checksum=c0454854619de409\n"
                "snapshots=results/snapshots/niu-niu-lai.csv\n"
                "cues=results/snapshots/niu-niu-lai.cues.csv\n",
                "snapshot serialisation gate; not a renderer or encoder result",
                9U,
            },
        },
    }};
    return values;
}

} // namespace hpc::bench
