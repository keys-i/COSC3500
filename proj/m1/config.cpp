#include "model.hpp"

#include <algorithm>
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

namespace m1 {
namespace {

struct Field {
    std::string key;
    std::string value;
    std::size_t line = 0;
};

struct Section {
    std::string name;
    std::size_t line = 0;
    std::vector<Field> fields;
};

struct Draft {
    std::string name;
    std::size_t line = 0;
    std::optional<std::uint64_t> count;
    std::optional<double> speed;
    std::optional<double> sensing_radius;
    std::optional<double> capture_radius;
    std::optional<std::string> behaviours;
    std::optional<std::string> target;
    std::optional<double> x;
    std::optional<double> y;
    std::optional<bool> visible;
    std::optional<Shape> shape;
    std::optional<std::string> colour;
    std::optional<std::string> glyph;
    std::optional<std::int32_t> layer;
};

struct BehaviourDraft {
    std::string name;
    BehaviourCode code = BehaviourCode::idle;
    std::string target;
    double weight = 1.0;
    double parameter = 0.0;
};

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
    std::vector<Section> sections;
    std::string raw;
    std::size_t line_number = 0;
    while (std::getline(input, raw)) {
        ++line_number;
        if (!raw.empty() && raw.back() == '\r') {
            raw.pop_back();
        }
        const std::size_t comment = raw.find('#');
        const std::string_view line =
            trim(std::string_view(raw).substr(0U, comment));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[') {
            if (line.size() < 3U || line.back() != ']') {
                fail(error, line_number, "malformed section header");
                return std::nullopt;
            }
            const std::string name{trim(line.substr(1U, line.size() - 2U))};
            if (name.empty()) {
                fail(error, line_number, "empty section name");
                return std::nullopt;
            }
            for (const Section &section : sections) {
                if (section.name == name) {
                    fail(error, line_number, "duplicate section");
                    return std::nullopt;
                }
            }
            sections.push_back(Section{name, line_number, {}});
            continue;
        }
        if (sections.empty()) {
            fail(error, line_number, "field appears before a section");
            return std::nullopt;
        }
        const std::size_t separator = line.find('=');
        if (separator == std::string_view::npos) {
            fail(error, line_number, "expected key=value");
            return std::nullopt;
        }
        const std::string key{trim(line.substr(0U, separator))};
        const std::string value{trim(line.substr(separator + 1U))};
        if (!valid_name(key) || value.empty()) {
            fail(error, line_number, "invalid key=value field");
            return std::nullopt;
        }
        for (const Field &field : sections.back().fields) {
            if (field.key == key) {
                fail(error, line_number, "duplicate field");
                return std::nullopt;
            }
        }
        sections.back().fields.push_back(Field{key, value, line_number});
    }
    if (input.bad()) {
        fail(error, line_number + 1U, "cannot read scenario");
        return std::nullopt;
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

[[nodiscard]] bool parse_i64(const Field &field, std::int64_t &value,
                             std::string &error) {
    const char *const first = field.value.data();
    const char *const last = first + field.value.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        return fail(error, field.line, field.key + " must be an integer");
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

[[nodiscard]] bool parse_shape(const Field &field, Shape &shape,
                               std::string &error) {
    if (field.value == "circle") {
        shape = Shape::circle;
    } else if (field.value == "cell") {
        shape = Shape::cell;
    } else if (field.value == "text") {
        shape = Shape::text;
    } else if (field.value == "sprite") {
        shape = Shape::sprite;
    } else {
        return fail(error, field.line,
                    "shape must be circle, cell, text, or sprite");
    }
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

[[nodiscard]] std::size_t find_name(const std::vector<std::string> &names,
                                    const std::string_view name) {
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (names[index] == name) {
            return index;
        }
    }
    return names.size();
}

[[nodiscard]] std::optional<std::uint32_t>
intern_symbol(Scenario &scenario, const std::string_view value,
              std::string &error, const std::size_t line) {
    const std::size_t existing = find_name(scenario.symbols, value);
    if (existing != scenario.symbols.size()) {
        return static_cast<std::uint32_t>(existing);
    }
    if (scenario.symbols.size() >=
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        fail(error, line, "too many symbols");
        return std::nullopt;
    }
    scenario.symbols.emplace_back(value);
    return static_cast<std::uint32_t>(scenario.symbols.size() - 1U);
}

[[nodiscard]] std::size_t find_draft(const std::vector<Draft> &drafts,
                                     const std::string_view name) {
    for (std::size_t index = 0; index < drafts.size(); ++index) {
        if (drafts[index].name == name) {
            return index;
        }
    }
    return drafts.size();
}

[[nodiscard]] std::optional<Kernel> compile_kernel(const Section &section,
                                                   std::string &error) {
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
    fail(error, kernel->line,
         "kernel must be continuous, cellular, turn, or timeline");
    return std::nullopt;
}

[[nodiscard]] bool compile_world(const Section &section, const Kernel kernel,
                                 WorldConfig &world, std::string &error) {
    if (!reject_unknown(
            section,
            {"width", "height", "time_step", "steps", "seed", "boundary"},
            error)) {
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
    if (kernel == Kernel::continuous) {
        const Field *const step = require_field(section, "time_step", error);
        const Field *const seed = require_field(section, "seed", error);
        if (step == nullptr || seed == nullptr ||
            !parse_number(*step, false, world.time_step, error) ||
            !parse_u64(*seed, 0U, world.seed, error) ||
            boundary->value != "wrap") {
            return fail(error, boundary->line,
                        "continuous worlds require boundary=wrap");
        }
    } else if (boundary->value != "bounded" && boundary->value != "wrap") {
        return fail(error, boundary->line, "boundary must be bounded or wrap");
    }
    return true;
}

[[nodiscard]] bool compile_output(const Section *section, const Kernel kernel,
                                  std::uint64_t &stride, View &view,
                                  std::string &error) {
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

[[nodiscard]] bool read_optional_number(const Section &section,
                                        const std::string_view key,
                                        const bool allow_zero,
                                        std::optional<double> &result,
                                        std::string &error) {
    const Field *const field = find_field(section, key);
    if (field == nullptr) {
        return true;
    }
    double value = 0.0;
    if (!parse_number(*field, allow_zero, value, error)) {
        return false;
    }
    result = value;
    return true;
}

[[nodiscard]] bool read_draft(const Section &section, Draft &draft,
                              std::string &error) {
    if (!reject_unknown(section,
                        {"count", "speed", "sensing_radius", "capture_radius",
                         "behaviours", "target", "x", "y", "visible", "shape",
                         "colour", "glyph", "layer"},
                        error)) {
        return false;
    }
    constexpr std::string_view prefix{"character."};
    draft.name = section.name.substr(prefix.size());
    draft.line = section.line;
    if (!valid_name(draft.name)) {
        return fail(error, section.line, "invalid character name");
    }
    if (const Field *const field = find_field(section, "count")) {
        std::uint64_t value = 0;
        if (!parse_u64(*field, 1U, value, error)) {
            return false;
        }
        draft.count = value;
    }
    if (!read_optional_number(section, "speed", true, draft.speed, error) ||
        !read_optional_number(section, "sensing_radius", false,
                              draft.sensing_radius, error) ||
        !read_optional_number(section, "capture_radius", false,
                              draft.capture_radius, error) ||
        !read_optional_number(section, "x", true, draft.x, error) ||
        !read_optional_number(section, "y", true, draft.y, error)) {
        return false;
    }
    if (const Field *const field = find_field(section, "behaviours")) {
        draft.behaviours = field->value;
    }
    if (const Field *const field = find_field(section, "target")) {
        if (!valid_name(field->value)) {
            return fail(error, field->line, "invalid target character name");
        }
        draft.target = field->value;
    }
    if (const Field *const field = find_field(section, "visible")) {
        bool value = false;
        if (!parse_bool(*field, value, error)) {
            return false;
        }
        draft.visible = value;
    }
    if (const Field *const field = find_field(section, "shape")) {
        Shape value = Shape::circle;
        if (!parse_shape(*field, value, error)) {
            return false;
        }
        draft.shape = value;
    }
    if (const Field *const field = find_field(section, "colour")) {
        if (!valid_colour(field->value)) {
            return fail(error, field->line,
                        "colour must be six hexadecimal digits");
        }
        draft.colour = field->value;
    }
    if (const Field *const field = find_field(section, "glyph")) {
        if (field->value.size() > 16U ||
            field->value.find_first_of(",\"") != std::string::npos) {
            return fail(error, field->line, "glyph is not CSV-safe");
        }
        draft.glyph = field->value;
    }
    if (const Field *const field = find_field(section, "layer")) {
        std::int32_t value = 0;
        if (!parse_i32(*field, value, error)) {
            return false;
        }
        draft.layer = value;
    }
    return true;
}

[[nodiscard]] std::optional<BehaviourCode>
parse_behaviour_code(const Field &field, std::string &error) {
    if (field.value == "idle")
        return BehaviourCode::idle;
    if (field.value == "seek")
        return BehaviourCode::seek;
    if (field.value == "flee")
        return BehaviourCode::flee;
    if (field.value == "consume")
        return BehaviourCode::consume;
    if (field.value == "separate")
        return BehaviourCode::separate;
    if (field.value == "align")
        return BehaviourCode::align;
    if (field.value == "cohere")
        return BehaviourCode::cohere;
    if (field.value == "avoid")
        return BehaviourCode::avoid;
    if (field.value == "wander")
        return BehaviourCode::wander;
    if (field.value == "lua")
        return BehaviourCode::lua;
    fail(error, field.line, "unknown behaviour code: " + field.value);
    return std::nullopt;
}

[[nodiscard]] bool
compile_behaviour_definitions(const std::vector<Section> &sections,
                              std::vector<BehaviourDraft> &items,
                              std::string &error) {
    constexpr std::string_view prefix{"behaviour."};
    for (const Section &section : sections) {
        if (section.name.rfind(prefix, 0U) != 0U) {
            continue;
        }
        BehaviourDraft item;
        item.name = section.name.substr(prefix.size());
        if (!valid_name(item.name) ||
            !reject_unknown(section, {"code", "target", "weight", "parameter"},
                            error)) {
            return false;
        }
        const Field *const code = require_field(section, "code", error);
        if (code == nullptr) {
            return false;
        }
        const auto parsed = parse_behaviour_code(*code, error);
        if (!parsed) {
            return false;
        }
        item.code = *parsed;
        if (const Field *const field = find_field(section, "target")) {
            if (!valid_name(field->value)) {
                return fail(error, field->line, "invalid behaviour target");
            }
            item.target = field->value;
        }
        if (const Field *const field = find_field(section, "weight")) {
            if (!parse_number(*field, false, item.weight, error)) {
                return false;
            }
        }
        if (const Field *const field = find_field(section, "parameter")) {
            if (!parse_number(*field, true, item.parameter, error)) {
                return false;
            }
        }
        for (const BehaviourDraft &existing : items) {
            if (existing.name == item.name) {
                return fail(error, section.line, "duplicate behaviour name");
            }
        }
        items.push_back(std::move(item));
    }
    return true;
}

[[nodiscard]] bool split_list(const Field &field,
                              std::vector<std::string_view> &items,
                              std::string &error) {
    std::size_t begin = 0;
    while (begin <= field.value.size()) {
        const std::size_t comma = field.value.find(',', begin);
        const std::size_t length = comma == std::string::npos
                                       ? field.value.size() - begin
                                       : comma - begin;
        const std::string_view item =
            trim(std::string_view{field.value.data() + begin, length});
        if (item.empty()) {
            return fail(error, field.line, "list contains an empty item");
        }
        items.push_back(item);
        if (comma == std::string::npos) {
            return true;
        }
        begin = comma + 1U;
    }
    return false;
}

[[nodiscard]] bool
compile_behaviours(const Draft &draft,
                   const std::vector<BehaviourDraft> &definitions,
                   const std::vector<std::string> &names, Scenario &result,
                   CharacterPlan &plan, std::string &error) {
    plan.first_behaviour = result.behaviour_plan.size();
    if (!draft.behaviours) {
        return true;
    }
    Field field{"behaviours", *draft.behaviours, draft.line};
    std::vector<std::string_view> tokens;
    if (!split_list(field, tokens, error)) {
        return false;
    }
    for (const std::string_view token : tokens) {
        BehaviourRecord record;
        if (token == "idle") {
            record.code = BehaviourCode::idle;
        } else {
            std::size_t found = definitions.size();
            for (std::size_t index = 0; index < definitions.size(); ++index) {
                if (definitions[index].name == token) {
                    found = index;
                    break;
                }
            }
            if (found != definitions.size()) {
                const BehaviourDraft &source = definitions[found];
                record.code = source.code;
                record.weight = source.weight;
                record.parameter = source.parameter;
                if (!source.target.empty()) {
                    const std::size_t target = find_name(names, source.target);
                    if (target == names.size()) {
                        return fail(error, draft.line,
                                    "unknown behaviour target: " +
                                        source.target);
                    }
                    record.target = static_cast<std::uint32_t>(target);
                }
            } else {
                Field code{"behaviours", std::string(token), draft.line};
                const auto parsed = parse_behaviour_code(code, error);
                if (!parsed) {
                    return false;
                }
                record.code = *parsed;
            }
        }
        if (record.code == BehaviourCode::seek)
            plan.behaviours |= seek;
        if (record.code == BehaviourCode::flee)
            plan.behaviours |= flee;
        if (record.code == BehaviourCode::consume)
            plan.behaviours |= consume;
        result.behaviour_plan.push_back(record);
    }
    plan.behaviour_count = result.behaviour_plan.size() - plan.first_behaviour;
    return true;
}

[[nodiscard]] std::optional<ScalarKind> parse_scalar_kind(const Field &field,
                                                          std::string &error) {
    if (field.value == "bool")
        return ScalarKind::boolean;
    if (field.value == "int")
        return ScalarKind::integer;
    if (field.value == "number")
        return ScalarKind::number;
    if (field.value == "id")
        return ScalarKind::identifier;
    fail(error, field.line, "type must be bool, int, number, or id");
    return std::nullopt;
}

[[nodiscard]] bool compile_state(const std::vector<Section> &sections,
                                 const std::vector<std::string> &names,
                                 Scenario &result, std::string &error) {
    constexpr std::string_view field_prefix{"state."};
    constexpr std::string_view buffer_prefix{"buffer."};
    for (const Section &section : sections) {
        const bool scalar = section.name.rfind(field_prefix, 0U) == 0U;
        const bool buffer = section.name.rfind(buffer_prefix, 0U) == 0U;
        if (!scalar && !buffer) {
            continue;
        }
        const std::string name = section.name.substr(
            scalar ? field_prefix.size() : buffer_prefix.size());
        if (!valid_name(name)) {
            return fail(error, section.line, "invalid state name");
        }
        if (!reject_unknown(
                section,
                scalar
                    ? std::initializer_list<std::string_view>{"type", "value"}
                    : std::initializer_list<std::string_view>{"type",
                                                              "capacity"},
                error)) {
            return false;
        }
        const Field *const type = require_field(section, "type", error);
        if (type == nullptr) {
            return false;
        }
        const auto kind = parse_scalar_kind(*type, error);
        if (!kind) {
            return false;
        }
        if (scalar) {
            const Field *const value = require_field(section, "value", error);
            if (value == nullptr)
                return false;
            ScalarPlan item{name, *kind};
            if (*kind == ScalarKind::boolean) {
                if (!parse_bool(*value, item.boolean, error))
                    return false;
            } else if (*kind == ScalarKind::integer) {
                if (!parse_i64(*value, item.integer, error))
                    return false;
            } else if (*kind == ScalarKind::number) {
                if (!parse_finite_number(*value, item.number, error))
                    return false;
            } else {
                const std::size_t identifier = find_name(names, value->value);
                if (identifier == names.size()) {
                    return fail(error, value->line,
                                "id state must name a character");
                }
                item.identifier = static_cast<std::uint32_t>(identifier);
            }
            result.scalars.push_back(std::move(item));
        } else {
            const Field *const capacity =
                require_field(section, "capacity", error);
            std::uint64_t size = 0;
            if (capacity == nullptr || !parse_u64(*capacity, 1U, size, error) ||
                size > std::vector<std::uint8_t>{}.max_size()) {
                return fail(error, section.line,
                            "buffer capacity is too large");
            }
            result.buffers.push_back(
                BufferPlan{name, *kind, static_cast<std::size_t>(size)});
        }
    }
    return true;
}

[[nodiscard]] bool compile_rules(const Section *section, Scenario &result,
                                 std::string &error) {
    if (section == nullptr)
        return true;
    if (!reject_unknown(*section, {"file"}, error))
        return false;
    const Field *const file = require_field(*section, "file", error);
    if (file == nullptr)
        return false;
    if (!valid_path(file->value) || file->value.size() < 5U ||
        file->value.substr(file->value.size() - 4U) != ".lua") {
        return fail(error, file->line, "rules file must be a safe .lua path");
    }
    result.lua_rules = file->value;
    return true;
}

[[nodiscard]] bool compile_assets(const std::vector<Section> &sections,
                                  Scenario &result, std::string &error) {
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
            (kind->value != "image" && kind->value != "video" &&
             kind->value != "audio" && kind->value != "font")) {
            return fail(error, section.line,
                        "asset needs a safe file and kind");
        }
        result.assets.push_back(AssetPlan{name, file->value, kind->value});
    }
    return true;
}

[[nodiscard]] bool parse_count_mask(const Field &field, std::uint16_t &mask,
                                    std::string &error) {
    mask = 0U;
    std::vector<std::string_view> values;
    if (!split_list(field, values, error))
        return false;
    for (const std::string_view item : values) {
        std::uint64_t count = 0;
        const char *first = item.data();
        const char *last = first + item.size();
        const auto parsed = std::from_chars(first, last, count);
        if (parsed.ec != std::errc{} || parsed.ptr != last || count > 8U) {
            return fail(error, field.line, field.key + " values must be 0..8");
        }
        const std::uint16_t bit = static_cast<std::uint16_t>(1U << count);
        if ((mask & bit) != 0U) {
            return fail(error, field.line,
                        field.key + " has a duplicate value");
        }
        mask = static_cast<std::uint16_t>(mask | bit);
    }
    return true;
}

[[nodiscard]] bool compile_cellular(const Section &section, Scenario &result,
                                    std::string &error) {
    if (!reject_unknown(section,
                        {"states", "count_state", "boundary", "birth",
                         "survive", "cells", "transition"},
                        error))
        return false;
    const Field *const states = require_field(section, "states", error);
    const Field *const boundary = require_field(section, "boundary", error);
    const Field *const cells = require_field(section, "cells", error);
    if (states == nullptr || boundary == nullptr || cells == nullptr)
        return false;
    std::uint64_t count = 0;
    if (!parse_u64(*states, 2U, count, error) || count > 255U ||
        (boundary->value != "wrap" && boundary->value != "bounded")) {
        return fail(error, boundary->line,
                    "cellular boundary must be wrap or bounded");
    }
    const double columns = result.world.width;
    const double rows = result.world.height;
    if (std::floor(columns) != columns || std::floor(rows) != rows ||
        columns >
            static_cast<double>(std::numeric_limits<std::size_t>::max()) ||
        rows > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        return fail(error, section.line,
                    "cellular world dimensions must be integers");
    }
    result.cellular.columns = static_cast<std::size_t>(columns);
    result.cellular.rows = static_cast<std::size_t>(rows);
    if (result.cellular.columns == 0U ||
        result.cellular.rows >
            std::vector<std::uint8_t>{}.max_size() / result.cellular.columns) {
        return fail(error, section.line, "cellular grid is too large");
    }
    const std::size_t total = result.cellular.columns * result.cellular.rows;
    if (total > std::numeric_limits<std::uint32_t>::max()) {
        return fail(error, section.line,
                    "cellular grid exceeds index capacity");
    }
    if (cells->value.size() != total) {
        return fail(error, cells->line,
                    "cells must contain width*height states");
    }
    result.cellular.state_count = static_cast<std::uint8_t>(count);
    result.cellular.count_state = 1U;
    if (const Field *const count_state = find_field(section, "count_state")) {
        std::uint64_t state = 0;
        if (!parse_u64(*count_state, 0U, state, error) || state >= count) {
            return fail(error, count_state->line,
                        "count_state must be less than states");
        }
        result.cellular.count_state = static_cast<std::uint8_t>(state);
    }
    result.cellular.wraps = boundary->value == "wrap";
    result.cellular.initial.reserve(total);
    for (const char value : cells->value) {
        if (value < '0' || value > '9' ||
            static_cast<std::uint8_t>(value - '0') >=
                result.cellular.state_count) {
            return fail(error, cells->line,
                        "cell state is outside its declared range");
        }
        result.cellular.initial.push_back(
            static_cast<std::uint8_t>(value - '0'));
    }
    if (count == 2U) {
        const Field *const birth = require_field(section, "birth", error);
        const Field *const survive = require_field(section, "survive", error);
        if (birth == nullptr || survive == nullptr ||
            !parse_count_mask(*birth, result.cellular.birth_mask, error) ||
            !parse_count_mask(*survive, result.cellular.survive_mask, error)) {
            return false;
        }
        result.cellular.transition.resize(18U);
        for (std::size_t neighbours = 0; neighbours <= 8U; ++neighbours) {
            const std::uint16_t bit =
                static_cast<std::uint16_t>(1U << neighbours);
            result.cellular.transition[neighbours] =
                (result.cellular.birth_mask & bit) != 0U ? 1U : 0U;
            result.cellular.transition[9U + neighbours] =
                (result.cellular.survive_mask & bit) != 0U ? 1U : 0U;
        }
        return true;
    }
    const Field *const table = require_field(section, "transition", error);
    if (table == nullptr)
        return false;
    std::vector<std::string_view> entries;
    if (!split_list(*table, entries, error))
        return false;
    const std::size_t needed = static_cast<std::size_t>(count) * 9U;
    if (entries.size() != needed) {
        return fail(error, table->line,
                    "transition needs states*9 entries in state-major order");
    }
    result.cellular.transition.reserve(needed);
    for (const std::string_view entry : entries) {
        std::uint64_t next = 0;
        const char *first = entry.data();
        const char *last = first + entry.size();
        const auto parsed = std::from_chars(first, last, next);
        if (parsed.ec != std::errc{} || parsed.ptr != last || next >= count) {
            return fail(error, table->line,
                        "transition state is outside range");
        }
        result.cellular.transition.push_back(static_cast<std::uint8_t>(next));
    }
    return true;
}

[[nodiscard]] std::optional<SearchAlgorithm> parse_search(const Field &field,
                                                          std::string &error) {
    if (field.value == "none")
        return SearchAlgorithm::none;
    if (field.value == "bfs")
        return SearchAlgorithm::bfs;
    if (field.value == "dijkstra")
        return SearchAlgorithm::dijkstra;
    if (field.value == "astar")
        return SearchAlgorithm::astar;
    if (field.value == "alphabeta" || field.value == "mcts") {
        fail(error, field.line,
             "alpha-beta and MCTS need a concrete native game kernel");
        return std::nullopt;
    }
    if (field.value == "lua")
        return SearchAlgorithm::lua;
    fail(error, field.line, "unknown search algorithm");
    return std::nullopt;
}

[[nodiscard]] bool compile_turn(const Section &section, Scenario &result,
                                std::string &error) {
    if (!reject_unknown(section, {"topology", "search", "budget", "edges"},
                        error)) {
        return false;
    }
    const Field *const topology = require_field(section, "topology", error);
    const Field *const search = require_field(section, "search", error);
    const Field *const budget = require_field(section, "budget", error);
    if (topology == nullptr || search == nullptr || budget == nullptr)
        return false;
    if (std::floor(result.world.width) != result.world.width ||
        std::floor(result.world.height) != result.world.height ||
        result.world.width >
            static_cast<double>(std::numeric_limits<std::size_t>::max()) ||
        result.world.height >
            static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        return fail(error, section.line,
                    "turn world dimensions must be integers");
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
    const auto algorithm = parse_search(*search, error);
    if (!algorithm ||
        !parse_u64(*budget, 1U, result.turn.search_budget, error)) {
        return false;
    }
    result.turn.search = *algorithm;
    if (*algorithm == SearchAlgorithm::lua && result.lua_rules.empty()) {
        return fail(error, search->line, "search=lua needs a rules file");
    }
    if (topology->value == "grid") {
        if (find_field(section, "edges") != nullptr) {
            return fail(error, topology->line,
                        "grid turn board does not take edges");
        }
        return true;
    }
    if (topology->value != "graph") {
        return fail(error, topology->line, "topology must be grid or graph");
    }
    const Field *const edges = require_field(section, "edges", error);
    if (edges == nullptr)
        return false;
    std::vector<std::string_view> pairs;
    if (!split_list(*edges, pairs, error))
        return false;
    const std::size_t nodes = result.turn.columns * result.turn.rows;
    for (const std::string_view pair : pairs) {
        const std::size_t colon = pair.find(':');
        if (colon == std::string_view::npos ||
            pair.find(':', colon + 1U) != std::string_view::npos) {
            return fail(error, edges->line, "edges must be source:destination");
        }
        std::uint64_t source = 0;
        std::uint64_t destination = 0;
        const char *const middle = pair.begin() + colon;
        const auto left = std::from_chars(pair.begin(), middle, source);
        const auto right =
            std::from_chars(middle + 1U, pair.end(), destination);
        if (left.ec != std::errc{} || left.ptr != middle ||
            right.ec != std::errc{} || right.ptr != pair.end() ||
            source >= nodes || destination >= nodes || source == destination) {
            return fail(error, edges->line, "invalid graph edge");
        }
        result.turn.edges.push_back(static_cast<std::uint32_t>(source));
        result.turn.edges.push_back(static_cast<std::uint32_t>(destination));
    }
    return true;
}

[[nodiscard]] bool compile_events(const std::vector<Section> &sections,
                                  const std::vector<std::string> &names,
                                  Scenario &result, std::string &error) {
    constexpr std::string_view event_prefix{"event."};
    for (const Section &section : sections) {
        const bool event = section.name.rfind(event_prefix, 0U) == 0U;
        if (!event)
            continue;
        if (result.kernel != Kernel::timeline) {
            return fail(error, section.line,
                        "event sections need the timeline kernel");
        }
        if (!valid_name(section.name.substr(event_prefix.size())) ||
            !reject_unknown(section,
                            {"step", "action", "character", "x", "y", "value"},
                            error))
            return false;
        const Field *const step = require_field(section, "step", error);
        const Field *const kind = require_field(section, "action", error);
        if (step == nullptr || kind == nullptr)
            return false;
        TimelineEvent item;
        if (!parse_u64(*step, 1U, item.step, error) ||
            item.step > result.world.steps)
            return false;
        if (const Field *const character = find_field(section, "character")) {
            const std::size_t entity = find_name(names, character->value);
            if (entity == names.size()) {
                return fail(error, character->line, "unknown event character");
            }
            if (result.characters[entity].count != 1U) {
                return fail(error, character->line,
                            "event character must be a singleton");
            }
            item.entity = result.characters[entity].first;
        } else {
            return fail(error, section.line, "event needs character");
        }
        const Field *const x = find_field(section, "x");
        const Field *const y = find_field(section, "y");
        if (kind->value == "move") {
            if (x == nullptr || y == nullptr ||
                !parse_number(*x, true, item.x, error) ||
                !parse_number(*y, true, item.y, error) ||
                item.x >= result.world.width || item.y >= result.world.height) {
                return fail(error, kind->line, "invalid move event");
            }
        } else if (kind->value == "show") {
            item.action = EventAction::show;
        } else if (kind->value == "hide") {
            item.action = EventAction::hide;
        } else {
            return fail(error, kind->line, "unknown event action");
        }
        if (item.action != EventAction::move &&
            (x != nullptr || y != nullptr)) {
            return fail(error, kind->line, "only move events take x and y");
        }
        if (const Field *const value = find_field(section, "value")) {
            std::uint64_t parsed = 0;
            if (!parse_u64(*value, 0U, parsed, error) ||
                parsed > std::numeric_limits<std::uint32_t>::max()) {
                return false;
            }
            item.value = static_cast<std::uint32_t>(parsed);
        }
        result.events.push_back(item);
    }
    std::stable_sort(result.events.begin(), result.events.end(),
                     [](const TimelineEvent &left, const TimelineEvent &right) {
                         return left.step < right.step;
                     });
    return true;
}

[[nodiscard]] bool read_optional_finite(const Section &section,
                                        const std::string_view key,
                                        double &value, std::string &error) {
    const Field *const field = find_field(section, key);
    return field == nullptr || parse_finite_number(*field, value, error);
}

[[nodiscard]] std::size_t find_asset(const Scenario &scenario,
                                     const std::string_view name) {
    for (std::size_t index = 0; index < scenario.assets.size(); ++index) {
        if (scenario.assets[index].name == name) {
            return index;
        }
    }
    return scenario.assets.size();
}

[[nodiscard]] bool compile_actions(const std::vector<Section> &sections,
                                   Scenario &result, std::string &error) {
    constexpr std::string_view prefix{"action."};
    for (const Section &section : sections) {
        if (section.name.rfind(prefix, 0U) != 0U) {
            continue;
        }
        if (result.kernel != Kernel::turn) {
            return fail(error, section.line,
                        "action sections need the turn kernel");
        }
        if (!valid_name(section.name.substr(prefix.size())) ||
            !reject_unknown(
                section,
                {"step", "actor", "verb", "arg0", "arg1", "arg2", "arg3"},
                error)) {
            return false;
        }
        const Field *const step = require_field(section, "step", error);
        const Field *const actor = require_field(section, "actor", error);
        const Field *const verb = require_field(section, "verb", error);
        if (step == nullptr || actor == nullptr || verb == nullptr ||
            !valid_name(verb->value)) {
            return fail(error, section.line,
                        "action needs step, actor, and verb");
        }
        ActionPlan action;
        if (!parse_u64(*step, 1U, action.step, error) ||
            action.step > result.world.steps) {
            return false;
        }
        const std::size_t actor_id = find_name(result.names, actor->value);
        if (actor_id == result.names.size()) {
            return fail(error, actor->line, "unknown action actor");
        }
        if (result.characters[actor_id].count != 1U) {
            return fail(error, actor->line, "action actor must be a singleton");
        }
        if (result.characters[actor_id].first >
            std::numeric_limits<std::uint32_t>::max()) {
            return fail(error, actor->line, "action actor id is too large");
        }
        action.actor =
            static_cast<std::uint32_t>(result.characters[actor_id].first);
        const auto symbol =
            intern_symbol(result, verb->value, error, verb->line);
        if (!symbol) {
            return false;
        }
        action.verb = *symbol;
        for (std::size_t index = 0; index < action.arguments.size(); ++index) {
            const std::string key = "arg" + std::to_string(index);
            if (!read_optional_finite(section, key, action.arguments[index],
                                      error)) {
                return false;
            }
        }
        result.actions.push_back(action);
    }
    std::stable_sort(result.actions.begin(), result.actions.end(),
                     [](const ActionPlan &left, const ActionPlan &right) {
                         return left.step < right.step;
                     });
    return true;
}

[[nodiscard]] bool valid_cue_text(const std::string_view text) noexcept {
    return text.size() <= 512U &&
           text.find_first_of("\n\r,\"") == std::string_view::npos;
}

[[nodiscard]] bool valid_cue_kind(const std::string_view kind) noexcept {
    return kind == "camera" || kind == "caption" || kind == "sprite" ||
           kind == "video" || kind == "audio";
}

[[nodiscard]] bool compile_cues(const std::vector<Section> &sections,
                                Scenario &result, std::string &error) {
    constexpr std::string_view prefix{"cue."};
    for (const Section &section : sections) {
        if (section.name.rfind(prefix, 0U) != 0U) {
            continue;
        }
        if (!valid_name(section.name.substr(prefix.size())) ||
            !reject_unknown(section,
                            {"frame", "kind", "asset", "text", "x", "y",
                             "width", "height", "rotation", "scale", "opacity",
                             "duration", "volume", "layer"},
                            error)) {
            return false;
        }
        const Field *const frame = require_field(section, "frame", error);
        const Field *const kind = require_field(section, "kind", error);
        if (frame == nullptr || kind == nullptr ||
            !valid_cue_kind(kind->value)) {
            return fail(error, section.line, "cue needs frame and kind");
        }
        CuePlan cue;
        if (!parse_u64(*frame, 0U, cue.frame, error) ||
            cue.frame > result.world.steps) {
            return false;
        }
        const auto symbol =
            intern_symbol(result, kind->value, error, kind->line);
        if (!symbol) {
            return false;
        }
        cue.kind = *symbol;
        if (const Field *const asset = find_field(section, "asset")) {
            const std::size_t asset_id = find_asset(result, asset->value);
            if (asset_id == result.assets.size()) {
                return fail(error, asset->line, "unknown cue asset");
            }
            cue.asset = static_cast<std::uint32_t>(asset_id);
        }
        if (const Field *const text = find_field(section, "text")) {
            if (!valid_cue_text(text->value)) {
                return fail(error, text->line, "cue text is not CSV-safe");
            }
            cue.text = text->value;
        }
        const bool needs_asset = kind->value == "sprite" ||
                                 kind->value == "video" ||
                                 kind->value == "audio";
        if (needs_asset &&
            cue.asset == std::numeric_limits<std::uint32_t>::max()) {
            return fail(error, section.line, "cue kind needs an asset");
        }
        if (!needs_asset &&
            cue.asset != std::numeric_limits<std::uint32_t>::max()) {
            return fail(error, section.line,
                        "only sprite, video, and audio cues take assets");
        }
        if (needs_asset) {
            std::string_view expected{kind->value};
            if (kind->value == "sprite") {
                expected = "image";
            }
            if (result.assets[cue.asset].kind != expected) {
                return fail(error, section.line,
                            "cue kind does not match its asset");
            }
        }
        if (kind->value == "caption" && cue.text.empty()) {
            return fail(error, section.line, "caption cue needs text");
        }
        if (!read_optional_finite(section, "x", cue.x, error) ||
            !read_optional_finite(section, "y", cue.y, error) ||
            !read_optional_finite(section, "width", cue.width, error) ||
            !read_optional_finite(section, "height", cue.height, error) ||
            !read_optional_finite(section, "rotation", cue.rotation, error) ||
            !read_optional_finite(section, "scale", cue.scale, error) ||
            !read_optional_finite(section, "opacity", cue.opacity, error) ||
            !read_optional_finite(section, "duration", cue.duration, error) ||
            !read_optional_finite(section, "volume", cue.volume, error)) {
            return false;
        }
        if (cue.width < 0.0 || cue.height < 0.0 || cue.scale < 0.0 ||
            cue.opacity < 0.0 || cue.opacity > 1.0 || cue.duration < 0.0 ||
            cue.volume < 0.0) {
            return fail(error, section.line,
                        "cue has an invalid numeric range");
        }
        if (const Field *const layer = find_field(section, "layer")) {
            if (!parse_i32(*layer, cue.layer, error)) {
                return false;
            }
        }
        result.cue_names.push_back(section.name.substr(prefix.size()));
        result.cues.push_back(std::move(cue));
    }
    return true;
}

[[nodiscard]] bool
compile_characters(const std::vector<Section> &sections, const Kernel kernel,
                   const std::vector<BehaviourDraft> &definitions,
                   Scenario &result, std::string &error) {
    const Section *const characters = find_section(sections, "characters");
    if (characters == nullptr) {
        return kernel == Kernel::cellular || kernel == Kernel::turn;
    }
    if (!reject_unknown(*characters, {"count"}, error))
        return false;
    const Field *const declared = require_field(*characters, "count", error);
    std::uint64_t number = 0;
    if (declared == nullptr || !parse_u64(*declared, 1U, number, error)) {
        return false;
    }
    std::vector<Draft> drafts;
    constexpr std::string_view prefix{"character."};
    for (const Section &section : sections) {
        if (section.name.rfind(prefix, 0U) != 0U)
            continue;
        Draft item;
        if (!read_draft(section, item, error) ||
            find_draft(drafts, item.name) != drafts.size()) {
            return fail(error, section.line, "duplicate character name");
        }
        drafts.push_back(std::move(item));
    }
    if (number != drafts.size()) {
        return fail(error, characters->line,
                    "characters count does not match character sections");
    }
    for (const Draft &draft : drafts) {
        result.names.push_back(draft.name);
    }
    result.characters.reserve(drafts.size());
    result.styles.reserve(drafts.size());
    for (const Draft &draft : drafts) {
        if (!draft.count) {
            return fail(error, draft.line, "character needs count");
        }
        const bool positioned = draft.x.has_value() || draft.y.has_value();
        const double initial_x = draft.x.value_or(0.0);
        const double initial_y = draft.y.value_or(0.0);
        if (draft.x.has_value() != draft.y.has_value() ||
            (positioned && *draft.count != 1U)) {
            return fail(error, draft.line,
                        "x and y require a singleton character");
        }
        if (positioned && (initial_x >= result.world.width ||
                           initial_y >= result.world.height)) {
            return fail(error, draft.line, "character is outside the world");
        }
        if (*draft.count >
            std::numeric_limits<std::size_t>::max() - result.entity_count) {
            return fail(error, draft.line, "entity count overflows size_t");
        }
        CharacterPlan plan;
        plan.first = result.entity_count;
        plan.count = static_cast<std::size_t>(*draft.count);
        plan.initial_x = initial_x;
        plan.initial_y = initial_y;
        plan.positioned = positioned;
        plan.initial_alive = draft.visible.value_or(true);
        if (!compile_behaviours(draft, definitions, result.names, result, plan,
                                error))
            return false;
        if (draft.target) {
            const std::size_t target = find_name(result.names, *draft.target);
            if (target == result.names.size()) {
                return fail(error, draft.line,
                            "unknown target: " + *draft.target);
            }
            plan.target = static_cast<std::uint32_t>(target);
        } else {
            plan.target = std::numeric_limits<std::uint32_t>::max();
        }
        for (std::size_t index = plan.first_behaviour;
             index < plan.first_behaviour + plan.behaviour_count; ++index) {
            if (result.behaviour_plan[index].target ==
                std::numeric_limits<std::uint32_t>::max()) {
                result.behaviour_plan[index].target = plan.target;
            }
        }
        const bool active = plan.behaviours != 0U;
        const bool consumes = (plan.behaviours & consume) != 0U;
        if (kernel == Kernel::continuous) {
            if (!draft.speed ||
                (active && (!draft.target || !draft.sensing_radius)) ||
                (consumes != draft.capture_radius.has_value())) {
                return fail(
                    error, draft.line,
                    "continuous character has incomplete behaviour data");
            }
            const double speed = draft.speed.value_or(0.0);
            const double sensing_radius = draft.sensing_radius.value_or(0.0);
            const double capture_radius = draft.capture_radius.value_or(0.0);
            plan.step_distance = speed * result.world.time_step;
            plan.sensing_radius_squared =
                active ? sensing_radius * sensing_radius : 0.0;
            plan.capture_radius_squared =
                consumes ? capture_radius * capture_radius : 0.0;
            if (!std::isfinite(plan.step_distance) ||
                !std::isfinite(plan.sensing_radius_squared) ||
                !std::isfinite(plan.capture_radius_squared) ||
                (consumes &&
                 plan.capture_radius_squared > plan.sensing_radius_squared)) {
                return fail(error, draft.line,
                            "invalid derived continuous value");
            }
        } else if (draft.speed || draft.sensing_radius ||
                   draft.capture_radius || active) {
            return fail(error, draft.line,
                        "only continuous characters may have behaviours");
        }
        const Shape shape =
            draft.shape.value_or(draft.glyph ? Shape::text : Shape::circle);
        if (shape == Shape::text && !draft.glyph) {
            return fail(error, draft.line, "text shape needs glyph");
        }
        result.characters.push_back(plan);
        result.styles.push_back(RenderStyle{shape, draft.colour.value_or(""),
                                            draft.glyph.value_or(""),
                                            draft.layer.value_or(0)});
        result.entity_count += plan.count;
    }
    if (result.entity_count > std::vector<double>{}.max_size()) {
        return fail(error, characters->line,
                    "population exceeds addressable memory");
    }
    return kernel != Kernel::continuous ||
           result.entity_count <= std::numeric_limits<std::uint32_t>::max() ||
           fail(error, characters->line,
                "continuous population exceeds grid index capacity");
}

[[nodiscard]] bool known_section(const Section &section) {
    constexpr std::string_view prefixes[] = {
        "character.", "behaviour.", "state.",  "buffer.",
        "asset.",     "event.",     "action.", "cue.",
    };
    if (section.name == "scenario" || section.name == "world" ||
        section.name == "output" || section.name == "characters" ||
        section.name == "cellular" || section.name == "turn" ||
        section.name == "rules")
        return true;
    for (const std::string_view prefix : prefixes) {
        if (section.name.rfind(prefix, 0U) == 0U)
            return true;
    }
    return false;
}

[[nodiscard]] std::optional<Scenario>
compile_document(const std::vector<Section> &sections, std::string &error) {
    const Section *const scenario = find_section(sections, "scenario");
    const Section *const world = find_section(sections, "world");
    if (scenario == nullptr || world == nullptr) {
        fail(error, 1U, "scenario and world sections are required");
        return std::nullopt;
    }
    const auto kernel = compile_kernel(*scenario, error);
    if (!kernel)
        return std::nullopt;
    for (const Section &section : sections) {
        if (!known_section(section)) {
            fail(error, section.line, "unknown section: " + section.name);
            return std::nullopt;
        }
    }
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
    if (!compile_world(*world, *kernel, result.world, error) ||
        !compile_output(find_section(sections, "output"), *kernel,
                        result.snapshot_stride, result.view, error) ||
        !compile_rules(find_section(sections, "rules"), result, error) ||
        !compile_assets(sections, result, error)) {
        return std::nullopt;
    }
    std::vector<BehaviourDraft> definitions;
    if (!compile_behaviour_definitions(sections, definitions, error) ||
        !compile_characters(sections, *kernel, definitions, result, error)) {
        return std::nullopt;
    }
    if (!compile_state(sections, result.names, result, error)) {
        return std::nullopt;
    }
    if (*kernel == Kernel::cellular) {
        const Section *const cellular = find_section(sections, "cellular");
        if (cellular == nullptr ||
            !compile_cellular(*cellular, result, error)) {
            if (cellular == nullptr)
                fail(error, 1U, "cellular section is required");
            return std::nullopt;
        }
        result.entity_count = result.cellular.initial.size();
    } else if (*kernel == Kernel::turn) {
        const Section *const turn = find_section(sections, "turn");
        if (turn == nullptr || !compile_turn(*turn, result, error)) {
            if (turn == nullptr)
                fail(error, 1U, "turn section is required");
            return std::nullopt;
        }
    }
    if (!compile_events(sections, result.names, result, error) ||
        !compile_actions(sections, result, error) ||
        !compile_cues(sections, result, error))
        return std::nullopt;
    return result;
}

} // namespace

std::optional<Scenario> parse_scenario(std::istream &input,
                                       std::string &error) {
    const auto document = read_document(input, error);
    if (!document)
        return std::nullopt;
    return compile_document(*document, error);
}

} // namespace m1
