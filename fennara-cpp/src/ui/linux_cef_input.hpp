#pragma once

#ifdef __linux__

#include "linux_cef_bridge_api.hpp"

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/texture_rect.hpp>

namespace fennara::linux_cef_osr::input {

struct MouseState {
    bool has_position = false;
    int last_x = 0;
    int last_y = 0;
};

bool handle_input(const godot::Ref<godot::InputEvent> &event,
                  const fennara_cef_bridge_api *api,
                  fennara_cef_bridge_browser *browser,
                  godot::TextureRect *texture_rect,
                  int width,
                  int height,
                  MouseState &mouse_state,
                  bool &request_focus);

void notify_mouse_leave(const fennara_cef_bridge_api *api,
                        fennara_cef_bridge_browser *browser,
                        MouseState &mouse_state);

} // namespace fennara::linux_cef_osr::input

#endif
