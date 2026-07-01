#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara::runtime_scene_preflight {

godot::Dictionary collect_scene_script_context(const godot::String &scene_path);
godot::Dictionary diagnose_collected_scripts(const godot::Dictionary &context);
godot::Dictionary check_scene_scripts(const godot::String &scene_path);

} // namespace fennara::runtime_scene_preflight
