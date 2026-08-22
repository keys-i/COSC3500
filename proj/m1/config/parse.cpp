#include "detail.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <istream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// \file
/// Read text into source-located sections, then assemble one checked Scenario
namespace m1::config_detail {

[[nodiscard]] std::string_view trim(std::string_view value) {
    constexpr std::string_view whitespace{" \t\v\f"};
    const std::size_t first = value.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1U);
}

bool fail(std::string &error, const std::size_t line,
          const std::string_view message) {
    error = std::to_string(line) + ": " + std::string(message);
    return false;
}

[[nodiscard]] bool valid_name(const std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    for (const char character : value) {
        const bool letter = (character >= 'a' && character <= 'z') ||
                            (character >= 'A' && character <= 'Z');
        const bool digit = character >= '0' && character <= '9';
        if (!letter && !digit && character != '_' && character != '-') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_path(const std::string_view value) noexcept {
    // Accept only scenario-relative paths
    if (value.empty() || value.front() == '/' || value.front() == '\\' ||
        value.find('\\') != std::string_view::npos ||
        value.find(':') != std::string_view::npos) {
        return false;
    }
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t slash = value.find('/', begin);
        const std::size_t size = slash == std::string_view::npos
                                     ? value.size() - begin
                                     : slash - begin;
        const std::string_view part{value.data() + begin, size};
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        if (slash == std::string_view::npos) {
            return true;
        }
        begin = slash + 1U;
    }
    return false;
}

[[nodiscard]] std::optional<std::vector<Section>>
read_document(std::istream &input, std::string &error) {
    // Phase 1: retain the original line for every non-empty logical record
    // Collapse continued physical lines before interpreting sections and fields
    std::vector<Section> sections;
    std::vector<std::pair<std::size_t, std::string>> lines;
    std::string raw;
    std::string continued;
    std::size_t line_number = 0;
    std::size_t continued_line = 0;
    while (std::getline(input, raw)) {
        ++line_number;
        if (!raw.empty() && raw.back() == '\r') {
            raw.pop_back();
        }
        const std::size_t comment = raw.find('#');
        std::string_view line = trim(std::string_view(raw).substr(0U, comment));
        if (line.empty()) {
            continue;
        }
        if (continued.empty()) {
            continued_line = line_number;
        }
        const bool more = line.back() == '\\';
        if (more) {
            line = trim(line.substr(0U, line.size() - 1U));
        }
        continued.append(line);
        if (more) {
            continue;
        }
        lines.emplace_back(continued_line, std::move(continued));
        continued.clear();
    }
    if (input.bad()) {
        fail(error, line_number + 1U, "cannot read scenario");
        return std::nullopt;
    }
    // Report a dangling continuation at the line where it started
    if (!continued.empty()) {
        fail(error, continued_line, "unfinished line continuation");
        return std::nullopt;
    }
    // This pass checks syntax; kernel-specific checks run during compilation
    for (const auto &[source_line, text] : lines) {
        const std::string_view line{text};
        if (line.front() == '[') {
            if (line.size() < 3U || line.back() != ']') {
                fail(error, source_line, "malformed section header");
                return std::nullopt;
            }
            const std::string name{trim(line.substr(1U, line.size() - 2U))};
            if (name.empty()) {
                fail(error, source_line, "empty section name");
                return std::nullopt;
            }
            for (const Section &section : sections) {
                if (section.name == name) {
                    fail(error, source_line, "duplicate section");
                    return std::nullopt;
                }
            }
            sections.push_back(Section{name, source_line, {}});
            continue;
        }
        if (sections.empty()) {
            fail(error, source_line, "field appears before a section");
            return std::nullopt;
        }
        const std::size_t separator = line.find('=');
        if (separator == std::string_view::npos) {
            fail(error, source_line, "expected key=value");
            return std::nullopt;
        }
        const std::string key{trim(line.substr(0U, separator))};
        const std::string value{trim(line.substr(separator + 1U))};
        if (!valid_name(key) || value.empty()) {
            fail(error, source_line, "invalid key=value field");
            return std::nullopt;
        }
        for (const Field &field : sections.back().fields) {
            if (field.key == key) {
                fail(error, source_line, "duplicate field");
                return std::nullopt;
            }
        }
        sections.back().fields.push_back(Field{key, value, source_line});
    }
    if (sections.empty()) {
        fail(error, 1U, "empty scenario");
        return std::nullopt;
    }
    return sections;
}

[[nodiscard]] const Section *find_section(const std::vector<Section> &items,
                                          const std::string_view name) {
    for (const Section &section : items) {
        if (section.name == name) {
            return &section;
        }
    }
    return nullptr;
}

[[nodiscard]] const Field *find_field(const Section &section,
                                      const std::string_view key) {
    for (const Field &field : section.fields) {
        if (field.key == key) {
            return &field;
        }
    }
    return nullptr;
}

[[nodiscard]] const Field *require_field(const Section &section,
                                         const std::string_view key,
                                         std::string &error) {
    const Field *const field = find_field(section, key);
    if (field == nullptr) {
        fail(error, section.line, "missing field: " + std::string(key));
    }
    return field;
}

[[nodiscard]] bool
reject_unknown(const Section &section,
               const std::initializer_list<std::string_view> keys,
               std::string &error) {
    for (const Field &field : section.fields) {
        bool known = false;
        for (const std::string_view key : keys) {
            known = known || field.key == key;
        }
        if (!known) {
            return fail(error, field.line, "unknown field: " + field.key);
        }
    }
    return true;
}

// Scalar parsers leave policy to each compile stage and attach errors to fields
[[nodiscard]] bool parse_u64(const Field &field, const std::uint64_t minimum,
                             std::uint64_t &value, std::string &error) {
    const char *const first = field.value.data();
    const char *const last = first + field.value.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last || value < minimum) {
        return fail(error, field.line,
                    field.key +
                        " must be an integer >= " + std::to_string(minimum));
    }
    return true;
}

