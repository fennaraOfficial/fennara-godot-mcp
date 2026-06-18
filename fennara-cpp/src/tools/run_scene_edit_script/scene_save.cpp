#include "fennara/tools/run_scene_edit_script/internal.hpp"

#include "fennara/helpers.hpp"
#include "fennara/snapshot_manager.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/scene_state.hpp>

namespace fennara::run_scene_edit_script_internal {

namespace {

godot::String read_text_file(const godot::String &path) {
    godot::Ref<godot::FileAccess> file =
        godot::FileAccess::open(path, godot::FileAccess::READ);
    if (file.is_null()) {
        return "";
    }
    return file->get_as_text();
}

bool write_text_file(const godot::String &path, const godot::String &text) {
    godot::Ref<godot::FileAccess> file =
        godot::FileAccess::open(path, godot::FileAccess::WRITE);
    if (file.is_null()) {
        return false;
    }
    file->store_string(text);
    return true;
}

bool packed_scene_has_inherited_root(const godot::Ref<godot::PackedScene> &packed) {
    if (!packed.is_valid()) {
        return false;
    }
    godot::Ref<godot::SceneState> state = packed->get_state();
    if (!state.is_valid()) {
        return false;
    }
    return state->get_base_scene_state().is_valid();
}

bool text_has_inherited_root_node(const godot::String &text) {
    godot::PackedStringArray lines = text.split("\n");
    for (int i = 0; i < lines.size(); i++) {
        godot::String line = lines[i].strip_edges();
        if (!line.begins_with("[node ")) {
            continue;
        }
        return line.find(" parent=") < 0 && line.find(" instance=ExtResource(") >= 0;
    }
    return false;
}

} // namespace

bool load_or_prepare_scene(const godot::String &normalized_scene,
                           godot::Dictionary &result,
                           godot::Node *&root_node,
                           bool &created_new_scene,
                           bool &inherited_root_scene) {
    bool scene_exists = godot::ResourceLoader::get_singleton()->exists(normalized_scene);
    created_new_scene = !scene_exists;
    inherited_root_scene = false;
    root_node = nullptr;

    if (scene_exists) {
        godot::Ref<godot::PackedScene> packed =
            godot::ResourceLoader::get_singleton()->load(
                normalized_scene, "PackedScene",
                godot::ResourceLoader::CACHE_MODE_IGNORE);
        if (!packed.is_valid()) {
            result["success"] = false;
            result["error"] = "Failed to load scene: " + normalized_scene;
            result["runtime_errors"] = godot::Array();
            result["logs"] = godot::Array();
            return false;
        }

        inherited_root_scene = packed_scene_has_inherited_root(packed);
        result["inherited_root_scene"] = inherited_root_scene;

        godot::PackedScene::GenEditState edit_state = inherited_root_scene
            ? godot::PackedScene::GEN_EDIT_STATE_MAIN_INHERITED
            : godot::PackedScene::GEN_EDIT_STATE_MAIN;
        root_node = packed->instantiate(edit_state);
        if (root_node == nullptr) {
            result["success"] = false;
            result["error"] = "Failed to instantiate scene: " + normalized_scene;
            result["runtime_errors"] = godot::Array();
            result["logs"] = godot::Array();
            return false;
        }
        return true;
    }

    godot::String dir_path = normalized_scene.get_base_dir();
    if (!godot::DirAccess::dir_exists_absolute(dir_path)) {
        godot::Error dir_err = godot::DirAccess::make_dir_recursive_absolute(dir_path);
        if (dir_err != godot::OK) {
            result["success"] = false;
            result["error"] = "Failed to create directory: " + dir_path;
            result["runtime_errors"] = godot::Array();
            result["logs"] = godot::Array();
            return false;
        }
    }
    result["inherited_root_scene"] = false;
    return true;
}

void append_capture_errors(const godot::Array &captured,
                           godot::Array &runtime_errors) {
    for (int i = 0; i < captured.size(); i++) {
        godot::Dictionary entry = captured[i];
        godot::String type = entry.get("type", "");
        if (type == "warning") {
            continue;
        }
        runtime_errors.append(entry);
    }
}

bool save_scene(godot::Node *root_node,
                const godot::String &normalized_scene,
                bool created_new_scene,
                bool inherited_root_scene,
                godot::Dictionary &result) {
    godot::String original_text;
    if (inherited_root_scene && !created_new_scene) {
        original_text = read_text_file(normalized_scene);
    }

    godot::Ref<godot::PackedScene> packed;
    packed.instantiate();
    godot::Error pack_err = packed->pack(root_node);
    if (pack_err != godot::OK) {
        root_node->queue_free();
        result["success"] = false;
        result["error"] = "Failed to pack generated scene.";
        return false;
    }

    auto *snap = FennaraSnapshotManager::get_active();
    if (snap) {
        if (created_new_scene) {
            snap->snapshot_created(normalized_scene);
        } else {
            snap->snapshot_file(normalized_scene);
        }
    }

    godot::Error save_err =
        godot::ResourceSaver::get_singleton()->save(packed, normalized_scene);
    root_node->queue_free();
    if (save_err != godot::OK) {
        result["success"] = false;
        result["error"] = "Failed to save scene.";
        return false;
    }

    if (inherited_root_scene && !created_new_scene) {
        godot::String saved_text = read_text_file(normalized_scene);
        if (!text_has_inherited_root_node(saved_text)) {
            if (!original_text.is_empty()) {
                write_text_file(normalized_scene, original_text);
            }
            notify_editor_filesystem(normalized_scene);
            result["success"] = false;
            result["scene_saved"] = false;
            result["inherited_root_scene"] = true;
            result["error"] =
                "run_scene_edit_script refused to keep a save that flattened the inherited scene root. "
                "The original scene file was restored.";
            result["scene_status_note"] =
                "Scene was not changed because preserving the inherited root failed.";
            return false;
        }
        result["inherited_root_preserved"] = true;
    }

    notify_editor_filesystem(normalized_scene);

    result["scene_saved"] = true;
    result["scene_created"] = created_new_scene;
    if (created_new_scene) {
        result["scene_status_note"] = "Scene was created and saved successfully.";
    } else {
        result["scene_status_note"] = "Scene changes were saved successfully.";
    }
    return true;
}

} // namespace fennara::run_scene_edit_script_internal
