#include "fennara/tools/validate_scene.hpp"

#include <godot_cpp/classes/animation_player.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

namespace fennara {

namespace {

void s_check_node_paths_recursive(
    godot::Node *node, godot::Node *root, godot::Array &issues) {

    godot::TypedArray<godot::Dictionary> props = node->get_property_list();
    for (int p = 0; p < props.size(); p++) {
        godot::Dictionary prop = props[p];
        int type = prop.get("type", 0);
        if (type != godot::Variant::NODE_PATH) continue;

        godot::String prop_name = prop.get("name", "");
        if (prop_name.is_empty()) continue;

        int usage = prop.get("usage", 0);
        if (!(usage & godot::PROPERTY_USAGE_STORAGE)) continue;
        bool is_exported_script_var =
            (usage & godot::PROPERTY_USAGE_SCRIPT_VARIABLE) != 0;

        godot::NodePath path = node->get(godot::StringName(prop_name));
        if (path.is_empty()) continue;

        if (prop_name == "root_node" &&
            godot::Object::cast_to<godot::AnimationPlayer>(node)) {
            continue;
        }

        godot::Node *target = node->get_node_or_null(path);
        if (!target) {
            godot::String node_path =
                godot::String(root->get_path_to(node));

            godot::Dictionary issue;
            issue["node"] = node_path;
            issue["node_path"] = node_path;
            issue["check"] = "invalid_node_path";
            issue["severity"] = is_exported_script_var ? "error" : "warning";
            issue["message"] =
                godot::String("Property '") + prop_name +
                "' has NodePath '" + godot::String(path) +
                "' which does not resolve to any node in the scene" +
                (is_exported_script_var
                     ? godot::String("; exported NodePaths are commonly used with get_node() and can break at runtime")
                     : godot::String());
            issues.append(issue);
        }
    }

    int child_count = node->get_child_count();
    for (int c = 0; c < child_count; c++) {
        s_check_node_paths_recursive(node->get_child(c), root, issues);
    }
}

} // namespace

void FennaraValidateSceneTool::_check_invalid_node_paths(
    const godot::String &scene_path, godot::Array &issues) {
    godot::Ref<godot::PackedScene> packed =
        godot::ResourceLoader::get_singleton()->load(
            scene_path, "PackedScene",
            godot::ResourceLoader::CACHE_MODE_IGNORE);
    if (!packed.is_valid()) return;

    godot::Node *root = packed->instantiate();
    if (!root) return;

    s_check_node_paths_recursive(root, root, issues);

    root->queue_free();
}

} // namespace fennara
