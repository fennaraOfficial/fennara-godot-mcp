#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara::runtime_scene_preflight {

godot::Dictionary check_scene_scripts(const godot::String &scene_path);

} // namespace fennara::runtime_scene_preflight
