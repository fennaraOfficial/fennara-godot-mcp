#include "fennara/tools/get_node_properties/tscn_parser.hpp"
#include "fennara/tools/get_node_properties/animation_tree_graph.hpp"
#include "fennara/tools/get_node_properties/animation_mixer.hpp"
#include "fennara/tools/get_node_properties/sprite_frames.hpp"
#include "fennara/tools/get_node_properties/mesh_library_resources.hpp"
#include "fennara/tools/get_node_properties/theme_resources.hpp"
#include "fennara/tools/get_node_properties/tile_resources.hpp"

#include <godot_cpp/classes/animation_mixer.hpp>
#include <godot_cpp/classes/animation_tree.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/grid_map.hpp>
#include <godot_cpp/classes/tile_map.hpp>
#include <godot_cpp/classes/tile_map_layer.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace fennara::get_node_properties {

godot::String format_properties_internal(
    const TscnData &data, const godot::Vector<godot::String> &lines,
    int indent_depth, godot::Node *scene_root, godot::Node *target,
    const godot::Vector<godot::String> &external_stack,
    int subresource_depth, int external_resource_depth);

namespace {

constexpr int kMaxSubResourceRecursionDepth = 5;
constexpr int kMaxExternalResourceRecursionDepth = 5;

// Extract a quoted attribute value from a section header line.
// e.g. extract_attr("[node name=\"Foo\" ...]", "name") -> "Foo"
godot::String extract_attr(const godot::String &line, const godot::String &attr) {
    godot::String key = attr + godot::String("=\"");
    int search_from = 0;

    while (true) {
        int start = line.find(key, search_from);
        if (start == -1) return godot::String();

        bool valid_boundary =
            (start == 0 || line[start - 1] == ' ' || line[start - 1] == '[');
        if (valid_boundary) {
            start += key.length();
            int end = line.find("\"", start);
            if (end == -1) return godot::String();
            return line.substr(start, end - start);
        }

        search_from = start + 1;
    }
}

// Build indent string for a given depth (2 spaces per level).
godot::String indent_str(int depth) {
    godot::String s;
    for (int i = 0; i < depth; i++) s += "  ";
    return s;
}

// Count unescaped occurrences of a char in a string (for brace/bracket balancing).
int count_char(const godot::String &s, char32_t ch) {
    int count = 0;
    for (int i = 0; i < s.length(); i++) {
        if (s[i] == ch) count++;
    }
    return count;
}

godot::Vector<godot::String> collect_section_lines(
    const godot::PackedStringArray &lines, int &i) {
    godot::Vector<godot::String> result;

    while (i < lines.size()) {
        godot::String prop_line = lines[i];
        godot::String trimmed = prop_line.strip_edges();
        if (trimmed.begins_with("[") && !trimmed.begins_with("[{")) break;
        if (!trimmed.is_empty()) {
            godot::String joined = trimmed;
            int open_braces = count_char(joined, '{') - count_char(joined, '}');
            int open_brackets = count_char(joined, '[') - count_char(joined, ']');
            int open_parens = count_char(joined, '(') - count_char(joined, ')');
            while ((open_braces > 0 || open_brackets > 0 || open_parens > 0) &&
                   i + 1 < lines.size()) {
                i++;
                godot::String next = lines[i].strip_edges();
                if (next.begins_with("[") && open_brackets <= 0 &&
                    open_braces <= 0 && open_parens <= 0) {
                    break;
                }
                joined += " " + next;
                open_braces += count_char(next, '{') - count_char(next, '}');
                open_brackets += count_char(next, '[') - count_char(next, ']');
                open_parens += count_char(next, '(') - count_char(next, ')');
            }
            result.push_back(joined);
        }
        i++;
    }

    return result;
}

bool path_seen(const godot::Vector<godot::String> &external_stack,
               const godot::String &path) {
    for (int i = 0; i < (int)external_stack.size(); i++) {
        if (external_stack[i] == path) return true;
    }
    return false;
}

bool is_text_resource_header(const godot::String &file_text,
                             const godot::String &header) {
    godot::PackedStringArray lines = file_text.split("\n");
    for (int i = 0; i < lines.size(); i++) {
        godot::String trimmed = lines[i].strip_edges();
        if (trimmed.is_empty()) continue;
        return trimmed.begins_with(header);
    }
    return false;
}

bool can_expand_external_resource(const ExtResourceEntry &entry,
                                  TextResourceData *out_data = nullptr) {
    godot::String file_text =
        godot::FileAccess::get_file_as_string(entry.path);
    if (file_text.is_empty() ||
        !is_text_resource_header(file_text, "[gd_resource ")) {
        return false;
    }

    if (out_data != nullptr) {
        *out_data = parse_text_resource(file_text);
        return out_data->valid;
    }

    return true;
}

godot::String format_external_resource(
    const godot::String &property_name, const ExtResourceEntry &entry,
    int indent_depth, godot::Node *scene_root, godot::Node *target,
    const godot::Vector<godot::String> &external_stack,
    int subresource_depth, int external_resource_depth) {
    godot::String indent = indent_str(indent_depth);

    if (external_resource_depth >= kMaxExternalResourceRecursionDepth ||
        path_seen(external_stack, entry.path)) {
        return indent + property_name + " = " + entry.path + " [" +
               entry.type + "]\n";
    }

    TextResourceData resource_data;
    if (!can_expand_external_resource(entry, &resource_data)) {
        return indent + property_name + " = " + entry.path + " [" +
               entry.type + "]\n";
    }

    godot::String resource_type =
        resource_data.root_type.is_empty() ? entry.type : resource_data.root_type;
    godot::String out = indent + property_name + " = <" + resource_type + ">\n";

    godot::String comment = indent_str(indent_depth + 1) +
                            "<!-- external resource: " + entry.path;
    if (!entry.type.is_empty()) {
        comment += " [declared " + entry.type + "]";
    }
    comment += " -->\n";
    out += comment;

    if (resource_type == "Theme" && should_summarize_theme_resource(entry)) {
        out += format_theme_resource(entry, resource_data, scene_root, target,
                                     indent_depth + 1);
    } else if (needs_special_care(resource_type)) {
        out += handle_special_care(resource_type, property_name, scene_root,
                                   target, indent_depth + 1);
    } else if (!resource_data.root_lines.is_empty()) {
        godot::Vector<godot::String> next_stack = external_stack;
        next_stack.push_back(entry.path);

        TscnData nested_data;
        nested_data.ext_resources = resource_data.ext_resources;
        nested_data.sub_resources = resource_data.sub_resources;
        out += format_properties_internal(nested_data, resource_data.root_lines,
                                          indent_depth + 1, scene_root, target,
                                          next_stack, subresource_depth,
                                          external_resource_depth + 1);
    }

    out += indent + "</" + resource_type + ">\n";
    return out;
}

// Check if a value string is exactly SubResource("id") or ExtResource("id").
// Returns the id if matched, empty string otherwise.
godot::String extract_ref_id(const godot::String &value, const godot::String &prefix) {
    godot::String trimmed = value.strip_edges();
    if (!trimmed.begins_with(prefix + godot::String("(\""))) return godot::String();
    if (!trimmed.ends_with("\")")) return godot::String();
    int start = prefix.length() + 2; // skip prefix("
    int end = trimmed.length() - 2;  // skip ")
    if (end <= start) return godot::String();
    return trimmed.substr(start, end - start);
}

// Replace inline ExtResource("id") and SubResource("id") refs in a value string.
godot::String resolve_inline_refs(const godot::String &value, const TscnData &data) {
    godot::String result = value;

    // Replace ExtResource refs
    int pos = 0;
    while (pos < result.length()) {
        int ext_start = result.find("ExtResource(\"", pos);
        if (ext_start == -1) break;
        int id_start = ext_start + 14; // len of ExtResource("
        int id_end = result.find("\")", id_start);
        if (id_end == -1) break;
        godot::String id = result.substr(id_start, id_end - id_start);
        godot::String replacement;
        if (data.ext_resources.has(id)) {
            const ExtResourceEntry &entry = data.ext_resources[id];
            replacement = entry.path + godot::String(" [") + entry.type + "]";
        } else {
            replacement = "ExtResource(\"" + id + "\")";
        }
        result = result.left(ext_start) + replacement +
                 result.substr(id_end + 2);
        pos = ext_start + replacement.length();
    }

    // Replace SubResource refs inline with <Type> summary
    pos = 0;
    while (pos < result.length()) {
        int sub_start = result.find("SubResource(\"", pos);
        if (sub_start == -1) break;
        int id_start = sub_start + 13; // len of SubResource("
        int id_end = result.find("\")", id_start);
        if (id_end == -1) break;
        godot::String id = result.substr(id_start, id_end - id_start);
        godot::String replacement;
        if (data.sub_resources.has(id)) {
            replacement = "<" + data.sub_resources[id].type + ">";
        } else {
            replacement = "SubResource(\"" + id + "\")";
        }
        result = result.left(sub_start) + replacement +
                 result.substr(id_end + 2);
        pos = sub_start + replacement.length();
    }

    return result;
}

// Check if a value contains any SubResource ref to a special care type.
bool has_special_care_subresource(const godot::String &value, const TscnData &data) {
    int pos = 0;
    while (pos < value.length()) {
        int sub_start = value.find("SubResource(\"", pos);
        if (sub_start == -1) break;
        int id_start = sub_start + 13;
        int id_end = value.find("\")", id_start);
        if (id_end == -1) break;
        godot::String id = value.substr(id_start, id_end - id_start);
        if (data.sub_resources.has(id) &&
            needs_special_care(data.sub_resources[id].type)) {
            return true;
        }
        pos = id_end + 2;
    }
    return false;
}

} // namespace

