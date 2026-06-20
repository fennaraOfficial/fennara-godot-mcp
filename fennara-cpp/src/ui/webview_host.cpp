#include "fennara/ui/webview_host.hpp"

#include "fennara/logger.hpp"

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <webview/webview.h>
#endif

#include <string>

namespace fennara {

namespace {

void output_log(const godot::String &message) {
    FLOG_UI(message);
    godot::UtilityFunctions::print(godot::String("[Fennara] ") + message);
}

void output_error(const godot::String &message) {
    FLOG_ERR(message);
    godot::UtilityFunctions::push_error(godot::String("[Fennara] ") + message);
}

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

} // namespace

WebviewHost::~WebviewHost() {
    stop();
}

bool WebviewHost::start(godot::Control *owner, const godot::String &url) {
    if (started) {
        output_log("Web chat host already started");
        return true;
    }

#ifdef _WIN32
    if (owner == nullptr) {
        output_error("Web chat host cannot start: owner Control is null");
        return false;
    }

    godot::DisplayServer *display = godot::DisplayServer::get_singleton();
    if (display == nullptr) {
        output_error("Web chat host cannot start: DisplayServer is unavailable");
        return false;
    }
    godot::OS *os = godot::OS::get_singleton();
    bool headless =
        (os != nullptr && os->has_feature("headless")) ||
        display->get_name().to_lower() == "headless";
    if (headless) {
        output_log("Web chat host skipped: headless editor has no native window");
        return false;
    }

    int window_id = owner_window_id(owner);
    int64_t native_window = display->window_get_native_handle(
        godot::DisplayServer::WINDOW_HANDLE,
        window_id);
    output_log("Web chat native window id=" + godot::String::num_int64(window_id) +
               " handle=" + godot::String::num_int64(native_window));
    if (native_window == 0) {
        output_error("Web chat host cannot start: Godot native window handle is 0");
        return false;
    }
    current_window_id = window_id;
    parent_window = reinterpret_cast<void *>(native_window);

    output_log("Web chat creating native webview url=" + url);
    webview = webview_create(0, parent_window);
    if (webview == nullptr) {
        output_error("Web chat host cannot start: webview_create returned null");
        return false;
    }

    widget = webview_get_native_handle(
        static_cast<webview_t>(webview),
        WEBVIEW_NATIVE_HANDLE_KIND_UI_WIDGET);
    output_log("Web chat native widget handle=" +
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
    output_log("Web chat native webview started");
    return true;
#else
    (void)owner;
    (void)url;
    output_error("Web chat native webview is not wired for this platform build yet");
    return false;
#endif
}

void WebviewHost::resize_to(godot::Control *owner) {
    if (!started || owner == nullptr) {
        return;
    }

#ifdef _WIN32
    if (widget == nullptr) {
        output_error("Web chat resize skipped: native widget handle is null");
        return;
    }

    HWND hwnd = reinterpret_cast<HWND>(widget);
    if (!owner->is_visible_in_tree()) {
        ShowWindow(hwnd, SW_HIDE);
        return;
    }

    godot::Vector2 screen_position = owner->get_screen_position();
    godot::Vector2 size = owner->get_size();
    int width = static_cast<int>(size.x);
    int height = static_cast<int>(size.y);
    if (width <= 0 || height <= 0) {
        ShowWindow(hwnd, SW_HIDE);
        return;
    }

    int window_id = owner_window_id(owner);
    if (window_id != current_window_id) {
        output_log("Web chat recreating native webview for window id=" +
                   godot::String::num_int64(window_id));
        godot::String url = current_url;
        stop();
        start(owner, url);
        return;
    }

    HWND parent_hwnd = reinterpret_cast<HWND>(parent_window);
    POINT origin{
        static_cast<LONG>(screen_position.x),
        static_cast<LONG>(screen_position.y),
    };
    if (parent_hwnd != nullptr) {
        ScreenToClient(parent_hwnd, &origin);
    }
    int x = static_cast<int>(origin.x);
    int y = static_cast<int>(origin.y);

    MoveWindow(hwnd, x, y, width, height, TRUE);
    ShowWindow(hwnd, SW_SHOW);

    if (x != last_x || y != last_y || width != last_width || height != last_height) {
        last_x = x;
        last_y = y;
        last_width = width;
        last_height = height;
        output_log("Web chat geometry x=" + godot::String::num_int64(x) +
                   " y=" + godot::String::num_int64(y) +
                   " w=" + godot::String::num_int64(width) +
                   " h=" + godot::String::num_int64(height));
    }
#endif
}

void WebviewHost::stop() {
    if (!started) {
        return;
    }

#ifdef _WIN32
    output_log("Web chat destroying native webview");
    if (webview != nullptr) {
        webview_destroy(static_cast<webview_t>(webview));
    }
#endif

    webview = nullptr;
    widget = nullptr;
    parent_window = nullptr;
    started = false;
    current_window_id = -1;
    last_x = -1;
    last_y = -1;
    last_width = -1;
    last_height = -1;
}

bool WebviewHost::is_started() const {
    return started;
}

} // namespace fennara
