#include "fennara/tools/get_node_properties/get_node_properties.hpp"
#include "fennara/tools/get_node_properties/common.hpp"
#include "fennara/tools/get_node_properties/theme_resources.hpp"
#include "fennara/tools/get_node_properties/tscn_parser.hpp"
#include "fennara/helpers.hpp"
#include "fennara/logger.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace fennara {

void FennaraGetNodePropertiesTool::_bind_methods() {
    godot::ClassDB::bind_static_method(
        "FennaraGetNodePropertiesTool",
        godot::D_METHOD("execute", "args"),
        &FennaraGetNodePropertiesTool::execute);
}

namespace {

constexpr int kMaxBatchTargets = 5;

int count_text_lines(const godot::String &text) {
    if (text.is_empty()) {
        return 0;
    }
    int lines = 1;
    for (int i = 0; i < text.length(); i++) {
        if (text[i] == '\n' && i < text.length() - 1) {
            lines++;
        }
    }
    return lines;
}

struct ScriptExportInfo {
    godot::String name;
    godot::String type_hint;
    godot::String default_value;
    godot::String declaration_text;
    bool has_default = false;
};

godot::String extract_ref_id(const godot::String &value,
                             const godot::String &prefix) {
    godot::String trimmed = value.strip_edges();
    if (!trimmed.begins_with(prefix + godot::String("(\""))) {
        return godot::String();
    }
    if (!trimmed.ends_with("\")")) return godot::String();
    int start = prefix.length() + 2;
    int end = trimmed.length() - 2;
    if (end <= start) return godot::String();
    return trimmed.substr(start, end - start);
}

godot::String strip_inline_comment(const godot::String &line) {
    bool in_single = false;
    bool in_double = false;

    for (int i = 0; i < line.length(); i++) {
        char32_t ch = line[i];
        char32_t prev = (i > 0) ? line[i - 1] : 0;

        if (ch == '"' && !in_single && prev != '\\') {
            in_double = !in_double;
            continue;
        }
        if (ch == '\'' && !in_double && prev != '\\') {
            in_single = !in_single;
            continue;
        }
        if (ch == '#' && !in_single && !in_double) {
            return line.left(i).strip_edges();
        }
    }

    return line.strip_edges();
}

godot::String strip_trailing_comment(const godot::String &line) {
    bool in_single = false;
    bool in_double = false;

    for (int i = 0; i < line.length(); i++) {
        char32_t ch = line[i];
        char32_t prev = (i > 0) ? line[i - 1] : 0;

        if (ch == '"' && !in_single && prev != '\\') {
            in_double = !in_double;
            continue;
        }
        if (ch == '\'' && !in_double && prev != '\\') {
            in_single = !in_single;
            continue;
        }
        if (ch == '#' && !in_single && !in_double) {
            return line.left(i).rstrip(" \t");
        }
    }

    return line.rstrip(" \t");
}

bool parse_export_var_declaration(const godot::String &line,
                                  ScriptExportInfo &out_info) {
    godot::String cleaned = strip_inline_comment(line);
    int var_pos = cleaned.find("var ");
    if (var_pos == -1) return false;

    godot::String decl = cleaned.substr(var_pos + 4).strip_edges();
    if (decl.is_empty()) return false;

    int name_end = 0;
    while (name_end < decl.length()) {
        char32_t ch = decl[name_end];
        bool is_valid =
            (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_';
        if (!is_valid) break;
        name_end++;
    }

    if (name_end == 0) return false;

    out_info.name = decl.left(name_end);
    godot::String rest = decl.substr(name_end).strip_edges();

    if (rest.begins_with(":=")) {
        out_info.default_value = rest.substr(2).strip_edges();
        out_info.has_default = !out_info.default_value.is_empty();
        return true;
    }

    if (rest.begins_with(":")) {
        rest = rest.substr(1).strip_edges();
        int eq_pos = rest.find("=");
        if (eq_pos != -1) {
            out_info.type_hint = rest.left(eq_pos).strip_edges();
            out_info.default_value = rest.substr(eq_pos + 1).strip_edges();
            out_info.has_default = !out_info.default_value.is_empty();
        } else {
            out_info.type_hint = rest.strip_edges();
        }
        return true;
    }

    if (rest.begins_with("=")) {
        out_info.default_value = rest.substr(1).strip_edges();
        out_info.has_default = !out_info.default_value.is_empty();
        return true;
    }

    return true;
}

godot::Vector<ScriptExportInfo> parse_script_exports(
    const godot::String &script_path) {
    godot::Vector<ScriptExportInfo> exports;
    godot::String script_text =
        godot::FileAccess::get_file_as_string(script_path);
    if (script_text.is_empty()) return exports;

    godot::PackedStringArray lines = script_text.split("\n");
    bool pending_export = false;
    godot::String pending_export_line;

    for (int i = 0; i < lines.size(); i++) {
        godot::String trimmed = strip_inline_comment(lines[i]).strip_edges();
        if (trimmed.is_empty()) continue;

        if (trimmed.begins_with("@export")) {
            ScriptExportInfo info;
            if (parse_export_var_declaration(trimmed, info)) {
                info.declaration_text = strip_trailing_comment(trimmed);
                exports.push_back(info);
                pending_export = false;
            } else {
                pending_export_line = strip_trailing_comment(trimmed);
                pending_export = true;
            }
            continue;
        }

        if (pending_export) {
            if (trimmed.find("var ") != -1) {
                ScriptExportInfo info;
                if (parse_export_var_declaration(trimmed, info)) {
                    info.declaration_text =
                        pending_export_line + "\n" +
                        strip_trailing_comment(trimmed);
                    exports.push_back(info);
                }
            }
            pending_export_line = "";
            pending_export = false;
        }
    }

    return exports;
}

godot::HashMap<godot::String, godot::String> build_node_property_map(
    const godot::Vector<godot::String> &node_lines) {
    godot::HashMap<godot::String, godot::String> props;

    for (int i = 0; i < (int)node_lines.size(); i++) {
        const godot::String &line = node_lines[i];
        int eq_pos = line.find(" = ");
        if (eq_pos == -1) continue;
        props[line.left(eq_pos)] = line.substr(eq_pos + 3);
    }

    return props;
}

godot::String get_script_path_from_node_lines(
    const godot::Vector<godot::String> &node_lines,
    const get_node_properties::TscnData &tscn_data) {
    godot::HashMap<godot::String, godot::String> props =
        build_node_property_map(node_lines);
    if (!props.has("script")) return godot::String();

    godot::String script_value = props["script"];
    godot::String ext_id = extract_ref_id(script_value, "ExtResource");
    if (!ext_id.is_empty() && tscn_data.ext_resources.has(ext_id)) {
        const get_node_properties::ExtResourceEntry &entry =
            tscn_data.ext_resources[ext_id];
        if (entry.type == "Script") return entry.path;
    }

    if (script_value.begins_with("res://")) return script_value;
    if (script_value.begins_with("SubResource(\"")) return "<embedded>";
    return godot::String();
}

godot::String format_script_exports(const godot::String &script_path) {
    if (script_path.is_empty() || script_path == "<embedded>") {
        return godot::String();
    }

    godot::Vector<ScriptExportInfo> exports = parse_script_exports(script_path);
    if (exports.is_empty()) return godot::String();

    godot::String out = "script_exports:\n";
    out += "  <!-- declarations copied from script; if a value is overridden, it appears in properties below -->\n";
    for (int i = 0; i < (int)exports.size(); i++) {
        const ScriptExportInfo &info = exports[i];
        godot::String decl = info.declaration_text;
        if (decl.is_empty()) {
            decl = "@export var " + info.name;
            if (!info.type_hint.is_empty()) {
                decl += ": " + info.type_hint;
            }
            if (info.has_default) {
                decl += " = " + info.default_value;
            }
        }

        godot::PackedStringArray decl_lines = decl.split("\n");
        for (int j = 0; j < decl_lines.size(); j++) {
            if (!decl_lines[j].strip_edges().is_empty()) {
                out += "  " + decl_lines[j].strip_edges() + "\n";
            }
        }
    }

    return out;
}

} // namespace

godot::Dictionary get_node_properties_single(const godot::Dictionary &args) {
    godot::Dictionary result;

    if (!args.has("scene_path") || !args.has("node_path")) {
        result["status"] = "failed";
        result["error"] = "Missing required args: scene_path, node_path";
        return result;
    }

    godot::String scene_path = normalize_path(args["scene_path"]);
    godot::String node_path = args["node_path"];
    result["scene_path"] = scene_path;
    result["node_path"] = node_path;

    FLOG_TOOL(godot::String("get_node_properties: scene=") + scene_path +
              " node=" + node_path);

    // Load .tscn file as text for property parsing
    godot::String file_text =
        godot::FileAccess::get_file_as_string(scene_path);
    if (file_text.is_empty()) {
        result["status"] = "failed";
        result["error"] = "Failed to read scene file: " + scene_path;
        return result;
    }

    // Instantiate for metadata (groups, connections, name/type)
    godot::Ref<godot::PackedScene> packed =
        godot::ResourceLoader::get_singleton()->load(
            scene_path, "PackedScene",
            godot::ResourceLoader::CACHE_MODE_IGNORE);
    if (!packed.is_valid()) {
        result["status"] = "failed";
        result["error"] = "Failed to load scene: " + scene_path;
        return result;
    }

    godot::Node *scene_root = packed->instantiate();
    if (!scene_root) {
        result["status"] = "failed";
        result["error"] = "Failed to instantiate scene: " + scene_path;
        return result;
    }

    godot::Node *target =
        get_node_properties::resolve_node(scene_root, node_path);
    if (!target) {
        godot::Array available;
        get_node_properties::collect_available_paths(
            scene_root, "", available);
        scene_root->queue_free();
        result["status"] = "failed";
        result["error"] = "Node not found: \"" + node_path +
                          "\". Available nodes: " +
                          godot::String(godot::Variant(available));
        return result;
    }

    // Build relative path for SceneState lookup
    godot::String relative_path;
    if (target == scene_root) {
        relative_path = ".";
    } else {
        relative_path = godot::String(scene_root->get_path_to(target));
    }

    // Parse .tscn and find node block
    get_node_properties::TscnData tscn_data =
        get_node_properties::parse_tscn(file_text);

    // Determine node_name and parent_path for .tscn lookup
    godot::String tscn_node_name = godot::String(target->get_name());
    godot::String node_type = target->get_class();
    godot::String tscn_parent_path;
    if (target == scene_root) {
        // Root node: no parent attribute in .tscn
        tscn_parent_path = "";
    } else {
        godot::String rel = relative_path;
        int last_slash = rel.rfind("/");
        if (last_slash == -1) {
            // Direct child of root
            tscn_parent_path = ".";
        } else {
            tscn_parent_path = rel.left(last_slash);
        }
    }

    godot::Vector<godot::String> node_lines =
        get_node_properties::find_node_block(
            file_text, tscn_node_name, tscn_parent_path);
    godot::HashMap<godot::String, godot::String> node_props =
        build_node_property_map(node_lines);
    godot::String script_path =
        get_script_path_from_node_lines(node_lines, tscn_data);

    // ---- Build output ----
    godot::String out;

    // Header: name, type, path
    out += godot::String(target->get_name()) + " (" +
           target->get_class() + ") path:" + relative_path + "\n";

    // Groups
    godot::TypedArray<godot::StringName> groups = target->get_groups();
    if (groups.size() > 0) {
        out += "groups: ";
        for (int g = 0; g < groups.size(); g++) {
            if (g > 0) out += ", ";
            out += godot::String(godot::StringName(groups[g]));
        }
        out += "\n";
    }

    // Instance
    godot::String scene_file = target->get_scene_file_path();
    if (target != scene_root && !scene_file.is_empty()) {
        out += "instance_of: " + scene_file + "\n";
    }

    // Script path comes from the parsed node block, not instantiation.
    if (!script_path.is_empty()) {
        out += "script: " + script_path + "\n";
        out += format_script_exports(script_path);
    }

    out += get_node_properties::format_inherited_theme_note(
        tscn_data, file_text, scene_root, target, relative_path,
        node_props.has("theme"));

    // Format properties from .tscn text
    out += get_node_properties::format_properties(
        tscn_data, node_lines, 0, scene_root, target);

    // Connections (from SceneState)
    godot::Ref<godot::SceneState> state = packed->get_state();
    if (state.is_valid()) {
        int state_idx = get_node_properties::find_scene_state_index(
            state, relative_path);
        if (state_idx >= 0) {
            godot::String conns =
                get_node_properties::format_connections(state, state_idx);
            if (!conns.is_empty()) {
                out += conns;
            }
        }
    }

    scene_root->queue_free();

    result["status"] = "success";
    result["scene_path"] = scene_path;
    result["node_path"] = node_path;
    result["resolved_path"] = relative_path;
    result["node_name"] = tscn_node_name;
    result["node_type"] = node_type;
    result["properties_line_count"] = count_text_lines(out);
    result["properties_text"] = out;
    return result;
}

godot::Dictionary FennaraGetNodePropertiesTool::execute(
    const godot::Dictionary &args) {
    godot::Dictionary result;

    if (!args.has("targets")) {
        result["success"] = false;
        result["tool_name"] = "get_node_properties";
        result["format_version"] = "get-node-properties-result-v1";
        result["error"] = "Missing required arg: targets";
        return result;
    }

    godot::Variant targets_var = args["targets"];
    if (targets_var.get_type() != godot::Variant::ARRAY) {
        result["success"] = false;
        result["tool_name"] = "get_node_properties";
        result["format_version"] = "get-node-properties-result-v1";
        result["error"] = "targets must be an array of objects";
        return result;
    }

    godot::Array targets = targets_var;
    if (targets.is_empty()) {
        result["success"] = false;
        result["tool_name"] = "get_node_properties";
        result["format_version"] = "get-node-properties-result-v1";
        result["error"] = "targets must contain at least one target";
        return result;
    }
    if (targets.size() > kMaxBatchTargets) {
        result["success"] = false;
        result["tool_name"] = "get_node_properties";
        result["format_version"] = "get-node-properties-result-v1";
        result["error"] =
            "targets supports at most " +
            godot::String::num_int64(kMaxBatchTargets) +
            " entries per call. Split larger requests into multiple calls.";
        return result;
    }

    godot::Array nodes;
    for (int i = 0; i < targets.size(); i++) {
        godot::Variant item = targets[i];
        if (item.get_type() != godot::Variant::DICTIONARY) {
            godot::Dictionary item_result;
            item_result["status"] = "failed";
            item_result["scene_path"] = "";
            item_result["node_path"] = "";
            item_result["error"] =
                "targets[" + godot::String::num_int64(i) +
                "] must be an object with scene_path and node_path";
            nodes.append(item_result);
            continue;
        }

        godot::Dictionary target = item;
        if (!target.has("scene_path") || !target.has("node_path")) {
            godot::Dictionary item_result;
            item_result["status"] = "failed";
            item_result["scene_path"] = target.get("scene_path", "");
            item_result["node_path"] = target.get("node_path", "");
            item_result["error"] =
                "targets[" + godot::String::num_int64(i) +
                "] is missing scene_path or node_path";
            nodes.append(item_result);
            continue;
        }

        nodes.append(get_node_properties_single(target));
    }

    int success_count = 0;
    int failure_count = 0;
    int total_property_lines = 0;
    for (int i = 0; i < nodes.size(); i++) {
        if (nodes[i].get_type() != godot::Variant::DICTIONARY) {
            failure_count++;
            continue;
        }
        godot::Dictionary node = nodes[i];
        if (godot::String(node.get("status", "")) == "success") {
            success_count++;
            total_property_lines += static_cast<int>(node.get("properties_line_count", 0));
        } else {
            failure_count++;
        }
    }

    godot::Dictionary summary;
    summary["status"] = failure_count == 0 ? "success" :
        (success_count == 0 ? "failed" : "partial");
    summary["requested_count"] = targets.size();
    summary["checked_count"] = nodes.size();
    summary["success_count"] = success_count;
    summary["failure_count"] = failure_count;
    summary["total_property_lines"] = total_property_lines;

    result["success"] = failure_count == 0;
    result["tool_name"] = "get_node_properties";
    result["format_version"] = "get-node-properties-result-v1";
    result["summary"] = summary;
    result["nodes"] = nodes;
    if (!(bool)result["success"]) {
        result["error"] = failure_count == nodes.size()
            ? "Failed to inspect requested node target(s)"
            : "Some node target(s) could not be inspected";
    }
    return result;
}

} // namespace fennara