// -------------------------------------------------------------------------
// parse_tscn
// -------------------------------------------------------------------------

TscnData parse_tscn(const godot::String &file_text) {
    TscnData data;
    godot::PackedStringArray lines = file_text.split("\n");

    int i = 0;
    while (i < lines.size()) {
        godot::String line = lines[i].strip_edges();

        // [ext_resource type="X" ... path="Y" id="Z"]
        if (line.begins_with("[ext_resource ")) {
            ExtResourceEntry entry;
            entry.type = extract_attr(line, "type");
            entry.path = extract_attr(line, "path");
            godot::String id = extract_attr(line, "id");
            if (!id.is_empty()) {
                data.ext_resources[id] = entry;
            }
            i++;
            continue;
        }

        // [sub_resource type="X" id="Y"]
        if (line.begins_with("[sub_resource ")) {
            SubResourceBlock block;
            block.type = extract_attr(line, "type");
            godot::String id = extract_attr(line, "id");
            i++;
            block.lines = collect_section_lines(lines, i);

            if (!id.is_empty()) {
                data.sub_resources[id] = block;
            }
            continue;
        }

        i++;
    }

    return data;
}

// -------------------------------------------------------------------------
// parse_text_resource
// -------------------------------------------------------------------------

TextResourceData parse_text_resource(const godot::String &file_text) {
    TextResourceData data;
    godot::PackedStringArray lines = file_text.split("\n");

    int i = 0;
    while (i < lines.size()) {
        godot::String line = lines[i].strip_edges();

        if (line.begins_with("[gd_resource ")) {
            data.root_type = extract_attr(line, "type");
            data.valid = true;
            i++;
            continue;
        }

        if (line.begins_with("[ext_resource ")) {
            ExtResourceEntry entry;
            entry.type = extract_attr(line, "type");
            entry.path = extract_attr(line, "path");
            godot::String id = extract_attr(line, "id");
            if (!id.is_empty()) {
                data.ext_resources[id] = entry;
            }
            i++;
            continue;
        }

        if (line.begins_with("[sub_resource ")) {
            SubResourceBlock block;
            block.type = extract_attr(line, "type");
            godot::String id = extract_attr(line, "id");
            i++;
            block.lines = collect_section_lines(lines, i);
            if (!id.is_empty()) {
                data.sub_resources[id] = block;
            }
            continue;
        }

        if (line == "[resource]") {
            i++;
            data.root_lines = collect_section_lines(lines, i);
            continue;
        }

        i++;
    }

    return data;
}

