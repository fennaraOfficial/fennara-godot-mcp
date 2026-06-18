#include "fennara/file_utils.hpp"
#include "fennara/addon_access.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>

namespace fennara {
namespace file_utils {

namespace {
void scan_dir_recursive(godot::String path, godot::Array &results,
                        bool include_csharp, bool include_gdshader) {
    godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(path);
    if (!dir.is_valid()) {
        return;
    }

    godot::String addon_root = addon_access::addon_root_for_path(path);
    if (!addon_root.is_empty() && !addon_access::is_addon_root_allowed(addon_root)) {
        return;
    }

    dir->list_dir_begin();
    godot::String file_name = dir->get_next();
    while (!file_name.is_empty()) {
        if (file_name.begins_with(".")) {
            file_name = dir->get_next();
            continue;
        }

        godot::String full_path = path.path_join(file_name);
        if (dir->current_is_dir()) {
            scan_dir_recursive(full_path, results, include_csharp,
                               include_gdshader);
        } else if (file_name.ends_with(".gd") ||
                   (include_csharp && file_name.ends_with(".cs")) ||
                   (include_gdshader && file_name.ends_with(".gdshader"))) {
            results.append(
                godot::ProjectSettings::get_singleton()->globalize_path(
                    full_path));
        }
        file_name = dir->get_next();
    }
    dir->list_dir_end();
}
}

godot::Array find_all_gd_files() {
    godot::Array results;
    scan_dir_recursive("res://", results, false, false);
    return results;
}

godot::Array find_all_indexable_script_files() {
    godot::Array results;
    scan_dir_recursive("res://", results, true, false);
    return results;
}

godot::Array find_all_diagnostic_files() {
    godot::Array results;
    scan_dir_recursive("res://", results, true, true);
    return results;
}

void scan_dir_recursive(godot::String path, godot::Array &results) {
    scan_dir_recursive(path, results, false, false);
}

godot::String resolve_path(godot::String file_path) {
    if (file_path.begins_with("res://")) {
        return godot::ProjectSettings::get_singleton()->globalize_path(
            file_path);
    } else if (file_path.is_absolute_path()) {
        return file_path;
    }
    return godot::ProjectSettings::get_singleton()->globalize_path(
        "res://" + file_path);
}

godot::String read_file_content(godot::String abs_path) {
    godot::Ref<godot::FileAccess> file =
        godot::FileAccess::open(abs_path, godot::FileAccess::READ);
    if (!file.is_valid()) {
        return "";
    }
    godot::String content = file->get_as_text();
    file->close();
    return content;
}

godot::String path_to_uri(godot::String abs_path) {
    godot::String normalized = abs_path.replace("\\", "/");
    if (!normalized.begins_with("/")) {
        normalized = "/" + normalized;
    }
    return "file://" + normalized;
}

godot::String uri_to_res_path(godot::String uri) {
    godot::String abs_path =
        uri.replace("file:///", "").replace("file://", "");
    abs_path = abs_path.uri_decode();
    godot::String project_root =
        godot::ProjectSettings::get_singleton()
            ->globalize_path("res://")
            .replace("\\", "/");
    abs_path = abs_path.replace("\\", "/");

    // Some URI forms preserve a leading slash before the Windows drive letter,
    // e.g. "/C:/project/file.gd". Normalize that to "C:/project/file.gd".
    if (abs_path.length() >= 3 && abs_path[0] == '/' && abs_path[2] == ':') {
        abs_path = abs_path.substr(1);
    }

    if (abs_path.begins_with(project_root)) {
        return "res://" + abs_path.substr(project_root.length());
    }
    return abs_path;
}

} // namespace file_utils
} // namespace fennara
