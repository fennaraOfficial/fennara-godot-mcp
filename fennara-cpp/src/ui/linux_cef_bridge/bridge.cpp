#ifdef __linux__

#include "../linux_cef_bridge_api.hpp"

#include <include/cef_app.h>
#include <include/cef_browser.h>
#include <include/cef_client.h>
#include <include/cef_render_handler.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace fennara_cef_bridge_impl {

constexpr int kMinimumDimension = 1;

void emit_log(const fennara_cef_bridge_callbacks *callbacks, const std::string &message) {
    if (callbacks != nullptr && callbacks->log != nullptr) {
        callbacks->log(message.c_str(), callbacks->user_data);
    }
}

std::string safe_string(const char *value) {
    return value != nullptr ? std::string(value) : std::string();
}

class FennaraCefApp : public CefApp {
public:
    FennaraCefApp() = default;

private:
    IMPLEMENT_REFCOUNTING(FennaraCefApp);
    DISALLOW_COPY_AND_ASSIGN(FennaraCefApp);
};

class OsrRenderHandler : public CefRenderHandler {
public:
    explicit OsrRenderHandler(fennara_cef_bridge_callbacks callbacks) :
            callbacks_(callbacks) {
    }

    void SetSize(int width, int height) {
        std::lock_guard<std::mutex> lock(mutex_);
        width_ = std::max(kMinimumDimension, width);
        height_ = std::max(kMinimumDimension, height);
    }

    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect) override {
        (void)browser;
        std::lock_guard<std::mutex> lock(mutex_);
        rect = CefRect(0, 0, width_, height_);
    }

    void OnPaint(CefRefPtr<CefBrowser> browser,
                 PaintElementType type,
                 const RectList &dirtyRects,
                 const void *buffer,
                 int width,
                 int height) override {
        (void)browser;
        (void)dirtyRects;
        if (type != PET_VIEW || buffer == nullptr || width <= 0 || height <= 0 ||
            callbacks_.paint == nullptr) {
            return;
        }

        const auto *src = static_cast<const uint8_t *>(buffer);
        const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
        std::vector<uint8_t> rgba(pixel_count * 4);
        for (size_t i = 0; i < pixel_count; i++) {
            rgba[i * 4 + 0] = src[i * 4 + 2];
            rgba[i * 4 + 1] = src[i * 4 + 1];
            rgba[i * 4 + 2] = src[i * 4 + 0];
            rgba[i * 4 + 3] = src[i * 4 + 3];
        }

        callbacks_.paint(rgba.data(), width, height, callbacks_.user_data);
    }

private:
    fennara_cef_bridge_callbacks callbacks_{};
    std::mutex mutex_;
    int width_ = 1;
    int height_ = 1;

    IMPLEMENT_REFCOUNTING(OsrRenderHandler);
    DISALLOW_COPY_AND_ASSIGN(OsrRenderHandler);
};

class OsrClient : public CefClient, public CefLifeSpanHandler {
public:
    explicit OsrClient(CefRefPtr<OsrRenderHandler> render_handler) :
            render_handler_(render_handler) {
    }

    CefRefPtr<CefRenderHandler> GetRenderHandler() override {
        return render_handler_;
    }

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
        return this;
    }

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
        browser_ = browser;
    }

    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
        (void)browser;
        browser_ = nullptr;
    }

private:
    CefRefPtr<OsrRenderHandler> render_handler_;
    CefRefPtr<CefBrowser> browser_;

    IMPLEMENT_REFCOUNTING(OsrClient);
    DISALLOW_COPY_AND_ASSIGN(OsrClient);
};

} // namespace fennara_cef_bridge_impl

struct fennara_cef_bridge_browser {
    CefRefPtr<fennara_cef_bridge_impl::OsrRenderHandler> render_handler;
    CefRefPtr<fennara_cef_bridge_impl::OsrClient> client;
    CefRefPtr<CefBrowser> browser;
};

