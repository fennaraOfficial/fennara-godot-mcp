#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara::get_node_properties {

godot::String format_tile_map_layer_data(godot::Node *target,
                                         int indent_depth);

godot::String format_tile_map_legacy_data(godot::Node *target,
                                          const godot::String &property_name,
                                          int indent_depth);

godot::String format_tile_resource(const godot::String &resource_type,
                                   const godot::String &property_name,
                                   godot::Node *target, int indent_depth);

} // namespace fennara::get_node_properties
