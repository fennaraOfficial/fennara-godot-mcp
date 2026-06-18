#include "fennara/executor.hpp"

#include "fennara/tools/screenshot_scene.hpp"

#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>

namespace fennara {

void FennaraExecutor::_start_next_screenshot_scene() {
    if (_batch_cancelled || _screenshot_running ||
        _pending_screenshot_scenes.empty()) {
        return;
    }

    PendingScreenshotScene pending = _pending_screenshot_scenes.front();
    _pending_screenshot_scenes.erase(_pending_screenshot_scenes.begin());
    _screenshot_running = true;

    godot::Dictionary open_result = FennaraScreenshotSceneTool::execute(pending.args);
    if (!(bool)open_result.get("success", false)) {
        _screenshot_running = false;
        _on_async_tool_complete(open_result, pending.tool_index, "screenshot_scene", pending.args);
        _start_next_screenshot_scene();
        return;
    }

    _screenshot_tool_index = pending.tool_index;
    _screenshot_args = pending.args;
    _screenshot_nav_result = godot::Dictionary();
    _screenshot_views = godot::Array();
    _screenshot_captures = godot::Array();
    _screenshot_view_index = 0;
    bool has_camera_path = pending.args.has("camera_path") &&
        !godot::String(pending.args.get("camera_path", "")).strip_edges().is_empty();
    if ((bool)open_result.get("is_3d", false) && !has_camera_path) {
        godot::String view = pending.args.get("view", "perspective");
        view = view.to_lower();
        if (view == "all") {
            _screenshot_views.append("front");
            _screenshot_views.append("back");
            _screenshot_views.append("left");
            _screenshot_views.append("right");
            _screenshot_views.append("top");
            _screenshot_views.append("perspective");
        } else {
            _screenshot_views.append(view);
        }
    }

    godot::SceneTree *tree = get_tree();
    if (tree) {
        godot::Ref<godot::SceneTreeTimer> timer = tree->create_timer(0.3);
        timer->connect("timeout", callable_mp(this, &FennaraExecutor::_on_screenshot_scene_opened));
    } else {
        _on_screenshot_scene_opened();
    }
}

void FennaraExecutor::_on_screenshot_scene_opened() {
    if (_batch_cancelled) {
        return;
    }

    godot::Dictionary nav_args = _screenshot_args;
    if (_screenshot_views.size() > 0) {
        nav_args["view"] = _screenshot_views[_screenshot_view_index];
    }

    godot::Dictionary nav_result = FennaraScreenshotSceneTool::navigate(nav_args);
    if (!(bool)nav_result.get("success", false)) {
        int idx = _screenshot_tool_index;
        godot::Dictionary args = _screenshot_args;
        _screenshot_tool_index = -1;
        _screenshot_args = godot::Dictionary();
        _screenshot_views = godot::Array();
        _screenshot_captures = godot::Array();
        _screenshot_view_index = 0;
        _screenshot_running = false;
        _on_async_tool_complete(nav_result, idx, "screenshot_scene", args);
        _start_next_screenshot_scene();
        return;
    }

    _screenshot_nav_result = nav_result;

    double capture_delay = double(nav_result.get("capture_delay_seconds", 0.15));
    if (capture_delay < 0.0) capture_delay = 0.0;
    if (capture_delay > 10.0) capture_delay = 10.0;

    godot::SceneTree *tree = get_tree();
    if (tree) {
        godot::Ref<godot::SceneTreeTimer> timer = tree->create_timer(capture_delay);
        timer->connect("timeout", callable_mp(this, &FennaraExecutor::_on_screenshot_capture));
    } else {
        _on_screenshot_capture();
    }
}

void FennaraExecutor::_on_screenshot_capture() {
    if (_batch_cancelled) {
        return;
    }

    godot::Dictionary capture_result = FennaraScreenshotSceneTool::capture();

    godot::Dictionary merged = _screenshot_nav_result;
    if ((bool)capture_result.get("success", false)) {
        merged["image_base64"] = capture_result["image_base64"];
        merged["format"] = capture_result.get("format", "png");
        merged["mime_type"] = capture_result.get("mime_type", "image/png");
        merged["width"] = capture_result["width"];
        merged["height"] = capture_result["height"];
        merged["image_role"] = capture_result.get("image_role", "single");
        if (capture_result.has("image_res_path")) {
            merged["image_res_path"] = capture_result["image_res_path"];
            merged["image_path"] = capture_result["image_path"];
            merged["transport"] = capture_result.get("transport", "file_path");
        }
    } else {
        merged["success"] = false;
        merged["error"] = capture_result.get("error", "Capture failed");
    }

    if (_screenshot_views.size() > 1) {
        _screenshot_captures.append(merged);
        _screenshot_view_index++;
        if (_screenshot_view_index < _screenshot_views.size()) {
            godot::SceneTree *tree = get_tree();
            if (tree) {
                godot::Ref<godot::SceneTreeTimer> timer = tree->create_timer(0.05);
                timer->connect("timeout", callable_mp(this, &FennaraExecutor::_on_screenshot_scene_opened));
            } else {
                _on_screenshot_scene_opened();
            }
            return;
        }

        godot::Dictionary all_result;
        all_result["success"] = true;
        all_result["is_3d"] = true;
        all_result["scene_path"] = _screenshot_args.get("scene_path", "");
        if (_screenshot_args.has("target_node_path")) {
            all_result["target_node_path"] = _screenshot_args["target_node_path"];
        }
        all_result["view"] = "all";
        godot::Dictionary collage =
            FennaraScreenshotSceneTool::make_collage(_screenshot_captures);
        godot::Array image_metadata;
        for (int i = 0; i < _screenshot_captures.size(); i++) {
            godot::Dictionary image = _screenshot_captures[i];
            image.erase("image_base64");
            image_metadata.append(image);
        }
        all_result["images"] = image_metadata;
        if ((bool)collage.get("success", false)) {
            all_result["image_base64"] = collage["image_base64"];
            all_result["format"] = collage.get("format", "png");
            all_result["mime_type"] = collage.get("mime_type", "image/png");
            all_result["width"] = collage["width"];
            all_result["height"] = collage["height"];
            all_result["image_role"] = "collage";
            all_result["image_res_path"] = collage.get("image_res_path", "");
            all_result["image_path"] = collage.get("image_path", "");
            all_result["transport"] = collage.get("transport", "file_path");
            all_result["collage_columns"] = collage["columns"];
            all_result["collage_rows"] = collage["rows"];
        } else {
            all_result["collage_error"] =
                collage.get("error", "Failed to build collage");
        }
        merged = all_result;
    }

    int idx = _screenshot_tool_index;
    godot::Dictionary args = _screenshot_args;
    _screenshot_tool_index = -1;
    _screenshot_args = godot::Dictionary();
    _screenshot_nav_result = godot::Dictionary();
    _screenshot_views = godot::Array();
    _screenshot_captures = godot::Array();
    _screenshot_view_index = 0;
    _screenshot_running = false;

    _on_async_tool_complete(merged, idx, "screenshot_scene", args);
    _start_next_screenshot_scene();
}

} // namespace fennara
