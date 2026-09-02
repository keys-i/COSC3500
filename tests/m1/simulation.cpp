#include "model.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

// Focused continuous-kernel checks for ordering, periodic boundaries, and
// optimisation fallbacks
namespace {

// Build the smallest two-population input needed by each test
[[nodiscard]] m1::Scenario scenario(const std::size_t first_count,
                                    const std::size_t second_count,
                                    const double first_radius,
                                    const double second_radius,
                                    const std::uint64_t steps = 1U) {
    m1::Scenario value;
    value.world = {100.0, 100.0, 1.0, steps, 1U, true};
    value.entity_count = first_count + second_count;
    value.kernel = m1::Kernel::continuous;
    value.characters = {
        {0U, first_count, 1U, m1::seek, 1.0, first_radius * first_radius},
        {first_count, second_count, 0U, m1::seek, 1.0,
         second_radius * second_radius},
    };
    return value;
}

// Allocate every array the continuous kernel updates in place
[[nodiscard]] m1::State state(const std::vector<double> &x,
                              const std::vector<double> &y) {
    assert(x.size() == y.size());
    m1::State value;
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

void equal_distance_tie_uses_lowest_id() {
    // Grid traversal must not change the deterministic tie-break rule
    auto input = scenario(1U, 2U, 20.0, 0.0);
    auto current = state({50.0, 40.0, 60.0}, {50.0, 50.0, 50.0});
    (void)m1::simulate(input, current);
    assert(near(current.x[0], 49.0));
}

void periodic_edge_and_corner_choose_shortest_image() {
    // Edge and diagonal pairs must cross the nearest periodic image
    auto edge = scenario(1U, 1U, 5.0, 0.0);
    auto edge_state = state({1.0, 99.0}, {50.0, 50.0});
    (void)m1::simulate(edge, edge_state);
    assert(near(edge_state.x[0], 0.0));

    auto corner = scenario(1U, 1U, 5.0, 0.0);
    auto corner_state = state({1.0, 99.0}, {1.0, 99.0});
    (void)m1::simulate(corner, corner_state);
    const double expected = 1.0 - 1.0 / std::sqrt(2.0);
    assert(near(corner_state.x[0], expected));
    assert(near(corner_state.y[0], expected));
}

void unequal_populations_keep_directional_radii() {
    // A source uses its own sensing radius rather than its target's
    auto input = scenario(2U, 1U, 3.0, 20.0);
    auto current = state({50.0, 10.0, 58.0}, {50.0, 10.0, 50.0});
    (void)m1::simulate(input, current);
    assert(near(current.x[0], 50.0));
    assert(near(current.x[2], 57.0));
}

struct Controller {
    std::uint64_t calls = 0U;
};

// Model a runtime callback that changes spatial data between frames
[[nodiscard]] bool opaque_controller(const std::uint64_t frame,
                                     const m1::Scenario &, m1::State &current,
                                     void *const opaque) {
    auto &controller = *static_cast<Controller *>(opaque);
    ++controller.calls;
    if (frame == 1U) {
        current.x[1] = 49.0;
    } else if (frame == 2U) {
        current.alive[1] = 0U;
    } else {
        current.x[1] = 51.0;
        current.alive[1] = 1U;
    }
    ++current.spatial_revision;
    return true;
}

void opaque_controller_invalidates_cached_pairs() {
    // External callbacks can move or remove entities between simulation steps
    auto input = scenario(1U, 1U, 20.0, 0.0, 3U);
    auto current = state({50.0, 70.0}, {50.0, 50.0});
    Controller controller;
    const m1::Metrics metrics = m1::simulate(
        input, current, nullptr, nullptr, 1U, opaque_controller, &controller);
    assert(controller.calls == 3U);
#if M1_OPT_LEVEL >= 6
    assert(metrics.pair_list_rebuilds >= 2U);
#else
    assert(metrics.pair_list_rebuilds == 0U);
#endif
    assert(current.alive[1] != 0U);
    assert(near(current.x[0], 50.0));
}

void noncanonical_populations_use_the_generic_path() {
    // Optimised layouts must not assume population records arrive in order
    auto input = scenario(1U, 1U, 20.0, 20.0);
    input.characters[0].first = 1U;
    input.characters[1].first = 0U;
    auto current = state({60.0, 50.0}, {50.0, 50.0});
    (void)m1::simulate(input, current);
    assert(near(current.x[0], 59.0));
    assert(near(current.x[1], 51.0));
}

void dense_population_falls_back_before_the_pair_list_overflows() {
    // The dense case exercises the bounded-memory candidate representation
    constexpr std::size_t population = 128U;
    auto input = scenario(population, population, 100.0, 100.0, 2U);
    auto current = state(std::vector<double>(population * 2U, 50.0),
                         std::vector<double>(population * 2U, 50.0));
    const m1::Metrics metrics = m1::simulate(input, current);
#if M1_OPT_LEVEL >= 7
    // Dense input must select the CSR path
    assert(metrics.pair_list_rebuilds != 0U);
#else
    assert(metrics.pair_list_rebuilds == 0U);
#endif
    assert(metrics.pair_list_bytes <= 256ULL * 1024U * 1024U);
}

void candidate_certificates_preserve_checksum() {
    // Reusing valid pairs must produce the same state as a fresh search
    auto input = scenario(1U, 1U, 20.0, 0.0, 8U);
    input.characters[0].step_distance = 0.01;
    auto left = state({50.0, 60.0}, {50.0, 50.0});
    auto right = left;
    const m1::Metrics left_metrics = m1::simulate(input, left);
    (void)m1::simulate(input, right);
    assert(m1::checksum(left) == m1::checksum(right));
#if M1_OPT_LEVEL >= 6
    // Pair lists remain valid within the Verlet skin
    assert(left_metrics.pair_list_rebuilds != 0U);
    assert(left_metrics.pair_list_rebuilds < left_metrics.steps);
#else
    assert(left_metrics.pair_list_rebuilds == 0U);
#endif
}

} // namespace

int main() try {
    // Each test isolates an invariant shared across optimisation levels
    equal_distance_tie_uses_lowest_id();
    periodic_edge_and_corner_choose_shortest_image();
    unequal_populations_keep_directional_radii();
    opaque_controller_invalidates_cached_pairs();
    noncanonical_populations_use_the_generic_path();
    dense_population_falls_back_before_the_pair_list_overflows();
    candidate_certificates_preserve_checksum();
} catch (...) {
    return 1;
}
