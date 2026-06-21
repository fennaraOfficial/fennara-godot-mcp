#pragma once

#ifdef __linux__

#include "linux_cef_runtime.hpp"

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/variant/string.hpp>

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

    godot::TextureRect *texture_rect = nullptr;
    godot::Ref<godot::ImageTexture> texture;
    std::unique_ptr<CefObjects> cef;
    bool started = false;
    bool focused = false;
};

} // namespace fennara::linux_cef_osr

#endif
