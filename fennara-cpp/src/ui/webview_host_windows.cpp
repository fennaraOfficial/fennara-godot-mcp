#ifdef _WIN32

#include "webview_backend.hpp"

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/window.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <webview/webview.h>

#include <array>
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

godot::String hwnd_string(HWND hwnd) {
    return godot::String::num_int64(reinterpret_cast<int64_t>(hwnd));
}

godot::String bool_string(bool value) {
    return value ? "true" : "false";
}

godot::String rect_string(const RECT &rect) {
    return "left=" + godot::String::num_int64(rect.left) +
           " top=" + godot::String::num_int64(rect.top) +
           " right=" + godot::String::num_int64(rect.right) +
           " bottom=" + godot::String::num_int64(rect.bottom) +
           " w=" + godot::String::num_int64(rect.right - rect.left) +
           " h=" + godot::String::num_int64(rect.bottom - rect.top);
}

godot::String window_state_string(HWND hwnd) {
    if (hwnd == nullptr) {
        return "hwnd=null";
    }

    RECT window_rect{};
    RECT client_rect{};
    const bool has_window_rect = GetWindowRect(hwnd, &window_rect) != 0;
    const bool has_client_rect = GetClientRect(hwnd, &client_rect) != 0;
    std::array<char, 128> class_name{};
    int class_len = GetClassNameA(hwnd, class_name.data(), static_cast<int>(class_name.size()));
    godot::String class_value = class_len > 0
                                    ? godot::String(class_name.data())
                                    : "<unknown>";

    return "hwnd=" + hwnd_string(hwnd) +
           " class=" + class_value +
           " visible=" + bool_string(IsWindowVisible(hwnd) != 0) +
           " enabled=" + bool_string(IsWindowEnabled(hwnd) != 0) +
           " parent=" + hwnd_string(GetParent(hwnd)) +
           " style=0x" + godot::String::num_uint64(
                              static_cast<uint64_t>(GetWindowLongPtr(hwnd, GWL_STYLE)), 16) +
           " ex_style=0x" + godot::String::num_uint64(
                                 static_cast<uint64_t>(GetWindowLongPtr(hwnd, GWL_EXSTYLE)), 16) +
           " window_rect={" + (has_window_rect ? rect_string(window_rect) : "unavailable") + "}" +
           " client_rect={" + (has_client_rect ? rect_string(client_rect) : "unavailable") + "}";
}

godot::String webview_error_string(webview_error_t error) {
    return godot::String::num_int64(static_cast<int64_t>(error));
}

