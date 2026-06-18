#include "fennara/tools/get_scene_tree.hpp"
#include "fennara/helpers.hpp"
#include "fennara/logger.hpp"
#include "fennara/tools/scene_io.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/reg_ex.hpp>
#include <godot_cpp/classes/reg_ex_match.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace fennara {

namespace {

constexpr int kMaxBatchScenes = 5;

int count_tree_lines(const godot::String &tree) {
    if (tree.is_empty()) {
        return 0;
    }
    int lines = 1;
    for (int i = 0; i < tree.length(); i++) {
        if (tree[i] == '\n' && i < tree.length() - 1) {
            lines++;
        }
    }
    return lines;
}

int count_nodes(godot::Node *node) {
    if (node == nullptr) {
        return 0;
    }
    int total = 1;
    godot::TypedArray<godot::Node> children = node->get_children();
    for (int i = 0; i < children.size(); i++) {
        godot::Node *child =
            godot::Object::cast_to<godot::Node>((godot::Object *)children[i]);
        total += count_nodes(child);
    }
    return total;
}

godot::Dictionary inspect_single_scene(const godot::String &scene_path) {
    godot::Dictionary result;

    if (scene_path.is_empty()) {
        FLOG_ERR("Inspect: scene_path required");
        result["status"] = "failed";
        result["scene_path"] = scene_path;
        result["error"] = "scene_path required";
        return result;
    }
    FLOG_TOOL(godot::String("Inspect: scene=") + scene_path);

    godot::String normalized_path = scene_path;
    if (!scene_path.begins_with("res://")) {
        normalized_path = "res://" + scene_path;
    }

    if (!godot::ResourceLoader::get_singleton()->exists(normalized_path)) {
        FLOG_ERR(godot::String("Inspect: scene not found ") + scene_path);
        result["status"] = "failed";
        result["scene_path"] = normalized_path;
        result["error"] = "Scene not found: " + scene_path;
        return result;
    }

    godot::Ref<godot::PackedScene> packed_scene =
        scene_io::load_packed_scene(normalized_path,
                                    godot::ResourceLoader::CACHE_MODE_IGNORE);
    if (!packed_scene.is_valid()) {
        FLOG_ERR(godot::String("Inspect: failed to load scene ") + scene_path);
        result["status"] = "failed";
        result["scene_path"] = normalized_path;
        result["error"] = "Failed to load scene: " + scene_path;
        return result;
    }

    godot::Node *scene_root = scene_io::instantiate_scene(packed_scene);
    if (scene_root == nullptr) {
        FLOG_ERR(godot::String("Inspect: failed to instantiate ") + scene_path);
        result["status"] = "failed";
        result["scene_path"] = normalized_path;
        result["error"] = "Failed to instantiate scene";
        return result;
    }

    godot::String tree_output =
        FennaraGetSceneTreeTool::_build_tree_structure(scene_root, "", true);
    int node_count = count_nodes(scene_root);
    godot::String root_name = scene_root->get_name();
    godot::String root_type = scene_root->get_class();

    scene_root->queue_free();

    FLOG_TOOL(godot::String("Inspect: done, scene=") + normalized_path);
    result["status"] = "success";
    result["scene_path"] = normalized_path;
    result["root_name"] = root_name;
    result["root_type"] = root_type;
    result["node_count"] = node_count;
    result["tree_line_count"] = count_tree_lines(tree_output);
    result["tree"] = tree_output;
    return result;
}

} // namespace

void FennaraGetSceneTreeTool::_bind_methods() {
    godot::ClassDB::bind_static_method(
        "FennaraGetSceneTreeTool", godot::D_METHOD("execute", "args"),
        &FennaraGetSceneTreeTool::execute);
}

