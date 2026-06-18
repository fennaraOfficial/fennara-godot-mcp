#include "fennara/common/variant_utils.hpp"

namespace fennara::variant_utils {

bool get_bool(const godot::Dictionary &dict,
              const godot::Variant &key,
              bool default_value) {
    godot::Variant value = dict.get(key, default_value);
    return value.get_type() == godot::Variant::BOOL ? (bool)value : default_value;
}

int get_int(const godot::Dictionary &dict,
            const godot::Variant &key,
            int default_value) {
    godot::Variant value = dict.get(key, default_value);
    return value.get_type() == godot::Variant::INT
               ? static_cast<int>(static_cast<int64_t>(value))
               : default_value;
}

godot::String get_string(const godot::Dictionary &dict,
                         const godot::Variant &key,
                         const godot::String &default_value) {
    godot::Variant value = dict.get(key, default_value);
    return value.get_type() == godot::Variant::STRING ? godot::String(value)
                                                      : default_value;
}

godot::Array get_array(const godot::Dictionary &dict,
                       const godot::Variant &key,
                       const godot::Array &default_value) {
    godot::Variant value = dict.get(key, default_value);
    return value.get_type() == godot::Variant::ARRAY ? godot::Array(value)
                                                     : default_value;
}

godot::Dictionary get_dictionary(
    const godot::Dictionary &dict,
    const godot::Variant &key,
    const godot::Dictionary &default_value) {
    godot::Variant value = dict.get(key, default_value);
    return value.get_type() == godot::Variant::DICTIONARY
               ? godot::Dictionary(value)
               : default_value;
}

} // namespace fennara::variant_utils
