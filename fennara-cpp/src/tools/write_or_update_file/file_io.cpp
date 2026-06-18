#include "fennara/tools/write_or_update_file.hpp"

#include "fennara/helpers.hpp"
#include "fennara/snapshot_manager.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/gd_script.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/shader.hpp>

namespace fennara {

godot::String FennaraWriteOrUpdateFileTool::_normalize_file_path(
    const godot::String &path) {
    if (path.begins_with("res://")) {
        return path;
    }
    return "res://" + path;
}

bool FennaraWriteOrUpdateFileTool::_ensure_parent_dir(
    const godot::String &path, godot::Dictionary &result) {
    godot::String dir_path = path.get_base_dir();
    if (godot::DirAccess::dir_exists_absolute(dir_path)) {
        return true;
    }

    godot::Error dir_err =
        godot::DirAccess::make_dir_recursive_absolute(dir_path);
    if (dir_err == godot::OK) {
        return true;
    }

    result["success"] = false;
    result["error"] = "Failed to create directory: " + dir_path;
    return false;
}

void FennaraWriteOrUpdateFileTool::_snapshot_before_write(
    const godot::String &path, bool file_exists) {
    auto *snap = FennaraSnapshotManager::get_active();
    if (!snap) {
        return;
    }

    if (file_exists) {
        snap->snapshot_file(path);
    } else {
        snap->snapshot_created(path);
    }
}

godot::Dictionary FennaraWriteOrUpdateFileTool::_read_content(
    const godot::String &path, const godot::String &input_path) {
    godot::Dictionary result;

    if (path.ends_with(".gd")) {
        godot::Ref<godot::GDScript> script =
            godot::ResourceLoader::get_singleton()->load(path);
        if (!script.is_valid()) {
            result["success"] = false;
            result["error"] = "Failed to load script: " + input_path;
            return result;
        }
        result["success"] = true;
        result["content"] = script->get_source_code();
        return result;
    }

    godot::Ref<godot::FileAccess> file =
        godot::FileAccess::open(path, godot::FileAccess::READ);
    if (!file.is_valid()) {
        result["success"] = false;
        result["error"] = "Cannot open file for reading: " + input_path;
        return result;
    }

    result["success"] = true;
    result["content"] = file->get_as_text();
    file->close();
    return result;
}

godot::Dictionary FennaraWriteOrUpdateFileTool::_write_content(
    const godot::String &path, const godot::String &content,
    const godot::String &input_path, bool file_exists) {
    godot::Dictionary result;

    if (path.ends_with(".gd")) {
        godot::Ref<godot::GDScript> script;
        if (file_exists) {
            script = godot::ResourceLoader::get_singleton()->load(path);
        }
        if (!script.is_valid()) {
            script.instantiate();
        }

        script->set_source_code(content);

        godot::Error save_err =
            godot::ResourceSaver::get_singleton()->save(script, path);
        if (save_err != godot::OK) {
            result["success"] = false;
            result["error"] =
                "Failed to save script: " +
                godot::String::num_int64(static_cast<int>(save_err));
            return result;
        }

        if (godot::Engine::get_singleton()->is_editor_hint()) {
            script->reload();
        }

        result["success"] = true;
        return result;
    }

    godot::Ref<godot::FileAccess> file =
        godot::FileAccess::open(path, godot::FileAccess::WRITE);
    if (!file.is_valid()) {
        result["success"] = false;
        result["error"] = "Cannot open file for writing: " + input_path;
        return result;
    }

    file->store_string(content);
    file->close();

    result["success"] = true;
    return result;
}

void FennaraWriteOrUpdateFileTool::_refresh_cached_shader_resource(
    const godot::String &path, const godot::String &content) {
    godot::Ref<godot::Shader> shader =
        godot::ResourceLoader::get_singleton()->load(
            path, "Shader", godot::ResourceLoader::CACHE_MODE_REUSE);
    if (!shader.is_valid()) {
        return;
    }

    shader->set_code(content);
}

bool FennaraWriteOrUpdateFileTool::_is_protected_path(
    const godot::String &path) {
    if (path == "res://project.godot") {
        return true;
    }
    if (path.begins_with("res://addons/fennara/")) {
        return true;
    }
    return false;
}

} // namespace fennara
