#include "fennara/tools/write_or_update_file.hpp"

#include "fennara/helpers.hpp"
#include "fennara/snapshot_manager.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/scene_state.hpp>
#include <godot_cpp/variant/array.hpp>

namespace fennara {

namespace {

constexpr int kMaxShaderOwnersToResave = 25;

bool has_supported_owner_extension(const godot::String &path) {
    return path.ends_with(".tscn") || path.ends_with(".tres");
}

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

bool text_references_shader(const godot::String &text,
                            const godot::String &shader_path) {
    if (text.find(shader_path) >= 0) {
        return true;
    }
    if (shader_path.begins_with("res://")) {
        return text.find(shader_path.substr(6)) >= 0;
    }
    return false;
}

void collect_shader_owner_paths(const godot::String &dir_path,
                                const godot::String &shader_path,
                                godot::Array &owners,
                                godot::Array &warnings) {
    godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(dir_path);
    if (dir.is_null()) {
        godot::Dictionary warning;
        warning["path"] = dir_path;
        warning["warning"] = "Could not open directory while scanning shader owners.";
        warnings.append(warning);
        return;
    }

    dir->list_dir_begin();
    while (true) {
        godot::String name = dir->get_next();
        if (name.is_empty()) {
            break;
        }
        if (name == "." || name == "..") {
            continue;
        }

        godot::String child_path = dir_path.path_join(name);
        if (dir->current_is_dir()) {
            if (name == ".godot" || name == ".git" || name == ".fennara") {
                continue;
            }
            collect_shader_owner_paths(child_path, shader_path, owners, warnings);
            continue;
        }

        if (!has_supported_owner_extension(child_path) || child_path == shader_path) {
            continue;
        }

        godot::String text = read_text_file(child_path);
        if (!text.is_empty() && text_references_shader(text, shader_path)) {
            owners.append(child_path);
        }
    }
    dir->list_dir_end();
}

bool packed_scene_has_inherited_root(
    const godot::Ref<godot::PackedScene> &packed) {
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
        return line.find(" parent=") < 0 &&
               line.find(" instance=ExtResource(") >= 0;
    }
    return false;
}

void snapshot_before_resave(const godot::String &path) {
    auto *snap = FennaraSnapshotManager::get_active();
    if (snap) {
        snap->snapshot_file(path);
    }
}

godot::Dictionary make_status(const godot::String &path,
                              const godot::String &status) {
    godot::Dictionary item;
    item["path"] = path;
    item["status"] = status;
    return item;
}

void resave_scene_owner(const godot::String &path, godot::Array &resaved,
                        godot::Array &skipped) {
    godot::String original_text = read_text_file(path);
    godot::Ref<godot::PackedScene> packed =
        godot::ResourceLoader::get_singleton()->load(
            path, "PackedScene", godot::ResourceLoader::CACHE_MODE_IGNORE);
    if (!packed.is_valid()) {
        godot::Dictionary item = make_status(path, "skipped");
        item["reason"] = "Failed to load scene.";
        skipped.append(item);
        return;
    }

    bool inherited_root_scene = packed_scene_has_inherited_root(packed);
    godot::PackedScene::GenEditState edit_state = inherited_root_scene
        ? godot::PackedScene::GEN_EDIT_STATE_MAIN_INHERITED
        : godot::PackedScene::GEN_EDIT_STATE_MAIN;
    godot::Node *root = packed->instantiate(edit_state);
    if (root == nullptr) {
        godot::Dictionary item = make_status(path, "skipped");
        item["reason"] = "Failed to instantiate scene.";
        skipped.append(item);
        return;
    }

    godot::Ref<godot::PackedScene> repacked;
    repacked.instantiate();
    godot::Error pack_err = repacked->pack(root);
    if (pack_err != godot::OK) {
        root->queue_free();
        godot::Dictionary item = make_status(path, "skipped");
        item["reason"] = "Failed to pack scene.";
        item["error_code"] = static_cast<int>(pack_err);
        skipped.append(item);
        return;
    }

    snapshot_before_resave(path);
    godot::Error save_err =
        godot::ResourceSaver::get_singleton()->save(repacked, path);
    root->queue_free();
    if (save_err != godot::OK) {
        godot::Dictionary item = make_status(path, "skipped");
        item["reason"] = "Failed to save scene.";
        item["error_code"] = static_cast<int>(save_err);
        skipped.append(item);
        return;
    }

    if (inherited_root_scene) {
        godot::String saved_text = read_text_file(path);
        if (!text_has_inherited_root_node(saved_text)) {
            if (!original_text.is_empty()) {
                write_text_file(path, original_text);
            }
            notify_editor_filesystem(path);
            godot::Dictionary item = make_status(path, "skipped");
            item["reason"] =
                "Refused to keep save because it flattened the inherited root.";
            skipped.append(item);
            return;
        }
    }

    notify_editor_filesystem(path);
    resaved.append(path);
}

void resave_resource_owner(const godot::String &path, godot::Array &resaved,
                           godot::Array &skipped) {
    godot::Ref<godot::Resource> resource =
        godot::ResourceLoader::get_singleton()->load(
            path, "", godot::ResourceLoader::CACHE_MODE_IGNORE);
    if (!resource.is_valid()) {
        godot::Dictionary item = make_status(path, "skipped");
        item["reason"] = "Failed to load resource.";
        skipped.append(item);
        return;
    }

    snapshot_before_resave(path);
    godot::Error save_err =
        godot::ResourceSaver::get_singleton()->save(resource, path);
    if (save_err != godot::OK) {
        godot::Dictionary item = make_status(path, "skipped");
        item["reason"] = "Failed to save resource.";
        item["error_code"] = static_cast<int>(save_err);
        skipped.append(item);
        return;
    }

    notify_editor_filesystem(path);
    resaved.append(path);
}

} // namespace

void FennaraWriteOrUpdateFileTool::_reserialize_shader_owners(
    const godot::String &shader_path, godot::Dictionary &result) {
    godot::Array owners;
    godot::Array warnings;
    collect_shader_owner_paths("res://", shader_path, owners, warnings);

    godot::Array resaved;
    godot::Array skipped;
    if (owners.size() > kMaxShaderOwnersToResave) {
        godot::Dictionary warning;
        warning["warning"] =
            "Too many shader owners found; skipped automatic reserialization.";
        warning["owner_count"] = owners.size();
        warning["limit"] = kMaxShaderOwnersToResave;
        warnings.append(warning);
    } else {
        for (int i = 0; i < owners.size(); i++) {
            godot::String owner_path = owners[i];
            if (owner_path.ends_with(".tscn")) {
                resave_scene_owner(owner_path, resaved, skipped);
            } else if (owner_path.ends_with(".tres")) {
                resave_resource_owner(owner_path, resaved, skipped);
            }
        }
    }

    result["shader_owner_count"] = owners.size();
    result["shader_owner_paths"] = owners;
    result["reserialized_resources"] = resaved;
    result["reserialize_warnings"] = warnings;
    result["reserialize_skipped"] = skipped;
}

} // namespace fennara
