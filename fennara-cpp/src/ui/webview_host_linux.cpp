#ifdef __linux__

#include "linux_cef_osr.hpp"
#include "webview_backend.hpp"

namespace fennara {
namespace webview_backend {

class LinuxWebviewBackend : public NativeWebviewBackend {
public:
    WebviewSurfaceMode surface_mode() const override {
        return WebviewSurfaceMode::InternalGodotSurface;
    }

    godot::Control *create_internal_control() override {
        return webview.create_control();
    }

    bool start(godot::Control *owner, const godot::String &url) override {
        return webview.start(owner, url);
    }

    void resize_to(godot::Control *owner) override {
        webview.resize_to(owner);
    }

    void set_visible(bool visible) override {
        webview.set_visible(visible);
    }

    void process(double delta) override {
        webview.process(delta);
    }

    bool handle_input(const godot::Ref<godot::InputEvent> &event) override {
        return webview.handle_input(event);
    }

    void set_focused(bool focused) override {
        webview.set_focused(focused);
    }

    void notify_mouse_leave() override {
        webview.notify_mouse_leave();
    }

    void stop() override {
        webview.stop();
    }

    bool is_started() const override {
        return webview.is_started();
    }

private:
    linux_cef_osr::LinuxCefOsrWebview webview;
};

std::unique_ptr<NativeWebviewBackend> create_backend() {
    return std::make_unique<LinuxWebviewBackend>();
}

} // namespace webview_backend
} // namespace fennara

#endif