[[nodiscard]] bool parse_i32(const Field &field, std::int32_t &value,
                             std::string &error) {
    const char *const first = field.value.data();
    const char *const last = first + field.value.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        return fail(error, field.line, field.key + " must be an integer");
    }
    return true;
}

[[nodiscard]] bool parse_number(const Field &field, const bool allow_zero,
                                double &value, std::string &error) {
    // strtod gives the required full-token and range checks for decimal input
    char *end = nullptr;
    errno = 0;
    value = std::strtod(field.value.c_str(), &end);
    const bool invalid = allow_zero ? value < 0.0 : value <= 0.0;
    if (errno == ERANGE || end == field.value.c_str() || *end != '\0' ||
        !std::isfinite(value) || invalid) {
        return fail(error, field.line,
                    field.key + " must be a finite " +
                        (allow_zero ? "non-negative" : "positive") + " number");
    }
    return true;
}

[[nodiscard]] bool parse_finite_number(const Field &field, double &value,
                                       std::string &error) {
    char *end = nullptr;
    errno = 0;
    value = std::strtod(field.value.c_str(), &end);
    if (errno == ERANGE || end == field.value.c_str() || *end != '\0' ||
        !std::isfinite(value)) {
        return fail(error, field.line, field.key + " must be a finite number");
    }
    return true;
}

[[nodiscard]] bool parse_bool(const Field &field, bool &value,
                              std::string &error) {
    if (field.value == "true") {
        value = true;
        return true;
    }
    if (field.value == "false") {
        value = false;
        return true;
    }
    return fail(error, field.line, field.key + " must be true or false");
}

