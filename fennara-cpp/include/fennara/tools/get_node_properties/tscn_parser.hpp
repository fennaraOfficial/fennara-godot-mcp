#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara::get_node_properties {

struct ExtResourceEntry {
    godot::String type;
    godot::String path;
};

struct SubResourceBlock {
    godot::String type;
    godot::Vector<godot::String> lines; // raw property lines
};

struct TscnData {
    godot::HashMap<godot::String, ExtResourceEntry> ext_resources;
    godot::HashMap<godot::String, SubResourceBlock> sub_resources;
};

struct TextResourceData {
    godot::String root_type;
    godot::Vector<godot::String> root_lines;
    godot::HashMap<godot::String, ExtResourceEntry> ext_resources;
    godot::HashMap<godot::String, SubResourceBlock> sub_resources;
    bool valid = false;
};

// Parse the entire .tscn file into ext_resource and sub_resource lookup tables.
TscnData parse_tscn(const godot::String &file_text);

// Parse a text-based external Godot resource file (.tres/.tscn-like text).
// Only [gd_resource] files are expanded recursively; other file types stay compact.
TextResourceData parse_text_resource(const godot::String &file_text);

// Find property lines for a specific node by name + parent path.
// parent_path: "" for root, "." for child of root, "Child/SubChild" for deeper.
godot::Vector<godot::String> find_node_block(const godot::String &file_text,
                                              const godot::String &node_name,
                                              const godot::String &parent_path);

// Format all properties from node block lines.
// Resolves ExtResources, recurses SubResources with XML tags,
// dispatches special care resources to Godot API handlers.
godot::String format_properties(const TscnData &data,
                                const godot::Vector<godot::String> &lines,
                                int indent_depth,
                                godot::Node *scene_root,
                                godot::Node *target);

// Check if a resource type needs special care (Godot API instead of text).
bool needs_special_care(const godot::String &resource_type);

// Dispatch to special care handler -- returns formatted string.
godot::String handle_special_care(const godot::String &resource_type,
                                  const godot::String &property_name,
                                  godot::Node *scene_root,
                                  godot::Node *target,
                                  int indent_depth);

} // namespace fennara::get_node_properties
