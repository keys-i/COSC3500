#include "model.hpp"

#include <algorithm>
#include <cstring>
#include <string>

/// \file
/// Hash, size, and count State without exposing kernel-specific storage rules
namespace m1 {
namespace {

constexpr std::uint64_t fnv_offset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;

// Feed values in a fixed byte order so checksums remain reproducible
void hash_u64(std::uint64_t &value, const std::uint64_t input) noexcept {
    std::uint64_t bits = input;
    for (unsigned byte = 0U; byte < sizeof(bits); ++byte) {
        value ^= bits & 0xffU;
        value *= fnv_prime;
        bits >>= 8U;
    }
}

void hash_double(std::uint64_t &value, const double input) noexcept {
    std::uint64_t bits = 0U;
    static_assert(sizeof(bits) == sizeof(input));
    std::memcpy(&bits, &input, sizeof(bits));
    hash_u64(value, bits);
}

void hash_text(std::uint64_t &value, const std::string &text) noexcept {
    hash_u64(value, static_cast<std::uint64_t>(text.size()));
    for (const char byte : text) {
        hash_u64(value, static_cast<unsigned char>(byte));
    }
}

} // namespace

// State fingerprint
// Keep this order stable because tests and benchmark comparisons use it
std::uint64_t checksum(const State &state) noexcept {
    // Hash raw floating-point bits so small state changes are visible
    std::uint64_t value = fnv_offset;
    hash_u64(value, static_cast<std::uint64_t>(state.x.size()));
    for (std::size_t index = 0; index < state.x.size(); ++index) {
        hash_double(value, state.x[index]);
        hash_double(value, state.y[index]);
        if (index < state.velocity_x.size()) {
            hash_double(value, state.velocity_x[index]);
            hash_double(value, state.velocity_y[index]);
        }
        hash_u64(value, state.alive[index]);
        if (index < state.timeline_z.size()) {
            hash_double(value, state.timeline_z[index]);
            hash_u64(value, state.timeline_state[index]);
            hash_text(value, state.timeline_text[index]);
        }
    }
    if (!state.cells.empty()) {
        // Cellular state uses one byte per cell, including state zero
        hash_u64(value, static_cast<std::uint64_t>(state.cells.size()));
        for (const std::uint8_t cell : state.cells)
            hash_u64(value, cell);
    }
    if (!state.board.empty()) {
        // Turn state is meaningful only with the board it advances
        hash_u64(value, static_cast<std::uint64_t>(state.board.size()));
        for (const std::uint32_t cell : state.board)
            hash_u64(value, cell);
        hash_u64(value, state.turn);
    }
    if (state.pde.steps != 0U) {
        // PDE arrays are retained only after a PDE run has produced samples
        for (const double sample : state.pde.values)
            hash_double(value, sample);
        for (const double reference : state.pde.references)
            hash_double(value, reference);
        for (const double error : state.pde.errors)
            hash_double(value, error);
        hash_double(value, state.pde.minimum_x_spacing);
        hash_double(value, state.pde.minimum_y_spacing);
        hash_double(value, state.pde.time_step);
        hash_u64(value, state.pde.cell_updates);
        hash_u64(value, state.pde.steps);
    }
    hash_u64(value, static_cast<std::uint64_t>(
                        static_cast<std::uint32_t>(state.result)));
    return value;
}

// Live memory accounting
// This mirrors the State payload rather than allocator-specific capacity
std::uint64_t state_bytes(const State &state) noexcept {
    // Report serialised payload, not allocator capacity
    const std::size_t doubles =
        state.x.size() + state.y.size() + state.next_x.size() +
        state.next_y.size() + state.velocity_x.size() +
        state.velocity_y.size() + state.timeline_z.size() +
        state.timeline_start_x.size() + state.timeline_start_y.size() +
        state.timeline_start_z.size() + state.timeline_target_x.size() +
        state.timeline_target_y.size() + state.timeline_target_z.size() +
        state.timeline_arc_height.size() + state.next_velocity_x.size() +
        state.next_velocity_y.size() + state.pde.values.size() +
        state.pde.references.size() + state.pde.errors.size();
    const std::size_t words = state.timeline_state.size() + state.board.size();
    const std::size_t wide =
        state.timeline_start_step.size() + state.timeline_end_step.size();
    const std::size_t bytes = state.alive.size() + state.next_alive.size() +
                              state.cells.size() + state.next_cells.size();
    std::size_t text_bytes = 0U;
    for (const std::string &text : state.timeline_text)
        text_bytes += sizeof(std::string) + text.size();
    const std::uint64_t pde_bytes =
        state.pde.steps == 0U ? 0U : sizeof(state.pde);
    return static_cast<std::uint64_t>(doubles) * sizeof(double) +
           static_cast<std::uint64_t>(words) * sizeof(std::uint32_t) +
           static_cast<std::uint64_t>(wide) * sizeof(std::uint64_t) +
           static_cast<std::uint64_t>(bytes) +
           static_cast<std::uint64_t>(text_bytes) + sizeof(state.result) +
           sizeof(state.turn_duration_us) + pde_bytes;
}

// Kernel-specific activity summary
std::size_t active_count(const State &state) noexcept {
    // Each kernel has its own definition of an active item
    if (state.pde.steps != 0U)
        return 0U;
    if (!state.cells.empty())
        return static_cast<std::size_t>(
            std::count_if(state.cells.begin(), state.cells.end(),
                          [](const std::uint8_t cell) { return cell != 0U; }));
    if (!state.board.empty())
        return static_cast<std::size_t>(
            std::count_if(state.board.begin(), state.board.end(),
                          [](const std::uint32_t cell) { return cell != 0U; }));
    return static_cast<std::size_t>(
        std::count(state.alive.begin(), state.alive.end(), 1U));
}

} // namespace m1
