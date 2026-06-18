#include "fennara/tool_results/screenshot_scene.hpp"

#include "fennara/tool_results/envelope.hpp"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara::tool_results {

namespace {

godot::String bool_text(bool value) {
    return value ? "true" : "false";
}

godot::String image_summary_line(const godot::Dictionary &image) {
    int64_t width = static_cast<int64_t>(image.get("width", 0));
    int64_t height = static_cast<int64_t>(image.get("height", 0));
    godot::String mime = image.get("mime_type", "");
    godot::String line = "Image:";
    if (width > 0 && height > 0) {
        line += " " + godot::String::num_int64(width) + "x" +
                godot::String::num_int64(height);
    }
    if (!mime.is_empty()) {
        line += " " + mime;
    }
    godot::String role = image.get("image_role", "");
    if (!role.is_empty()) {
        line += " (" + role + ")";
    }
    return line;
}

void append_rect(godot::PackedStringArray &lines,
                 const godot::String &label,
                 const godot::Dictionary &rect) {
    godot::String line = label;
    line += ": x1=" + godot::String::num(rect.get("x1", 0.0), 1) +
                 ", y1=" + godot::String::num(rect.get("y1", 0.0), 1) +
                 ", x2=" + godot::String::num(rect.get("x2", 0.0), 1) +
                 ", y2=" + godot::String::num(rect.get("y2", 0.0), 1);
    lines.append(line);
}

void copy_if_present(godot::Dictionary &target,
                     const godot::Dictionary &source,
                     const godot::String &key) {
    if (source.has(key)) {
        target[key] = source[key];
    }
}

godot::Dictionary image_metadata_from_result(const godot::Dictionary &result) {
    godot::Dictionary image;
    image["view"] = result.get("view", "");
    image["image_role"] = result.get("image_role", "");
    image["format"] = result.get("format", "");
    image["mime_type"] = result.get("mime_type", "");
    image["width"] = result.get("width", 0);
    image["height"] = result.get("height", 0);
    image["image_res_path"] = result.get("image_res_path", "");
    image["image_path"] = result.get("image_path", "");
    image["transport"] = result.get("transport", "");
    return image;
}

} // namespace

