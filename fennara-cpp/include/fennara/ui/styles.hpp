#pragma once

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/style_box_line.hpp>
#include <godot_cpp/variant/color.hpp>

namespace fennara::ui_styles {

godot::Ref<godot::StyleBoxFlat> panel(const godot::Color &bg,
                                      const godot::Color &border,
                                      int radius = 4);
godot::Ref<godot::StyleBoxFlat> button(const godot::Color &bg,
                                       const godot::Color &border,
                                       int left_right = 10,
                                       int top_bottom = 6,
                                       int radius = 4);
godot::Ref<godot::StyleBoxLine> separator(const godot::Color &color,
                                          int thickness = 1);
void apply_button(godot::Button *button,
                  const godot::Ref<godot::StyleBoxFlat> &normal,
                  const godot::Ref<godot::StyleBoxFlat> &hover,
                  const godot::Ref<godot::StyleBoxFlat> &pressed,
                  const godot::Color &font_color,
                  const godot::Color &hover_font_color,
                  const godot::Color &pressed_font_color);

} // namespace fennara::ui_styles
