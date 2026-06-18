#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/scene_state.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara::get_node_properties {

// Compact text representation of a Variant value.
// e.g. Vector2 -> "(0,-47.4)", Color -> "rgba(1,0.5,0.5,0.9)"
godot::String format_variant(const godot::Variant &value);

// Format a resource value compactly.
godot::String format_resource(godot::Object *resource);

int find_scene_state_index(const godot::Ref<godot::SceneState> &state,
                           const godot::String &relative_path);

// Format connections as compact text lines.
godot::String format_connections(
    const godot::Ref<godot::SceneState> &state, int node_idx);

godot::Node *resolve_node(godot::Node *root, const godot::String &node_path);

void collect_available_paths(godot::Node *node,
                             const godot::String &current_path,
                             godot::Array &paths);

} // namespace fennara::get_node_properties
