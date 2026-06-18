#pragma once

#include "fennara/tools/get_node_properties/tscn_parser.hpp"

#include <godot_cpp/variant/string.hpp>

namespace fennara::get_node_properties {

godot::String format_animation_tree_graph(const TscnData &data,
                                          const godot::String &root_id,
                                          int indent_depth);

godot::String format_animation_tree_parameters(
    const godot::Vector<godot::String> &node_lines, int indent_depth);

} // namespace fennara::get_node_properties
