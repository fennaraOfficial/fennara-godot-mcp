#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara::get_node_properties {

// Returns compact text lines for animation libraries on an AnimationMixer node.
// Used by special care handler for AnimationLibrary SubResources.
godot::String format_animation_libraries(godot::Node *target);

} // namespace fennara::get_node_properties
