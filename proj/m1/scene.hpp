#ifndef MOLLY_M1_SCENE_HPP
#define MOLLY_M1_SCENE_HPP

#include "model.hpp"

#include <cstdint>
#include <string>

/// \file
/// Convert simulation state into the stable CSV stream read by the visualiser

namespace m1 {

/// Column order shared by file snapshots and the interactive stream
extern const char snapshot_header[];

/// Derive visual-only randomness without advancing the simulation seed
[[nodiscard]] std::uint64_t render_seed(std::uint64_t run_seed) noexcept;
/// Build the conventional CSV destination for a scenario name
[[nodiscard]] std::string snapshot_path(std::string_view scenario);
/// Write a complete frame to an ostream supplied through context
/// Context must point to a live std::ostream for the duration of the callback
void write_snapshot(std::uint64_t frame, const Scenario &scenario,
                    const State &state, void *context);
/// Write a frame followed by the record that releases stream clients
void write_stream_snapshot(std::uint64_t frame, const Scenario &scenario,
                           const State &state, void *context);

} // namespace m1

#endif
