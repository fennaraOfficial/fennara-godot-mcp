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

#ifdef __linux__
#include <gtk/gtk.h>
#include <webview/webview.h>
#endif

#include <string>

namespace fennara {

#ifdef __APPLE__
namespace mac_webview {
bool start(void **webview, void **parent_window, godot::Control *owner, const godot::String &url);
void resize_to(void *webview, void **parent_window, godot::Control *owner);
void set_visible(void *webview, bool visible);
void stop(void **webview, void **parent_window);
} // namespace mac_webview
#endif

namespace {

void output_log(const godot::String &message) {
    FLOG_UI(message);
    godot::UtilityFunctions::print(godot::String("[Fennara] ") + message);
}

void output_error(const godot::String &message) {
    FLOG_ERR(message);
    godot::UtilityFunctions::push_error(godot::String("[Fennara] ") + message);
}

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

bool editor_is_headless() {
    godot::DisplayServer *display = godot::DisplayServer::get_singleton();
    godot::OS *os = godot::OS::get_singleton();
    return (os != nullptr && os->has_feature("headless")) ||
           (display != nullptr && display->get_name().to_lower() == "headless");
}

#ifdef __linux__
bool linux_webview_debug_enabled() {
    godot::OS *os = godot::OS::get_singleton();
    return os != nullptr && os->get_environment("FENNARA_LINUX_WEBVIEW_DEBUG") == "1";
}

void linux_webview_debug_log(const godot::String &message) {
    if (!linux_webview_debug_enabled()) {
        return;
    }
    output_log(godot::String("Linux webview debug: ") + message);
}

void pump_linux_webview_events() {
    int iterations = 0;
    while (g_main_context_pending(nullptr) && iterations < 64) {
        g_main_context_iteration(nullptr, FALSE);
        iterations++;
    }
    if (iterations > 0) {
        linux_webview_debug_log("pumped gtk events count=" + godot::String::num_int64(iterations));
    }
}
#endif

} // namespace

WebviewHost::~WebviewHost() {
    stop();
}

bool WebviewHost::start(godot::Control *owner, const godot::String &url) {
    if (started) {
        output_log("Web chat host already started");
        return true;
    }

    if (editor_is_headless()) {
        output_log("Web chat host skipped: headless editor has no native window");
        return false;
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
#elif defined(__APPLE__)
    if (mac_webview::start(&webview, &parent_window, owner, url)) {
        current_url = url;
        started = true;
        resize_to(owner);
        output_log("Web chat native macOS webview started");
        return true;
    }
    output_error("Web chat native macOS webview could not start");
    return false;
#elif defined(__linux__)
    if (owner == nullptr) {
        output_error("Web chat host cannot start: owner Control is null");
        return false;
    }

    WebviewGeometry geometry = compute_webview_geometry(owner);
    int width = geometry.width > 0 ? geometry.width : 420;
    int height = geometry.height > 0 ? geometry.height : 720;
    linux_webview_debug_log("start requested visible=" +
                            godot::String(geometry.visible ? "true" : "false") +
                            " owner_size=" + godot::String::num_int64(geometry.width) +
                            "x" + godot::String::num_int64(geometry.height) +
                            " initial_window_size=" + godot::String::num_int64(width) +
                            "x" + godot::String::num_int64(height));

    webview = webview_create(0, nullptr);
    if (webview == nullptr) {
        output_error("Web chat Linux WebKitGTK host cannot start: webview_create returned null");
        return false;
    }

    parent_window = webview_get_native_handle(
        static_cast<webview_t>(webview),
        WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
    widget = webview_get_native_handle(
        static_cast<webview_t>(webview),
        WEBVIEW_NATIVE_HANDLE_KIND_UI_WIDGET);
    linux_webview_debug_log("native handles webview=" +
                            godot::String::num_int64(reinterpret_cast<int64_t>(webview)) +
                            " window=" +
                            godot::String::num_int64(reinterpret_cast<int64_t>(parent_window)) +
                            " widget=" +
                            godot::String::num_int64(reinterpret_cast<int64_t>(widget)));

    webview_set_title(static_cast<webview_t>(webview), "Fennara");
    webview_set_size(static_cast<webview_t>(webview), width, height, WEBVIEW_HINT_NONE);

    std::string url_utf8 = url.utf8().get_data();
    webview_navigate(static_cast<webview_t>(webview), url_utf8.c_str());

    current_url = url;
    started = true;
    last_width = width;
    last_height = height;
    pump_linux_webview_events();
    output_log("Web chat native Linux WebKitGTK window started");
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

    WebviewGeometry geometry = compute_webview_geometry(owner);
    HWND hwnd = reinterpret_cast<HWND>(widget);
    if (!geometry.visible) {
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
        output_log("Web chat geometry x=" + godot::String::num_int64(x) +
                   " y=" + godot::String::num_int64(y) +
                   " w=" + godot::String::num_int64(geometry.width) +
                   " h=" + godot::String::num_int64(geometry.height));
    }
#else
#ifdef __APPLE__
    mac_webview::resize_to(webview, &parent_window, owner);
#elif defined(__linux__)
    WebviewGeometry geometry = compute_webview_geometry(owner);
    if (parent_window != nullptr) {
        GtkWidget *window = static_cast<GtkWidget *>(parent_window);
        if (!geometry.visible) {
            linux_webview_debug_log("resize hiding window: owner not visible");
            gtk_widget_hide(window);
        } else {
            gtk_widget_show(window);
            if (geometry.width > 0 && geometry.height > 0 &&
                (geometry.width != last_width || geometry.height != last_height)) {
                linux_webview_debug_log("resize window size=" +
                                        godot::String::num_int64(geometry.width) +
                                        "x" + godot::String::num_int64(geometry.height) +
                                        " previous=" + godot::String::num_int64(last_width) +
                                        "x" + godot::String::num_int64(last_height));
                webview_set_size(static_cast<webview_t>(webview),
                                 geometry.width,
                                 geometry.height,
                                 WEBVIEW_HINT_NONE);
                last_width = geometry.width;
                last_height = geometry.height;
            }
        }
    } else {
        linux_webview_debug_log("resize skipped: native window handle is null");
    }
    pump_linux_webview_events();
#else
    (void)owner;
#endif
#endif
}

void WebviewHost::set_visible(bool visible) {
#ifdef _WIN32
    if (!started || widget == nullptr) {
        return;
    }
    ShowWindow(reinterpret_cast<HWND>(widget), visible ? SW_SHOW : SW_HIDE);
#elif defined(__APPLE__)
    mac_webview::set_visible(webview, visible);
#elif defined(__linux__)
    if (!started || parent_window == nullptr) {
        linux_webview_debug_log("set_visible skipped started=" +
                                godot::String(started ? "true" : "false") +
                                " window_null=" +
                                godot::String(parent_window == nullptr ? "true" : "false"));
        return;
    }
    linux_webview_debug_log(godot::String("set_visible ") +
                            (visible ? godot::String("true") : godot::String("false")));
    GtkWidget *window = static_cast<GtkWidget *>(parent_window);
    if (visible) {
        gtk_widget_show(window);
    } else {
        gtk_widget_hide(window);
    }
    pump_linux_webview_events();
#else
    (void)visible;
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
#elif defined(__APPLE__)
    mac_webview::stop(&webview, &parent_window);
#elif defined(__linux__)
    output_log("Web chat destroying Linux WebKitGTK window");
    if (webview != nullptr) {
        webview_destroy(static_cast<webview_t>(webview));
        pump_linux_webview_events();
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
