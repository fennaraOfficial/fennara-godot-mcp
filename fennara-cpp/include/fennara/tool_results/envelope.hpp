#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara::tool_results {

godot::Dictionary make_base_metadata(const godot::String &tool_name,
                                     const godot::String &formatter_version,
                                     const godot::String &status);

godot::Dictionary make_envelope(const godot::String &content,
                                const godot::Dictionary &metadata,
                                bool success);

bool is_envelope(const godot::Dictionary &result);

} // namespace fennara::tool_results
