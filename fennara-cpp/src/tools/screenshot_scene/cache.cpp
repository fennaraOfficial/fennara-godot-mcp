#include "fennara/tools/screenshot_scene.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>

namespace fennara {

namespace {

void cleanup_screenshot_cache(const godot::String &dir) {
    const int64_t max_age_msec = 7LL * 24LL * 60LL * 60LL * 1000LL;
    const int max_pngs = 200;
    int64_t now = godot::Time::get_singleton()->get_ticks_msec();

    godot::Ref<godot::DirAccess> da = godot::DirAccess::open(dir);
    if (da.is_null()) {
        return;
    }

    godot::Array kept;
    da->list_dir_begin();
    while (true) {
        godot::String file = da->get_next();
        if (file.is_empty()) {
            break;
        }
        if (da->current_is_dir() || !file.to_lower().ends_with(".png")) {
            continue;
        }

        godot::String path = dir.path_join(file);
        uint64_t modified = godot::FileAccess::get_modified_time(path);
        int64_t modified_msec = int64_t(modified) * 1000LL;
        if (modified_msec > 0 && now - modified_msec > max_age_msec) {
            da->remove(file);
            continue;
        }

        godot::Dictionary entry;
        entry["path"] = path;
        entry["modified"] = int64_t(modified);
        kept.append(entry);
    }
    da->list_dir_end();

    while (kept.size() > max_pngs) {
        int oldest_index = 0;
        int64_t oldest_time = int64_t(godot::Dictionary(kept[0])["modified"]);
        for (int i = 1; i < kept.size(); i++) {
            godot::Dictionary entry = kept[i];
            int64_t modified = int64_t(entry["modified"]);
            if (modified < oldest_time) {
                oldest_time = modified;
                oldest_index = i;
            }
        }

        godot::Dictionary oldest = kept[oldest_index];
        godot::String path = oldest["path"];
        da->remove(path.get_file());
        kept.remove_at(oldest_index);
    }
}

} // namespace

godot::String FennaraScreenshotSceneTool::_make_name_hint(
    const godot::String &scene_path,
    const godot::String &target_node_path,
    const godot::String &view) {
    godot::String scene_name = scene_path.get_file().get_basename();
    if (scene_name.is_empty()) {
        scene_name = "scene";
    }

    godot::String hint = scene_name;
    if (!target_node_path.strip_edges().is_empty()) {
        hint += "__" + target_node_path.get_file();
    }
    if (!view.strip_edges().is_empty()) {
        hint += "__" + view;
    }
    return hint;
}

godot::String FennaraScreenshotSceneTool::_save_png_data(
    const godot::PackedByteArray &png_data,
    const godot::String &name_hint,
    godot::Dictionary &result) {
    if (png_data.is_empty()) {
        result["save_error"] = "PNG data was empty";
        return "";
    }

    godot::String artifact_dir = _artifact_dir_ref();
    godot::String dir = artifact_dir.is_empty()
        ? godot::String("user://.fennara/screenshots")
        : artifact_dir.path_join("screenshots");
    godot::String abs_dir =
        godot::ProjectSettings::get_singleton()->globalize_path(dir);
    godot::Error dir_err = godot::DirAccess::make_dir_recursive_absolute(abs_dir);
    if (dir_err != godot::OK) {
        result["save_error"] = "Could not create screenshot directory: " + abs_dir;
        result["save_error_code"] = int(dir_err);
        return "";
    }
    cleanup_screenshot_cache(dir);

    godot::String safe_hint = name_hint.strip_edges().to_lower();
    safe_hint = safe_hint.replace(" ", "_")
                         .replace("/", "_")
                         .replace("\\", "_")
                         .replace(":", "_")
                         .replace("@", "_")
                         .replace(".", "_");
    if (safe_hint.is_empty()) {
        safe_hint = "image";
    }

    int64_t ticks = godot::Time::get_singleton()->get_ticks_msec();
    godot::String image_path =
        dir.path_join(godot::String("fennara_screenshot_") + safe_hint + "_" +
                      godot::String::num_int64(ticks) + ".png");
    godot::Ref<godot::FileAccess> file =
        godot::FileAccess::open(image_path, godot::FileAccess::WRITE);
    if (file.is_null()) {
        result["save_error"] = "Could not open screenshot path: " + image_path;
        return "";
    }

    file->store_buffer(png_data);
    file.unref();

    godot::String abs_path =
        godot::ProjectSettings::get_singleton()->globalize_path(image_path);
    result["image_res_path"] = image_path;
    result["image_path"] = abs_path;
    result["screenshot_dir"] = dir;
    result["screenshot_absolute_dir"] = abs_dir;
    result["transport"] = "file_path";
    return image_path;
}

} // namespace fennara
