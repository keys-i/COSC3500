#include "detail.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <vector>

/// \file
/// Compile shared and kernel-specific sections after parsing
namespace m1::config_detail {

#ifndef M1_WIDE_GRID
#define M1_WIDE_GRID 0
#endif

#ifndef M1_OPT_LEVEL
#define M1_OPT_LEVEL 0
#endif

#if M1_OPT_LEVEL == 0 || M1_WIDE_GRID
constexpr std::uint64_t cellular_grid_index_limit =
    std::numeric_limits<std::uint64_t>::max();
#else
constexpr std::uint64_t cellular_grid_index_limit =
    std::numeric_limits<std::uint32_t>::max();
#endif

[[nodiscard]] std::optional<Kernel> compile_kernel(const Section &section,
                                                   std::string &error) {
    // The version selects the grammar before kernel-specific parsing
    if (!reject_unknown(section, {"version", "kernel"}, error)) {
        return std::nullopt;
    }
    const Field *const version = require_field(section, "version", error);
    const Field *const kernel = require_field(section, "kernel", error);
    if (version == nullptr || kernel == nullptr) {
        return std::nullopt;
    }
    if (version->value != "4") {
        fail(error, version->line, "version must be 4");
        return std::nullopt;
    }
    if (kernel->value == "continuous") {
        return Kernel::continuous;
    }
    if (kernel->value == "cellular") {
        return Kernel::cellular;
    }
    if (kernel->value == "turn") {
        return Kernel::turn;
    }
    if (kernel->value == "timeline") {
        return Kernel::timeline;
    }
    if (kernel->value == "pde") {
        return Kernel::pde;
    }
    fail(error, kernel->line,
         "kernel must be continuous, cellular, turn, timeline, or pde");
    return std::nullopt;
}

// Translate world dimensions and time settings into the common runtime record
[[nodiscard]] bool compile_world(const Section &section, const Kernel kernel,
                                 WorldConfig &world, std::string &error) {
    // Continuous integration needs a time step; other kernels advance by turns
    const bool fields_known =
        kernel == Kernel::continuous
            ? reject_unknown(
                  section,
                  {"width", "height", "time_step", "steps", "seed", "boundary"},
                  error)
            : reject_unknown(section,
                             {"width", "height", "steps", "seed", "boundary"},
                             error);
    if (!fields_known) {
        return false;
    }
    const Field *const width = require_field(section, "width", error);
    const Field *const height = require_field(section, "height", error);
    const Field *const steps = require_field(section, "steps", error);
    const Field *const boundary = require_field(section, "boundary", error);
    if (width == nullptr || height == nullptr || steps == nullptr ||
        boundary == nullptr ||
        !parse_number(*width, false, world.width, error) ||
        !parse_number(*height, false, world.height, error) ||
        !parse_u64(*steps, 1U, world.steps, error)) {
        return false;
    }
    world.time_step = 1.0;
    world.seed = 0U;
    if (const Field *const seed = find_field(section, "seed")) {
        if (!parse_u64(*seed, 0U, world.seed, error)) {
            return false;
        }
    }
    if (boundary->value == "wrap") {
        world.wraps = true;
    } else if (boundary->value != "bounded") {
        return fail(error, boundary->line, "boundary must be bounded or wrap");
    }
    if (kernel == Kernel::continuous) {
        const Field *const step = require_field(section, "time_step", error);
        if (step == nullptr ||
            require_field(section, "seed", error) == nullptr ||
            !parse_number(*step, false, world.time_step, error)) {
            return false;
        }
    } else if (kernel != Kernel::cellular && world.wraps) {
        return fail(error, boundary->line,
                    kernel == Kernel::pde
                        ? "pde worlds require boundary=bounded"
                        : "turn and timeline worlds require "
                          "boundary=bounded");
    }
    return true;
}

[[nodiscard]] bool compile_output(const Section *section, const Kernel kernel,
                                  std::uint64_t &stride, View &view,
                                  std::string &error) {
    // Defaults keep minimal scenario files usable while matching their topology
    stride = 1U;
    view = kernel == Kernel::cellular || kernel == Kernel::turn ? View::grid
                                                                : View::plane;
    if (section == nullptr) {
        return true;
    }
    if (!reject_unknown(*section, {"snapshot_stride", "view"}, error)) {
        return false;
    }
    if (const Field *const field = find_field(*section, "snapshot_stride")) {
        if (!parse_u64(*field, 1U, stride, error)) {
            return false;
        }
    }
    if (const Field *const field = find_field(*section, "view")) {
        if (field->value == "plane") {
            view = View::plane;
        } else if (field->value == "grid") {
            view = View::grid;
        } else {
            return fail(error, field->line, "view must be plane or grid");
        }
    }
    if ((kernel == Kernel::cellular || kernel == Kernel::turn) &&
        view != View::grid) {
        return fail(error, section->line,
                    "cellular and turn require view=grid");
    }
    return true;
}

// Record the Lua file name after rejecting paths that escape the scenario tree
[[nodiscard]] bool compile_rules(const Section *section, Scenario &result,
                                 std::string &error) {
    if (section == nullptr)
        return true;
    if (!reject_unknown(*section, {"file"}, error))
        return false;
    const Field *const file = require_field(*section, "file", error);
    if (file == nullptr)
        return false;
    // Rule files are resolved beneath the scenario directory by the caller
    if (!valid_path(file->value) || file->value.size() < 5U ||
        file->value.substr(file->value.size() - 4U) != ".lua") {
        return fail(error, file->line, "rules file must be a safe .lua path");
    }
    result.lua_rules = file->value;
    return true;
}

// Convert asset sections to render metadata without opening referenced files
[[nodiscard]] bool compile_assets(const std::vector<Section> &sections,
                                  Scenario &result, std::string &error) {
    // Store asset metadata here; loading remains a presentation concern
    constexpr std::string_view prefix{"asset."};
    for (const Section &section : sections) {
        if (section.name.rfind(prefix, 0U) != 0U)
            continue;
        const std::string name = section.name.substr(prefix.size());
        if (!valid_name(name) ||
            !reject_unknown(section, {"file", "kind"}, error))
            return false;
        const Field *const file = require_field(section, "file", error);
        const Field *const kind = require_field(section, "kind", error);
        if (file == nullptr || kind == nullptr || !valid_path(file->value) ||
            (kind->value != "image" && kind->value != "audio")) {
            return fail(error, section.line,
                        "asset needs a safe file and kind");
        }
        result.assets.push_back(AssetPlan{name, file->value, kind->value});
    }
    return true;
}

// Validate dense cell input and build the byte-oriented cellular start state
[[nodiscard]] bool compile_cellular(const Section &section, Scenario &result,
                                    std::string &error) {
    if (!reject_unknown(section, {"states", "cells"}, error))
        return false;
    const Field *const states = require_field(section, "states", error);
    const Field *const cells = find_field(section, "cells");
    if (states == nullptr)
        return false;
    std::uint64_t count = 0;
    if (!parse_u64(*states, 2U, count, error) || count > 255U) {
        return false;
    }
    const double columns = result.world.width;
    const double rows = result.world.height;
    if (std::floor(columns) != columns || std::floor(rows) != rows) {
        return fail(error, section.line,
                    "cellular world dimensions must be integers");
    }
    constexpr double max_dimension =
        static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    if (columns > max_dimension || rows > max_dimension) {
        return fail(error, section.line,
                    "cellular grid exceeds index capacity");
    }
    result.cellular.columns = static_cast<std::size_t>(columns);
    result.cellular.rows = static_cast<std::size_t>(rows);
    if (result.cellular.columns == 0U ||
        result.cellular.rows >
            std::vector<std::uint8_t>{}.max_size() / result.cellular.columns) {
        return fail(error, section.line, "cellular grid is too large");
    }
    // Check the product before allocating the dense cell state buffer
    const std::size_t total = result.cellular.columns * result.cellular.rows;
    if (total > cellular_grid_index_limit) {
        return fail(error, section.line,
                    "cellular grid exceeds index capacity");
    }
    if (cells != nullptr && cells->value.size() != total) {
        return fail(error, cells->line,
                    "cells must contain width*height states");
    }
    result.cellular.state_count = static_cast<std::uint8_t>(count);
    result.cellular.wraps = result.world.wraps;
    if (cells != nullptr) {
        result.cellular.initial.resize(total);
        for (std::size_t index = 0U; index < total; ++index) {
            const char value = cells->value[index];
            if (value < '0' || value > '9' ||
                static_cast<std::uint8_t>(value - '0') >=
                    result.cellular.state_count) {
                return fail(error, cells->line,
                            "cell state is outside its declared range");
            }
            result.cellular.initial[index] =
                static_cast<std::uint8_t>(value - '0');
        }
    }
    return !result.lua_rules.empty() ||
           fail(error, section.line, "cellular scenarios require a rules file");
}

// Validate the grid layout used by the scripted turn controller
[[nodiscard]] bool compile_turn(const Section &section, Scenario &result,
                                std::string &error) {
    // The turn kernel indexes one unsigned 32-bit occupant per grid square
    if (!reject_unknown(section, {"topology", "controller"}, error)) {
        return false;
    }
    const Field *const topology = require_field(section, "topology", error);
    if (topology == nullptr)
        return false;
    if (std::floor(result.world.width) != result.world.width ||
        std::floor(result.world.height) != result.world.height) {
        return fail(error, section.line,
                    "turn world dimensions must be integers");
    }
    constexpr double max_dimension =
        static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    if (result.world.width > max_dimension ||
        result.world.height > max_dimension) {
        return fail(error, section.line, "turn grid is too large");
    }
    result.turn.columns = static_cast<std::size_t>(result.world.width);
    result.turn.rows = static_cast<std::size_t>(result.world.height);
    if (result.turn.columns == 0U ||
        result.turn.rows >
            std::vector<std::uint32_t>{}.max_size() / result.turn.columns ||
        result.turn.columns * result.turn.rows >
            std::numeric_limits<std::uint32_t>::max()) {
        return fail(error, section.line, "turn grid is too large");
    }
    if (topology->value != "grid") {
        return fail(error, topology->line, "topology must be grid");
    }
    const Field *const controller = find_field(section, "controller");
    return controller == nullptr || controller->value == "script"
               ? true
               : fail(error, controller->line, "controller must be script");
}

// Validate the solver grid and convert physical bounds into a PdePlan
[[nodiscard]] bool compile_pde(const Section &section, Scenario &result,
                               std::string &error) {
    // Field names form the Lua-to-solver interface and must stay unique
    if (!reject_unknown(section,
                        {"fields", "x_min", "x_max", "y_min", "y_max",
                         "final_time", "sample_x", "sample_y", "x_focus",
                         "x_scale", "y_focus", "y_scale", "theta"},
                        error)) {
        return false;
    }
    const Field *const fields = require_field(section, "fields", error);
    const Field *const x_min = require_field(section, "x_min", error);
    const Field *const x_max = require_field(section, "x_max", error);
    const Field *const y_min = require_field(section, "y_min", error);
    const Field *const y_max = require_field(section, "y_max", error);
    const Field *const final_time = require_field(section, "final_time", error);
    const Field *const sample_x = require_field(section, "sample_x", error);
    const Field *const sample_y = require_field(section, "sample_y", error);
    if (fields == nullptr || x_min == nullptr || x_max == nullptr ||
        y_min == nullptr || y_max == nullptr || final_time == nullptr ||
        sample_x == nullptr || sample_y == nullptr) {
        return false;
    }
    PdePlan &plan = result.pde;
    std::vector<std::string_view> names;
    if (!split_list(*fields, names, error) || names.empty() ||
        names.size() > 8U) {
        return fail(error, fields->line, "fields needs 1..8 names");
    }
    for (const std::string_view name : names) {
        if (!valid_name(name) || name.size() > 32U) {
            return fail(error, fields->line, "fields contains an invalid name");
        }
        if (std::find(plan.fields.begin(), plan.fields.end(), name) !=
            plan.fields.end()) {
            return fail(error, fields->line, "fields has a duplicate name");
        }
        plan.fields.emplace_back(name);
    }
    if (!parse_finite_number(*x_min, plan.x_min, error) ||
        !parse_finite_number(*x_max, plan.x_max, error) ||
        !parse_finite_number(*y_min, plan.y_min, error) ||
        !parse_finite_number(*y_max, plan.y_max, error) ||
        !parse_finite_number(*final_time, plan.final_time, error) ||
        !parse_finite_number(*sample_x, plan.sample_x, error) ||
        !parse_finite_number(*sample_y, plan.sample_y, error)) {
        return false;
    }
    // Optional focus and scale terms leave the corresponding transform disabled
    const auto optional = [&](const std::string_view key, double &value) {
        const Field *const field = find_field(section, key);
        return field == nullptr || parse_finite_number(*field, value, error);
    };
    if (!optional("x_focus", plan.x_focus) ||
        !optional("x_scale", plan.x_scale) ||
        !optional("y_focus", plan.y_focus) ||
        !optional("y_scale", plan.y_scale) || !optional("theta", plan.theta)) {
        return false;
    }
    constexpr double max_dimension =
        static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    if (std::floor(result.world.width) != result.world.width ||
        std::floor(result.world.height) != result.world.height ||
        result.world.width < 3.0 || result.world.height < 3.0 ||
        result.world.width > max_dimension ||
        result.world.height > max_dimension || result.world.steps < 2U ||
        result.world.steps > std::numeric_limits<std::size_t>::max()) {
        return fail(error, section.line, "PDE grid and time steps are invalid");
    }
    if (!(plan.x_max > plan.x_min) || !(plan.y_max > plan.y_min) ||
        !(plan.final_time > 0.0)) {
        return fail(error, section.line,
                    "PDE bounds and final_time must increase");
    }
    const auto inside = [](const double value, const double low,
                           const double high) {
        return value >= low && value <= high;
    };
    if (!inside(plan.sample_x, plan.x_min, plan.x_max) ||
        !inside(plan.sample_y, plan.y_min, plan.y_max) ||
        (plan.x_scale > 0.0 && !inside(plan.x_focus, plan.x_min, plan.x_max)) ||
        (plan.y_scale > 0.0 && !inside(plan.y_focus, plan.y_min, plan.y_max)) ||
        plan.x_scale < 0.0 || plan.y_scale < 0.0 || plan.theta < 1.0 / 3.0 ||
        plan.theta > 1.0) {
        return fail(error, section.line,
                    "invalid PDE sample, focus, scale, or theta");
    }
    // Convert checked dimensions once for the solver's row-major storage
    plan.columns = static_cast<std::size_t>(result.world.width);
    plan.rows = static_cast<std::size_t>(result.world.height);
    if (plan.columns > std::vector<double>{}.max_size() / plan.rows) {
        return fail(error, section.line, "PDE grid is too large");
    }
    plan.steps = result.world.steps;
    result.world.time_step = plan.final_time / static_cast<double>(plan.steps);
    return true;
}

} // namespace m1::config_detail
