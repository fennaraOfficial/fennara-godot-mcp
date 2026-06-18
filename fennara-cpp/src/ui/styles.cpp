#include "fennara/ui/styles.hpp"

namespace fennara::ui_styles {

godot::Ref<godot::StyleBoxFlat> panel(const godot::Color &bg,
                                      const godot::Color &border,
                                      int radius) {
    godot::Ref<godot::StyleBoxFlat> style;
    style.instantiate();
    style->set_bg_color(bg);
    style->set_border_color(border);
    style->set_border_width_all(1);
    style->set_corner_radius_all(radius);
    return style;
}

godot::Ref<godot::StyleBoxFlat> button(const godot::Color &bg,
                                       const godot::Color &border,
                                       int left_right,
                                       int top_bottom,
                                       int radius) {
    godot::Ref<godot::StyleBoxFlat> style = panel(bg, border, radius);
    style->set_content_margin(godot::SIDE_LEFT, left_right);
    style->set_content_margin(godot::SIDE_TOP, top_bottom);
    style->set_content_margin(godot::SIDE_RIGHT, left_right);
    style->set_content_margin(godot::SIDE_BOTTOM, top_bottom);
    return style;
}

godot::Ref<godot::StyleBoxLine> separator(const godot::Color &color,
                                          int thickness) {
    godot::Ref<godot::StyleBoxLine> style;
    style.instantiate();
    style->set_color(color);
    style->set_thickness(thickness);
    return style;
}

void apply_button(godot::Button *button,
                  const godot::Ref<godot::StyleBoxFlat> &normal,
                  const godot::Ref<godot::StyleBoxFlat> &hover,
                  const godot::Ref<godot::StyleBoxFlat> &pressed,
                  const godot::Color &font_color,
                  const godot::Color &hover_font_color,
                  const godot::Color &pressed_font_color) {
    button->set_focus_mode(godot::Control::FOCUS_NONE);
    button->add_theme_stylebox_override("normal", normal);
    button->add_theme_stylebox_override("hover", hover);
    button->add_theme_stylebox_override("pressed", pressed);
    button->add_theme_color_override("font_color", font_color);
    button->add_theme_color_override("font_hover_color", hover_font_color);
    button->add_theme_color_override("font_pressed_color", pressed_font_color);
}

} // namespace fennara::ui_styles
