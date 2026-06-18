#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara::tool_results {

godot::Dictionary format_for_model(const godot::String &tool_name,
                                   const godot::Dictionary &args,
                                   const godot::Dictionary &raw_result);

bool is_envelope(const godot::Dictionary &result);

} // namespace fennara::tool_results
