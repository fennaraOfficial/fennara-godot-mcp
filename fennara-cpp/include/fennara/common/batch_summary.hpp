#pragma once

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara::batch_summary {

godot::Dictionary build(const godot::Array &results,
                        const godot::String &label);
godot::Dictionary build_result(const godot::Array &results,
                               const godot::String &label);

} // namespace fennara::batch_summary
