#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara::get_node_properties {

// Returns compact text lines for a SpriteFrames resource attached to an
// AnimatedSprite2D node.
godot::String format_sprite_frames(godot::Node *target);

} // namespace fennara::get_node_properties
