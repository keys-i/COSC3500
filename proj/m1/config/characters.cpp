#include "detail.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

/// \file
/// Compile character and behaviour sections into index-based runtime plans
namespace m1::config_detail {

#ifndef M1_WIDE_GRID
#define M1_WIDE_GRID 0
#endif

#ifndef M1_OPT_LEVEL
#define M1_OPT_LEVEL 0
#endif

#if M1_OPT_LEVEL <= 1 || M1_WIDE_GRID
constexpr std::uint64_t continuous_grid_index_limit =
    std::numeric_limits<std::uint64_t>::max();
#else
constexpr std::uint64_t continuous_grid_index_limit =
    std::numeric_limits<std::uint32_t>::max();
#endif

// Translate config spellings once instead of scattering string comparisons
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

[[nodiscard]] std::size_t find_draft(const std::vector<Draft> &drafts,
                                     const std::string_view name) {
    for (std::size_t index = 0; index < drafts.size(); ++index) {
        if (drafts[index].name == name) {
            return index;
        }
    }
    return drafts.size();
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

// Phase 3: read permissive character fields into a draft with source locations
[[nodiscard]] bool read_draft(const Section &section, Draft &draft,
                              std::string &error) {
    // Drafts keep optional fields until the kernel makes their meaning clear
    if (!reject_unknown(section,
                        {"count",
                         "capacity",
                         "speed",
                         "sensing_radius",
                         "capture_radius",
                         "max_steering",
                         "obstacle_radius",
                         "behaviours",
                         "target",
                         "x",
                         "y",
                         "visible",
                         "shape",
                         "colour",
                         "glyph",
                         "layer",
                         "size",
                         "label",
                         "sprite",
                         "sprite_north",
                         "sprite_south",
                         "motion"},
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
    if (const Field *const field = find_field(section, "capacity")) {
        std::uint64_t value = 0U;
        if (!parse_u64(*field, 1U, value, error)) {
            return false;
        }
        draft.capacity = value;
    }
    if (!read_optional_number(section, "speed", true, draft.speed, error) ||
        !read_optional_number(section, "sensing_radius", false,
                              draft.sensing_radius, error) ||
        !read_optional_number(section, "capture_radius", false,
                              draft.capture_radius, error) ||
        !read_optional_number(section, "max_steering", false,
                              draft.max_steering, error) ||
        !read_optional_number(section, "obstacle_radius", false,
                              draft.obstacle_radius, error) ||
        !read_optional_number(section, "x", true, draft.x, error) ||
        !read_optional_number(section, "y", true, draft.y, error) ||
        !read_optional_number(section, "size", false, draft.size, error)) {
        return false;
    }
    if (draft.size && *draft.size > 64.0) {
        return fail(error, section.line, "size must not exceed 64");
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
        if (!valid_text(field->value, 16U)) {
            return fail(error, field->line, "glyph is not CSV-safe");
        }
        draft.glyph = field->value;
    }
    if (const Field *const field = find_field(section, "label")) {
        if (!valid_text(field->value, 48U)) {
            return fail(error, field->line, "label is not CSV-safe");
        }
        draft.label = field->value;
    }
    if (const Field *const field = find_field(section, "sprite")) {
        if (!valid_name(field->value)) {
            return fail(error, field->line, "invalid sprite asset name");
        }
        draft.sprite = field->value;
    }
    for (const auto &[key, destination] : {
             std::pair{"sprite_north", &draft.sprite_north},
             std::pair{"sprite_south", &draft.sprite_south},
         }) {
        if (const Field *const field = find_field(section, key)) {
            if (!valid_name(field->value)) {
                return fail(error, field->line,
                            "invalid directional sprite asset name");
            }
            *destination = field->value;
        }
    }
    if (const Field *const field = find_field(section, "motion")) {
        Motion value = Motion::static_;
        if (!parse_motion(*field, value, error)) {
            return false;
        }
        draft.motion = value;
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
    static constexpr std::array values{
        std::pair{"idle", BehaviourCode::idle},
        std::pair{"seek", BehaviourCode::seek},
        std::pair{"flee", BehaviourCode::flee},
        std::pair{"pursue", BehaviourCode::pursue},
        std::pair{"evade", BehaviourCode::evade},
        std::pair{"consume", BehaviourCode::consume},
        std::pair{"separate", BehaviourCode::separate},
        std::pair{"align", BehaviourCode::align},
        std::pair{"cohere", BehaviourCode::cohere},
        std::pair{"avoid", BehaviourCode::avoid},
        std::pair{"wander", BehaviourCode::wander},
    };
    const auto parsed = named_value(std::string_view{field.value}, values);
    if (parsed) {
        return parsed;
    }
    fail(error, field.line, "unknown behaviour code: " + field.value);
    return std::nullopt;
}

[[nodiscard]] bool
compile_behaviour_definitions(const std::vector<Section> &sections,
                              std::vector<BehaviourDraft> &items,
                              std::string &error) {
    // Named definitions are shared by character behaviour lists
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
    // Each character stores a slice into the scenario-wide behaviour array
    plan.first_behaviour = result.behaviour_plan.size();
    if (!draft.behaviours) {
        return true;
    }
    Field field{"behaviours", *draft.behaviours, draft.line};
    std::vector<std::string_view> tokens;
    if (!split_list(field, tokens, error)) {
        return false;
    }
    // Expand each name into the compact records consumed by the continuous
    // kernel
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
                // Distance-based rules compare squared distances at runtime
                if (record.code == BehaviourCode::separate ||
                    record.code == BehaviourCode::avoid) {
                    record.parameter = source.parameter * source.parameter;
                } else if (record.code == BehaviourCode::pursue ||
                           record.code == BehaviourCode::evade) {
                    record.parameter = source.parameter;
                } else if (source.parameter != 0.0) {
                    return fail(error, draft.line,
                                "parameter does not apply to this behaviour");
                }
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
        // Record the broad capabilities needed to validate this character plan
        if (record.code == BehaviourCode::seek ||
            record.code == BehaviourCode::pursue) {
            plan.behaviours |= seek | sense;
        } else if (record.code == BehaviourCode::flee ||
                   record.code == BehaviourCode::evade) {
            plan.behaviours |= flee | sense;
        } else if (record.code == BehaviourCode::consume) {
            plan.behaviours |= consume | sense;
        } else if (record.code == BehaviourCode::separate ||
                   record.code == BehaviourCode::align ||
                   record.code == BehaviourCode::cohere ||
                   record.code == BehaviourCode::avoid) {
            plan.behaviours |= sense;
        }
        result.behaviour_plan.push_back(record);
    }
    plan.behaviour_count = result.behaviour_plan.size() - plan.first_behaviour;
    return true;
}
[[nodiscard]] bool
compile_characters(const std::vector<Section> &sections, const Kernel kernel,
                   const std::vector<BehaviourDraft> &definitions,
                   Scenario &result, std::string &error) {
    // Phase 4: validate drafts and append contiguous runtime character plans
    const Section *const characters = find_section(sections, "characters");
    if (characters == nullptr) {
        return kernel == Kernel::cellular || kernel == Kernel::turn ||
               kernel == Kernel::pde ||
               fail(error, 1U, "characters section is required");
    }
    if (kernel == Kernel::pde) {
        return fail(error, characters->line,
                    "PDE scenarios cannot have characters");
    }
    if (!reject_unknown(*characters, {"count"}, error))
        return false;
    const Field *const declared = require_field(*characters, "count", error);
    std::uint64_t number = 0;
    if (declared == nullptr || !parse_u64(*declared, 1U, number, error)) {
        return false;
    }
    // First collect every name, then resolve targets against the complete set
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
    // Names must be stable because plans and behaviours store their indices
    for (const Draft &draft : drafts) {
        result.names.push_back(draft.name);
    }
    result.characters.reserve(drafts.size());
    result.styles.reserve(drafts.size());
    for (const Draft &draft : drafts) {
        if (!draft.count) {
            return fail(error, draft.line, "character needs count");
        }
        const std::uint64_t capacity = draft.capacity.value_or(*draft.count);
        if (capacity < *draft.count) {
            return fail(error, draft.line, "capacity must not be below count");
        }
        if (capacity > std::numeric_limits<std::size_t>::max()) {
            return fail(error, draft.line,
                        "capacity exceeds addressable memory");
        }
        if (kernel != Kernel::continuous &&
            (draft.capacity || draft.behaviours || draft.target ||
             draft.speed || draft.sensing_radius || draft.capture_radius ||
             draft.max_steering || draft.obstacle_radius)) {
            return fail(error, draft.line,
                        "simulation fields need the continuous kernel");
        }
        if (kernel == Kernel::turn && (draft.x || draft.y || draft.visible)) {
            return fail(error, draft.line,
                        "turn characters may only define render fields");
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
        if (capacity >
            std::numeric_limits<std::size_t>::max() - result.entity_count) {
            return fail(error, draft.line, "entity count overflows size_t");
        }
        // A plan keeps counts and offsets while arrays hold per-entity state
        CharacterPlan plan;
        plan.first = result.entity_count;
        plan.count = static_cast<std::size_t>(capacity);
        plan.initial_count = static_cast<std::size_t>(*draft.count);
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
        // A behaviour without its own target inherits the character target
        for (std::size_t index = plan.first_behaviour;
             index < plan.first_behaviour + plan.behaviour_count; ++index) {
            if (result.behaviour_plan[index].target ==
                std::numeric_limits<std::uint32_t>::max()) {
                result.behaviour_plan[index].target = plan.target;
            }
        }
        const bool active = (plan.behaviours & sense) != 0U;
        const bool consumes = (plan.behaviours & consume) != 0U;
        const bool compiled_rules =
            !result.lua_rules.empty() && draft.sensing_radius.has_value();
        if (kernel == Kernel::continuous) {
            if ((result.lua_rules.empty() && !draft.speed) ||
                (active && (!draft.target || !draft.sensing_radius)) ||
                (result.lua_rules.empty() && !active &&
                 (draft.target.has_value() !=
                  draft.sensing_radius.has_value())) ||
                (!compiled_rules &&
                 consumes != draft.capture_radius.has_value())) {
                return fail(
                    error, draft.line,
                    "continuous character has incomplete behaviour data");
            }
            const double speed = draft.speed.value_or(0.0);
            const double sensing_radius = draft.sensing_radius.value_or(0.0);
            const double capture_radius = draft.capture_radius.value_or(0.0);
            // Precompute repeated products for the simulation loop
            plan.step_distance = speed * result.world.time_step;
            plan.sensing_radius_squared = (active || compiled_rules)
                                              ? sensing_radius * sensing_radius
                                              : 0.0;
            plan.capture_radius_squared = (consumes || compiled_rules)
                                              ? capture_radius * capture_radius
                                              : 0.0;
            if (!std::isfinite(plan.step_distance) ||
                !std::isfinite(plan.sensing_radius_squared) ||
                !std::isfinite(plan.capture_radius_squared) ||
                ((consumes || compiled_rules) &&
                 plan.capture_radius_squared > plan.sensing_radius_squared)) {
                return fail(error, draft.line,
                            "invalid derived continuous value");
            }
            plan.max_steering = draft.max_steering.value_or(0.0);
            plan.obstacle_radius = draft.obstacle_radius.value_or(0.0);
            if (plan.obstacle_radius != 0.0 && plan.step_distance != 0.0) {
                return fail(error, draft.line, "hard obstacle must be static");
            }
        }
        const Shape shape =
            draft.shape.value_or(draft.glyph ? Shape::text : Shape::circle);
        if ((shape == Shape::text || shape == Shape::icon) && !draft.glyph) {
            return fail(error, draft.line, "text and icon shapes need glyph");
        }
        // Missing and invalid names use distinct sentinels for the checks below
        const auto image_asset = [&](const std::optional<std::string> &name) {
            if (!name) {
                return std::numeric_limits<std::uint32_t>::max();
            }
            const auto asset = std::find_if(
                result.assets.begin(), result.assets.end(),
                [&](const AssetPlan &item) { return item.name == *name; });
            if (asset == result.assets.end() || asset->kind != "image") {
                return std::numeric_limits<std::uint32_t>::max() - 1U;
            }
            return static_cast<std::uint32_t>(asset - result.assets.begin());
        };
        const std::uint32_t sprite = image_asset(draft.sprite);
        const std::uint32_t sprite_north = image_asset(draft.sprite_north);
        const std::uint32_t sprite_south = image_asset(draft.sprite_south);
        if (sprite == std::numeric_limits<std::uint32_t>::max() - 1U ||
            sprite_north == std::numeric_limits<std::uint32_t>::max() - 1U ||
            sprite_south == std::numeric_limits<std::uint32_t>::max() - 1U) {
            return fail(error, draft.line, "sprite must name an image asset");
        }
        if ((shape == Shape::sprite) != draft.sprite.has_value()) {
            return fail(error, draft.line,
                        "sprite shape and sprite asset must be used together");
        }
        if (draft.sprite_north.has_value() != draft.sprite_south.has_value() ||
            ((draft.sprite_north || draft.sprite_south) &&
             shape != Shape::sprite)) {
            return fail(
                error, draft.line,
                "directional sprites need sprite shape and both directions");
        }
        result.characters.push_back(plan);
        result.styles.push_back(RenderStyle{
            shape, draft.colour.value_or(""), draft.glyph.value_or(""),
            draft.layer.value_or(0), draft.size.value_or(1.0),
            draft.label.value_or(""), sprite, sprite_north, sprite_south,
            draft.motion.value_or(Motion::static_)});
        result.entity_count += plan.count;
    }
    if (result.entity_count > std::vector<double>{}.max_size()) {
        return fail(error, characters->line,
                    "population exceeds addressable memory");
    }
    return kernel != Kernel::continuous ||
           result.entity_count <= continuous_grid_index_limit ||
           fail(error, characters->line,
                "continuous population exceeds grid index capacity");
}

} // namespace m1::config_detail
