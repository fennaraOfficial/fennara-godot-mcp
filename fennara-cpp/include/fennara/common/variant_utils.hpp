#pragma once

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara::variant_utils {

bool get_bool(const godot::Dictionary &dict,
              const godot::Variant &key,
              bool default_value = false);
int get_int(const godot::Dictionary &dict,
            const godot::Variant &key,
            int default_value = 0);
godot::String get_string(const godot::Dictionary &dict,
                         const godot::Variant &key,
                         const godot::String &default_value = godot::String());
godot::Array get_array(const godot::Dictionary &dict,
                       const godot::Variant &key,
                       const godot::Array &default_value = godot::Array());
godot::Dictionary get_dictionary(
    const godot::Dictionary &dict,
    const godot::Variant &key,
    const godot::Dictionary &default_value = godot::Dictionary());

} // namespace fennara::variant_utils
