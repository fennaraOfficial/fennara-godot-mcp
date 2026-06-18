#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara::tool_results {

godot::Dictionary format_script_diagnostics(const godot::Dictionary &args,
                                              const godot::Dictionary &raw_result);

} // namespace fennara::tool_results
