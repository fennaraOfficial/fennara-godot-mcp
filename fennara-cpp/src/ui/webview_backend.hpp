#pragma once

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/string.hpp>

#include <memory>

namespace fennara {
namespace webview_backend {

void output_log(const godot::String &message);
void output_error(const godot::String &message);

enum class WebviewSurfaceMode {
    NativeWindowOverlay,
    InternalGodotSurface,
};

class NativeWebviewBackend {
public:
    virtual ~NativeWebviewBackend() = default;

    virtual WebviewSurfaceMode surface_mode() const {
        return WebviewSurfaceMode::NativeWindowOverlay;
    }

    virtual godot::Control *create_internal_control() {
        return nullptr;
    }

    virtual bool start(godot::Control *owner, const godot::String &url) = 0;
    virtual void resize_to(godot::Control *owner) = 0;
    virtual void set_visible(bool visible) = 0;
    virtual void process(double delta) {
        (void)delta;
    }
    virtual bool handle_input(const godot::Ref<godot::InputEvent> &event) {
        (void)event;
        return false;
    }
    virtual void set_focused(bool focused) {
        (void)focused;
    }
    virtual void notify_mouse_leave() {
    }
    virtual void stop() = 0;
    virtual bool is_started() const = 0;
};

std::unique_ptr<NativeWebviewBackend> create_backend();

} // namespace webview_backend
} // namespace fennara