template <class Name, class T, std::size_t N>
[[nodiscard]] std::optional<T>
named_value(const std::string_view value,
            const std::array<std::pair<Name, T>, N> &values) noexcept {
    for (const auto &[name, result] : values) {
        if (value == std::string_view{name}) {
            return result;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool parse_shape(const Field &field, Shape &shape,
                               std::string &error) {
    static constexpr std::array values{
        std::pair{"circle", Shape::circle}, std::pair{"cell", Shape::cell},
        std::pair{"text", Shape::text}, std::pair{"icon", Shape::icon},
        std::pair{"sprite", Shape::sprite}};
    const auto parsed = named_value(std::string_view{field.value}, values);
    if (!parsed) {
        return fail(error, field.line,
                    "shape must be circle, cell, text, icon, or sprite");
    }
    shape = *parsed;
    return true;
}

[[nodiscard]] bool parse_motion(const Field &field, Motion &motion,
                                std::string &error) {
    static constexpr std::array values{
        std::pair{"static", Motion::static_},
        std::pair{"grounded", Motion::grounded},
        std::pair{"ballistic", Motion::ballistic},
        std::pair{"flight", Motion::flight}, std::pair{"water", Motion::water}};
    const auto parsed = named_value(std::string_view{field.value}, values);
    if (!parsed) {
        return fail(error, field.line,
                    "motion must be static, grounded, ballistic, flight, "
                    "or water");
    }
    motion = *parsed;
    return true;
}

[[nodiscard]] bool valid_colour(const std::string_view value) noexcept {
    if (value.size() != 6U) {
        return false;
    }
    for (const char digit : value) {
        const bool decimal = digit >= '0' && digit <= '9';
        const bool lower = digit >= 'a' && digit <= 'f';
        const bool upper = digit >= 'A' && digit <= 'F';
        if (!decimal && !lower && !upper) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_text(const std::string_view value,
                              const std::size_t limit) noexcept {
    return !value.empty() && value.size() <= limit &&
           value.find_first_of("\n\r") == std::string_view::npos;
}

[[nodiscard]] std::size_t find_name(const std::vector<std::string> &names,
                                    const std::string_view name) {
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (names[index] == name) {
            return index;
        }
    }
    return names.size();
}

namespace {

// Phase 2: check document-wide structure and dispatch the kernel compilers
// Prefixes describe repeated section families without accepting arbitrary names
[[nodiscard]] bool known_section(const Section &section) {
    constexpr std::string_view prefixes[]{"character.", "behaviour.", "asset.",
                                          "cue."};
    if (section.name == "scenario" || section.name == "world" ||
        section.name == "output" || section.name == "presentation" ||
        section.name == "characters" || section.name == "cellular" ||
        section.name == "turn" || section.name == "pde" ||
        section.name == "rules")
        return true;
    for (const std::string_view prefix : prefixes)
        if (section.name.rfind(prefix, 0U) == 0U)
            return true;
    return false;
}

[[nodiscard]] std::optional<Scenario>
compile_document(const std::vector<Section> &sections, std::string &error) {
    // Required sections establish the common data needed by every kernel
    const Section *const scenario = find_section(sections, "scenario");
    const Section *const world = find_section(sections, "world");
    if (scenario == nullptr || world == nullptr) {
        fail(error, 1U, "scenario and world sections are required");
        return std::nullopt;
    }
    const auto kernel = compile_kernel(*scenario, error);
    if (!kernel)
        return std::nullopt;
    // Reject incompatible sections before allocating or deriving runtime data
    for (const Section &section : sections) {
        if (!known_section(section)) {
            fail(error, section.line, "unknown section: " + section.name);
            return std::nullopt;
        }
        if (*kernel != Kernel::continuous &&
            section.name.rfind("behaviour.", 0U) == 0U) {
            fail(error, section.line,
                 "behaviour sections need the continuous kernel");
            return std::nullopt;
        }
        if (*kernel == Kernel::pde &&
            (section.name == "characters" ||
             section.name.rfind("character.", 0U) == 0U)) {
            fail(error, section.line, "PDE scenarios cannot have characters");
            return std::nullopt;
        }
    }
    // From here on, result contains validated runtime data rather than raw text
    Scenario result;
    result.kernel = *kernel;
    if (*kernel != Kernel::cellular &&
        find_section(sections, "cellular") != nullptr) {
        fail(error, find_section(sections, "cellular")->line,
             "cellular section needs the cellular kernel");
        return std::nullopt;
    }
    if (*kernel != Kernel::turn && find_section(sections, "turn") != nullptr) {
        fail(error, find_section(sections, "turn")->line,
             "turn section needs the turn kernel");
        return std::nullopt;
    }
    if (*kernel != Kernel::pde && find_section(sections, "pde") != nullptr) {
        fail(error, find_section(sections, "pde")->line,
             "pde section needs the pde kernel");
        return std::nullopt;
    }
    // Compile independent common sections before plans that refer to them
    if (!compile_world(*world, *kernel, result.world, error) ||
        !compile_output(find_section(sections, "output"), *kernel,
                        result.snapshot_stride, result.view, error) ||
        !compile_rules(find_section(sections, "rules"), result, error) ||
        !compile_assets(sections, result, error))
        return std::nullopt;
    if (*kernel == Kernel::pde && result.lua_rules.empty()) {
        fail(error, 1U, "pde scenarios require a rules file");
        return std::nullopt;
    }
    // Definitions precede characters so behaviour names can resolve by index
    std::vector<BehaviourDraft> definitions;
    if (!compile_behaviour_definitions(sections, definitions, error) ||
        !compile_characters(sections, *kernel, definitions, result, error))
        return std::nullopt;
    // Kernel sections add only the state their corresponding loop consumes
    if (*kernel == Kernel::cellular) {
        const Section *const cellular = find_section(sections, "cellular");
        if (cellular == nullptr ||
            !compile_cellular(*cellular, result, error)) {
            if (cellular == nullptr)
                fail(error, 1U, "cellular section is required");
            return std::nullopt;
        }
        result.entity_count = result.cellular.columns * result.cellular.rows;
    } else if (*kernel == Kernel::turn) {
        const Section *const turn = find_section(sections, "turn");
        if (turn == nullptr || !compile_turn(*turn, result, error)) {
            if (turn == nullptr)
                fail(error, 1U, "turn section is required");
            return std::nullopt;
        }
    } else if (*kernel == Kernel::pde) {
        const Section *const pde = find_section(sections, "pde");
        if (pde == nullptr || !compile_pde(*pde, result, error)) {
            if (pde == nullptr)
                fail(error, 1U, "pde section is required");
            return std::nullopt;
        }
    }
    return result;
}

} // namespace

} // namespace m1::config_detail

namespace m1 {

// The public entry point keeps text parsing and semantic compilation together
std::optional<Scenario> parse_scenario(std::istream &input,
                                       std::string &error) {
    // Keep parsing separate from compilation so errors retain source lines
    const auto document = config_detail::read_document(input, error);
    return document ? config_detail::compile_document(*document, error)
                    : std::nullopt;
}

} // namespace m1
