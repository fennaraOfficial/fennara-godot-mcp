#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara::get_node_properties {

godot::String format_mesh_library_resource(const godot::String &property_name,
                                           godot::Node *target,
                                           int indent_depth);

godot::String format_grid_map_data(godot::Node *target, int indent_depth);

} // namespace fennara::get_node_properties
