#pragma once

#ifdef __linux__

#include "linux_cef_runtime.hpp"

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <memory>

namespace fennara::linux_cef_osr {

class LinuxCefOsrWebview {
public:
    LinuxCefOsrWebview();
    ~LinuxCefOsrWebview();

    godot::Control *create_control();
    bool start(godot::Control *owner, const godot::String &url);
    void resize_to(godot::Control *owner);
    void set_visible(bool visible);
    void process(double delta);
    bool handle_input(const godot::Ref<godot::InputEvent> &event);
    void set_focused(bool focused);
    void notify_mouse_leave();
    void stop();
    bool is_started() const;

private:
    struct CefObjects;

    godot::TextureRect *current_texture_rect() const;

    uint64_t texture_rect_id = 0;
    godot::Ref<godot::ImageTexture> texture;
    std::unique_ptr<CefObjects> cef;
    int texture_width = 0;
    int texture_height = 0;
    int delivered_frame_count = 0;
    bool started = false;
    bool focused = false;
};

} // namespace fennara::linux_cef_osr

#endif
