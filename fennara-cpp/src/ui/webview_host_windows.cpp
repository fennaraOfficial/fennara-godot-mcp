#ifdef _WIN32

#include "webview_backend.hpp"

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/window.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <webview/webview.h>

#include <cstdlib>
#include <cstdint>
#include <string>

namespace fennara {
namespace webview_backend {

namespace {

struct WebviewGeometry {
    bool visible = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

int owner_window_id(godot::Control *owner) {
    if (owner == nullptr) {
        return 0;
    }
    godot::Window *window = owner->get_window();
    if (window == nullptr) {
        return 0;
    }
    return window->get_window_id();
}

WebviewGeometry compute_webview_geometry(godot::Control *owner) {
    WebviewGeometry geometry;
    if (owner == nullptr || !owner->is_visible_in_tree()) {
        return geometry;
    }

    godot::Vector2 size = owner->get_size();
    if (size.x <= 0 || size.y <= 0) {
        return geometry;
    }

    godot::Vector2 screen_position = owner->get_screen_position();
    geometry.visible = true;
    geometry.x = static_cast<int>(screen_position.x);
    geometry.y = static_cast<int>(screen_position.y);
    geometry.width = static_cast<int>(size.x);
    geometry.height = static_cast<int>(size.y);
    return geometry;
}

bool debug_logging_enabled() {
    const char *generic = std::getenv("FENNARA_WEBVIEW_DEBUG");
    const char *windows = std::getenv("FENNARA_WINDOWS_WEBVIEW_DEBUG");
    return (generic != nullptr && std::string(generic) == "1") ||
           (windows != nullptr && std::string(windows) == "1");
}

void debug_log(const godot::String &message) {
    if (debug_logging_enabled()) {
        output_log(message);
    }
}

} // namespace

class WindowsWebviewBackend : public NativeWebviewBackend {
public:
    ~WindowsWebviewBackend() override {
        stop();
    }

    bool start(godot::Control *owner, const godot::String &url) override {
        if (started) {
            debug_log("Web chat host already started");
            return true;
        }

        if (owner == nullptr) {
            output_error("Web chat host cannot start: owner Control is null");
            return false;
        }

        godot::DisplayServer *display = godot::DisplayServer::get_singleton();
        if (display == nullptr) {
            output_error("Web chat host cannot start: DisplayServer is unavailable");
            return false;
        }

        int window_id = owner_window_id(owner);
        int64_t native_window = display->window_get_native_handle(
            godot::DisplayServer::WINDOW_HANDLE,
            window_id);
        debug_log("Web chat native window id=" + godot::String::num_int64(window_id) +
                   " handle=" + godot::String::num_int64(native_window));
        if (native_window == 0) {
            output_error("Web chat host cannot start: Godot native window handle is 0");
            return false;
        }
        current_window_id = window_id;
        parent_window = reinterpret_cast<void *>(native_window);

        debug_log("Web chat creating native Windows webview url=" + url);
        webview = webview_create(0, parent_window);
        if (webview == nullptr) {
            output_error(
                "Web chat host cannot start: webview_create returned null. "
                "Microsoft Edge WebView2 Runtime may be missing or unavailable. "
                "Install WebView2 Evergreen Runtime from "
                "https://developer.microsoft.com/microsoft-edge/webview2/ "
                "and restart Godot. Fennara MCP tools still work without the built-in chat dock.");
            return false;
        }

        widget = webview_get_native_handle(
            static_cast<webview_t>(webview),
            WEBVIEW_NATIVE_HANDLE_KIND_UI_WIDGET);
        debug_log("Web chat native widget handle=" +
                   godot::String::num_int64(reinterpret_cast<int64_t>(widget)));
        if (widget == nullptr) {
            output_error("Web chat host cannot start: native widget handle is null");
            webview_destroy(static_cast<webview_t>(webview));
            webview = nullptr;
            parent_window = nullptr;
            current_window_id = -1;
            return false;
        }

        std::string url_utf8 = url.utf8().get_data();
        webview_navigate(static_cast<webview_t>(webview), url_utf8.c_str());
        current_url = url;
        started = true;
        resize_to(owner);
        debug_log("Web chat native Windows webview started");
        return true;
    }

    void resize_to(godot::Control *owner) override {
        if (!started || owner == nullptr) {
            return;
        }

        if (widget == nullptr) {
            output_error("Web chat resize skipped: native widget handle is null");
            return;
        }

        WebviewGeometry geometry = compute_webview_geometry(owner);
        HWND hwnd = reinterpret_cast<HWND>(widget);
        if (!geometry.visible) {
            ShowWindow(hwnd, SW_HIDE);
            return;
        }

        int window_id = owner_window_id(owner);
        if (window_id != current_window_id) {
            debug_log("Web chat recreating native Windows webview for window id=" +
                       godot::String::num_int64(window_id));
            godot::String url = current_url;
            stop();
            start(owner, url);
            return;
        }

        HWND parent_hwnd = reinterpret_cast<HWND>(parent_window);
        POINT origin{
            static_cast<LONG>(geometry.x),
            static_cast<LONG>(geometry.y),
        };
        if (parent_hwnd != nullptr) {
            ScreenToClient(parent_hwnd, &origin);
        }
        int x = static_cast<int>(origin.x);
        int y = static_cast<int>(origin.y);

        SetWindowPos(
            hwnd,
            nullptr,
            x,
            y,
            geometry.width,
            geometry.height,
            SWP_NOACTIVATE | SWP_NOZORDER);
        ShowWindow(hwnd, SW_SHOW);

        if (x != last_x || y != last_y || geometry.width != last_width ||
            geometry.height != last_height) {
            last_x = x;
            last_y = y;
            last_width = geometry.width;
            last_height = geometry.height;
            debug_log("Web chat Windows geometry x=" + godot::String::num_int64(x) +
                       " y=" + godot::String::num_int64(y) +
                       " w=" + godot::String::num_int64(geometry.width) +
                       " h=" + godot::String::num_int64(geometry.height));
        }
    }

    void set_visible(bool visible) override {
        if (!started || widget == nullptr) {
            return;
        }
        ShowWindow(reinterpret_cast<HWND>(widget), visible ? SW_SHOW : SW_HIDE);
    }

    void stop() override {
        if (!started) {
            return;
        }

        debug_log("Web chat destroying native Windows webview");
        if (webview != nullptr) {
            webview_destroy(static_cast<webview_t>(webview));
        }

        webview = nullptr;
        widget = nullptr;
        parent_window = nullptr;
        current_url = "";
        started = false;
        current_window_id = -1;
        last_x = -1;
        last_y = -1;
        last_width = -1;
        last_height = -1;
    }

    bool is_started() const override {
        return started;
    }

private:
    void *webview = nullptr;
    void *widget = nullptr;
    void *parent_window = nullptr;
    godot::String current_url;
    bool started = false;
    int current_window_id = -1;
    int last_x = -1;
    int last_y = -1;
    int last_width = -1;
    int last_height = -1;
};

std::unique_ptr<NativeWebviewBackend> create_backend() {
    return std::make_unique<WindowsWebviewBackend>();
}

} // namespace webview_backend
} // namespace fennara

#endif
