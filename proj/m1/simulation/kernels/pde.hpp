#ifndef MOLLY_M1_SIMULATION_KERNELS_PDE_HPP
#define MOLLY_M1_SIMULATION_KERNELS_PDE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// \file
/// Shared plan and result types for the two-dimensional scalar PDE solver
/// Lua supplies model data while C++ applies discretization and time stepping
namespace m1 {

/// Select the condition imposed on one edge of the PDE grid
enum class PdeBoundaryKind : std::uint8_t {
    dirichlet,
    neumann,
    natural,
};

/// Coefficients of Lu = xx u_xx + xy u_xy + yy u_yy + x u_x + y u_y
/// + value u + source
/// Coordinates, time, and field values use the units chosen by the Lua model
struct PdeCoefficients {
    double xx = 0.0;
    double xy = 0.0;
    double yy = 0.0;
    double x = 0.0;
    double y = 0.0;
    double value = 0.0;
    double source = 0.0;
};

/// Grid, time horizon and sampling settings for one PDE solve
struct PdePlan {
    std::size_t columns = 0U;
    std::size_t rows = 0U;
    std::uint64_t steps = 0U;
    // Each field has independent state and shares the configured grid
    std::vector<std::string> fields;
    double x_min = 0.0;
    double x_max = 1.0;
    double y_min = 0.0;
    double y_max = 1.0;
    // Time horizon and sample location use the Lua model's units
    double final_time = 1.0;
    double sample_x = 0.5;
    double sample_y = 0.5;
    // Positive scale clusters an asinh grid around the matching focus
    double x_focus = 0.0;
    double y_focus = 0.0;
    double x_scale = 0.0;
    double y_scale = 0.0;
    // Implicit fraction for the Craig-Sneyd directional solves
    double theta = 1.0 / 3.0;
};

/// Samples and work counters returned by the PDE solver
struct PdeResult {
    // One sampled value, optional reference, and signed error per field
    std::vector<double> values;
    std::vector<double> references;
    std::vector<double> errors;
    // Grid spacing and time step are reported in the plan's units
    double minimum_x_spacing = 0.0;
    double minimum_y_spacing = 0.0;
    double time_step = 0.0;
    std::uint64_t cell_updates = 0U;
    std::uint64_t steps = 0U;
};

} // namespace m1

#endif
