#ifdef __linux__

#include "linux_cef_input.hpp"

#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/input_event_with_modifiers.hpp>
#include <godot_cpp/classes/object.hpp>

#include <algorithm>
#include <cmath>

namespace fennara::linux_cef_osr::input {
namespace {

int clamp_coordinate(double value, int limit) {
    const int max_value = std::max(0, limit - 1);
    return std::clamp(static_cast<int>(std::lround(value)), 0, max_value);
}

uint32_t modifier_flags(const godot::InputEventWithModifiers *event) {
    uint32_t flags = EVENTFLAG_NONE;
    if (event == nullptr) {
        return flags;
    }
    if (event->is_shift_pressed()) {
        flags |= EVENTFLAG_SHIFT_DOWN;
    }
    if (event->is_ctrl_pressed()) {
        flags |= EVENTFLAG_CONTROL_DOWN;
    }
    if (event->is_alt_pressed()) {
        flags |= EVENTFLAG_ALT_DOWN;
    }
    if (event->is_meta_pressed()) {
        flags |= EVENTFLAG_COMMAND_DOWN;
    }
    return flags;
}

uint32_t mouse_button_flags(const godot::InputEventMouse *event) {
    uint32_t flags = modifier_flags(event);
    if (event == nullptr) {
        return flags;
    }

    const int64_t mask = static_cast<int64_t>(event->get_button_mask());
    if ((mask & godot::MOUSE_BUTTON_MASK_LEFT) != 0) {
        flags |= EVENTFLAG_LEFT_MOUSE_BUTTON;
    }
    if ((mask & godot::MOUSE_BUTTON_MASK_MIDDLE) != 0) {
        flags |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
    }
    if ((mask & godot::MOUSE_BUTTON_MASK_RIGHT) != 0) {
        flags |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
    }
    return flags;
}

bool cef_button_type(godot::MouseButton button, cef_mouse_button_type_t &type) {
    switch (button) {
        case godot::MOUSE_BUTTON_LEFT:
            type = MBT_LEFT;
            return true;
        case godot::MOUSE_BUTTON_MIDDLE:
            type = MBT_MIDDLE;
            return true;
        case godot::MOUSE_BUTTON_RIGHT:
            type = MBT_RIGHT;
            return true;
        default:
            return false;
    }
}

bool is_wheel_button(godot::MouseButton button) {
    return button == godot::MOUSE_BUTTON_WHEEL_UP ||
           button == godot::MOUSE_BUTTON_WHEEL_DOWN ||
           button == godot::MOUSE_BUTTON_WHEEL_LEFT ||
           button == godot::MOUSE_BUTTON_WHEEL_RIGHT;
}

int wheel_delta(float factor) {
    const float magnitude = std::max(1.0f, std::abs(factor));
    return std::max(1, static_cast<int>(std::lround(120.0f * magnitude)));
}

bool is_keypad_key(godot::Key keycode) {
    return (keycode >= godot::KEY_KP_MULTIPLY && keycode <= godot::KEY_KP_9) ||
           keycode == godot::KEY_KP_ENTER;
}

int windows_key_code(godot::Key keycode) {
    const int code = static_cast<int>(keycode);
    if ((code >= static_cast<int>(godot::KEY_SPACE) && code <= static_cast<int>(godot::KEY_ASCIITILDE)) ||
        (code >= static_cast<int>(godot::KEY_A) && code <= static_cast<int>(godot::KEY_Z)) ||
        (code >= static_cast<int>(godot::KEY_0) && code <= static_cast<int>(godot::KEY_9))) {
        return code;
    }
    if (keycode >= godot::KEY_F1 && keycode <= godot::KEY_F24) {
        return 112 + (static_cast<int>(keycode) - static_cast<int>(godot::KEY_F1));
    }
    if (keycode >= godot::KEY_KP_0 && keycode <= godot::KEY_KP_9) {
        return 96 + (static_cast<int>(keycode) - static_cast<int>(godot::KEY_KP_0));
    }

    switch (keycode) {
        case godot::KEY_BACKSPACE:
            return 8;
        case godot::KEY_TAB:
        case godot::KEY_BACKTAB:
            return 9;
        case godot::KEY_ENTER:
        case godot::KEY_KP_ENTER:
            return 13;
        case godot::KEY_SHIFT:
            return 16;
        case godot::KEY_CTRL:
            return 17;
        case godot::KEY_ALT:
            return 18;
        case godot::KEY_PAUSE:
            return 19;
        case godot::KEY_CAPSLOCK:
            return 20;
        case godot::KEY_ESCAPE:
            return 27;
        case godot::KEY_PAGEUP:
            return 33;
        case godot::KEY_PAGEDOWN:
            return 34;
        case godot::KEY_END:
            return 35;
        case godot::KEY_HOME:
            return 36;
        case godot::KEY_LEFT:
            return 37;
        case godot::KEY_UP:
            return 38;
        case godot::KEY_RIGHT:
            return 39;
        case godot::KEY_DOWN:
            return 40;
        case godot::KEY_PRINT:
            return 44;
        case godot::KEY_INSERT:
            return 45;
        case godot::KEY_DELETE:
            return 46;
        case godot::KEY_META:
            return 91;
        case godot::KEY_MENU:
            return 93;
        case godot::KEY_NUMLOCK:
            return 144;
        case godot::KEY_SCROLLLOCK:
            return 145;
        case godot::KEY_KP_MULTIPLY:
            return 106;
        case godot::KEY_KP_ADD:
            return 107;
        case godot::KEY_KP_SUBTRACT:
            return 109;
        case godot::KEY_KP_PERIOD:
            return 110;
        case godot::KEY_KP_DIVIDE:
            return 111;
        default:
            return 0;
    }
}

char16_t cef_character(char32_t value) {
    if (value == 0 || value > 0xffff) {
        return 0;
    }
    return static_cast<char16_t>(value);
}

cef_mouse_event_t make_mouse_event(const godot::InputEventMouse *event,
                                   godot::TextureRect *texture_rect,
                                   int width,
                                   int height,
                                   MouseState &mouse_state) {
    const godot::Vector2 position =
        event->get_global_position() -
        (texture_rect != nullptr ? texture_rect->get_global_position() : godot::Vector2());

    cef_mouse_event_t mouse_event;
    mouse_event.x = clamp_coordinate(position.x, width);
    mouse_event.y = clamp_coordinate(position.y, height);
    mouse_event.modifiers = mouse_button_flags(event);
    mouse_state.last_x = mouse_event.x;
    mouse_state.last_y = mouse_event.y;
    mouse_state.has_position = true;
    return mouse_event;
}

} // namespace

bool handle_input(const godot::Ref<godot::InputEvent> &event,
                  cef_browser_host_t *host,
                  godot::TextureRect *texture_rect,
                  int width,
                  int height,
                  MouseState &mouse_state,
                  bool &request_focus) {
    request_focus = false;
    if (event.is_null() || host == nullptr) {
        return false;
    }

    if (auto *motion = godot::Object::cast_to<godot::InputEventMouseMotion>(event.ptr())) {
        if (host->send_mouse_move_event == nullptr) {
            return false;
        }
        cef_mouse_event_t mouse_event = make_mouse_event(motion, texture_rect, width, height, mouse_state);
        host->send_mouse_move_event(host, &mouse_event, 0);
        return true;
    }

    if (auto *button = godot::Object::cast_to<godot::InputEventMouseButton>(event.ptr())) {
        cef_mouse_event_t mouse_event = make_mouse_event(button, texture_rect, width, height, mouse_state);
        const godot::MouseButton button_index = button->get_button_index();
        if (is_wheel_button(button_index)) {
            if (!button->is_pressed() || host->send_mouse_wheel_event == nullptr) {
                return false;
            }
            const int delta = wheel_delta(button->get_factor());
            int delta_x = 0;
            int delta_y = 0;
            if (button_index == godot::MOUSE_BUTTON_WHEEL_UP) {
                delta_y = delta;
            } else if (button_index == godot::MOUSE_BUTTON_WHEEL_DOWN) {
                delta_y = -delta;
            } else if (button_index == godot::MOUSE_BUTTON_WHEEL_LEFT) {
                delta_x = delta;
            } else if (button_index == godot::MOUSE_BUTTON_WHEEL_RIGHT) {
                delta_x = -delta;
            }
            host->send_mouse_wheel_event(host, &mouse_event, delta_x, delta_y);
            return true;
        }

        cef_mouse_button_type_t type = MBT_LEFT;
        if (!cef_button_type(button_index, type) || host->send_mouse_click_event == nullptr) {
            return false;
        }

        if (button->is_pressed()) {
            request_focus = true;
        }
        const int mouse_up = button->is_pressed() ? 0 : 1;
        const int click_count = button->is_double_click() ? 2 : 1;
        host->send_mouse_click_event(host, &mouse_event, type, mouse_up, click_count);
        return true;
    }

    if (auto *key = godot::Object::cast_to<godot::InputEventKey>(event.ptr())) {
        if (host->send_key_event == nullptr) {
            return false;
        }

        const int windows_code = windows_key_code(key->get_keycode());
        const char16_t character = cef_character(key->get_unicode());
        if (windows_code == 0 && character == 0) {
            return false;
        }

        cef_key_event_t key_event{};
        key_event.size = sizeof(key_event);
        key_event.type = key->is_pressed() ? KEYEVENT_RAWKEYDOWN : KEYEVENT_KEYUP;
        key_event.modifiers = modifier_flags(key);
        if (key->is_echo()) {
            key_event.modifiers |= EVENTFLAG_IS_REPEAT;
        }
        if (is_keypad_key(key->get_keycode())) {
            key_event.modifiers |= EVENTFLAG_IS_KEY_PAD;
        }
        if (key->get_location() == godot::KEY_LOCATION_LEFT) {
            key_event.modifiers |= EVENTFLAG_IS_LEFT;
        } else if (key->get_location() == godot::KEY_LOCATION_RIGHT) {
            key_event.modifiers |= EVENTFLAG_IS_RIGHT;
        }
        key_event.windows_key_code = windows_code != 0 ? windows_code : static_cast<int>(character);
        key_event.native_key_code = key_event.windows_key_code;
        host->send_key_event(host, &key_event);

        const bool shortcut_modifier = key->is_ctrl_pressed() || key->is_alt_pressed() || key->is_meta_pressed();
        if (key->is_pressed() && character != 0 && !shortcut_modifier) {
            cef_key_event_t char_event = key_event;
            char_event.type = KEYEVENT_CHAR;
            char_event.character = character;
            char_event.unmodified_character = character;
            host->send_key_event(host, &char_event);
        }
        return true;
    }

    return false;
}

void notify_mouse_leave(cef_browser_host_t *host, MouseState &mouse_state) {
    if (host == nullptr || host->send_mouse_move_event == nullptr || !mouse_state.has_position) {
        return;
    }

    cef_mouse_event_t mouse_event;
    mouse_event.x = mouse_state.last_x;
    mouse_event.y = mouse_state.last_y;
    mouse_event.modifiers = EVENTFLAG_NONE;
    host->send_mouse_move_event(host, &mouse_event, 1);
    mouse_state.has_position = false;
}

} // namespace fennara::linux_cef_osr::input

#endif
