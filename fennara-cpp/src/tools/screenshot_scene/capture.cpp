#include "fennara/tools/screenshot_scene.hpp"

#include "fennara/logger.hpp"

#include <godot_cpp/classes/camera2d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/marshalls.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>

namespace fennara {

godot::Dictionary FennaraScreenshotSceneTool::capture() {
    godot::Dictionary result;

    godot::EditorInterface *editor = godot::EditorInterface::get_singleton();
    if (!editor) {
        result["success"] = false;
        result["error"] = "EditorInterface not available";
        return result;
    }

    FLOG_TOOL(godot::String("SS: capture started, is_3d=") + (_is_3d_scene ? "true" : "false"));

    godot::SubViewport *viewport = _camera_capture_viewport_ref();
    bool using_camera_path_viewport = viewport != nullptr;
    if (viewport) {
        FLOG_TOOL("SS: capturing temporary camera_path viewport");
    } else if (_is_3d_scene) {
        viewport = editor->get_editor_viewport_3d(0);
        if (!viewport) {
            FLOG_ERR("SS: 3D viewport null");
            result["success"] = false;
            result["error"] = "3D editor viewport not available";
            return result;
        }
    } else {
        viewport = editor->get_editor_viewport_2d();
        if (!viewport) {
            FLOG_ERR("SS: 2D viewport null");
            result["success"] = false;
            result["error"] = "2D editor viewport not available";
            return result;
        }
    }

    godot::Ref<godot::ViewportTexture> tex = viewport->get_texture();
    if (!tex.is_valid()) {
        FLOG_ERR("SS: viewport texture null");
        result["success"] = false;
        result["error"] = "Could not get viewport texture";
        return result;
    }

    godot::Ref<godot::Image> image = tex->get_image();
    if (!image.is_valid()) {
        FLOG_ERR("SS: image from viewport null");
        result["success"] = false;
        result["error"] = "Could not get image from viewport texture";
        return result;
    }

    godot::PackedByteArray png_data = image->save_png_to_buffer();
    if (png_data.size() == 0) {
        FLOG_ERR("SS: PNG encode failed");
        result["success"] = false;
        result["error"] = "Failed to encode image as PNG";
        return result;
    }

    godot::String base64 = godot::Marshalls::get_singleton()->raw_to_base64(png_data);

    FLOG_TOOL(godot::String("SS: captured size=") + godot::String::num_int64(image->get_width()) + "x" + godot::String::num_int64(image->get_height()));
    result["success"] = true;
    if (using_camera_path_viewport) {
        godot::Node *root = _camera_capture_root_ref();
        godot::Node *current_camera = nullptr;
        godot::Camera2D *camera_2d = viewport->get_camera_2d();
        godot::Camera3D *camera_3d = viewport->get_camera_3d();
        if (camera_2d) {
            current_camera = camera_2d;
            result["current_camera_type"] = "Camera2D";
        } else if (camera_3d) {
            current_camera = camera_3d;
            result["current_camera_type"] = "Camera3D";
        }
        if (root && current_camera) {
            result["current_camera_path"] = godot::String(root->get_path_to(current_camera));
        } else if (!current_camera) {
            result["camera_warning"] = "No current Camera2D or Camera3D was active in the temporary SubViewport at capture time.";
        }
    }
    result["image_base64"] = base64;
    result["format"] = "png";
    result["mime_type"] = "image/png";
    result["width"] = image->get_width();
    result["height"] = image->get_height();
    result["image_role"] = _is_3d_scene ? "view" : "single";
    godot::String hint = _capture_name_hint_ref();
    if (hint.is_empty()) {
        hint = _is_3d_scene ? "3d_view" : "2d";
    }
    _save_png_data(png_data, hint, result);

    if (using_camera_path_viewport) {
        viewport->queue_free();
        _camera_capture_viewport_ref() = nullptr;
        _camera_capture_root_ref() = nullptr;
    }

    return result;
}

} // namespace fennara
