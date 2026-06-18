#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara::runtime_script_diagnostics {

godot::Dictionary check(const godot::String &script_path);
void apply_to_result(const godot::Dictionary &diagnostics,
                     godot::Dictionary &result);
bool has_blocking_error(const godot::Dictionary &diagnostics);

} // namespace fennara::runtime_script_diagnostics
