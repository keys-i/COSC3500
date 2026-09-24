#include "model.hpp"
#include "scene.hpp"
#include "simulation/runtime/lua.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#ifndef M2_OPENMP
#define M2_OPENMP 0
#endif

#if M2_OPENMP
#include <omp.h>
#endif

namespace {

[[nodiscard]] m2::Scenario continuous_scenario(const std::size_t first_count,
                                                const std::size_t second_count,
                                                const double first_radius,
                                                const double second_radius,
                                                const std::uint64_t steps = 1U) {
    m2::Scenario value;
    value.world = {100.0, 100.0, 1.0, steps, 1U, true};
    value.entity_count = first_count + second_count;
    value.kernel = m2::Kernel::continuous;
    value.characters = {
        {0U, first_count, 1U, m2::seek, 1.0, first_radius * first_radius},
        {first_count, second_count, 0U, m2::seek, 1.0,
         second_radius * second_radius},
    };
    return value;
}

[[nodiscard]] m2::State continuous_state(const std::vector<double> &x,
                                          const std::vector<double> &y) {
    assert(x.size() == y.size());
    m2::State value;
    value.x = x;
    value.y = y;
    value.next_x.resize(x.size());
    value.next_y.resize(x.size());
    value.velocity_x.assign(x.size(), 0.0);
    value.velocity_y.assign(x.size(), 0.0);
    value.next_velocity_x.resize(x.size());
    value.next_velocity_y.resize(x.size());
    value.alive.assign(x.size(), 1U);
    value.next_alive.resize(x.size());
    return value;
}

[[nodiscard]] bool near(const double actual, const double expected) {
    return std::abs(actual - expected) < 1e-12;
}

void deterministic_neighbour_search_handles_periodic_ties() {
    auto input = continuous_scenario(1U, 2U, 20.0, 0.0);
    auto current = continuous_state({50.0, 40.0, 60.0}, {50.0, 50.0, 50.0});
    (void)m2::simulate(input, current);
    assert(near(current.x[0], 49.0));

    auto edge = continuous_scenario(1U, 1U, 5.0, 0.0);
    auto edge_state = continuous_state({1.0, 99.0}, {50.0, 50.0});
    (void)m2::simulate(edge, edge_state);
    assert(near(edge_state.x[0], 0.0));
}

void bounded_candidate_storage_selects_a_safe_path() {
    constexpr std::size_t population = 128U;
    auto input = continuous_scenario(population, population, 100.0, 100.0, 2U);
    auto current = continuous_state(std::vector<double>(population * 2U, 50.0),
                                    std::vector<double>(population * 2U, 50.0));
    const m2::Metrics metrics = m2::simulate(input, current);
#if M2_OPT_LEVEL >= 7
    assert(metrics.pair_list_rebuilds != 0U);
#else
    assert(metrics.pair_list_rebuilds == 0U);
#endif
    assert(metrics.pair_list_bytes <= 256ULL * 1024U * 1024U);
}

#if M2_OPT_LEVEL >= 7
void csr_edge_scratch_preserves_results_and_falls_back_at_its_cap() {
    constexpr std::size_t small_population = 128U;
    const auto run = [](const std::size_t population) {
        const auto input =
            continuous_scenario(population, population, 100.0, 100.0, 2U);
        auto state = continuous_state(
            std::vector<double>(population * 2U, 50.0),
            std::vector<double>(population * 2U, 50.0));
        const std::uint64_t reference_checksum = m2::checksum(state);
        const m2::Metrics metrics = m2::simulate(input, state);
        return std::tuple{reference_checksum, m2::checksum(state), metrics};
    };
    const auto [small_reference, small_checksum, small_metrics] =
        run(small_population);
    const auto [repeat_reference, repeat_checksum, repeat_metrics] =
        run(small_population);
    assert(small_checksum == small_reference);
    assert(repeat_checksum == repeat_reference);
    assert(small_checksum == repeat_checksum);
    assert(small_metrics.pair_evaluations == repeat_metrics.pair_evaluations);
    assert(small_metrics.pair_list_bytes <= 256ULL * 1024U * 1024U);
    assert(small_metrics.pair_evaluations ==
           3U * small_population * small_population);

    constexpr std::size_t overflow_population = 1025U;
    const auto [overflow_reference, overflow_checksum, overflow_metrics] =
        run(overflow_population);
    assert(overflow_checksum == overflow_reference);
    assert(overflow_metrics.pair_list_bytes <= 256ULL * 1024U * 1024U);
    assert(overflow_metrics.pair_evaluations ==
           4U * overflow_population * overflow_population);
}
#endif

struct TimelineFrames {
    std::uint64_t count = 0U;
    double rotation_at_first = 0.0;
    double scale_at_first = 0.0;
    double opacity_at_first = 0.0;
};

void capture_timeline_frame(const std::uint64_t frame, const m2::Scenario &,
                            const m2::State &state, void *const opaque) {
    auto &frames = *static_cast<TimelineFrames *>(opaque);
    ++frames.count;
    if (frame == 1U) {
        frames.rotation_at_first = state.timeline_rotation[0];
        frames.scale_at_first = state.timeline_scale[0];
        frames.opacity_at_first = state.timeline_opacity[0];
    }
}

void timeline_keyframe_reaches_the_snapshot_contract() {
    m2::Scenario input;
    input.world = {100.0, 100.0, 1.0, 2U, 1U, false};
    input.entity_count = 1U;
    input.kernel = m2::Kernel::timeline;
    input.names = {"marker"};
    input.characters = {{0U, 1U}};
    input.styles = {{m2::Shape::circle, "#ffffff"}};
    m2::State current = m2::initialise(input);
    current.timeline_rotation = {0.0};
    current.timeline_scale = {1.0};
    current.timeline_opacity = {1.0};
    current.timeline_start_rotation = {0.0};
    current.timeline_start_scale = {1.0};
    current.timeline_start_opacity = {1.0};
    current.timeline_target_rotation = {180.0};
    current.timeline_target_scale = {2.0};
    current.timeline_target_opacity = {0.0};
    current.timeline_transform_start_step = {0U};
    current.timeline_transform_end_step = {2U};
    TimelineFrames frames;
    const m2::Metrics metrics = m2::simulate(input, current,
                                              capture_timeline_frame, &frames);
    assert(metrics.steps == 2U);
    assert(metrics.entity_updates == 2U);
    assert(frames.count == 2U);
    assert(near(frames.rotation_at_first, 90.0));
    assert(near(frames.scale_at_first, 1.5));
    assert(near(frames.opacity_at_first, 0.5));
    assert(near(current.timeline_rotation[0], 180.0));
    assert(near(current.timeline_scale[0], 2.0));
    assert(near(current.timeline_opacity[0], 0.0));
    std::ostringstream snapshot;
    m2::write_snapshot(2U, input, current, &snapshot);
    assert(snapshot.str().find(",180,2,0,") != std::string::npos);
}

[[nodiscard]] m2::Scenario timeline_lua_scenario(const std::string &directory) {
    m2::Scenario value;
    value.world = {10.0, 10.0, 1.0, 3U, 31U, false};
    value.entity_count = 1U;
    value.kernel = m2::Kernel::timeline;
    value.names = {"marker"};
    value.characters = {{0U, 1U}};
    value.styles = {{m2::Shape::circle, "#ffffff"}};
    value.lua_rules = "rules.lua";
    value.source_directory = directory;
    value.lua_directory = directory;
    return value;
}

void timeline_lua_rejects_invalid_depth() {
    for (const std::string &directory : {
             "tests/scenarios/fixtures/timeline-negative-z",
             "tests/scenarios/fixtures/timeline-negative-arc",
             "tests/scenarios/fixtures/timeline-nonfinite-z",
         }) {
        m2::Scenario input = timeline_lua_scenario(directory);
        m2::State current = m2::initialise(input);
        m2::ScenarioRuntime runtime;
        std::string error;
        assert(m2::prepare_scenario_program(input, current, runtime, error));
        assert(!m2::setup_scenario_program(input, current, runtime, error));
        assert(!error.empty());
        m2::destroy_scenario_program(runtime);
    }
}

void timeline_lua_depth_reaches_the_snapshot_contract() {
    m2::Scenario input =
        timeline_lua_scenario("tests/scenarios/fixtures/timeline-depth");
    m2::State current = m2::initialise(input);
    m2::ScenarioRuntime runtime;
    std::string error;
    assert(m2::prepare_scenario_program(input, current, runtime, error));
    assert(m2::setup_scenario_program(input, current, runtime, error));
    const m2::Metrics metrics = m2::simulate(input, current, nullptr, nullptr,
                                              1U, nullptr, nullptr, &runtime);
    m2::destroy_scenario_program(runtime);
    assert(metrics.steps == 3U);
    assert(near(current.x[0], 9.0));
    assert(near(current.y[0], 8.0));
    assert(near(current.timeline_z[0], 3.0));
    std::ostringstream snapshot;
    m2::write_snapshot(3U, input, current, &snapshot);
    assert(snapshot.str().find(",3,0,") != std::string::npos);
}

#if M2_OPENMP && M2_OPT_LEVEL >= 7
[[nodiscard]] bool same_metrics(const m2::Metrics &left,
                                const m2::Metrics &right) {
    return left.steps == right.steps &&
           left.entity_updates == right.entity_updates &&
           left.candidate_checks == right.candidate_checks &&
           left.pair_evaluations == right.pair_evaluations &&
           left.pair_list_rebuilds == right.pair_list_rebuilds &&
           left.pair_list_bytes == right.pair_list_bytes &&
           left.sensed_interactions == right.sensed_interactions &&
           left.captures == right.captures && left.births == right.births &&
           left.deaths == right.deaths;
}

void openmp_csr_matches_one_thread_exactly() {
    constexpr std::size_t population = 64U;
    auto input = continuous_scenario(population, population, 10.0, 10.0, 6U);
    input.characters[0].step_distance = 0.01;
    input.characters[1].step_distance = 0.01;
    const auto make_state = [] {
        return continuous_state(
            std::vector<double>(128U, 1.0), std::vector<double>(128U, 50.0));
    };
    auto make_edge_state = [&] {
        auto value = make_state();
        for (std::size_t index = population; index < population * 2U; ++index)
            value.x[index] = 99.0;
        return value;
    };
    omp_set_dynamic(0);
    omp_set_num_threads(1);
    auto serial = make_edge_state();
    const m2::Metrics serial_metrics = m2::simulate(input, serial);
    const std::uint64_t serial_checksum = m2::checksum(serial);
    for (const int threads : {2, 4}) {
        omp_set_num_threads(threads);
        auto parallel = make_edge_state();
        const m2::Metrics parallel_metrics = m2::simulate(input, parallel);
        assert(m2::checksum(parallel) == serial_checksum);
        assert(same_metrics(parallel_metrics, serial_metrics));
    }
}
#endif

} // namespace

int main() try {
    deterministic_neighbour_search_handles_periodic_ties();
    bounded_candidate_storage_selects_a_safe_path();
#if M2_OPT_LEVEL >= 7
    csr_edge_scratch_preserves_results_and_falls_back_at_its_cap();
#endif
    timeline_keyframe_reaches_the_snapshot_contract();
    timeline_lua_rejects_invalid_depth();
    timeline_lua_depth_reaches_the_snapshot_contract();
#if M2_OPENMP && M2_OPT_LEVEL >= 7
    openmp_csr_matches_one_thread_exactly();
#endif
} catch (...) {
    return 1;
}