godot::Dictionary format_screenshot_scene(const godot::Dictionary &raw_result) {
    bool raw_success = raw_result.get("success", false);
    bool has_image = raw_result.has("image_base64");
    godot::String status = raw_success ? "success" : "failed";
    if (raw_success && !has_image) {
        status = "partial";
    }

    godot::PackedStringArray lines;
    lines.append("Tool: screenshot_scene");
    lines.append("Status: " + status);
    lines.append("Scene: " + godot::String(raw_result.get("scene_path", "")));
    lines.append("3D scene: " + bool_text(raw_result.get("is_3d", false)));
    if (raw_result.has("target_node_path")) {
        lines.append("Target node: " + godot::String(raw_result.get("target_node_path", "")));
    }
    if (raw_result.has("camera_path")) {
        lines.append("Camera: " + godot::String(raw_result.get("camera_path", "")));
    }
    if (raw_result.has("view")) {
        lines.append("View: " + godot::String(raw_result.get("view", "")));
    }
    if (raw_result.has("current_camera_path")) {
        lines.append("Current camera: " + godot::String(raw_result.get("current_camera_path", "")));
    }
    if (has_image) {
        lines.append(image_summary_line(raw_result));
    }
    if (raw_result.has("image_res_path")) {
        lines.append("Saved resource: " + godot::String(raw_result.get("image_res_path", "")));
    }
    if (raw_result.has("image_path")) {
        lines.append("Saved file: " + godot::String(raw_result.get("image_path", "")));
    }
    if (raw_result.has("screenshot_dir")) {
        lines.append("Screenshot dir: " + godot::String(raw_result.get("screenshot_dir", "")));
    }
    if (raw_result.has("screenshot_absolute_dir")) {
        lines.append("Screenshot absolute dir: " +
                     godot::String(raw_result.get("screenshot_absolute_dir", "")));
    }
    if (raw_result.has("zoom_percent")) {
        lines.append("Zoom: " + godot::String::num_int64(
            static_cast<int64_t>(raw_result.get("zoom_percent", 0))) + "%");
    }
    if (raw_result.has("visible_rect") &&
        raw_result["visible_rect"].get_type() == godot::Variant::DICTIONARY) {
        append_rect(lines, "Visible rect", raw_result["visible_rect"]);
    }
    if (raw_result.has("collage_columns") && raw_result.has("collage_rows")) {
        lines.append("Collage: " +
                     godot::String::num_int64(static_cast<int64_t>(raw_result.get("collage_columns", 0))) +
                     " columns x " +
                     godot::String::num_int64(static_cast<int64_t>(raw_result.get("collage_rows", 0))) +
                     " rows");
    }
    if (raw_result.has("error")) {
        lines.append("Error: " + godot::String(raw_result.get("error", "")));
    }
    if (raw_result.has("collage_error")) {
        lines.append("Collage error: " + godot::String(raw_result.get("collage_error", "")));
    }
    if (raw_result.has("camera_warning")) {
        lines.append("Camera warning: " + godot::String(raw_result.get("camera_warning", "")));
    }

    godot::Array source_images = raw_result.get("images", godot::Array());
    godot::Array image_metadata;
    if (!source_images.is_empty()) {
        lines.append("");
        lines.append("## Captured views");
        for (int i = 0; i < source_images.size(); i++) {
            if (source_images[i].get_type() != godot::Variant::DICTIONARY) {
                continue;
            }
            godot::Dictionary image = source_images[i];
            image_metadata.append(image_metadata_from_result(image));
            godot::String view = image.get("view", godot::String::num_int64(i + 1));
            lines.append("- " + view + ": " + image_summary_line(image));
        }
    } else if (has_image) {
        image_metadata.append(image_metadata_from_result(raw_result));
    }

    godot::Dictionary metadata = make_base_metadata(
        "screenshot_scene", "screenshot_scene-md-v1", status);
    metadata["scene_path"] = raw_result.get("scene_path", "");
    metadata["target_node_path"] = raw_result.get("target_node_path", "");
    metadata["camera_path"] = raw_result.get("camera_path", "");
    metadata["current_camera_path"] = raw_result.get("current_camera_path", "");
    metadata["current_camera_type"] = raw_result.get("current_camera_type", "");
    metadata["view"] = raw_result.get("view", "");
    metadata["is_3d"] = raw_result.get("is_3d", false);
    metadata["image_count"] = image_metadata.size();
    metadata["images"] = image_metadata;
    metadata["has_primary_image"] = has_image;
    metadata["previewed"] = false;

    godot::Dictionary envelope = make_envelope(
        godot::String("\n").join(lines), metadata, raw_success);

    copy_if_present(envelope, raw_result, "image_base64");
    copy_if_present(envelope, raw_result, "format");
    copy_if_present(envelope, raw_result, "mime_type");
    copy_if_present(envelope, raw_result, "width");
    copy_if_present(envelope, raw_result, "height");
    copy_if_present(envelope, raw_result, "image_role");
    copy_if_present(envelope, raw_result, "current_camera_path");
    copy_if_present(envelope, raw_result, "current_camera_type");
    copy_if_present(envelope, raw_result, "image_res_path");
    copy_if_present(envelope, raw_result, "image_path");
    copy_if_present(envelope, raw_result, "screenshot_dir");
    copy_if_present(envelope, raw_result, "screenshot_absolute_dir");
    copy_if_present(envelope, raw_result, "transport");
    copy_if_present(envelope, raw_result, "images");
    copy_if_present(envelope, raw_result, "collage_columns");
    copy_if_present(envelope, raw_result, "collage_rows");
    return envelope;
}

} // namespace fennara::tool_results
