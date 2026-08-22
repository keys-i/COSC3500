#include "../grid.hpp"
#include "../internal.hpp"

/// \file
/// Advance continuous state through spatial search, buffered motion, and commit
/// The reciprocal path reuses neighbour lists; the general path evaluates all
/// configured behaviours and supports bounded worlds

namespace m1 {

State initialise(const Scenario &scenario) {
    State state;
    if (scenario.kernel == Kernel::pde) {
        return state;
    }
    if (scenario.kernel == Kernel::cellular) {
        state.cells = scenario.cellular.initial;
        if (state.cells.empty()) {
            state.cells.resize(scenario.cellular.columns *
                               scenario.cellular.rows);
        }
        state.next_cells.resize(state.cells.size());
        return state;
    }
    if (scenario.kernel == Kernel::turn) {
        state.board.resize(scenario.turn.columns * scenario.turn.rows);
        state.x.resize(scenario.entity_count);
        state.y.resize(scenario.entity_count);
        state.alive.assign(scenario.entity_count, 1U);
        for (const CharacterPlan &plan : scenario.characters) {
            if (plan.positioned) {
                state.x[plan.first] = plan.initial_x;
                state.y[plan.first] = plan.initial_y;
            }
            if (!plan.initial_alive) {
                std::fill_n(state.alive.begin() +
                                static_cast<std::ptrdiff_t>(plan.first),
                            plan.count, 0U);
            }
        }
        return state;
    }
    state.x.resize(scenario.entity_count);
    state.y.resize(scenario.entity_count);
    state.alive.assign(scenario.entity_count, 1U);
    if (scenario.kernel == Kernel::timeline) {
        // Timeline state is separate because it does not take spatial scans
        state.velocity_x.assign(scenario.entity_count, 0.0);
        state.velocity_y.assign(scenario.entity_count, 0.0);
        state.timeline_z.assign(scenario.entity_count, 0.0);
        state.timeline_state.assign(scenario.entity_count, 0U);
        state.timeline_text.assign(scenario.entity_count, {});
        state.timeline_start_x.resize(scenario.entity_count);
        state.timeline_start_y.resize(scenario.entity_count);
        state.timeline_start_z.resize(scenario.entity_count);
        state.timeline_target_x.resize(scenario.entity_count);
        state.timeline_target_y.resize(scenario.entity_count);
        state.timeline_target_z.resize(scenario.entity_count);
        state.timeline_arc_height.resize(scenario.entity_count);
        state.timeline_start_step.resize(scenario.entity_count);
        state.timeline_end_step.resize(scenario.entity_count);
        for (std::size_t type = 0U;
             type < scenario.characters.size() && type < scenario.styles.size();
             ++type) {
            const CharacterPlan &plan = scenario.characters[type];
            for (std::size_t entity = plan.first;
                 entity < plan.first + plan.count; ++entity) {
                if (entity < state.timeline_text.size()) {
                    state.timeline_text[entity] = scenario.styles[type].glyph;
                }
            }
        }
    }
    if (scenario.kernel == Kernel::continuous) {
        // Paired buffers prevent an entity from observing this frame's writes
        state.next_x.resize(scenario.entity_count);
        state.next_y.resize(scenario.entity_count);
        state.velocity_x.assign(scenario.entity_count, 0.0);
        state.velocity_y.assign(scenario.entity_count, 0.0);
        state.next_velocity_x.resize(scenario.entity_count);
        state.next_velocity_y.resize(scenario.entity_count);
        state.next_alive.resize(scenario.entity_count);
        std::mt19937_64 generator{scenario.world.seed};
        for (std::size_t index = 0; index < scenario.entity_count; ++index) {
            state.x[index] = random_unit(generator) * scenario.world.width;
            state.y[index] = random_unit(generator) * scenario.world.height;
        }
        constexpr double tau = 6.28318530717958647692;
        for (const CharacterPlan &plan : scenario.characters) {
            // Only velocity-based rules need an initial heading
            bool moving = false;
            const std::size_t end = plan.first_behaviour + plan.behaviour_count;
            for (std::size_t index = plan.first_behaviour; index < end;
                 ++index) {
                const BehaviourCode code = scenario.behaviour_plan[index].code;
                moving = moving || code == BehaviourCode::align ||
                         code == BehaviourCode::cohere ||
                         code == BehaviourCode::wander;
            }
            if (!moving || plan.step_distance == 0.0) {
                continue;
            }
            for (std::size_t entity = plan.first;
                 entity < plan.first + plan.count; ++entity) {
                const double angle = random_unit(generator) * tau;
                state.velocity_x[entity] = std::cos(angle) * plan.step_distance;
                state.velocity_y[entity] = std::sin(angle) * plan.step_distance;
            }
        }
    }
    for (const CharacterPlan &plan : scenario.characters) {
        if (plan.positioned) {
            state.x[plan.first] = plan.initial_x;
            state.y[plan.first] = plan.initial_y;
        }
        const std::size_t initial =
            plan.initial_count == 0U ? plan.count : plan.initial_count;
        if (!plan.initial_alive) {
            std::fill_n(state.alive.begin() +
                            static_cast<std::ptrdiff_t>(plan.first),
                        plan.count, 0U);
        } else if (initial < plan.count) {
            std::fill(state.alive.begin() +
                          static_cast<std::ptrdiff_t>(plan.first + initial),
                      state.alive.begin() +
                          static_cast<std::ptrdiff_t>(plan.first + plan.count),
                      0U);
        }
    }
    return state;
}

template <class GridIndex>
[[nodiscard]] static Metrics simulate_continuous_impl(
    const Scenario &scenario, State &state, const SnapshotObserver observer,
    void *const context, const std::uint64_t snapshot_stride,
    const StepController controller, void *const controller_context,
    const std::uint64_t first_step, const std::uint64_t step_count) {
    // Fast reciprocal path for wrapped worlds with simple interaction rules
    // Per-behaviour rules and bounded worlds use the general kernel below
    if (!scenario.world.wraps || needs_extended_continuous(scenario)) {
        return simulate_extended_continuous(
            scenario, state, observer, context, snapshot_stride, controller,
            controller_context, first_step, step_count);
    }
    Metrics metrics;
    const std::size_t count = scenario.entity_count;
    if (state.x.size() != count || state.y.size() != count ||
        state.next_x.size() != count || state.next_y.size() != count ||
        state.velocity_x.size() != count || state.velocity_y.size() != count ||
        state.next_velocity_x.size() != count ||
        state.next_velocity_y.size() != count || state.alive.size() != count ||
        state.next_alive.size() != count) {
        return metrics;
    }
    for (std::size_t entity = 0; entity < count; ++entity) {
        if (!valid_coordinates(state.x[entity], state.y[entity],
                               scenario.world)) {
            return metrics;
        }
    }
    // Describe the simple interactions that can share a grid traversal
    struct Scan {
        const CharacterPlan *plan;
        std::size_t target_first;
        std::size_t target_end;
        bool seeking;
        bool fleeing;
        bool consuming;
    };
    // Each scan maps one contiguous source range to one target range
    // The fast path only needs reciprocal nearest-neighbour scans
    std::vector<Scan> scans;
    scans.reserve(scenario.characters.size());
    for (const CharacterPlan &plan : scenario.characters) {
        const std::size_t target_index = simple_target(plan, scenario);
        if (target_index >= scenario.characters.size()) {
            continue;
        }
        const bool seeking = has_behaviour(plan, seek);
        const bool fleeing = has_behaviour(plan, flee);
        const bool consuming = has_behaviour(plan, consume);
        if ((!seeking && !fleeing && !consuming) || (seeking && fleeing)) {
            continue;
        }
        const CharacterPlan &target = scenario.characters[target_index];
        scans.push_back({&plan, target.first, target.first + target.count,
                         seeking, fleeing, consuming});
    }

#if M1_OPT_LEVEL >= 5
    // Pair mode requires two contiguous populations targeting each other
    // This layout permits one traversal to update both populations
    const bool pair_kernel = has_two_contiguous_populations(scenario) &&
                             scans.size() == 2U &&
                             scans[0].plan == &scenario.characters[0] &&
                             scans[1].plan == &scenario.characters[1] &&
                             scans[0].target_first == scans[1].plan->first &&
                             scans[1].target_first == scans[0].plan->first &&
                             count <= 0xffff'ffffULL;
    bool fixed_pair_membership =
        pair_kernel && !scans[0].consuming && !scans[1].consuming &&
        std::all_of(state.alive.begin(), state.alive.end(),
                    [](const std::uint8_t value) { return value != 0U; });
    bool alive_buffers_equal = false;
#endif
#if M1_OPT_LEVEL >= 6
    // Grid cells cover the farthest relevant distance for this route
    const double cutoff = maximum_cutoff(scenario);
    const double maximum_step = pair_kernel
                                    ? std::max(scans[0].plan->step_distance,
                                               scans[1].plan->step_distance)
                                    : 0.0;
    const double skin = std::min(
        cutoff * 0.1, maximum_step * 2.0 * static_cast<double>(step_count));
    const bool use_verlet = pair_kernel && step_count > 1U && cutoff > 0.0 &&
                            (M1_WIDE_GRID != 0 || count <= 20'000'000U);
    // The Verlet grid includes the skin so cached pairs remain candidates
    BasicGrid<GridIndex> grid =
        make_grid<GridIndex>(scenario, use_verlet ? cutoff + skin : cutoff);
#else
    BasicGrid<GridIndex> grid = make_grid<GridIndex>(scenario);
#endif
#if M1_OPT_LEVEL >= 5
    // Nearest targets are retained until next-state publication
    const GridIndex missing = static_cast<GridIndex>(count);
    std::vector<GridIndex> nearest(count, missing);
    std::vector<double> nearest_squared(count);
#if M1_OPT_LEVEL < 7
    std::vector<double> nearest_x(count);
    std::vector<double> nearest_y(count);
#endif
#endif
#if M1_OPT_LEVEL >= 6
    // Pair-cache setup keeps optional storage bounded before allocation
    // Bound optional neighbour storage independently of population size
    constexpr std::size_t candidate_storage_cap =
        std::size_t{256U} * 1024U * 1024U;
    std::vector<std::uint64_t> pairs;
    // Origins measure the largest displacement since the list was built
    const bool displacement_available =
        use_verlet && count <= candidate_storage_cap / (2U * sizeof(double));
    const std::size_t displacement_bytes =
        displacement_available ? count * 2U * sizeof(double) : 0U;
    std::vector<double> pair_origin_x;
    std::vector<double> pair_origin_y;
    if (displacement_available) {
        pair_origin_x.resize(count);
        pair_origin_y.resize(count);
    }
#if M1_OPT_LEVEL >= 7
    // CSR keeps each entity's candidate range contiguous during publication
    const std::size_t csr_fixed_per_entity =
        2U * sizeof(GridIndex) + 3U * sizeof(double);
    const bool csr_count_fits =
        count <= (std::numeric_limits<std::size_t>::max() - sizeof(GridIndex)) /
                     csr_fixed_per_entity;
    const std::size_t csr_fixed_bytes =
        csr_count_fits ? sizeof(GridIndex) + count * csr_fixed_per_entity : 0U;
    const bool csr_available =
        displacement_available && csr_count_fits &&
        displacement_bytes <= candidate_storage_cap &&
        csr_fixed_bytes <= candidate_storage_cap - displacement_bytes;
    std::vector<GridIndex> adjacency;
    std::vector<GridIndex> adjacency_offsets;
    std::vector<GridIndex> adjacency_cursors;
    std::vector<double> first_outer;
    std::vector<double> second_outer;
    std::vector<double> certified_travel;
    if (csr_available) {
        adjacency_offsets.resize(count + 1U);
        adjacency_cursors.resize(count);
        first_outer.resize(count);
        second_outer.resize(count);
        certified_travel.resize(count);
    }
    bool csr_active = csr_available;
#endif
    const std::size_t pair_budget =
        displacement_bytes <= candidate_storage_cap
#if M1_OPT_LEVEL >= 7
            ? candidate_storage_cap - displacement_bytes -
                  (csr_available ? csr_fixed_bytes : 0U)
#else
            ? candidate_storage_cap - displacement_bytes
#endif
            : 0U;
    const std::size_t pair_capacity = pair_budget / sizeof(std::uint64_t);
    const std::size_t wanted_pairs =
        count <= std::numeric_limits<std::size_t>::max() / 16U
            ? count * 16U
            : std::numeric_limits<std::size_t>::max();
    const std::size_t pair_limit = std::min(
        {wanted_pairs, pair_capacity,
         static_cast<std::size_t>(std::numeric_limits<GridIndex>::max() / 2U)});
    std::size_t active_pair_limit = pair_limit;
    if (use_verlet && pair_limit != 0U
#if M1_OPT_LEVEL >= 7
        && !csr_available
#endif
    ) {
        pairs.reserve(pair_limit);
    }
    double travelled = 0.0;
    std::uint64_t pair_revision = state.spatial_revision;
    bool rebuild_pairs = true;
    // A cached list is a superset of interactions within the cutoff
    bool pair_list_available = use_verlet && displacement_available &&
#if M1_OPT_LEVEL >= 7
                               (csr_available || pair_limit != 0U);
#else
                               pair_limit != 0U;
#endif
#endif
    [[maybe_unused]] double next_travelled_squared = 0.0;

    // Discover interactions before publishing the next state
    const auto publish = [&](const Scan &scan, const std::size_t entity,
                             const std::size_t target,
                             const double distance_squared,
                             const double delta_x, const double delta_y) {
        // Read only the current state so every entity sees the same frame
        const CharacterPlan &plan = *scan.plan;
        const double source_x = state.x[entity];
        const double source_y = state.y[entity];
        double velocity_x = state.velocity_x[entity];
        double velocity_y = state.velocity_y[entity];
        double next_x = source_x;
        double next_y = source_y;
        if (target != count) {
            if (scan.consuming &&
                distance_squared <= plan.capture_radius_squared &&
                state.next_alive[target] != 0U) {
                state.next_alive[target] = 0U;
                ++metrics.captures;
            }
            if (distance_squared != 0.0 && (scan.seeking || scan.fleeing)) {
                const double direction = scan.seeking ? 1.0 : -1.0;
                if (plan.behaviour_count == 0U) {
                    const double scale = std::min(
                        plan.step_distance / std::sqrt(distance_squared), 1.0);
                    velocity_x = direction * delta_x * scale;
                    velocity_y = direction * delta_y * scale;
                } else {
                    const double inverse_distance =
                        1.0 / std::sqrt(distance_squared);
                    velocity_x += direction * delta_x * inverse_distance;
                    velocity_y += direction * delta_y * inverse_distance;
                }
            }
        }
        const double magnitude_squared =
            velocity_x * velocity_x + velocity_y * velocity_y;
        const bool advances = plan.behaviour_count != 0U
                                  ? magnitude_squared != 0.0
                                  : target != count &&
                                        distance_squared != 0.0 &&
                                        (scan.seeking || scan.fleeing);
        if (advances) {
            if (plan.behaviour_count != 0U) {
                const double scale = std::min(
                    plan.step_distance / std::sqrt(magnitude_squared), 1.0);
                velocity_x *= scale;
                velocity_y *= scale;
            }
            next_x = add_wrapped(source_x, velocity_x, scenario.world.width);
            next_y = add_wrapped(source_y, velocity_y, scenario.world.height);
        }
        state.next_x[entity] = next_x;
        state.next_y[entity] = next_y;
        state.next_velocity_x[entity] = velocity_x;
        state.next_velocity_y[entity] = velocity_y;
#if M1_OPT_LEVEL >= 6
        if (pair_list_available && !rebuild_pairs) {
            const double displacement_x = wrapped_delta(
                next_x - pair_origin_x[entity], scenario.world.width);
            const double displacement_y = wrapped_delta(
                next_y - pair_origin_y[entity], scenario.world.height);
            next_travelled_squared = std::max(
                next_travelled_squared, displacement_x * displacement_x +
                                            displacement_y * displacement_y);
        }
#endif
    };

    // Fast-path frame phases: prepare, discover interactions, publish, commit
    for (std::uint64_t step = 0; step < step_count; ++step) {
        // One frame is prepared in next_* and committed after all scans finish
        next_travelled_squared = 0.0;
        const std::uint64_t frame = first_step + step + 1U;
        if (controller != nullptr &&
            !controller(frame, scenario, state, controller_context)) {
            return metrics;
        }
#if M1_OPT_LEVEL >= 5
        fixed_pair_membership = fixed_pair_membership &&
#if M1_OPT_LEVEL >= 6
                                state.spatial_revision == pair_revision;
#else
                                controller == nullptr;
#endif
#endif
#if M1_OPT_LEVEL < 2
        grid = make_grid<GridIndex>(scenario);
#endif
#if M1_OPT_LEVEL >= 7
        bool pair_published = false;
#endif
#if M1_OPT_LEVEL >= 5
        if (pair_kernel) {
            // Fixed membership avoids copying unchanged liveness each frame
            if (!fixed_pair_membership || !alive_buffers_equal) {
                std::copy(state.alive.begin(), state.alive.end(),
                          state.next_alive.begin());
                alive_buffers_equal = fixed_pair_membership;
            }
            if (!fixed_pair_membership) {
                for (std::size_t entity = 0; entity < count; ++entity) {
                    if (state.alive[entity] != 0U) {
                        continue;
                    }
                    state.next_x[entity] = state.x[entity];
                    state.next_y[entity] = state.y[entity];
                    state.next_velocity_x[entity] = state.velocity_x[entity];
                    state.next_velocity_y[entity] = state.velocity_y[entity];
                }
            }
        } else {
            std::copy(state.alive.begin(), state.alive.end(),
                      state.next_alive.begin());
            alive_buffers_equal = false;
            std::copy(state.x.begin(), state.x.end(), state.next_x.begin());
            std::copy(state.y.begin(), state.y.end(), state.next_y.begin());
            std::copy(state.velocity_x.begin(), state.velocity_x.end(),
                      state.next_velocity_x.begin());
            std::copy(state.velocity_y.begin(), state.velocity_y.end(),
                      state.next_velocity_y.begin());
        }
#else
        std::copy(state.x.begin(), state.x.end(), state.next_x.begin());
        std::copy(state.y.begin(), state.y.end(), state.next_y.begin());
        std::copy(state.velocity_x.begin(), state.velocity_x.end(),
                  state.next_velocity_x.begin());
        std::copy(state.velocity_y.begin(), state.velocity_y.end(),
                  state.next_velocity_y.begin());
        std::copy(state.alive.begin(), state.alive.end(),
                  state.next_alive.begin());
#endif

#if M1_OPT_LEVEL >= 5
        if (pair_kernel) {
#if M1_OPT_LEVEL >= 6
            // Custom controllers may move entities without bumping the revision
            const bool controller_is_opaque =
                controller != nullptr && controller != run_compiled_program;
            const bool topology_invalid =
                controller_is_opaque || state.spatial_revision != pair_revision;
            rebuild_pairs =
                rebuild_pairs || topology_invalid || travelled >= skin * 0.5;
#endif
            const bool reset_nearest =
#if M1_OPT_LEVEL >= 7
                !use_verlet || rebuild_pairs;
#else
                true;
#endif
            if (reset_nearest) {
                // Rebuilds reset nearest candidates to each source range cutoff
                std::fill(nearest.begin(), nearest.end(), missing);
                for (const Scan &scan : scans) {
                    std::fill(nearest_squared.begin() +
                                  static_cast<std::ptrdiff_t>(scan.plan->first),
                              nearest_squared.begin() +
                                  static_cast<std::ptrdiff_t>(scan.plan->first +
                                                              scan.plan->count),
                              scan.plan->sensing_radius_squared);
                }
            }
            const auto consider =
                [&](const Scan &scan, const std::size_t source,
                    const std::size_t candidate, const double delta_x,
                    const double delta_y, const double distance_squared) {
#if M1_OPT_LEVEL >= 7
                    (void)delta_x;
                    (void)delta_y;
#endif
                    if (distance_squared > scan.plan->sensing_radius_squared) {
                        return;
                    }
                    if (distance_squared == 0.0 &&
                        scan.plan->behaviour_count != 0U) {
                        return;
                    }
                    ++metrics.sensed_interactions;
                    // Entity order resolves equal-distance candidates
                    // consistently
                    if (distance_squared < nearest_squared[source] ||
                        (distance_squared == nearest_squared[source] &&
                         candidate < nearest[source])) {
                        nearest[source] = static_cast<GridIndex>(candidate);
                        nearest_squared[source] = distance_squared;
#if M1_OPT_LEVEL < 7
                        nearest_x[source] = delta_x;
                        nearest_y[source] = delta_y;
#endif
                    }
                };
            const auto traverse_cells = [&](const auto &visit) {
                // Type-split cell ranges yield each cross-population pair once
                const auto visit_cell = [&](const std::size_t cell) {
                    std::array<GridIndex, 9U> nearby{};
                    const std::size_t nearby_count = load_neighbours(
                        grid, cell, scenario.world.wraps, nearby);
                    for (std::size_t a_member = grid.offsets[cell];
                         a_member < grid.type_splits[cell]; ++a_member) {
                        const std::size_t a = grid.members[a_member];
                        for (std::size_t neighbour = 0;
                             neighbour < nearby_count; ++neighbour) {
                            const std::size_t other = nearby[neighbour];
                            const std::uint8_t image = periodic_image(
                                grid, cell, other, scenario.world.wraps);
                            // Image encodes the boundary crossing
                            // for this cell pair
                            for (std::size_t b_member = grid.type_splits[other];
                                 b_member < grid.offsets[other + 1U];
                                 ++b_member) {
                                visit(a,
                                      static_cast<std::size_t>(
                                          grid.members[b_member]),
                                      cell, other, image);
                            }
                        }
                    }
                };
#if M1_OPT_LEVEL >= 5
                for (const GridIndex cell : grid.occupied_cells) {
                    visit_cell(cell);
                }
#else
                for (std::size_t cell = 0; cell < grid.counts.size(); ++cell) {
                    visit_cell(cell);
                }
#endif
            };
#if M1_OPT_LEVEL >= 6
            if (pair_list_available && rebuild_pairs) {
                // Cache stores a cutoff-plus-skin interaction superset
                build_grid(grid, state);
                pairs.clear();
                bool list_full = false;
#if M1_OPT_LEVEL >= 7
                std::fill(first_outer.begin(), first_outer.end(),
                          std::numeric_limits<double>::infinity());
                std::fill(second_outer.begin(), second_outer.end(),
                          std::numeric_limits<double>::infinity());
#endif
                const double outer_squared = (cutoff + skin) * (cutoff + skin);
#if M1_OPT_LEVEL >= 7
                if (csr_active) {
                    // Count both adjacency degrees while
                    // selecting initial nearests
                    std::fill(adjacency_offsets.begin(),
                              adjacency_offsets.end(), 0U);
                    const std::size_t adjacency_capacity =
                        (candidate_storage_cap - displacement_bytes -
                         csr_fixed_bytes) /
                        sizeof(GridIndex);
                    std::size_t adjacency_count = 0U;
                    const auto observe = [&](const std::size_t entity,
                                             const double distance_squared) {
                        if (distance_squared < first_outer[entity]) {
                            second_outer[entity] = first_outer[entity];
                            first_outer[entity] = distance_squared;
                        } else if (distance_squared < second_outer[entity]) {
                            second_outer[entity] = distance_squared;
                        }
                    };
                    traverse_cells([&](const std::size_t a, const std::size_t b,
                                       const std::size_t, const std::size_t,
                                       const std::uint8_t image) {
                        double delta_x = state.x[b] - state.x[a];
                        double delta_y = state.y[b] - state.y[a];
                        apply_periodic_image(
                            image, scenario.world.wraps, scenario.world.width,
                            scenario.world.height, delta_x, delta_y);
                        const double distance_squared =
                            delta_x * delta_x + delta_y * delta_y;
                        ++metrics.candidate_checks;
                        ++metrics.pair_evaluations;
                        if (distance_squared > outer_squared) {
                            return;
                        }
                        observe(a, distance_squared);
                        observe(b, distance_squared);
                        if (adjacency_count > adjacency_capacity ||
                            adjacency_capacity - adjacency_count < 2U ||
                            adjacency_offsets[a + 1U] ==
                                std::numeric_limits<GridIndex>::max() ||
                            adjacency_offsets[b + 1U] ==
                                std::numeric_limits<GridIndex>::max()) {
                            list_full = true;
                        } else {
                            ++adjacency_offsets[a + 1U];
                            ++adjacency_offsets[b + 1U];
                            adjacency_count += 2U;
                        }
                        consider(scans[0], a, b, delta_x, delta_y,
                                 distance_squared);
                        consider(scans[1], b, a, -delta_x, -delta_y,
                                 distance_squared);
                    });
                    if (!list_full) {
                        for (std::size_t entity = 0U; entity < count;
                             ++entity) {
                            adjacency_offsets[entity + 1U] +=
                                adjacency_offsets[entity];
                        }
                        adjacency.resize(adjacency_count);
                        std::copy_n(adjacency_offsets.begin(), count,
                                    adjacency_cursors.begin());
                        // Repeat the same order so equal-distance
                        // ties stay stable
                        traverse_cells([&](const std::size_t a,
                                           const std::size_t b,
                                           const std::size_t, const std::size_t,
                                           const std::uint8_t image) {
                            double delta_x = state.x[b] - state.x[a];
                            double delta_y = state.y[b] - state.y[a];
                            apply_periodic_image(image, scenario.world.wraps,
                                                 scenario.world.width,
                                                 scenario.world.height, delta_x,
                                                 delta_y);
                            const double distance_squared =
                                delta_x * delta_x + delta_y * delta_y;
                            ++metrics.candidate_checks;
                            ++metrics.pair_evaluations;
                            if (distance_squared <= outer_squared) {
                                adjacency[adjacency_cursors[a]++] =
                                    static_cast<GridIndex>(b);
                                adjacency[adjacency_cursors[b]++] =
                                    static_cast<GridIndex>(a);
                            }
                        });
                        const double outer = cutoff + skin;
                        const double radii[] = {
                            std::sqrt(scans[0].plan->sensing_radius_squared),
                            std::sqrt(scans[1].plan->sensing_radius_squared)};
                        for (std::size_t entity = 0U; entity < count;
                             ++entity) {
                            // Certification avoids a full adjacency
                            // rescan after small moves
                            const std::size_t type = static_cast<std::size_t>(
                                entity >= scans[1].plan->first);
                            const double radius_squared =
                                scans[type].plan->sensing_radius_squared;
                            const double radius = radii[type];
                            if (radius == 0.0) {
                                certified_travel[entity] = 0.0;
                            } else if (nearest[entity] == missing) {
                                certified_travel[entity] =
                                    std::isfinite(first_outer[entity])
                                        ? std::max((first_outer[entity] -
                                                    radius_squared) /
                                                       (2.0 * (outer + radius)),
                                                   0.0)
                                        : skin * 0.5;
                            } else {
                                const double gap = (second_outer[entity] -
                                                    nearest_squared[entity]) /
                                                   (8.0 * outer);
                                const double range =
                                    (radius_squared - nearest_squared[entity]) /
                                    (4.0 * radius);
                                certified_travel[entity] =
                                    std::max(std::min(gap, range), 0.0);
                            }
                        }
                    } else {
                        // Packed pairs cost less metadata when
                        // CSR exceeds the cap
                        csr_active = false;
                        adjacency.clear();
                        adjacency_offsets.clear();
                        adjacency_cursors.clear();
                        first_outer.clear();
                        second_outer.clear();
                        certified_travel.clear();
                        const std::size_t fallback_capacity =
                            (candidate_storage_cap - displacement_bytes) /
                            sizeof(std::uint64_t);
                        const std::size_t fallback_limit =
                            std::min(wanted_pairs, fallback_capacity);
                        pairs.clear();
                        if (fallback_limit == 0U) {
                            pair_list_available = false;
                        } else {
                            active_pair_limit = fallback_limit;
                            pairs.reserve(fallback_limit);
                            list_full = false;
                            traverse_cells([&](const std::size_t a,
                                               const std::size_t b,
                                               const std::size_t,
                                               const std::size_t,
                                               const std::uint8_t image) {
                                double delta_x = state.x[b] - state.x[a];
                                double delta_y = state.y[b] - state.y[a];
                                apply_periodic_image(
                                    image, scenario.world.wraps,
                                    scenario.world.width, scenario.world.height,
                                    delta_x, delta_y);
                                const double distance_squared =
                                    delta_x * delta_x + delta_y * delta_y;
                                ++metrics.candidate_checks;
                                ++metrics.pair_evaluations;
                                if (distance_squared > outer_squared) {
                                    return;
                                }
                                if (pairs.size() == fallback_limit) {
                                    list_full = true;
                                } else {
                                    pairs.push_back(
                                        (static_cast<std::uint64_t>(a) << 32U) |
                                        b);
                                }
                            });
                        }
                    }
                } else
#endif
                {
                    traverse_cells([&](const std::size_t a, const std::size_t b,
                                       const std::size_t, const std::size_t,
                                       const std::uint8_t image) {
                        double delta_x = 0.0;
                        double delta_y = 0.0;
                        delta_x = state.x[b] - state.x[a];
                        delta_y = state.y[b] - state.y[a];
                        apply_periodic_image(
                            image, scenario.world.wraps, scenario.world.width,
                            scenario.world.height, delta_x, delta_y);
                        const double distance_squared =
                            delta_x * delta_x + delta_y * delta_y;
                        ++metrics.candidate_checks;
                        ++metrics.pair_evaluations;
                        if (distance_squared > outer_squared) {
                            return;
                        }
                        const std::uint64_t packed =
                            (static_cast<std::uint64_t>(a) << 32U) | b;
                        if (pairs.size() == active_pair_limit) {
                            list_full = true;
                        } else {
                            pairs.push_back(packed);
                        }
                        consider(scans[0], a, b, delta_x, delta_y,
                                 distance_squared);
                        consider(scans[1], b, a, -delta_x, -delta_y,
                                 distance_squared);
                    });
#if M1_OPT_LEVEL >= 7
#endif
                }
                metrics.pair_list_bytes = std::max<std::uint64_t>(
                    metrics.pair_list_bytes,
                    pairs.capacity() * sizeof(std::uint64_t) +
                        pair_origin_x.capacity() * sizeof(double) +
                        pair_origin_y.capacity() * sizeof(double)
#if M1_OPT_LEVEL >= 7
                        + adjacency.capacity() * sizeof(GridIndex) +
                        adjacency_offsets.capacity() * sizeof(GridIndex) +
                        adjacency_cursors.capacity() * sizeof(GridIndex) +
                        first_outer.capacity() * sizeof(double) +
                        second_outer.capacity() * sizeof(double) +
                        certified_travel.capacity() * sizeof(double)
#endif
                );
                if (list_full) {
                    // Direct grid scans preserve results when
                    // the cache cannot fit
                    pairs.clear();
                    pair_list_available = false;
                } else {
                    ++metrics.pair_list_rebuilds;
                    pair_revision = state.spatial_revision;
                    std::copy(state.x.begin(), state.x.end(),
                              pair_origin_x.begin());
                    std::copy(state.y.begin(), state.y.end(),
                              pair_origin_y.begin());
                    travelled = 0.0;
                    rebuild_pairs = false;
                }
            } else if (pair_list_available) {
                // Cache reuse tests saved candidates until the skin expires
#if M1_OPT_LEVEL >= 7
                if (csr_active) {
                    // Recheck candidates whose cached nearest
                    // is no longer safe
                    pair_published = true;
                    const auto publish_csr = [&]<bool fixed_membership>() {
                        // This hot loop walks one contiguous
                        // candidate range per entity
                        for (std::size_t entity = 0; entity < count; ++entity) {
                            if constexpr (!fixed_membership) {
                                if (state.alive[entity] == 0U)
                                    continue;
                            }
                            const Scan &scan = entity < scans[1].plan->first
                                                   ? scans[0]
                                                   : scans[1];
                            std::size_t target = nearest[entity];
                            double best_squared = nearest_squared[entity];
                            double best_x = 0.0;
                            double best_y = 0.0;
                            bool certified =
                                travelled < certified_travel[entity];
                            if (certified && target != count) {
                                best_x = wrapped_delta(state.x[target] -
                                                           state.x[entity],
                                                       scenario.world.width);
                                best_y = wrapped_delta(state.y[target] -
                                                           state.y[entity],
                                                       scenario.world.height);
                                best_squared =
                                    best_x * best_x + best_y * best_y;
                                ++metrics.candidate_checks;
                                ++metrics.pair_evaluations;
                                if constexpr (!fixed_membership) {
                                    certified = state.alive[target] != 0U;
                                }
                                certified =
                                    certified &&
                                    best_squared <=
                                        scan.plan->sensing_radius_squared;
                                metrics.sensed_interactions +=
                                    static_cast<std::uint64_t>(certified);
                            }
                            if (!certified) {
                                target = count;
                                best_squared =
                                    scan.plan->sensing_radius_squared;
                                const auto consider_candidate =
                                    [&](const std::size_t candidate) {
                                        if constexpr (!fixed_membership)
                                            if (state.alive[candidate] == 0U)
                                                return;
                                        const double delta_x =
                                            wrapped_delta(state.x[candidate] -
                                                              state.x[entity],
                                                          scenario.world.width);
                                        const double delta_y = wrapped_delta(
                                            state.y[candidate] -
                                                state.y[entity],
                                            scenario.world.height);
                                        const double distance_squared =
                                            delta_x * delta_x +
                                            delta_y * delta_y;
                                        ++metrics.candidate_checks;
                                        ++metrics.pair_evaluations;
                                        if (distance_squared >
                                                scan.plan
                                                    ->sensing_radius_squared ||
                                            (distance_squared == 0.0 &&
                                             scan.plan->behaviour_count !=
                                                 0U)) {
                                            return;
                                        }
                                        ++metrics.sensed_interactions;
                                        if (distance_squared < best_squared ||
                                            (distance_squared == best_squared &&
                                             candidate < target)) {
                                            target = candidate;
                                            best_squared = distance_squared;
                                            best_x = delta_x;
                                            best_y = delta_y;
                                        }
                                    };
                                for (std::size_t member =
                                         adjacency_offsets[entity];
                                     member < adjacency_offsets[entity + 1U];
                                     ++member) {
                                    consider_candidate(adjacency[member]);
                                }
                            }
                            publish(scan, entity, target, best_squared, best_x,
                                    best_y);
                        }
                    };
                    if (fixed_pair_membership) {
                        publish_csr.template operator()<true>();
                    } else {
                        publish_csr.template operator()<false>();
                    }
                } else {
#endif
                    // Packed 32-bit indices keep the streaming list compact
                    for (std::size_t pair = 0; pair < pairs.size(); ++pair) {
                        const std::size_t a =
                            static_cast<std::uint32_t>(pairs[pair] >> 32U);
                        const std::size_t b =
                            static_cast<std::uint32_t>(pairs[pair]);
                        if (state.alive[a] == 0U || state.alive[b] == 0U) {
                            continue;
                        }
                        double delta_x = 0.0;
                        double delta_y = 0.0;
                        delta_x = wrapped_delta(state.x[b] - state.x[a],
                                                scenario.world.width);
                        delta_y = wrapped_delta(state.y[b] - state.y[a],
                                                scenario.world.height);
                        const double distance_squared =
                            delta_x * delta_x + delta_y * delta_y;
                        ++metrics.candidate_checks;
                        ++metrics.pair_evaluations;
                        consider(scans[0], a, b, delta_x, delta_y,
                                 distance_squared);
                        consider(scans[1], b, a, -delta_x, -delta_y,
                                 distance_squared);
                    }
#if M1_OPT_LEVEL >= 7
                }
#endif
            } else
#endif
            {
                // This frame scans the newly built grid without a cache
                build_grid(grid, state);
                traverse_cells([&](const std::size_t a, const std::size_t b,
                                   const std::size_t, const std::size_t,
                                   const std::uint8_t image) {
                    double delta_x = 0.0;
                    double delta_y = 0.0;
                    delta_x = state.x[b] - state.x[a];
                    delta_y = state.y[b] - state.y[a];
                    apply_periodic_image(
                        image, scenario.world.wraps, scenario.world.width,
                        scenario.world.height, delta_x, delta_y);
                    const double distance_squared =
                        delta_x * delta_x + delta_y * delta_y;
                    ++metrics.candidate_checks;
                    ++metrics.pair_evaluations;
                    consider(scans[0], a, b, delta_x, delta_y,
                             distance_squared);
                    consider(scans[1], b, a, -delta_x, -delta_y,
                             distance_squared);
                });
            }
            if (
#if M1_OPT_LEVEL >= 7
                !pair_published
#else
                true
#endif
            ) {
                for (const Scan &scan : scans) {
                    for (std::size_t entity = scan.plan->first;
                         entity < scan.plan->first + scan.plan->count;
                         ++entity) {
                        if (state.alive[entity] != 0U) {
#if M1_OPT_LEVEL >= 7
                            double delta_x = 0.0;
                            double delta_y = 0.0;
                            if (nearest[entity] != missing) {
                                delta_x = wrapped_delta(
                                    state.x[nearest[entity]] - state.x[entity],
                                    scenario.world.width);
                                delta_y = wrapped_delta(
                                    state.y[nearest[entity]] - state.y[entity],
                                    scenario.world.height);
                            }
                            publish(scan, entity, nearest[entity],
                                    nearest_squared[entity], delta_x, delta_y);
#else
                            publish(scan, entity, nearest[entity],
                                    nearest_squared[entity], nearest_x[entity],
                                    nearest_y[entity]);
#endif
                        }
                    }
                }
            }
        } else
#endif
        {
            // Non-reciprocal scans select one nearest target for each source
            // General scans keep the target range and behaviour
            // set per source type
            build_grid(grid, state);
            for (const Scan &scan : scans) {
                const CharacterPlan &plan = *scan.plan;
                const auto scan_cell = [&](const std::size_t source_cell) {
                    visit_cell_members(
                        grid, source_cell, [&](const std::size_t entity) {
                            if (entity < plan.first) {
                                return;
                            }
                            if (entity >= plan.first + plan.count) {
                                return;
                            }
                            if (state.alive[entity] == 0U) {
                                return;
                            }
#if M1_OPT_LEVEL >= 4
                            const double source_x = state.x[entity];
                            const double source_y = state.y[entity];
#endif
                            std::size_t target = count;
                            double best_squared = plan.sensing_radius_squared;
                            double best_x = 0.0;
                            double best_y = 0.0;
                            std::array<GridIndex, 9U> nearby{};
                            const std::size_t nearby_count =
                                load_neighbours(grid, source_cell,
                                                scenario.world.wraps, nearby);
#if M1_OPT_LEVEL == 0
                            for (std::size_t neighbour = 0U;
                                 neighbour < nearby_count; ++neighbour) {
                                const std::size_t cell = nearby[neighbour];
                                visit_cell_members(
                                    grid, cell,
                                    [&](const std::size_t candidate) {
                                        if (candidate == entity ||
                                            candidate < scan.target_first ||
                                            candidate >= scan.target_end)
                                            return;
                                        const double delta_x =
                                            wrapped_delta(state.x[candidate] -
                                                              state.x[entity],
                                                          scenario.world.width);
                                        const double delta_y = wrapped_delta(
                                            state.y[candidate] -
                                                state.y[entity],
                                            scenario.world.height);
                                        const double distance_squared =
                                            delta_x * delta_x +
                                            delta_y * delta_y;
                                        ++metrics.candidate_checks;
                                        if (distance_squared >
                                                plan.sensing_radius_squared ||
                                            (distance_squared == 0.0 &&
                                             plan.behaviour_count != 0U))
                                            return;
                                        ++metrics.sensed_interactions;
                                        if (distance_squared < best_squared ||
                                            (distance_squared == best_squared &&
                                             candidate < target)) {
                                            target = candidate;
                                            best_squared = distance_squared;
                                            best_x = delta_x;
                                            best_y = delta_y;
                                        }
                                    });
                            }
#else
                        for (std::size_t neighbour = 0; neighbour < nearby_count; ++neighbour) {
                            const std::size_t cell = nearby[neighbour];
                            std::size_t begin = grid.offsets[cell];
                            std::size_t end = grid.offsets[cell + 1U];
#if M1_OPT_LEVEL >= 3
                            if (!grid.type_splits.empty()) {
                                if (scan.target_first == 0U) {
                                    end = grid.type_splits[cell];
                                } else {
                                    begin = grid.type_splits[cell];
                                }
                            }
#endif
                            for (std::size_t target_member = begin; target_member < end; ++target_member) {
                                const std::size_t candidate = grid.members[target_member];
#if M1_OPT_LEVEL >= 3
                                if (grid.type_splits.empty() && candidate < scan.target_first) {
                                    continue;
                                }
                                if (grid.type_splits.empty() && candidate >= scan.target_end) {
                                    break;
                                }
#endif
                                if (candidate == entity) {
                                    continue;
                                }
#if M1_OPT_LEVEL >= 4
                                double delta_x = 0.0;
                                double delta_y = 0.0;
                                classified_delta(grid, source_cell, cell, scenario.world.wraps,
                                                 scenario.world.width, scenario.world.height, source_x, source_y,
                                                 state.x[candidate], state.y[candidate], delta_x, delta_y);
#else
                                const double delta_x =
                                    wrapped_delta(state.x[candidate] - state.x[entity], scenario.world.width);
                                const double delta_y =
                                    wrapped_delta(state.y[candidate] - state.y[entity], scenario.world.height);
#endif
                                const double distance_squared = delta_x * delta_x + delta_y * delta_y;
                                ++metrics.candidate_checks;
#if M1_OPT_LEVEL < 3
                                if (distance_squared > plan.sensing_radius_squared ||
                                    (distance_squared == 0.0 && plan.behaviour_count != 0U)) {
                                    continue;
                                }
                                if (candidate < scan.target_first || candidate >= scan.target_end) {
                                    continue;
                                }
                                ++metrics.sensed_interactions;
#else
                                if (distance_squared > plan.sensing_radius_squared) {
                                    continue;
                                }
                                if (distance_squared == 0.0 && plan.behaviour_count != 0U) {
                                    continue;
                                }
                                ++metrics.sensed_interactions;
#endif
                                if (distance_squared < best_squared ||
                                    (distance_squared == best_squared && candidate < target)) {
                                    target = candidate;
                                    best_squared = distance_squared;
                                    best_x = delta_x;
                                    best_y = delta_y;
                                }
                            }
                        }
#endif
                            publish(scan, entity, target, best_squared, best_x,
                                    best_y);
                        });
                };
#if M1_OPT_LEVEL >= 3
                // Occupied cells avoid touching empty space in sparse worlds
                for (const GridIndex cell : grid.occupied_cells) {
                    scan_cell(static_cast<std::size_t>(cell));
                }
#else
                for (std::size_t cell = 0U; cell < grid.counts.size(); ++cell) {
                    scan_cell(cell);
                }
#endif
            }
        }

        bool membership_changed = false;
#if M1_OPT_LEVEL >= 5
        if (fixed_pair_membership) {
            metrics.entity_updates += count;
        } else {
#endif
            for (std::size_t entity = 0; entity < count; ++entity) {
                const bool alive = state.alive[entity] != 0U;
                metrics.entity_updates += static_cast<std::uint64_t>(alive);
                metrics.deaths += static_cast<std::uint64_t>(
                    alive && state.next_alive[entity] == 0U);
                membership_changed = membership_changed ||
                                     alive != (state.next_alive[entity] != 0U);
            }
#if M1_OPT_LEVEL >= 5
        }
#endif
        state.x.swap(state.next_x);
        state.y.swap(state.next_y);
        state.velocity_x.swap(state.next_velocity_x);
        state.velocity_y.swap(state.next_velocity_y);
        state.alive.swap(state.next_alive);
        // Commit only after all sources used the same current-state snapshot
#if M1_OPT_LEVEL >= 6
        if (use_verlet) {
            // Half the skin bounds motion before cached pairs
            // can become invalid
            travelled = std::sqrt(next_travelled_squared);
            rebuild_pairs = rebuild_pairs || membership_changed;
        }
#else
        (void)membership_changed;
#endif
        ++metrics.steps;
        if (observer != nullptr && snapshot_stride != 0U &&
            frame % snapshot_stride == 0U) {
            observer(frame, scenario, state, context);
        }
        if (state.result >= 0) {
            break;
        }
    }
    return metrics;
}

[[nodiscard]] Metrics simulate_continuous(
    const Scenario &scenario, State &state, const SnapshotObserver observer,
    void *const context, const std::uint64_t snapshot_stride,
    const StepController controller, void *const controller_context,
    const std::uint64_t first_step, const std::uint64_t step_count) {
#if M1_OPT_LEVEL == 1 && !M1_WIDE_GRID
    // The compact index type cannot address populations above uint32 capacity
    if (scenario.entity_count > std::numeric_limits<std::uint32_t>::max()) {
        return simulate_continuous_impl<std::uint64_t>(
            scenario, state, observer, context, snapshot_stride, controller,
            controller_context, first_step, step_count);
    }
    return simulate_continuous_impl<std::uint32_t>(
        scenario, state, observer, context, snapshot_stride, controller,
        controller_context, first_step, step_count);
#else
    return simulate_continuous_impl<GridIndex>(
        scenario, state, observer, context, snapshot_stride, controller,
        controller_context, first_step, step_count);
#endif
}

[[nodiscard]] std::uint64_t mix_bits(std::uint64_t value) noexcept {
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] Metrics simulate_extended_continuous(
    const Scenario &scenario, State &state, const SnapshotObserver observer,
    void *context, const std::uint64_t snapshot_stride,
    const StepController controller, void *const controller_context,
    const std::uint64_t first_step, const std::uint64_t step_count,
    const bool scripted_motion) {
    // General path for bounded worlds and multi-rule character plans
    Metrics metrics;
    const std::size_t count = scenario.entity_count;
    if (state.x.size() != count || state.y.size() != count ||
        state.next_x.size() != count || state.next_y.size() != count ||
        state.velocity_x.size() != count || state.velocity_y.size() != count ||
        state.next_velocity_x.size() != count ||
        state.next_velocity_y.size() != count || state.alive.size() != count ||
        state.next_alive.size() != count) {
        return metrics;
    }
    Grid grid = make_grid(scenario);
    std::vector<std::size_t> capture_owner(count, count);
    std::size_t maximum_behaviours = 0U;
    for (std::size_t plan_index = 0U; plan_index < scenario.characters.size();
         ++plan_index) {
        const CharacterPlan &plan = scenario.characters[plan_index];
        maximum_behaviours = std::max(maximum_behaviours, plan.behaviour_count);
    }
    std::vector<double> sum_x(maximum_behaviours);
    std::vector<double> sum_y(maximum_behaviours);
    std::vector<double> nearest_squared(maximum_behaviours);
    std::vector<std::size_t> nearest_id(maximum_behaviours);
    std::vector<std::size_t> neighbours(maximum_behaviours);
    // Per-rule scratch is sized once and reused for every entity in the frame
    // General-path frame phases: prepare, scan rules, resolve captures, commit
    for (std::uint64_t step = 0U; step < step_count; ++step) {
        // Capture claims and next-state buffers make
        // same-frame updates deterministic
        const std::uint64_t frame = first_step + step + 1U;
        if (controller != nullptr &&
            !controller(frame, scenario, state, controller_context)) {
            return metrics;
        }
        std::copy(state.x.begin(), state.x.end(), state.next_x.begin());
        std::copy(state.y.begin(), state.y.end(), state.next_y.begin());
        std::copy(state.velocity_x.begin(), state.velocity_x.end(),
                  state.next_velocity_x.begin());
        std::copy(state.velocity_y.begin(), state.velocity_y.end(),
                  state.next_velocity_y.begin());
        std::copy(state.alive.begin(), state.alive.end(),
                  state.next_alive.begin());
        std::fill(capture_owner.begin(), capture_owner.end(), count);
        // Rebuild each frame because a general rule may change membership
        build_grid(grid, state);
        if (scripted_motion) {
            // Scripted frames advance positions and make one capture claim
            for (std::size_t entity = 0U; entity < count; ++entity) {
                if (state.alive[entity] == 0U) {
                    continue;
                }
                if (scenario.world.wraps) {
                    state.next_x[entity] =
                        add_wrapped(state.x[entity], state.velocity_x[entity],
                                    scenario.world.width);
                    state.next_y[entity] =
                        add_wrapped(state.y[entity], state.velocity_y[entity],
                                    scenario.world.height);
                } else {
                    advance_bounded(state.x[entity], state.velocity_x[entity],
                                    scenario.world.width, state.next_x[entity]);
                    advance_bounded(state.y[entity], state.velocity_y[entity],
                                    scenario.world.height,
                                    state.next_y[entity]);
                }
            }
            for (std::size_t plan_index = 0U;
                 plan_index < scenario.characters.size(); ++plan_index) {
                const CharacterPlan &plan = scenario.characters[plan_index];
                if (plan.target >= scenario.characters.size() ||
                    plan.capture_radius_squared == 0.0) {
                    continue;
                }
                const CharacterPlan &target = scenario.characters[plan.target];
                for (std::size_t entity = plan.first;
                     entity < plan.first + plan.count; ++entity) {
                    if (state.alive[entity] == 0U) {
                        continue;
                    }
                    const std::size_t cell = grid.entity_cells[entity];
                    std::array<GridIndex, 9U> nearby{};
                    const std::size_t nearby_count = load_neighbours(
                        grid, cell, scenario.world.wraps, nearby);
                    std::size_t nearest = count;
                    double capture_nearest_squared =
                        std::numeric_limits<double>::infinity();
                    for (std::size_t neighbour = 0U; neighbour < nearby_count;
                         ++neighbour) {
                        visit_cell_members(
                            grid, nearby[neighbour],
                            [&](const std::size_t candidate) {
                                if (state.alive[candidate] == 0U ||
                                    candidate < target.first ||
                                    candidate >= target.first + target.count) {
                                    return;
                                }
                                const double dx = spatial_delta(
                                    state.x[candidate] - state.x[entity],
                                    scenario.world.width, scenario.world.wraps);
                                const double dy = spatial_delta(
                                    state.y[candidate] - state.y[entity],
                                    scenario.world.height,
                                    scenario.world.wraps);
                                const double distance_squared =
                                    dx * dx + dy * dy;
                                ++metrics.candidate_checks;
                                if (distance_squared >
                                        plan.capture_radius_squared ||
                                    distance_squared >
                                        capture_nearest_squared) {
                                    return;
                                }
                                ++metrics.sensed_interactions;
                                if (distance_squared <
                                        capture_nearest_squared ||
                                    candidate < nearest) {
                                    nearest = candidate;
                                    capture_nearest_squared = distance_squared;
                                }
                            });
                    }
                    if (nearest != count) {
                        capture_owner[nearest] =
                            std::min(capture_owner[nearest], entity);
                    }
                }
            }
        } else {
            // Behaviour frames accumulate forces before the speed limit
            for (std::size_t plan_index = 0U;
                 plan_index < scenario.characters.size(); ++plan_index) {
                const CharacterPlan &plan = scenario.characters[plan_index];
                const std::size_t behaviour_end =
                    plan.first_behaviour + plan.behaviour_count;
                if (behaviour_end > scenario.behaviour_plan.size()) {
                    return Metrics{};
                }
                const std::size_t entity_end = plan.first + plan.count;
                for (std::size_t entity = plan.first; entity < entity_end;
                     ++entity) {
                    if (state.alive[entity] == 0U) {
                        continue;
                    }
                    std::fill_n(sum_x.begin(), plan.behaviour_count, 0.0);
                    std::fill_n(sum_y.begin(), plan.behaviour_count, 0.0);
                    std::fill_n(nearest_squared.begin(), plan.behaviour_count,
                                std::numeric_limits<double>::infinity());
                    std::fill_n(nearest_id.begin(), plan.behaviour_count,
                                count);
                    std::fill_n(neighbours.begin(), plan.behaviour_count, 0U);
                    double force_x = 0.0;
                    double force_y = 0.0;
                    std::size_t capture_target = count;
                    double capture_distance =
                        std::numeric_limits<double>::infinity();
                    std::size_t hard_obstacle = count;
                    double hard_obstacle_distance =
                        std::numeric_limits<double>::infinity();
                    double hard_obstacle_radius = 0.0;
                    double hard_obstacle_weight = 0.0;
                    const std::size_t cell = grid.entity_cells[entity];
                    std::array<GridIndex, 9U> nearby{};
                    const std::size_t nearby_count = load_neighbours(
                        grid, cell, scenario.world.wraps, nearby);
                    for (std::size_t neighbour = 0U; neighbour < nearby_count;
                         ++neighbour) {
                        // Visit nearby cells once and accumulate
                        // every configured rule
                        const std::size_t neighbour_cell = nearby[neighbour];
                        visit_cell_members(
                            grid, neighbour_cell,
                            [&](const std::size_t candidate) {
                                if (candidate == entity) {
                                    return;
                                }
                                const double delta_x = spatial_delta(
                                    state.x[candidate] - state.x[entity],
                                    scenario.world.width, scenario.world.wraps);
                                const double delta_y = spatial_delta(
                                    state.y[candidate] - state.y[entity],
                                    scenario.world.height,
                                    scenario.world.wraps);
                                const double distance_squared =
                                    delta_x * delta_x + delta_y * delta_y;
                                ++metrics.candidate_checks;
                                if (distance_squared == 0.0 ||
                                    distance_squared >
                                        plan.sensing_radius_squared) {
                                    return;
                                }
                                ++metrics.sensed_interactions;
                                const double inverse_distance =
                                    1.0 / std::sqrt(distance_squared);
                                for (std::size_t index = plan.first_behaviour;
                                     index < behaviour_end; ++index) {
                                    const BehaviourRecord &record =
                                        scenario.behaviour_plan[index];
                                    if (record.target >=
                                        scenario.characters.size()) {
                                        continue;
                                    }
                                    const CharacterPlan &target =
                                        scenario.characters[record.target];
                                    if (candidate < target.first ||
                                        candidate >=
                                            target.first + target.count) {
                                        continue;
                                    }
                                    const double unit_x =
                                        delta_x * inverse_distance;
                                    const double unit_y =
                                        delta_y * inverse_distance;
                                    const std::size_t local =
                                        index - plan.first_behaviour;
                                    if (record.parameter != 0.0 &&
                                        (record.code ==
                                             BehaviourCode::separate ||
                                         record.code == BehaviourCode::avoid) &&
                                        distance_squared > record.parameter) {
                                        continue;
                                    }
                                    if (record.code == BehaviourCode::cohere) {
                                        sum_x[local] += delta_x;
                                        sum_y[local] += delta_y;
                                        ++neighbours[local];
                                    } else if (record.code ==
                                               BehaviourCode::align) {
                                        sum_x[local] +=
                                            state.velocity_x[candidate];
                                        sum_y[local] +=
                                            state.velocity_y[candidate];
                                        ++neighbours[local];
                                    } else if (record.code ==
                                                   BehaviourCode::seek ||
                                               record.code ==
                                                   BehaviourCode::flee ||
                                               record.code ==
                                                   BehaviourCode::pursue ||
                                               record.code ==
                                                   BehaviourCode::evade) {
                                        if (distance_squared >
                                                nearest_squared[local] ||
                                            (distance_squared ==
                                                 nearest_squared[local] &&
                                             candidate >= nearest_id[local])) {
                                            continue;
                                        }
                                        double direction_x = unit_x;
                                        double direction_y = unit_y;
                                        if (record.code ==
                                                BehaviourCode::pursue ||
                                            record.code ==
                                                BehaviourCode::evade) {
                                            direction_x = spatial_delta(
                                                delta_x + state.velocity_x
                                                                  [candidate] *
                                                              record.parameter,
                                                scenario.world.width,
                                                scenario.world.wraps);
                                            direction_y = spatial_delta(
                                                delta_y + state.velocity_y
                                                                  [candidate] *
                                                              record.parameter,
                                                scenario.world.height,
                                                scenario.world.wraps);
                                            const double predicted_squared =
                                                direction_x * direction_x +
                                                direction_y * direction_y;
                                            if (predicted_squared != 0.0) {
                                                const double inverse =
                                                    1.0 /
                                                    std::sqrt(
                                                        predicted_squared);
                                                direction_x *= inverse;
                                                direction_y *= inverse;
                                            }
                                        }
                                        const bool approaches =
                                            record.code ==
                                                BehaviourCode::seek ||
                                            record.code ==
                                                BehaviourCode::pursue;
                                        const double sign =
                                            approaches ? 1.0 : -1.0;
                                        sum_x[local] = sign * direction_x;
                                        sum_y[local] = sign * direction_y;
                                        neighbours[local] = 1U;
                                        nearest_squared[local] =
                                            distance_squared;
                                        nearest_id[local] = candidate;
                                    } else if (record.code ==
                                                   BehaviourCode::avoid ||
                                               record.code ==
                                                   BehaviourCode::separate) {
                                        if (record.code ==
                                                BehaviourCode::avoid &&
                                            target.obstacle_radius != 0.0 &&
                                            distance_squared <
                                                hard_obstacle_distance) {
                                            hard_obstacle = candidate;
                                            hard_obstacle_distance =
                                                distance_squared;
                                            hard_obstacle_radius =
                                                target.obstacle_radius;
                                            hard_obstacle_weight =
                                                record.weight;
                                        }
                                        const double inverse_square =
                                            1.0 /
                                            std::max(distance_squared, 0.25);
                                        sum_x[local] -=
                                            delta_x * inverse_square;
                                        sum_y[local] -=
                                            delta_y * inverse_square;
                                        ++neighbours[local];
                                    } else if (
                                        record.code == BehaviourCode::consume &&
                                        distance_squared <=
                                            plan.capture_radius_squared) {
                                        if (distance_squared <
                                                capture_distance ||
                                            (distance_squared ==
                                                 capture_distance &&
                                             candidate < capture_target)) {
                                            capture_target = candidate;
                                            capture_distance = distance_squared;
                                        }
                                    }
                                }
                            });
                    }
                    if (capture_target != count) {
                        capture_owner[capture_target] =
                            std::min(capture_owner[capture_target], entity);
                    }
                    for (std::size_t index = plan.first_behaviour;
                         index < behaviour_end; ++index) {
                        const BehaviourRecord &record =
                            scenario.behaviour_plan[index];
                        const std::size_t local = index - plan.first_behaviour;
                        if (neighbours[local] != 0U) {
                            const bool averaged =
                                record.code != BehaviourCode::separate &&
                                record.code != BehaviourCode::avoid;
                            const double inverse =
                                averaged
                                    ? 1.0 /
                                          static_cast<double>(neighbours[local])
                                    : 1.0;
                            double desired_x = sum_x[local] * inverse;
                            double desired_y = sum_y[local] * inverse;
                            if (record.code == BehaviourCode::cohere) {
                                const double range =
                                    std::sqrt(plan.sensing_radius_squared);
                                if (range != 0.0) {
                                    desired_x *= plan.step_distance / range;
                                    desired_y *= plan.step_distance / range;
                                }
                                desired_x -= state.velocity_x[entity];
                                desired_y -= state.velocity_y[entity];
                            } else if (record.code == BehaviourCode::align) {
                                desired_x -= state.velocity_x[entity];
                                desired_y -= state.velocity_y[entity];
                            }
                            force_x += desired_x * record.weight;
                            force_y += desired_y * record.weight;
                        }
                        if (record.code == BehaviourCode::wander) {
                            const std::uint64_t bits = mix_bits(
                                scenario.world.seed ^
                                ((step / 120U) * 0x9e3779b97f4a7c15ULL) ^
                                static_cast<std::uint64_t>(entity));
                            force_x += (bits & 1U) == 0U ? record.weight
                                                         : -record.weight;
                            force_y += (bits & 2U) == 0U ? record.weight
                                                         : -record.weight;
                        }
                    }
                    if (hard_obstacle != count &&
                        hard_obstacle_distance <=
                            (hard_obstacle_radius + plan.step_distance) *
                                (hard_obstacle_radius + plan.step_distance)) {
                        const double inverse =
                            1.0 / std::sqrt(hard_obstacle_distance);
                        const double normal_x =
                            -spatial_delta(
                                state.x[hard_obstacle] - state.x[entity],
                                scenario.world.width, scenario.world.wraps) *
                            inverse;
                        const double normal_y =
                            -spatial_delta(
                                state.y[hard_obstacle] - state.y[entity],
                                scenario.world.height, scenario.world.wraps) *
                            inverse;
                        double tangent_x = -normal_y;
                        double tangent_y = normal_x;
                        const double along =
                            state.velocity_x[entity] * tangent_x +
                            state.velocity_y[entity] * tangent_y;
                        if (along < 0.0 ||
                            (along == 0.0 && (entity & 1U) != 0U)) {
                            tangent_x = -tangent_x;
                            tangent_y = -tangent_y;
                        }
                        force_x += tangent_x * hard_obstacle_weight;
                        force_y += tangent_y * hard_obstacle_weight;
                    }
                    if (plan.max_steering > 0.0) {
                        const double force_squared =
                            force_x * force_x + force_y * force_y;
                        const double limit_squared =
                            plan.max_steering * plan.max_steering;
                        if (force_squared > limit_squared) {
                            const double scale =
                                plan.max_steering / std::sqrt(force_squared);
                            force_x *= scale;
                            force_y *= scale;
                        }
                    }
                    const double velocity_x =
                        state.velocity_x[entity] + force_x;
                    const double velocity_y =
                        state.velocity_y[entity] + force_y;
                    const double magnitude_squared =
                        velocity_x * velocity_x + velocity_y * velocity_y;
                    if (magnitude_squared == 0.0) {
                        continue;
                    }
                    const double scale = std::min(
                        plan.step_distance / std::sqrt(magnitude_squared), 1.0);
                    state.next_velocity_x[entity] = velocity_x * scale;
                    state.next_velocity_y[entity] = velocity_y * scale;
                    if (scenario.world.wraps) {
                        state.next_x[entity] = add_wrapped(
                            state.x[entity], state.next_velocity_x[entity],
                            scenario.world.width);
                        state.next_y[entity] = add_wrapped(
                            state.y[entity], state.next_velocity_y[entity],
                            scenario.world.height);
                    } else {
                        advance_bounded(
                            state.x[entity], state.next_velocity_x[entity],
                            scenario.world.width, state.next_x[entity]);
                        advance_bounded(
                            state.y[entity], state.next_velocity_y[entity],
                            scenario.world.height, state.next_y[entity]);
                    }
                    if (hard_obstacle != count) {
                        double obstacle_x = spatial_delta(
                            state.next_x[entity] - state.x[hard_obstacle],
                            scenario.world.width, scenario.world.wraps);
                        double obstacle_y = spatial_delta(
                            state.next_y[entity] - state.y[hard_obstacle],
                            scenario.world.height, scenario.world.wraps);
                        double obstacle_squared =
                            obstacle_x * obstacle_x + obstacle_y * obstacle_y;
                        const double radius_squared =
                            hard_obstacle_radius * hard_obstacle_radius;
                        if (obstacle_squared < radius_squared) {
                            // Project overlaps to the boundary
                            // and remove inward speed
                            if (obstacle_squared == 0.0) {
                                obstacle_x = (entity & 1U) == 0U ? 1.0 : -1.0;
                                obstacle_y = 0.0;
                                obstacle_squared = 1.0;
                            }
                            const double inverse_distance =
                                1.0 / std::sqrt(obstacle_squared);
                            const double unit_x = obstacle_x * inverse_distance;
                            const double unit_y = obstacle_y * inverse_distance;
                            const double normal_x =
                                unit_x * hard_obstacle_radius;
                            const double normal_y =
                                unit_y * hard_obstacle_radius;
                            if (scenario.world.wraps) {
                                state.next_x[entity] = add_wrapped(
                                    state.x[hard_obstacle] + normal_x, 0.0,
                                    scenario.world.width);
                                state.next_y[entity] = add_wrapped(
                                    state.y[hard_obstacle] + normal_y, 0.0,
                                    scenario.world.height);
                            } else {
                                state.next_x[entity] = std::clamp(
                                    state.x[hard_obstacle] + normal_x, 0.0,
                                    scenario.world.width);
                                state.next_y[entity] = std::clamp(
                                    state.y[hard_obstacle] + normal_y, 0.0,
                                    scenario.world.height);
                            }
                            const double inward =
                                state.next_velocity_x[entity] * unit_x +
                                state.next_velocity_y[entity] * unit_y;
                            if (inward < 0.0) {
                                state.next_velocity_x[entity] -=
                                    inward * unit_x;
                                state.next_velocity_y[entity] -=
                                    inward * unit_y;
                            }
                        }
                    }
                }
            }
        }
        for (std::size_t target = 0U; target < count; ++target) {
            // The lowest claimant wins when several entities capture one target
            const std::size_t owner = capture_owner[target];
            if (owner == count || state.alive[target] == 0U) {
                continue;
            }
            state.next_alive[target] = 0U;
            ++metrics.captures;
            ++metrics.deaths;
        }
        for (std::size_t entity = 0U; entity < count; ++entity) {
            // Count committed live-entity work rather than rejected candidates
            const bool alive = state.alive[entity] != 0U;
            metrics.entity_updates += static_cast<std::uint64_t>(alive);
        }
        state.x.swap(state.next_x);
        state.y.swap(state.next_y);
        state.velocity_x.swap(state.next_velocity_x);
        state.velocity_y.swap(state.next_velocity_y);
        state.alive.swap(state.next_alive);
        // Observer output is taken after the complete frame becomes current
        ++metrics.steps;
        if (observer != nullptr && snapshot_stride != 0U &&
            frame % snapshot_stride == 0U) {
            observer(frame, scenario, state, context);
        }
        if (state.result >= 0) {
            break;
        }
    }
    return metrics;
}

} // namespace m1
