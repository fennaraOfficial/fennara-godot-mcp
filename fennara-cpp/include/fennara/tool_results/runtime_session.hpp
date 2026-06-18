#pragma once

#include <godot_cpp/variant/dictionary.hpp>

namespace fennara::tool_results {

godot::Dictionary format_runtime_session(const godot::Dictionary &raw_result);

} // namespace fennara::tool_results