namespace fennara_cef_bridge_impl {

std::atomic<bool> g_initialized{false};

CefMainArgs make_main_args(const fennara_cef_bridge_settings *settings) {
    if (settings == nullptr || settings->argc <= 0 || settings->argv == nullptr) {
        static char fallback[] = "godot";
        static char *fallback_argv[] = { fallback, nullptr };
        return CefMainArgs(1, fallback_argv);
    }
    return CefMainArgs(settings->argc, settings->argv);
}

void assign_cef_string(cef_string_t &target, const char *value) {
    CefString cef_value(&target);
    cef_value = safe_string(value);
}

void apply_settings(CefSettings &settings, const fennara_cef_bridge_settings *config) {
    settings.no_sandbox = true;
    settings.windowless_rendering_enabled = true;
    settings.disable_signal_handlers = true;
    settings.log_severity = LOGSEVERITY_WARNING;
    assign_cef_string(settings.browser_subprocess_path, config->subprocess_path);
    assign_cef_string(settings.resources_dir_path, config->resources_path);
    assign_cef_string(settings.locales_dir_path, config->locales_path);
    assign_cef_string(settings.root_cache_path, config->root_cache_path);
    assign_cef_string(settings.cache_path, config->cache_path);
    assign_cef_string(settings.log_file, config->log_file);
}

int bridge_execute_process(const fennara_cef_bridge_settings *settings,
                           const fennara_cef_bridge_callbacks *callbacks) {
    CefMainArgs args = make_main_args(settings);
    CefRefPtr<FennaraCefApp> app(new FennaraCefApp());
    const int result = CefExecuteProcess(args, app, nullptr);
    emit_log(callbacks, "cef_execute_process returned " + std::to_string(result));
    return result;
}

int bridge_initialize(const fennara_cef_bridge_settings *settings,
                      const fennara_cef_bridge_callbacks *callbacks) {
    if (settings == nullptr) {
        emit_log(callbacks, "cef_initialize missing settings");
        return 0;
    }
    if (g_initialized.load(std::memory_order_acquire)) {
        return 1;
    }

    CefMainArgs args = make_main_args(settings);
    CefSettings cef_settings;
    apply_settings(cef_settings, settings);
    CefRefPtr<FennaraCefApp> app(new FennaraCefApp());
    const bool ok = CefInitialize(args, cef_settings, app, nullptr);
    g_initialized.store(ok, std::memory_order_release);
    emit_log(callbacks, ok ? "cef_initialize succeeded" : "cef_initialize failed");
    return ok ? 1 : 0;
}

void bridge_do_message_loop_work() {
    if (g_initialized.load(std::memory_order_acquire)) {
        CefDoMessageLoopWork();
    }
}

void bridge_shutdown() {
    if (g_initialized.exchange(false, std::memory_order_acq_rel)) {
        CefShutdown();
    }
}

fennara_cef_bridge_browser *bridge_create_browser(
    const fennara_cef_bridge_browser_config *config,
    const fennara_cef_bridge_callbacks *callbacks) {
    if (!g_initialized.load(std::memory_order_acquire) || config == nullptr || config->url == nullptr) {
        emit_log(callbacks, "create_browser called before CEF initialization or without URL");
        return nullptr;
    }

    auto *handle = new fennara_cef_bridge_browser();
    handle->render_handler = new OsrRenderHandler(callbacks != nullptr ? *callbacks : fennara_cef_bridge_callbacks{});
    handle->render_handler->SetSize(config->width, config->height);
    handle->client = new OsrClient(handle->render_handler);

    CefWindowInfo window_info;
    window_info.SetAsWindowless(0);
    window_info.bounds = CefRect(0, 0, std::max(kMinimumDimension, config->width),
                                 std::max(kMinimumDimension, config->height));
    window_info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;

    CefBrowserSettings browser_settings;
    browser_settings.windowless_frame_rate = 30;
    browser_settings.background_color = 0xffffffff;

    handle->browser = CefBrowserHost::CreateBrowserSync(
        window_info,
        handle->client,
        safe_string(config->url),
        browser_settings,
        nullptr,
        nullptr);

    if (!handle->browser) {
        emit_log(callbacks, "CefBrowserHost::CreateBrowserSync returned null");
        delete handle;
        return nullptr;
    }

    return handle;
}

void bridge_close_browser(fennara_cef_bridge_browser *browser) {
    if (browser == nullptr) {
        return;
    }
    if (browser->browser) {
        browser->browser->GetHost()->CloseBrowser(true);
        browser->browser = nullptr;
    }
    browser->client = nullptr;
    browser->render_handler = nullptr;
    delete browser;
}

void bridge_resize_browser(fennara_cef_bridge_browser *browser, int width, int height) {
    if (browser == nullptr || !browser->browser || !browser->render_handler) {
        return;
    }
    browser->render_handler->SetSize(width, height);
    browser->browser->GetHost()->WasResized();
}

void bridge_set_browser_hidden(fennara_cef_bridge_browser *browser, int hidden) {
    if (browser != nullptr && browser->browser) {
        browser->browser->GetHost()->WasHidden(hidden != 0);
    }
}

void bridge_set_browser_focus(fennara_cef_bridge_browser *browser, int focused) {
    if (browser != nullptr && browser->browser) {
        browser->browser->GetHost()->SetFocus(focused != 0);
    }
}

CefMouseEvent make_mouse_event(const fennara_cef_bridge_mouse_event *event) {
    CefMouseEvent out{};
    if (event != nullptr) {
        out.x = event->x;
        out.y = event->y;
        out.modifiers = event->modifiers;
    }
    return out;
}

void bridge_send_mouse_move(fennara_cef_bridge_browser *browser,
                            const fennara_cef_bridge_mouse_event *event,
                            int mouse_leave) {
    if (browser != nullptr && browser->browser) {
        browser->browser->GetHost()->SendMouseMoveEvent(make_mouse_event(event), mouse_leave != 0);
    }
}

void bridge_send_mouse_click(fennara_cef_bridge_browser *browser,
                             const fennara_cef_bridge_mouse_event *event,
                             int button,
                             int mouse_up,
                             int click_count) {
    if (browser != nullptr && browser->browser) {
        browser->browser->GetHost()->SendMouseClickEvent(
            make_mouse_event(event),
            static_cast<cef_mouse_button_type_t>(button),
            mouse_up != 0,
            click_count);
    }
}

void bridge_send_mouse_wheel(fennara_cef_bridge_browser *browser,
                             const fennara_cef_bridge_mouse_event *event,
                             int delta_x,
                             int delta_y) {
    if (browser != nullptr && browser->browser) {
        browser->browser->GetHost()->SendMouseWheelEvent(make_mouse_event(event), delta_x, delta_y);
    }
}

void bridge_send_key_event(fennara_cef_bridge_browser *browser,
                           const fennara_cef_bridge_key_event *event) {
    if (browser == nullptr || !browser->browser || event == nullptr) {
        return;
    }

    CefKeyEvent key_event{};
    key_event.type = static_cast<cef_key_event_type_t>(event->type);
    key_event.modifiers = event->modifiers;
    key_event.windows_key_code = event->windows_key_code;
    key_event.native_key_code = event->native_key_code;
    key_event.character = event->character;
    key_event.unmodified_character = event->unmodified_character;
    browser->browser->GetHost()->SendKeyEvent(key_event);
}

const fennara_cef_bridge_api kApi = {
    FENNARA_LINUX_CEF_BRIDGE_API_VERSION,
    sizeof(fennara_cef_bridge_api),
    bridge_execute_process,
    bridge_initialize,
    bridge_do_message_loop_work,
    bridge_shutdown,
    bridge_create_browser,
    bridge_close_browser,
    bridge_resize_browser,
    bridge_set_browser_hidden,
    bridge_set_browser_focus,
    bridge_send_mouse_move,
    bridge_send_mouse_click,
    bridge_send_mouse_wheel,
    bridge_send_key_event,
};

} // namespace fennara_cef_bridge_impl

extern "C" __attribute__((visibility("default")))
const fennara_cef_bridge_api *fennara_linux_cef_bridge_get_api(uint32_t version) {
    if (version != FENNARA_LINUX_CEF_BRIDGE_API_VERSION) {
        return nullptr;
    }
    return &fennara_cef_bridge_impl::kApi;
}

#endif
