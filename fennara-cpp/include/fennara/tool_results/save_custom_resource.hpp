#pragma once

#include <godot_cpp/variant/dictionary.hpp>

namespace fennara::tool_results {

godot::Dictionary format_save_custom_resource(const godot::Dictionary &raw_result);

} // namespace fennara::tool_results
