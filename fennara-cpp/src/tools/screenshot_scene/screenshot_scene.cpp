#include "fennara/tools/screenshot_scene.hpp"

#include "fennara/logger.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace fennara {

bool FennaraScreenshotSceneTool::_is_3d_scene = false;

godot::String &FennaraScreenshotSceneTool::_current_scene_path_ref() {
    static godot::String *value = new godot::String;
    return *value;
}

godot::String &FennaraScreenshotSceneTool::_capture_name_hint_ref() {
    static godot::String *value = new godot::String;
    return *value;
}

godot::String &FennaraScreenshotSceneTool::_artifact_dir_ref() {
    static godot::String *value = new godot::String;
    return *value;
}

godot::SubViewport *&FennaraScreenshotSceneTool::_camera_capture_viewport_ref() {
    static godot::SubViewport *value = nullptr;
    return value;
}

godot::Node *&FennaraScreenshotSceneTool::_camera_capture_root_ref() {
    static godot::Node *value = nullptr;
    return value;
}

void FennaraScreenshotSceneTool::_bind_methods() {
    godot::ClassDB::bind_static_method(
        "FennaraScreenshotSceneTool", godot::D_METHOD("open_scene", "scene_path"),
        &FennaraScreenshotSceneTool::open_scene);
    godot::ClassDB::bind_static_method(
        "FennaraScreenshotSceneTool", godot::D_METHOD("navigate", "args"),
        &FennaraScreenshotSceneTool::navigate);
    godot::ClassDB::bind_static_method(
        "FennaraScreenshotSceneTool", godot::D_METHOD("capture"),
        &FennaraScreenshotSceneTool::capture);
    godot::ClassDB::bind_static_method(
        "FennaraScreenshotSceneTool", godot::D_METHOD("make_collage", "images"),
        &FennaraScreenshotSceneTool::make_collage);
    godot::ClassDB::bind_static_method(
        "FennaraScreenshotSceneTool", godot::D_METHOD("execute", "args"),
        &FennaraScreenshotSceneTool::execute);
}

godot::Dictionary FennaraScreenshotSceneTool::execute(const godot::Dictionary &args) {
    godot::Dictionary result;

    godot::String artifact_dir =
        godot::String(args.get("_fennara_tool_artifact_dir", "")).strip_edges();
    _artifact_dir_ref() = artifact_dir.is_empty()
        ? godot::String()
        : artifact_dir.path_join("screenshot_scene");

    godot::String scene_path = args.get("scene_path", "");
    if (scene_path.is_empty()) {
        FLOG_ERR("SS: scene_path is required");
        result["success"] = false;
        result["error"] = "scene_path is required";
        return result;
    }

    return open_scene(scene_path);
}

} // namespace fennara
