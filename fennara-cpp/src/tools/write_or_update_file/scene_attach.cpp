#include "fennara/tools/write_or_update_file.hpp"

#include "fennara/helpers.hpp"

#include <godot_cpp/classes/gd_script.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>

namespace fennara {

godot::Dictionary FennaraWriteOrUpdateFileTool::_attach_script_to_scene(
    const godot::String &script_path, const godot::String &scene_path,
    const godot::String &node_path) {
    godot::Dictionary result;

    godot::String normalized_scene = scene_path;
    if (!scene_path.begins_with("res://")) {
        normalized_scene = "res://" + scene_path;
    }

    if (!godot::ResourceLoader::get_singleton()->exists(normalized_scene)) {
        result["success"] = false;
        result["error"] = "Scene not found: " + scene_path;
        return result;
    }

    godot::Ref<godot::PackedScene> scene =
        godot::ResourceLoader::get_singleton()->load(
            normalized_scene, "",
            godot::ResourceLoader::CACHE_MODE_IGNORE);
    if (!scene.is_valid()) {
        result["success"] = false;
        result["error"] = "Failed to load scene: " + scene_path;
        return result;
    }

    godot::Node *root = scene->instantiate();
    if (root == nullptr) {
        result["success"] = false;
        result["error"] = "Failed to instantiate scene";
        return result;
    }

    godot::Node *target = _resolve_node(root, node_path);
    if (target == nullptr) {
        root->queue_free();
        result["success"] = false;
        result["error"] =
            "Node not found: " + node_path + " in " + scene_path;
        return result;
    }

    if (!godot::ResourceLoader::get_singleton()->exists(script_path)) {
        root->queue_free();
        result["success"] = false;
        result["error"] = "Script not found: " + script_path;
        return result;
    }

    godot::Ref<godot::GDScript> script =
        godot::ResourceLoader::get_singleton()->load(script_path);
    if (!script.is_valid()) {
        root->queue_free();
        result["success"] = false;
        result["error"] = "Invalid or non-GDScript file: " + script_path;
        return result;
    }

    godot::Error reload_err = script->reload();
    if (reload_err != godot::OK) {
        root->queue_free();
        result["success"] = false;
        result["error"] = "Script validation failed";
        return result;
    }

    target->set_script(script);

    godot::Ref<godot::PackedScene> packed;
    packed.instantiate();
    godot::Error pack_err = packed->pack(root);
    if (pack_err != godot::OK) {
        root->queue_free();
        result["success"] = false;
        result["error"] = "Failed to pack scene";
        return result;
    }

    godot::Error save_err =
        godot::ResourceSaver::get_singleton()->save(packed, normalized_scene);
    root->queue_free();
    if (save_err != godot::OK) {
        result["success"] = false;
        result["error"] = "Failed to save scene";
        return result;
    }

    fennara::notify_editor_filesystem(normalized_scene);

    result["success"] = true;
    return result;
}

godot::Node *FennaraWriteOrUpdateFileTool::_resolve_node(
    godot::Node *root, const godot::String &node_path) {
    godot::String p = node_path.strip_edges();
    if (p.is_empty() || p == "." || p == "/") {
        return root;
    }

    if (p.begins_with("/")) {
        p = p.substr(1);
    }

    int slash = p.find("/");
    godot::String head = (slash == -1) ? p : p.left(slash);
    if (head == godot::String(root->get_name())) {
        p = (slash == -1) ? godot::String() : p.substr(slash + 1);
    }

    if (p.is_empty() || p == godot::String(root->get_name())) {
        return root;
    }

    return root->get_node_or_null(godot::NodePath(p));
}

} // namespace fennara
