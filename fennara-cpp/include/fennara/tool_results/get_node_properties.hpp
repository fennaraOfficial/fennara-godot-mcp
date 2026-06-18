#pragma once

#include <godot_cpp/variant/dictionary.hpp>

namespace fennara::tool_results {

godot::Dictionary format_get_node_properties(const godot::Dictionary &raw_result);

} // namespace fennara::tool_results
