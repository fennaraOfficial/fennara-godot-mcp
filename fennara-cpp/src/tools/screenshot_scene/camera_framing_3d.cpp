#include "fennara/tools/screenshot_scene.hpp"

#include "fennara/logger.hpp"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_selection.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/visual_instance3d.hpp>
#include <godot_cpp/variant/node_path.hpp>

namespace fennara {

void FennaraScreenshotSceneTool::_accumulate_3d_bounds(godot::Node *node,
                                                godot::AABB &bounds,
                                                bool &has_bounds) {
    if (!node) return;

    godot::VisualInstance3D *visual =
        godot::Object::cast_to<godot::VisualInstance3D>(node);
    if (visual && visual->is_visible()) {
        godot::AABB local_bounds = visual->get_aabb();
        if (local_bounds.has_surface()) {
            godot::AABB global_bounds =
                visual->get_global_transform().xform(local_bounds).abs();
            if (has_bounds) {
                bounds.merge_with(global_bounds);
            } else {
                bounds = global_bounds;
                has_bounds = true;
            }
        }
    }

    for (int i = 0; i < node->get_child_count(); i++) {
        _accumulate_3d_bounds(node->get_child(i), bounds, has_bounds);
    }
}

godot::Dictionary FennaraScreenshotSceneTool::_frame_3d_editor_camera(const godot::Dictionary &args) {
    godot::Dictionary result;

    godot::EditorInterface *editor = godot::EditorInterface::get_singleton();
    if (!editor) {
        result["success"] = false;
        result["error"] = "EditorInterface not available";
        return result;
    }

    godot::EditorSelection *selection = editor->get_selection();
    if (selection) {
        selection->clear();
    }

    godot::Node *edited_root = editor->get_edited_scene_root();
    if (!edited_root) {
        result["success"] = false;
        result["error"] = "No edited scene root available";
        return result;
    }

    godot::Node *base = godot::Object::cast_to<godot::Node>(editor->get_base_control());
    if (!base) {
        result["success"] = false;
        result["error"] = "Editor base control not available";
        return result;
    }

    godot::SubViewport *previous = _camera_capture_viewport_ref();
    if (previous) {
        previous->queue_free();
        _camera_capture_viewport_ref() = nullptr;
        _camera_capture_root_ref() = nullptr;
    }

    godot::String scene_path = _current_scene_path_ref();
    godot::Ref<godot::PackedScene> packed =
        godot::ResourceLoader::get_singleton()->load(
            scene_path, "PackedScene", godot::ResourceLoader::CACHE_MODE_IGNORE);
    if (packed.is_null() || !packed->can_instantiate()) {
        result["success"] = false;
        result["error"] = "Could not load scene for framed 3D capture: " + scene_path;
        return result;
    }

    godot::Node *root = packed->instantiate();
    if (!root) {
        result["success"] = false;
        result["error"] = "Could not instantiate scene for framed 3D capture: " + scene_path;
        return result;
    }

    godot::Node *bounds_root = root;
    godot::String target_path = args.get("target_node_path", "");
    bool has_target = !target_path.is_empty();
    if (has_target) {
        bounds_root = _resolve_scene_node(root, target_path);
        if (!bounds_root) {
            memdelete(root);
            result["success"] = false;
            result["error"] = "target_node_path not found: " + target_path;
            return result;
        }
    }

    godot::AABB bounds;
    bool has_bounds = false;
    _accumulate_3d_bounds(bounds_root, bounds, has_bounds);
    if (!has_bounds) {
        godot::Node3D *target_3d = godot::Object::cast_to<godot::Node3D>(bounds_root);
        if (target_3d) {
            bounds = godot::AABB(target_3d->get_global_position() - godot::Vector3(1, 1, 1),
                                 godot::Vector3(2, 2, 2));
            has_bounds = true;
        } else {
            memdelete(root);
            result["success"] = true;
            result["is_3d"] = true;
            result["note"] = "3D scene: no visible 3D geometry bounds found; using default 3D editor camera view";
            return result;
        }
    }

    godot::String view = args.get("view", "perspective");
    view = view.to_lower();
    if (view == "all") view = "perspective";

    godot::Vector3 center = bounds.get_center();
    godot::Vector3 size = bounds.get_size();
    double diagonal = std::sqrt(double(size.x * size.x + size.y * size.y + size.z * size.z));
    double radius = std::max(diagonal * 0.5, 1.0);
    double margin = double(args.get("context_margin", has_target ? 0.75 : 1.1));
    margin = std::max(margin, 0.25);

    godot::Vector3 view_dir = godot::Vector3(1.0, 0.65, 1.0).normalized();
    godot::Vector3 up = godot::Vector3(0.0, 1.0, 0.0);
    if (view == "front") {
        view_dir = godot::Vector3(0.0, 0.0, 1.0);
    } else if (view == "back") {
        view_dir = godot::Vector3(0.0, 0.0, -1.0);
    } else if (view == "left") {
        view_dir = godot::Vector3(-1.0, 0.0, 0.0);
    } else if (view == "right") {
        view_dir = godot::Vector3(1.0, 0.0, 0.0);
    } else if (view == "top") {
        view_dir = godot::Vector3(0.0, 1.0, 0.0);
        up = godot::Vector3(0.0, 0.0, -1.0);
    } else if (view == "isometric" || view == "perspective") {
        view = "perspective";
    } else {
        memdelete(root);
        result["success"] = false;
        result["error"] = "Unsupported 3D view: " + view;
        return result;
    }

    godot::Vector3 right = up.cross(view_dir).normalized();
    godot::Vector3 camera_up = view_dir.cross(right).normalized();

    const double fov_degrees = 70.0;
    double fov_rad = fov_degrees * 3.14159265358979323846 / 180.0;
    double tan_vertical = std::tan(fov_rad * 0.5);
    godot::ProjectSettings *ps = godot::ProjectSettings::get_singleton();
    int width = int(ps->get_setting("display/window/size/viewport_width", 1920));
    int height = int(ps->get_setting("display/window/size/viewport_height", 1080));
    width = std::max(width, 64);
    height = std::max(height, 64);
    godot::Vector2i viewport_size(width, height);
    double aspect = viewport_size.y > 0 ? double(viewport_size.x) / double(viewport_size.y) : 1.0;
    double tan_horizontal = tan_vertical * aspect;

    godot::Vector3 half = size * 0.5;
    double fit_distance = 0.0;
    for (int xi = -1; xi <= 1; xi += 2) {
        for (int yi = -1; yi <= 1; yi += 2) {
            for (int zi = -1; zi <= 1; zi += 2) {
                godot::Vector3 corner = center + godot::Vector3(
                    half.x * double(xi),
                    half.y * double(yi),
                    half.z * double(zi));
                godot::Vector3 offset = corner - center;
                double x = std::abs(double(offset.dot(right)));
                double y = std::abs(double(offset.dot(camera_up)));
                double toward_camera = double(offset.dot(view_dir));
                double required = toward_camera + std::max(
                    x / std::max(tan_horizontal, 0.0001),
                    y / std::max(tan_vertical, 0.0001));
                fit_distance = std::max(fit_distance, required);
            }
        }
    }

    double effective_margin = std::max(margin, 1.08);
    double distance = std::max(fit_distance * effective_margin, radius + 0.75);
    godot::Vector3 camera_position = center + view_dir * distance;

    godot::SubViewport *viewport = memnew(godot::SubViewport);
    viewport->set_name("FennaraFramedScreenshotViewport");
    viewport->set_size(viewport_size);
    viewport->set_update_mode(godot::SubViewport::UPDATE_ALWAYS);
    viewport->set_clear_mode(godot::SubViewport::CLEAR_MODE_ALWAYS);
    viewport->set_transparent_background(false);
    viewport->set_use_own_world_3d(true);

    godot::Camera3D *camera = memnew(godot::Camera3D);
    camera->set_name("FennaraFramedScreenshotCamera");
    camera->set_projection(godot::Camera3D::PROJECTION_PERSPECTIVE);
    camera->set_fov(float(fov_degrees));
    camera->set_near(0.05f);
    camera->set_far(float(std::max(distance + radius * 10.0, 1000.0)));

    base->add_child(viewport);
    viewport->add_child(root);
    viewport->add_child(camera);
    camera->look_at_from_position(camera_position, center, up);
    camera->make_current();

    _camera_capture_viewport_ref() = viewport;
    _camera_capture_root_ref() = root;

    godot::Dictionary bounds_dict;
    bounds_dict["center"] = center;
    bounds_dict["size"] = size;
    result["success"] = true;
    result["is_3d"] = true;
    result["scene_path"] = _current_scene_path_ref();
    result["view"] = view;
    if (has_target) result["target_node_path"] = target_path;
    result["note"] = "3D scene: auto-framed visible geometry bounds";
    result["framed_bounds"] = bounds_dict;
    result["camera_distance"] = distance;
    result["camera_position"] = camera_position;
    result["context_margin"] = margin;
    result["effective_context_margin"] = effective_margin;
    result["capture_delay_seconds"] = 0.15;
    godot::Dictionary viewport_dict;
    viewport_dict["width"] = width;
    viewport_dict["height"] = height;
    result["viewport_size"] = viewport_dict;
    _capture_name_hint_ref() = _make_name_hint(_current_scene_path_ref(), target_path, view);

    FLOG_TOOL(godot::String("SS: auto-framed 3D bounds center=") +
              godot::String(center) + " size=" + godot::String(size) +
              " view=" + view + " distance=" + godot::String::num(distance, 2));
    return result;
}

} // namespace fennara