godot::Dictionary FennaraGetSceneTreeTool::execute(const godot::Dictionary &args) {
    godot::Dictionary result;

    if (!args.has("scene_paths")) {
        result["success"] = false;
        result["tool_name"] = "get_scene_tree";
        result["format_version"] = "get-scene-tree-result-v1";
        result["error"] = "Missing required arg: scene_paths";
        return result;
    }

    godot::Variant scene_paths_var = args["scene_paths"];
    if (scene_paths_var.get_type() != godot::Variant::ARRAY) {
        result["success"] = false;
        result["tool_name"] = "get_scene_tree";
        result["format_version"] = "get-scene-tree-result-v1";
        result["error"] = "scene_paths must be an array of strings";
        return result;
    }

    godot::Array scene_paths = scene_paths_var;
    if (scene_paths.is_empty()) {
        result["success"] = false;
        result["tool_name"] = "get_scene_tree";
        result["format_version"] = "get-scene-tree-result-v1";
        result["error"] = "scene_paths must contain at least one path";
        return result;
    }
    if (scene_paths.size() > kMaxBatchScenes) {
        result["success"] = false;
        result["tool_name"] = "get_scene_tree";
        result["format_version"] = "get-scene-tree-result-v1";
        result["error"] =
            "scene_paths supports at most " +
            godot::String::num_int64(kMaxBatchScenes) +
            " scenes per call. Split larger requests into multiple calls.";
        return result;
    }

    godot::Array scenes;
    for (int i = 0; i < scene_paths.size(); i++) {
        godot::Variant item = scene_paths[i];
        if (item.get_type() != godot::Variant::STRING) {
            godot::Dictionary item_result;
            item_result["status"] = "failed";
            item_result["scene_path"] = "";
            item_result["error"] =
                "scene_paths[" + godot::String::num_int64(i) +
                "] must be a string";
            scenes.append(item_result);
            continue;
        }

        scenes.append(inspect_single_scene(item));
    }

    int success_count = 0;
    int failure_count = 0;
    int total_nodes = 0;
    for (int i = 0; i < scenes.size(); i++) {
        if (scenes[i].get_type() != godot::Variant::DICTIONARY) {
            failure_count++;
            continue;
        }
        godot::Dictionary scene = scenes[i];
        if (godot::String(scene.get("status", "")) == "success") {
            success_count++;
            total_nodes += static_cast<int>(scene.get("node_count", 0));
        } else {
            failure_count++;
        }
    }

    godot::Dictionary summary;
    summary["status"] = failure_count == 0 ? "success" :
        (success_count == 0 ? "failed" : "partial");
    summary["requested_count"] = scene_paths.size();
    summary["checked_count"] = scenes.size();
    summary["success_count"] = success_count;
    summary["failure_count"] = failure_count;
    summary["total_nodes"] = total_nodes;

    result["success"] = failure_count == 0;
    result["tool_name"] = "get_scene_tree";
    result["format_version"] = "get-scene-tree-result-v1";
    result["summary"] = summary;
    result["scenes"] = scenes;
    if (!(bool)result["success"]) {
        result["error"] = failure_count == scenes.size()
            ? "Failed to inspect requested scene(s)"
            : "Some scene(s) could not be inspected";
    }
    return result;
}

godot::String FennaraGetSceneTreeTool::_build_tree_structure(
    godot::Node *node, const godot::String &indent, bool is_last) {

    godot::String output;
    godot::String node_name = node->get_name();
    godot::String node_type = node->get_class();
    godot::String metadata;

    godot::String scene_file = node->get_scene_file_path();
    if (!scene_file.is_empty()) {
        metadata = " [instance: " + scene_file + "]";
    }

    godot::Ref<godot::Script> script = node->get_script();
    if (script.is_valid() && !script->get_path().is_empty()) {
        int line_count = _get_script_line_count(script->get_path());
        godot::String script_info = " [script: " + script->get_path();
        if (line_count > 0) {
            script_info +=
                "(has " + godot::String::num_int64(line_count) + " lines)";
        }
        script_info += "]";
        metadata += script_info;
    }

    godot::String prefix;
    if (!indent.is_empty()) {
        prefix = is_last ? godot::String::utf8("└─ ")
                         : godot::String::utf8("├─ ");
    }

    output += indent + prefix + node_name + " (" + node_type + ")" + metadata +
              "\n";

    godot::TypedArray<godot::Node> children = node->get_children();
    if (children.size() > 0) {
        godot::Array grouped = _group_children(children);
        godot::String child_indent =
            indent +
            (is_last ? godot::String("   ")
                     : godot::String::utf8("│  "));

        for (int i = 0; i < grouped.size(); i++) {
            godot::Dictionary child_data = grouped[i];
            bool is_last_child = (i == grouped.size() - 1);

            if (child_data.has("group")) {
                output += _build_grouped_node_line(child_data, child_indent,
                                                   is_last_child);
            } else {
                godot::Node *child_node =
                    godot::Object::cast_to<godot::Node>(
                        (godot::Object *)child_data["node"]);
                output += _build_tree_structure(child_node, child_indent,
                                               is_last_child);
            }
        }
    }

    return output;
}