// -------------------------------------------------------------------------
// find_node_block
// -------------------------------------------------------------------------

godot::Vector<godot::String> find_node_block(const godot::String &file_text,
                                              const godot::String &node_name,
                                              const godot::String &parent_path) {
    godot::Vector<godot::String> result;
    godot::PackedStringArray lines = file_text.split("\n");

    int i = 0;
    while (i < lines.size()) {
        godot::String line = lines[i].strip_edges();

        if (line.begins_with("[node ")) {
            godot::String name = extract_attr(line, "name");
            godot::String parent = extract_attr(line, "parent");

            bool match = false;
            if (parent_path.is_empty()) {
                // Root node: no parent attribute
                match = (name == node_name && parent.is_empty());
            } else {
                match = (name == node_name && parent == parent_path);
            }

            if (match) {
                i++;
                result = collect_section_lines(lines, i);
                break;
            }
        }
        i++;
    }

    return result;
}

// -------------------------------------------------------------------------
// format_properties
// -------------------------------------------------------------------------

godot::String format_properties_internal(
    const TscnData &data, const godot::Vector<godot::String> &lines,
    int indent_depth, godot::Node *scene_root, godot::Node *target,
    const godot::Vector<godot::String> &external_stack,
    int subresource_depth, int external_resource_depth) {
    godot::String out;
    godot::String indent = indent_str(indent_depth);
    bool animation_tree_parameters_printed = false;

    for (int i = 0; i < (int)lines.size(); i++) {
        const godot::String &line = lines[i];

        // Split into name = value
        int eq_pos = line.find(" = ");
        if (eq_pos == -1) {
            // Not a property line, pass through
            out += indent + line + "\n";
            continue;
        }

        godot::String prop_name = line.left(eq_pos);
        godot::String prop_value = line.substr(eq_pos + 3);

        // Skip script property -- handled separately in header
        if (prop_name == "script") continue;

        if (prop_name.begins_with("parameters/") &&
            godot::Object::cast_to<godot::AnimationTree>(target) != nullptr) {
            if (!animation_tree_parameters_printed) {
                out += format_animation_tree_parameters(lines, indent_depth);
                animation_tree_parameters_printed = true;
            }
            continue;
        }

        if (prop_name == "tile_map_data" &&
            godot::Object::cast_to<godot::TileMapLayer>(target) != nullptr) {
            out += indent + prop_name + " = <TileMapLayerData>\n";
            out += format_tile_map_layer_data(target, indent_depth + 1);
            out += indent + "</TileMapLayerData>\n";
            continue;
        }

        if (prop_name.begins_with("layer_") &&
            prop_name.ends_with("/tile_data") &&
            godot::Object::cast_to<godot::TileMap>(target) != nullptr) {
            out += indent + prop_name + " = <TileMapLayerData>\n";
            out += format_tile_map_legacy_data(target, prop_name,
                                               indent_depth + 1);
            out += indent + "</TileMapLayerData>\n";
            continue;
        }

        if (prop_name == "data" &&
            godot::Object::cast_to<godot::GridMap>(target) != nullptr) {
            out += indent + prop_name + " = <GridMapData>\n";
            out += format_grid_map_data(target, indent_depth + 1);
            out += indent + "</GridMapData>\n";
            continue;
        }

        // Check for top-level ExtResource reference
        godot::String ext_id = extract_ref_id(prop_value, "ExtResource");
        if (!ext_id.is_empty()) {
            if (data.ext_resources.has(ext_id)) {
                const ExtResourceEntry &entry = data.ext_resources[ext_id];
                out += format_external_resource(prop_name, entry, indent_depth,
                                                scene_root, target,
                                                external_stack,
                                                subresource_depth,
                                                external_resource_depth);
            } else {
                out += indent + prop_name + " = " + prop_value + "\n";
            }
            continue;
        }

        // Check for top-level SubResource reference
        godot::String sub_id = extract_ref_id(prop_value, "SubResource");
        if (!sub_id.is_empty()) {
            if (data.sub_resources.has(sub_id)) {
                const SubResourceBlock &block = data.sub_resources[sub_id];

                godot::String display_name = prop_name;
                if (display_name == "libraries" &&
                    block.type == "AnimationLibrary") {
                    display_name = "libraries/";
                }

                if (prop_name == "tree_root" &&
                    block.type.begins_with("AnimationNode") &&
                    godot::Object::cast_to<godot::AnimationTree>(target) !=
                        nullptr) {
                    out += indent + display_name + " = <AnimationTreeGraph>\n";
                    out += format_animation_tree_graph(data, sub_id,
                                                       indent_depth + 1);
                    out += indent + "</AnimationTreeGraph>\n";
                } else if (needs_special_care(block.type)) {
                    // Dispatch to special care handler
                    out += indent + display_name + " = <" + block.type + ">\n";
                    out += handle_special_care(block.type, display_name,
                                               scene_root, target,
                                               indent_depth + 1);
                    out += indent + "</" + block.type + ">\n";
                } else if (subresource_depth < kMaxSubResourceRecursionDepth) {
                    // Recurse into SubResource with XML tags
                    out += indent + display_name + " = <" + block.type + ">\n";
                    out += format_properties_internal(
                        data, block.lines, indent_depth + 1, scene_root, target,
                        external_stack, subresource_depth + 1,
                        external_resource_depth);
                    out += indent + "</" + block.type + ">\n";
                } else {
                    // Depth limit reached
                    out += indent + prop_name + " = " + prop_value + "\n";
                }
            } else {
                out += indent + prop_name + " = " + prop_value + "\n";
            }
            continue;
        }

        // Check if value contains special care SubResource refs (e.g. libraries dict)
        if (has_special_care_subresource(prop_value, data)) {
            // Find the type of the first special care SubResource
            godot::String sc_type;
            int pos = 0;
            while (pos < prop_value.length()) {
                int sub_start = prop_value.find("SubResource(\"", pos);
                if (sub_start == -1) break;
                int id_start = sub_start + 13;
                int id_end = prop_value.find("\")", id_start);
                if (id_end == -1) break;
                godot::String id = prop_value.substr(id_start, id_end - id_start);
                if (data.sub_resources.has(id) &&
                    needs_special_care(data.sub_resources[id].type)) {
                    sc_type = data.sub_resources[id].type;
                    break;
                }
                pos = id_end + 2;
            }

            if (!sc_type.is_empty()) {
                godot::String display_name = prop_name;
                if (display_name == "libraries" &&
                    sc_type == "AnimationLibrary") {
                    display_name = "libraries/";
                }

                out += indent + display_name + " = <" + sc_type + ">\n";
                out += handle_special_care(sc_type, display_name,
                                           scene_root, target,
                                           indent_depth + 1);
                out += indent + "</" + sc_type + ">\n";
                continue;
            }
        }

        // Value with inline refs -- resolve them
        if (prop_value.contains("ExtResource(\"") ||
            prop_value.contains("SubResource(\"")) {
            out += indent + prop_name + " = " +
                   resolve_inline_refs(prop_value, data) + "\n";
            continue;
        }

        // Simple value -- return as-is
        out += indent + prop_name + " = " + prop_value + "\n";
    }

    return out;
}

