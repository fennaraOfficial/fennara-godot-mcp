#pragma once

#include <godot_cpp/variant/dictionary.hpp>

namespace fennara::tool_results {

godot::Dictionary format_run_scene_edit_script(const godot::Dictionary &raw_result);

} // namespace fennara::tool_results
