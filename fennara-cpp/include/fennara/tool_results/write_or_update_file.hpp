#pragma once

#include <godot_cpp/variant/dictionary.hpp>

namespace fennara::tool_results {

godot::Dictionary format_write_or_update_file(
    const godot::Dictionary &raw_result);

} // namespace fennara::tool_results
