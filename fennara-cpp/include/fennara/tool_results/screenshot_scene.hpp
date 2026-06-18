#pragma once

#include <godot_cpp/variant/dictionary.hpp>

namespace fennara::tool_results {

godot::Dictionary format_screenshot_scene(const godot::Dictionary &raw_result);

} // namespace fennara::tool_results
