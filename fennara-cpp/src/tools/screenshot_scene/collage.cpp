#include "fennara/tools/screenshot_scene.hpp"

#include <algorithm>

#include <godot_cpp/classes/marshalls.hpp>

namespace fennara {

namespace {

uint8_t _label_glyph(char c, int row) {
    switch (c) {
        case 'A': { static const uint8_t g[7] = {14, 17, 17, 31, 17, 17, 17}; return g[row]; }
        case 'B': { static const uint8_t g[7] = {30, 17, 17, 30, 17, 17, 30}; return g[row]; }
        case 'C': { static const uint8_t g[7] = {14, 17, 16, 16, 16, 17, 14}; return g[row]; }
        case 'E': { static const uint8_t g[7] = {31, 16, 16, 30, 16, 16, 31}; return g[row]; }
        case 'F': { static const uint8_t g[7] = {31, 16, 16, 30, 16, 16, 16}; return g[row]; }
        case 'G': { static const uint8_t g[7] = {14, 17, 16, 23, 17, 17, 14}; return g[row]; }
        case 'H': { static const uint8_t g[7] = {17, 17, 17, 31, 17, 17, 17}; return g[row]; }
        case 'I': { static const uint8_t g[7] = {14, 4, 4, 4, 4, 4, 14}; return g[row]; }
        case 'K': { static const uint8_t g[7] = {17, 18, 20, 24, 20, 18, 17}; return g[row]; }
        case 'L': { static const uint8_t g[7] = {16, 16, 16, 16, 16, 16, 31}; return g[row]; }
        case 'O': { static const uint8_t g[7] = {14, 17, 17, 17, 17, 17, 14}; return g[row]; }
        case 'P': { static const uint8_t g[7] = {30, 17, 17, 30, 16, 16, 16}; return g[row]; }
        case 'R': { static const uint8_t g[7] = {30, 17, 17, 30, 20, 18, 17}; return g[row]; }
        case 'S': { static const uint8_t g[7] = {15, 16, 16, 14, 1, 1, 30}; return g[row]; }
        case 'T': { static const uint8_t g[7] = {31, 4, 4, 4, 4, 4, 4}; return g[row]; }
        case 'V': { static const uint8_t g[7] = {17, 17, 17, 17, 17, 10, 4}; return g[row]; }
        case 'W': { static const uint8_t g[7] = {17, 17, 17, 21, 21, 21, 10}; return g[row]; }
        default: return 0;
    }
}

} // namespace

void FennaraScreenshotSceneTool::_draw_label_text(const godot::Ref<godot::Image> &image,
                                           const godot::String &text,
                                           const godot::Vector2i &position,
                                           const godot::Color &color) {
    if (!image.is_valid()) return;

    const int scale = 4;
    int cursor_x = position.x;
    for (int i = 0; i < text.length(); i++) {
        char c = char(text[i]);
        if (c == ' ') {
            cursor_x += 4 * scale;
            continue;
        }

        for (int row = 0; row < 7; row++) {
            uint8_t bits = _label_glyph(c, row);
            for (int col = 0; col < 5; col++) {
                if ((bits & (1 << (4 - col))) == 0) continue;
                image->fill_rect(
                    godot::Rect2i(cursor_x + col * scale,
                                  position.y + row * scale,
                                  scale,
                                  scale),
                    color);
            }
        }
        cursor_x += 7 * scale;
    }
}

godot::Dictionary FennaraScreenshotSceneTool::make_collage(const godot::Array &images) {
    godot::Dictionary result;

    int count = images.size();
    if (count == 0) {
        result["success"] = false;
        result["error"] = "No images provided for collage";
        return result;
    }

    godot::Array decoded;
    int cell_w = 0;
    int cell_h = 0;
    for (int i = 0; i < count; i++) {
        godot::Dictionary entry = images[i];
        godot::String base64 = entry.get("image_base64", "");
        if (base64.is_empty()) {
            result["success"] = false;
            result["error"] = "Image entry missing image_base64";
            return result;
        }

        godot::PackedByteArray png_data =
            godot::Marshalls::get_singleton()->base64_to_raw(base64);
        godot::Ref<godot::Image> image;
        image.instantiate();
        if (image->load_png_from_buffer(png_data) != godot::OK) {
            result["success"] = false;
            result["error"] = "Failed to decode PNG for collage";
            return result;
        }

        image->convert(godot::Image::FORMAT_RGBA8);
        cell_w = std::max(cell_w, image->get_width());
        cell_h = std::max(cell_h, image->get_height());
        decoded.append(image);
    }

    int cols = 1;
    if (count == 2) {
        cols = 2;
    } else if (count == 3) {
        cols = 3;
    } else if (count == 4) {
        cols = 2;
    } else {
        cols = 3;
    }
    int rows = (count + cols - 1) / cols;

    const int gutter = 24;
    const int label_h = 64;
    int panel_w = cell_w;
    int panel_h = cell_h + label_h;
    int collage_w = cols * panel_w + (cols + 1) * gutter;
    int collage_h = rows * panel_h + (rows + 1) * gutter;

    godot::Ref<godot::Image> collage =
        godot::Image::create_empty(collage_w, collage_h, false,
                                   godot::Image::FORMAT_RGBA8);
    godot::Color background(0.035, 0.04, 0.05, 1.0);
    godot::Color label_bg(0.08, 0.09, 0.11, 1.0);
    godot::Color label_fg(0.94, 0.94, 0.9, 1.0);
    collage->fill(background);

    for (int i = 0; i < count; i++) {
        godot::Ref<godot::Image> image = decoded[i];
        godot::Dictionary entry = images[i];
        godot::String view = entry.get("view", godot::String::num_int64(i + 1));
        view = view.to_upper();

        int x = gutter + (i % cols) * (panel_w + gutter);
        int y = gutter + (i / cols) * (panel_h + gutter);
        collage->fill_rect(godot::Rect2i(x, y, panel_w, label_h), label_bg);
        collage->fill_rect(godot::Rect2i(x, y + label_h, panel_w, cell_h),
                           godot::Color(0, 0, 0, 1));
        _draw_label_text(collage, view, godot::Vector2i(x + 20, y + 16), label_fg);
        collage->blit_rect(image,
                           godot::Rect2i(0, 0, image->get_width(), image->get_height()),
                           godot::Vector2i(x, y + label_h));
    }

    godot::PackedByteArray png_data = collage->save_png_to_buffer();
    if (png_data.is_empty()) {
        result["success"] = false;
        result["error"] = "Failed to encode collage PNG";
        return result;
    }

    result["success"] = true;
    result["image_base64"] = godot::Marshalls::get_singleton()->raw_to_base64(png_data);
    result["format"] = "png";
    result["mime_type"] = "image/png";
    result["width"] = collage->get_width();
    result["height"] = collage->get_height();
    result["columns"] = cols;
    result["rows"] = rows;
    result["gutter"] = gutter;
    result["label_height"] = label_h;
    result["image_role"] = "collage";
    godot::Dictionary first = images[0];
    godot::String scene_path = first.get("scene_path", _current_scene_path_ref());
    godot::String target_path = first.get("target_node_path", "");
    godot::String hint = _make_name_hint(scene_path, target_path,
                                         "collage_" + godot::String::num_int64(count) + "_views");
    _save_png_data(png_data, hint, result);
    return result;
}

} // namespace fennara