constexpr const char *kWindowsLifecycleScript = R"JS(
(() => {
  const post = (event, extra = {}) => {
    try {
      if (typeof window.__fennaraWebviewDebug === "function") {
        window.__fennaraWebviewDebug(JSON.stringify({
          event,
          href: String(location.href || ""),
          readyState: String(document.readyState || ""),
          hasBody: Boolean(document.body),
          bodyChildren: document.body ? document.body.children.length : -1,
          title: String(document.title || ""),
          ...extra,
        }));
      }
    } catch (_) {}
  };
  post("init");
  document.addEventListener("DOMContentLoaded", () => post("domcontentloaded"), { once: true });
  window.addEventListener("load", () => post("load"), { once: true });
  window.addEventListener("error", (event) => post("error", {
    message: String(event.message || ""),
    source: String(event.filename || ""),
    line: Number(event.lineno || 0),
    column: Number(event.colno || 0),
  }));
  window.addEventListener("unhandledrejection", (event) => post("unhandledrejection", {
    reason: String(event.reason && (event.reason.stack || event.reason.message || event.reason) || ""),
  }));
  setTimeout(() => post("heartbeat-1000ms", {
    activeElement: document.activeElement ? document.activeElement.tagName : "",
  }), 1000);
  setTimeout(() => post("heartbeat-3000ms", {
    activeElement: document.activeElement ? document.activeElement.tagName : "",
  }), 3000);
})();
)JS";

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
        debug_log("Web chat Windows parent state before create: " +
                  window_state_string(reinterpret_cast<HWND>(parent_window)));
        const int webview_debug = debug_logging_enabled() ? 1 : 0;
        webview = webview_create(webview_debug, parent_window);
        if (webview == nullptr) {
            output_error(
                "Web chat host cannot start: webview_create returned null. "
                "Microsoft Edge WebView2 Runtime may be missing or unavailable. "
                "Install WebView2 Evergreen Runtime from "
                "https://developer.microsoft.com/microsoft-edge/webview2/ "
                "and restart Godot. Fennara MCP tools still work without the built-in chat dock.");
            return false;
        }
        const webview_version_info_t *version = webview_version();
        if (version != nullptr) {
            debug_log("Web chat Windows webview wrapper version=" +
                      godot::String(version->version_number));
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
        debug_log("Web chat Windows widget state after create: " +
                  window_state_string(reinterpret_cast<HWND>(widget)));

        void *controller = webview_get_native_handle(
            static_cast<webview_t>(webview),
            WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER);
        debug_log("Web chat Windows browser controller handle=" +
                  godot::String::num_int64(reinterpret_cast<int64_t>(controller)));

        webview_error_t bind_error = webview_bind(
            static_cast<webview_t>(webview),
            "__fennaraWebviewDebug",
            &WindowsWebviewBackend::debug_binding,
            this);
        debug_log("Web chat Windows debug binding result=" +
                  webview_error_string(bind_error));
        webview_error_t init_error =
            webview_init(static_cast<webview_t>(webview), kWindowsLifecycleScript);
        debug_log("Web chat Windows lifecycle script init result=" +
                  webview_error_string(init_error));

        std::string url_utf8 = url.utf8().get_data();
        webview_error_t navigate_error =
            webview_navigate(static_cast<webview_t>(webview), url_utf8.c_str());
        debug_log("Web chat Windows navigate result=" +
                  webview_error_string(navigate_error) +
                  " url=" + url);
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
        debug_log("Web chat Windows widget state after ShowWindow: " +
                  window_state_string(hwnd));

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
        if (!visible) {
            set_focused(false);
        }
        ShowWindow(reinterpret_cast<HWND>(widget), visible ? SW_SHOW : SW_HIDE);
        debug_log("Web chat Windows set_visible visible=" + bool_string(visible) +
                  " widget={" + window_state_string(reinterpret_cast<HWND>(widget)) + "}");
    }

    void set_focused(bool next_focused) override {
        if (!started || widget == nullptr) {
            focused = false;
            return;
        }

        HWND webview_hwnd = reinterpret_cast<HWND>(widget);
        if (next_focused) {
            HWND current = GetFocus();
            if (focused && (current == webview_hwnd ||
                            (current != nullptr && IsChild(webview_hwnd, current)))) {
                return;
            }
            SetFocus(webview_hwnd);
            focused = true;
            debug_log("Web chat Windows focus requested widget=" + hwnd_string(webview_hwnd) +
                      " before=" + hwnd_string(current) +
                      " after=" + hwnd_string(GetFocus()));
            return;
        }

        HWND focused_hwnd = GetFocus();
        if (focused_hwnd == webview_hwnd ||
            (focused_hwnd != nullptr && IsChild(webview_hwnd, focused_hwnd))) {
            HWND parent_hwnd = reinterpret_cast<HWND>(parent_window);
            if (parent_hwnd != nullptr) {
                SetFocus(parent_hwnd);
            }
            debug_log("Web chat Windows focus released widget=" + hwnd_string(webview_hwnd) +
                      " before=" + hwnd_string(focused_hwnd) +
                      " after=" + hwnd_string(GetFocus()));
        } else {
            debug_log("Web chat Windows focus release skipped: current focus outside widget=" +
                      hwnd_string(webview_hwnd) + " current=" + hwnd_string(focused_hwnd));
        }
        focused = false;
    }

    void stop() override {
        if (!started) {
            return;
        }

        debug_log("Web chat destroying native Windows webview");
        set_focused(false);
        if (webview != nullptr) {
            webview_destroy(static_cast<webview_t>(webview));
        }

        webview = nullptr;
        widget = nullptr;
        parent_window = nullptr;
        current_url = "";
        started = false;
        focused = false;
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
    static void debug_binding(const char *id, const char *req, void *arg) {
        auto *self = static_cast<WindowsWebviewBackend *>(arg);
        if (self == nullptr) {
            return;
        }
        self->debug_log_from_js(id, req);
    }

    void debug_log_from_js(const char *id, const char *req) {
        debug_log("Web chat Windows JS lifecycle id=" +
                  godot::String(id != nullptr ? id : "") +
                  " payload=" + godot::String(req != nullptr ? req : ""));
        if (webview != nullptr && id != nullptr) {
            webview_error_t return_error =
                webview_return(static_cast<webview_t>(webview), id, 0, "null");
            debug_log("Web chat Windows JS lifecycle return result=" +
                      webview_error_string(return_error));
        }
    }

    void *webview = nullptr;
    void *widget = nullptr;
    void *parent_window = nullptr;
    godot::String current_url;
    bool started = false;
    bool focused = false;
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
