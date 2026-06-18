#include "fennara/tools/screenshot_scene.hpp"

#include "fennara/logger.hpp"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/camera2d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/variant/node_path.hpp>

namespace fennara {

godot::Node *FennaraScreenshotSceneTool::_find_node_by_class(
    godot::Node *node, const godot::String &cls) {
    if (!node) return nullptr;
    if (node->get_class() == cls) return node;
    for (int i = 0; i < node->get_child_count(); i++) {
        godot::Node *result = _find_node_by_class(node->get_child(i), cls);
        if (result) return result;
    }
    return nullptr;
}

godot::Node *FennaraScreenshotSceneTool::_resolve_scene_node(
    godot::Node *root, const godot::String &node_path) {
    if (!root || node_path.strip_edges().is_empty()) {
        return nullptr;
    }
    godot::String path = node_path.strip_edges();
    if (path == "." || path == godot::String(root->get_name())) {
        return root;
    }
    return root->get_node_or_null(godot::NodePath(path));
}

godot::Dictionary FennaraScreenshotSceneTool::navigate(const godot::Dictionary &args) {
    godot::Dictionary result;

    godot::EditorInterface *editor = godot::EditorInterface::get_singleton();
    if (!editor) {
        result["success"] = false;
        result["error"] = "EditorInterface not available";
        return result;
    }

    godot::String camera_path = args.get("camera_path", "");
    bool has_camera = !camera_path.strip_edges().is_empty();
    if (has_camera) {
        return _setup_camera_path_viewport(args);
    }

    if (_is_3d_scene) {
        FLOG_TOOL("SS: auto-framing 3D scene");
        return _frame_3d_editor_camera(args);
    }

    godot::Node *base = godot::Object::cast_to<godot::Node>(editor->get_base_control());
    if (!base) {
        result["success"] = false;
        result["error"] = "Editor base control not available";
        return result;
    }

    godot::Node *cie = _find_node_by_class(base, "CanvasItemEditor");
    if (!cie) {
        result["success"] = false;
        result["error"] = "CanvasItemEditor not found. Is the 2D editor open?";
        return result;
    }

    godot::Node *vp = _find_node_by_class(cie, "CanvasItemEditorViewport");
    if (!vp) {
        result["success"] = false;
        result["error"] = "CanvasItemEditorViewport not found";
        return result;
    }

    if (vp->get_child_count() < 3) {
        result["success"] = false;
        result["error"] = "CanvasItemEditorViewport has unexpected child count";
        return result;
    }

    godot::Node *hscroll = vp->get_child(0);
    godot::Node *vscroll = vp->get_child(1);

    godot::Node *toolbar = vp->get_child(2);
    if (!toolbar || toolbar->get_child_count() < 1) {
        result["success"] = false;
        result["error"] = "Could not find toolbar container";
        return result;
    }
    godot::Node *toolbar_inner = toolbar->get_child(0);
    if (!toolbar_inner || toolbar_inner->get_child_count() < 2) {
        result["success"] = false;
        result["error"] = "Could not find zoom widget container";
        return result;
    }
    godot::Node *zoom_widget = toolbar_inner->get_child(1);

    double x1, y1, x2, y2;
    double margin = 50.0;
    if (args.has("view_rect")) {
        godot::Dictionary vr = args["view_rect"];
        x1 = double(vr.get("x1", 0.0));
        y1 = double(vr.get("y1", 0.0));
        x2 = double(vr.get("x2", 1920.0));
        y2 = double(vr.get("y2", 1080.0));
    } else {
        godot::ProjectSettings *ps = godot::ProjectSettings::get_singleton();
        x1 = 0.0;
        y1 = 0.0;
        x2 = double(int(ps->get_setting("display/window/size/viewport_width", 1920)));
        y2 = double(int(ps->get_setting("display/window/size/viewport_height", 1080)));
    }

    double editor_scale = editor->get_editor_scale();
    godot::Control *vp_control = godot::Object::cast_to<godot::Control>(vp);
    if (!vp_control) {
        result["success"] = false;
        result["error"] = "CanvasItemEditorViewport is not a Control";
        return result;
    }
    godot::Vector2 panel_size = vp_control->get_size();

    double world_w = (x2 - x1) + margin * 2.0;
    double world_h = (y2 - y1) + margin * 2.0;

    double zoom_x = panel_size.x / (editor_scale * world_w);
    double zoom_y = panel_size.y / (editor_scale * world_h);
    double zoom = (zoom_x < zoom_y) ? zoom_x : zoom_y;

    double widget_value = zoom * 1.5;
    zoom_widget->call("set_zoom", widget_value);
    zoom_widget->emit_signal("zoom_changed", widget_value);

    double vis_w = panel_size.x / (editor_scale * zoom);
    double vis_h = panel_size.y / (editor_scale * zoom);

    double cx = (x1 + x2) / 2.0;
    double cy = (y1 + y2) / 2.0;
    double scroll_x = cx - vis_w / 2.0;
    double scroll_y = cy - vis_h / 2.0;

    FLOG_TOOL(godot::String("SS: navigating zoom=") + godot::String::num(zoom * 100.0, 1) + "% scroll=" + godot::String::num(scroll_x, 0) + "," + godot::String::num(scroll_y, 0));
    hscroll->set("value", scroll_x);
    vscroll->set("value", scroll_y);

    result["success"] = true;
    result["scene_path"] = _current_scene_path_ref();
    _capture_name_hint_ref() = _make_name_hint(_current_scene_path_ref(), "", "2d");

    godot::Dictionary visible_rect;
    visible_rect["x1"] = scroll_x;
    visible_rect["y1"] = scroll_y;
    visible_rect["x2"] = scroll_x + vis_w;
    visible_rect["y2"] = scroll_y + vis_h;
    result["visible_rect"] = visible_rect;

    result["zoom_percent"] = int(zoom * 100.0);

    godot::Dictionary ps_dict;
    ps_dict["width"] = int(panel_size.x);
    ps_dict["height"] = int(panel_size.y);
    result["panel_size"] = ps_dict;

    return result;
}

godot::Dictionary FennaraScreenshotSceneTool::_setup_camera_path_viewport(
    const godot::Dictionary &args) {
    godot::Dictionary result;

    godot::EditorInterface *editor = godot::EditorInterface::get_singleton();
    if (!editor) {
        result["success"] = false;
        result["error"] = "EditorInterface not available";
        return result;
    }

    godot::SubViewport *previous = _camera_capture_viewport_ref();
    if (previous) {
        previous->queue_free();
        _camera_capture_viewport_ref() = nullptr;
        _camera_capture_root_ref() = nullptr;
    }

    godot::String scene_path = _current_scene_path_ref();
    godot::String camera_path = args.get("camera_path", "");
    godot::Ref<godot::PackedScene> packed =
        godot::ResourceLoader::get_singleton()->load(
            scene_path, "PackedScene", godot::ResourceLoader::CACHE_MODE_IGNORE);
    if (packed.is_null() || !packed->can_instantiate()) {
        result["success"] = false;
        result["error"] = "Could not load scene for camera_path capture: " + scene_path;
        return result;
    }

    godot::Node *root = packed->instantiate();
    if (!root) {
        result["success"] = false;
        result["error"] = "Could not instantiate scene for camera_path capture: " + scene_path;
        return result;
    }

    godot::Node *camera_node = nullptr;
    godot::Camera2D *camera_2d = nullptr;
    godot::Camera3D *camera_3d = nullptr;
    camera_node = _resolve_scene_node(root, camera_path);
    camera_2d = godot::Object::cast_to<godot::Camera2D>(camera_node);
    camera_3d = godot::Object::cast_to<godot::Camera3D>(camera_node);
    if (!camera_2d && !camera_3d) {
        memdelete(root);
        result["success"] = false;
        result["error"] = "camera_path must resolve to Camera2D or Camera3D: " + camera_path;
        return result;
    }

    godot::ProjectSettings *ps = godot::ProjectSettings::get_singleton();
    int width = int(ps->get_setting("display/window/size/viewport_width", 1920));
    int height = int(ps->get_setting("display/window/size/viewport_height", 1080));
    width = std::max(width, 64);
    height = std::max(height, 64);

    godot::SubViewport *viewport = memnew(godot::SubViewport);
    viewport->set_name("FennaraCameraPathScreenshotViewport");
    viewport->set_size(godot::Vector2i(width, height));
    viewport->set_update_mode(godot::SubViewport::UPDATE_ALWAYS);
    viewport->set_clear_mode(godot::SubViewport::CLEAR_MODE_ALWAYS);
    viewport->set_transparent_background(false);
    bool root_may_need_3d = godot::Object::cast_to<godot::Node3D>(root) != nullptr ||
        _find_node_by_class(root, "Camera3D") != nullptr;
    if (camera_3d || root_may_need_3d) {
        viewport->set_use_own_world_3d(true);
    }

    godot::Node *base = godot::Object::cast_to<godot::Node>(editor->get_base_control());
    if (!base) {
        memdelete(viewport);
        memdelete(root);
        result["success"] = false;
        result["error"] = "Editor base control not available";
        return result;
    }

    base->add_child(viewport);
    viewport->add_child(root);

    if (camera_2d) {
        camera_2d->set_enabled(true);
        camera_2d->make_current();
        camera_2d->force_update_scroll();
        _is_3d_scene = false;
        result["view"] = "camera_2d";
    } else {
        camera_3d->make_current();
        _is_3d_scene = true;
        result["view"] = "camera_3d";
    }

    _camera_capture_viewport_ref() = viewport;
    _camera_capture_root_ref() = root;
    _capture_name_hint_ref() = _make_name_hint(
        scene_path, camera_path, "camera");

    result["success"] = true;
    result["scene_path"] = scene_path;
    result["is_3d"] = camera_3d != nullptr;
    result["camera_path"] = camera_path;
    result["capture_delay_seconds"] = 0.15;
    result["note"] = "Captured from scene camera via temporary SubViewport";
    godot::Dictionary viewport_size;
    viewport_size["width"] = width;
    viewport_size["height"] = height;
    result["viewport_size"] = viewport_size;
    return result;
}

} // namespace fennara
