#ifndef MOLLY_M1_CONFIG_DETAIL_HPP
#define MOLLY_M1_CONFIG_DETAIL_HPP

#include "model.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// \file
/// Parsed records and compiler stages used to build Scenario
namespace m1::config_detail {

// Preserve source locations until every semantic check has completed
struct Field {
    std::string key;
    std::string value;
    std::size_t line = 0U;
};

// Keep each named section together before compiling it into Scenario
struct Section {
    std::string name;
    std::size_t line = 0U;
    std::vector<Field> fields;
};

// Hold character fields before names and cross-references can be resolved
struct Draft {
    std::string name;
    std::size_t line = 0U;
    std::optional<std::uint64_t> count;
    std::optional<double> speed;
    std::optional<double> sensing_radius;
    std::optional<double> capture_radius;
    std::optional<std::uint64_t> capacity;
    std::optional<double> max_steering;
    std::optional<double> obstacle_radius;
    std::optional<std::string> behaviours;
    std::optional<std::string> target;
    std::optional<double> x;
    std::optional<double> y;
    std::optional<bool> visible;
    std::optional<Shape> shape;
    std::optional<std::string> colour;
    std::optional<std::string> glyph;
    std::optional<std::int32_t> layer;
    std::optional<double> size;
    std::optional<std::string> label;
    std::optional<std::string> sprite;
    std::optional<std::string> sprite_north;
    std::optional<std::string> sprite_south;
    std::optional<Motion> motion;
};

// Store reusable behaviour settings before character plans expand them
struct BehaviourDraft {
    std::string name;
    BehaviourCode code = BehaviourCode::idle;
    std::string target;
    double weight = 1.0;
    double parameter = 0.0;
};

// Syntax and scalar checks
/// Trim configuration whitespace without allocating a new string
[[nodiscard]] std::string_view trim(std::string_view value);
/// Store one source-located diagnostic and return false to the caller
bool fail(std::string &error, std::size_t line, std::string_view message);
[[nodiscard]] bool valid_name(std::string_view value) noexcept;
[[nodiscard]] bool valid_path(std::string_view value) noexcept;
/// Parse physical lines into named sections while preserving line numbers
[[nodiscard]] std::optional<std::vector<Section>>
read_document(std::istream &input, std::string &error);
[[nodiscard]] const Section *find_section(const std::vector<Section> &items,
                                          std::string_view name);
[[nodiscard]] const Field *find_field(const Section &section,
                                      std::string_view key);
[[nodiscard]] const Field *
require_field(const Section &section, std::string_view key, std::string &error);
[[nodiscard]] bool reject_unknown(const Section &section,
                                  std::initializer_list<std::string_view> keys,
                                  std::string &error);
[[nodiscard]] bool parse_u64(const Field &field, std::uint64_t minimum,
                             std::uint64_t &value, std::string &error);
[[nodiscard]] bool parse_i32(const Field &field, std::int32_t &value,
                             std::string &error);
[[nodiscard]] bool parse_number(const Field &field, bool allow_zero,
                                double &value, std::string &error);
[[nodiscard]] bool parse_finite_number(const Field &field, double &value,
                                       std::string &error);
[[nodiscard]] bool parse_bool(const Field &field, bool &value,
                              std::string &error);
[[nodiscard]] bool parse_shape(const Field &field, Shape &shape,
                               std::string &error);
[[nodiscard]] bool parse_motion(const Field &field, Motion &motion,
                                std::string &error);
[[nodiscard]] bool valid_colour(std::string_view value) noexcept;
[[nodiscard]] bool valid_text(std::string_view value,
                              std::size_t limit) noexcept;
[[nodiscard]] std::size_t find_name(const std::vector<std::string> &names,
                                    std::string_view name);

// Compilation stages
/// Select the update kernel named by the required scenario section
[[nodiscard]] std::optional<Kernel> compile_kernel(const Section &section,
                                                   std::string &error);
/// Compile dimensions, duration, seed, and boundary mode shared by all kernels
[[nodiscard]] bool compile_world(const Section &section, Kernel kernel,
                                 WorldConfig &world, std::string &error);
/// Compile snapshot stride and visual coordinate mode
[[nodiscard]] bool compile_output(const Section *section, Kernel kernel,
                                  std::uint64_t &stride, View &view,
                                  std::string &error);
/// Record the checked bundle-local Lua entry point
[[nodiscard]] bool compile_rules(const Section *section, Scenario &result,
                                 std::string &error);
/// Collect named presentation assets without loading their contents
[[nodiscard]] bool compile_assets(const std::vector<Section> &sections,
                                  Scenario &result, std::string &error);
/// Compile a dense byte grid and its allowed state range
[[nodiscard]] bool compile_cellular(const Section &section, Scenario &result,
                                    std::string &error);
/// Compile the dimensions of a turn-based board
[[nodiscard]] bool compile_turn(const Section &section, Scenario &result,
                                std::string &error);
/// Compile the PDE grid, time plan, fields, and sampling point
[[nodiscard]] bool compile_pde(const Section &section, Scenario &result,
                               std::string &error);
/// Resolve character and behaviour names into contiguous runtime plans
[[nodiscard]] bool
compile_characters(const std::vector<Section> &sections, Kernel kernel,
                   const std::vector<BehaviourDraft> &definitions,
                   Scenario &result, std::string &error);
/// Parse reusable named behaviours before character targets are resolved
[[nodiscard]] bool
compile_behaviour_definitions(const std::vector<Section> &sections,
                              std::vector<BehaviourDraft> &items,
                              std::string &error);
/// Split a comma list after checking empty and repeated entries
[[nodiscard]] bool split_list(const Field &field,
                              std::vector<std::string_view> &items,
                              std::string &error);

} // namespace m1::config_detail

#endif
