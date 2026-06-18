#pragma once

#include "fennara/tools/get_node_properties/tscn_parser.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara::get_node_properties {

bool should_summarize_theme_resource(const ExtResourceEntry &entry);

godot::String format_theme_resource(const ExtResourceEntry &entry,
                                    const TextResourceData &resource_data,
                                    godot::Node *scene_root,
                                    godot::Node *target,
                                    int indent_depth);

godot::String format_inherited_theme_note(const TscnData &data,
                                          const godot::String &file_text,
                                          godot::Node *scene_root,
                                          godot::Node *target,
                                          const godot::String &relative_path,
                                          bool target_has_explicit_theme);

} // namespace fennara::get_node_properties