godot::String format_properties(const TscnData &data,
                                const godot::Vector<godot::String> &lines,
                                int indent_depth,
                                godot::Node *scene_root,
                                godot::Node *target) {
    godot::Vector<godot::String> external_stack;
    return format_properties_internal(data, lines, indent_depth, scene_root,
                                      target, external_stack, 0, 0);
}

// -------------------------------------------------------------------------
// needs_special_care
// -------------------------------------------------------------------------

bool needs_special_care(const godot::String &resource_type) {
    return resource_type == "AnimationLibrary" ||
           resource_type == "Animation" ||
           resource_type == "SpriteFrames" ||
           resource_type == "MeshLibrary" ||
           resource_type == "TileSet" ||
           resource_type == "TileSetSource" ||
           resource_type == "TileSetAtlasSource" ||
           resource_type == "TileSetScenesCollectionSource" ||
           resource_type == "TileMapPattern";
}

// -------------------------------------------------------------------------
// handle_special_care
// -------------------------------------------------------------------------

godot::String handle_special_care(const godot::String &resource_type,
                                  const godot::String &property_name,
                                  godot::Node *scene_root,
                                  godot::Node *target,
                                  int indent_depth) {
    (void)property_name;
    (void)scene_root;
    godot::String indent = indent_str(indent_depth);
    godot::String out;

    if (resource_type == "AnimationLibrary" || resource_type == "Animation") {
        out += indent + "<!-- read via AnimationMixer API -->\n";
        godot::String libs = format_animation_libraries(target);
        if (!libs.is_empty()) {
            // Indent each line of the library output
            godot::PackedStringArray lib_lines = libs.split("\n");
            for (int i = 0; i < lib_lines.size(); i++) {
                if (!lib_lines[i].is_empty()) {
                    out += indent + lib_lines[i] + "\n";
                }
            }
        }
    } else if (resource_type == "SpriteFrames") {
        out += indent + "<!-- read via SpriteFrames API -->\n";
        godot::String frames = format_sprite_frames(target);
        if (!frames.is_empty()) {
            godot::PackedStringArray frame_lines = frames.split("\n");
            for (int i = 0; i < frame_lines.size(); i++) {
                if (!frame_lines[i].is_empty()) {
                    out += indent + frame_lines[i] + "\n";
                }
            }
        }
    } else if (resource_type == "MeshLibrary") {
        out += format_mesh_library_resource(property_name, target,
                                            indent_depth);
    } else if (resource_type == "TileSet" ||
               resource_type == "TileSetSource" ||
               resource_type == "TileSetAtlasSource" ||
               resource_type == "TileSetScenesCollectionSource" ||
               resource_type == "TileMapPattern") {
        out += format_tile_resource(resource_type, property_name, target,
                                    indent_depth);
    }

    return out;
}

} // namespace fennara::get_node_properties