godot::String FennaraGetSceneTreeTool::_build_grouped_node_line(
    const godot::Dictionary &group_data, const godot::String &indent,
    bool is_last) {

    godot::String base_name = group_data["base_name"];
    int start_num = group_data["start_num"];
    int end_num = group_data["end_num"];
    godot::String node_type = group_data["node_type"];
    godot::String metadata = group_data["metadata"];

    godot::String prefix = is_last ? godot::String::utf8("└─ ")
                                   : godot::String::utf8("├─ ");

    godot::String range_name = base_name +
                               godot::String::num_int64(start_num) + "-" +
                               godot::String::num_int64(end_num);

    return indent + prefix + range_name + " (" + node_type + ")" + metadata +
           "\n";
}

godot::Array FennaraGetSceneTreeTool::_group_children(
    const godot::TypedArray<godot::Node> &children) {

    godot::Array result;
    if (children.size() == 0) {
        return result;
    }

    int i = 0;
    while (i < children.size()) {
        godot::Node *current_node =
            godot::Object::cast_to<godot::Node>(
                (godot::Object *)children[i]);
        godot::Variant parsed = _parse_node_name(current_node->get_name());

        if (parsed.get_type() != godot::Variant::DICTIONARY) {
            godot::Dictionary single;
            single["node"] = current_node;
            result.append(single);
            i++;
            continue;
        }

        godot::Dictionary parsed_dict = parsed;
        godot::String base_name = parsed_dict["base"];
        int start_num = parsed_dict["number"];
        godot::String current_type = current_node->get_class();
        godot::String current_scene_path =
            current_node->get_scene_file_path();
        godot::Ref<godot::Script> current_script =
            current_node->get_script();
        godot::String current_script_path =
            current_script.is_valid() ? current_script->get_path()
                                      : godot::String();

        int end_num = start_num;
        int j = i + 1;
        bool can_group = false;

        while (j < children.size()) {
            godot::Node *next_node =
                godot::Object::cast_to<godot::Node>(
                    (godot::Object *)children[j]);
            godot::Variant next_parsed =
                _parse_node_name(next_node->get_name());

            if (next_parsed.get_type() != godot::Variant::DICTIONARY) {
                break;
            }

            godot::Dictionary next_dict = next_parsed;
            if (godot::String(next_dict["base"]) == base_name &&
                int(next_dict["number"]) == end_num + 1 &&
                next_node->get_class() == current_type) {

                godot::String next_scene_path =
                    next_node->get_scene_file_path();
                godot::Ref<godot::Script> next_script =
                    next_node->get_script();
                godot::String next_script_path =
                    next_script.is_valid() ? next_script->get_path()
                                           : godot::String();

                if (next_scene_path == current_scene_path &&
                    next_script_path == current_script_path) {
                    end_num = int(next_dict["number"]);
                    j++;
                    can_group = true;
                } else {
                    break;
                }
            } else {
                break;
            }
        }

        if (can_group && end_num > start_num) {
            godot::String metadata;
            if (!current_scene_path.is_empty()) {
                metadata = " [instance: " + current_scene_path + "]";
            }
            if (!current_script_path.is_empty()) {
                int line_count = _get_script_line_count(current_script_path);
                godot::String script_info = " [script: " + current_script_path;
                if (line_count > 0) {
                    script_info += "(has " +
                                   godot::String::num_int64(line_count) +
                                   " lines)";
                }
                script_info += "]";
                metadata += script_info;
            }

            godot::Dictionary group;
            group["group"] = true;
            group["base_name"] = base_name;
            group["start_num"] = start_num;
            group["end_num"] = end_num;
            group["node_type"] = current_type;
            group["metadata"] = metadata;
            result.append(group);
            i = j;
        } else {
            godot::Dictionary single;
            single["node"] = current_node;
            result.append(single);
            i++;
        }
    }

    return result;
}

godot::Variant FennaraGetSceneTreeTool::_parse_node_name(
    const godot::String &node_name) {

    godot::Ref<godot::RegEx> regex;
    regex.instantiate();
    regex->compile("^(.+?)(\\d+)$");
    godot::Ref<godot::RegExMatch> match = regex->search(node_name);

    if (match.is_valid()) {
        godot::Dictionary result;
        result["base"] = match->get_string(1);
        result["number"] = match->get_string(2).to_int();
        return result;
    }

    return godot::Variant();
}

int FennaraGetSceneTreeTool::_get_script_line_count(
    const godot::String &script_path) {

    if (!godot::FileAccess::file_exists(script_path)) {
        return 0;
    }

    godot::Ref<godot::FileAccess> file =
        godot::FileAccess::open(script_path, godot::FileAccess::READ);
    if (!file.is_valid()) {
        return 0;
    }

    int line_count = 0;
    while (!file->eof_reached()) {
        file->get_line();
        line_count++;
    }

    file->close();
    return line_count;
}

} // namespace fennara
