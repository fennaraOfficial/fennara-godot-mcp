#pragma once

#ifdef __linux__

#include "linux_cef_capi.hpp"

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
                  fennara::linux_cef_runtime::capi::cef_browser_host_t *host,
                  godot::TextureRect *texture_rect,
                  int width,
                  int height,
                  MouseState &mouse_state,
                  bool &request_focus);

void notify_mouse_leave(fennara::linux_cef_runtime::capi::cef_browser_host_t *host,
                        MouseState &mouse_state);

} // namespace fennara::linux_cef_osr::input

#endif
