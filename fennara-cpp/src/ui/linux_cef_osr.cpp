#ifdef __linux__

#include "linux_cef_osr.hpp"

#include "linux_cef_capi.hpp"
#include "linux_cef_input.hpp"
#include "webview_backend.hpp"

#include "fennara/app_paths.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <algorithm>
#include <cstring>
#include <chrono>
#include <mutex>
#include <random>
#include <string>
#include <vector>
namespace fennara::linux_cef_osr {
namespace {

using namespace fennara::linux_cef_runtime;
using namespace fennara::linux_cef_runtime::capi;

constexpr int kMinimumDimension = 1;

std::string utf8(const godot::String &value) {
    return value.utf8().get_data();
}

int clamp_dimension(double value) {
    return std::max(kMinimumDimension, static_cast<int>(value));
}

void ensure_dir(const godot::String &path) {
    if (!path.is_empty()) {
        godot::DirAccess::make_dir_recursive_absolute(path);
    }
}

godot::String process_profile_name() {
    godot::OS *os = godot::OS::get_singleton();
    const int32_t pid = os != nullptr ? os->get_process_id() : 0;
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    uint32_t nonce = 0;
    try {
        nonce = std::random_device{}();
    } catch (...) {
        nonce = static_cast<uint32_t>(millis);
    }
    return "godot-" + godot::String::num_int64(pid) +
           "-" + godot::String::num_int64(millis) +
           "-" + godot::String::num_int64(nonce);
}

struct CefString {
    CefString(const cef_api_t &api_ref, const godot::String &value) :
            api(api_ref) {
        const std::string bytes = utf8(value);
        if (!bytes.empty()) {
            api.cef_string_utf8_to_utf16(bytes.c_str(), bytes.size(), &str);
        }
    }

    ~CefString() {
        api.cef_string_utf16_clear(&str);
    }

    CefString(const CefString &) = delete;
    CefString &operator=(const CefString &) = delete;

    const cef_string_t *ptr() const {
        return &str;
    }

    cef_string_t str;
    const cef_api_t &api;
};

void base_add_ref(cef_base_ref_counted_t *base) {
    (void)base;
}

int base_release(cef_base_ref_counted_t *base) {
    (void)base;
    return 0;
}

int base_has_one_ref(cef_base_ref_counted_t *base) {
    (void)base;
    return 1;
}

int base_has_at_least_one_ref(cef_base_ref_counted_t *base) {
    (void)base;
    return 1;
}

cef_base_ref_counted_t make_base(size_t size) {
    cef_base_ref_counted_t base;
    base.size = size;
    base.add_ref = base_add_ref;
    base.release = base_release;
    base.has_one_ref = base_has_one_ref;
    base.has_at_least_one_ref = base_has_at_least_one_ref;
    return base;
}

} // namespace

struct LinuxCefOsrWebview::CefObjects {
    struct RenderHandler {
        cef_render_handler_t handler;
        std::mutex mutex;
        std::vector<uint8_t> rgba;
        int width = 1;
        int height = 1;
        int pending_width = 0;
        int pending_height = 0;
        bool dirty = false;

        RenderHandler() {
            handler.base = make_base(sizeof(cef_render_handler_t));
            handler.get_view_rect = get_view_rect;
            handler.on_paint = on_paint;
        }

        void set_size(int new_width, int new_height) {
            std::lock_guard<std::mutex> lock(mutex);
            width = std::max(kMinimumDimension, new_width);
            height = std::max(kMinimumDimension, new_height);
        }

        bool take_frame(std::vector<uint8_t> &out, int &out_width, int &out_height) {
            std::lock_guard<std::mutex> lock(mutex);
            if (!dirty) {
                return false;
            }
            out = std::move(rgba);
            out_width = pending_width;
            out_height = pending_height;
            dirty = false;
            return true;
        }

        static RenderHandler *from(cef_render_handler_t *handler_ptr) {
            return reinterpret_cast<RenderHandler *>(handler_ptr);
        }

        static void get_view_rect(cef_render_handler_t *self,
                                  cef_browser_t *browser,
                                  cef_rect_t *rect) {
            (void)browser;
            if (rect == nullptr) {
                return;
            }
            RenderHandler *owner = from(self);
            std::lock_guard<std::mutex> lock(owner->mutex);
            rect->x = 0;
            rect->y = 0;
            rect->width = owner->width;
            rect->height = owner->height;
        }

        static void on_paint(cef_render_handler_t *self,
                             cef_browser_t *browser,
                             cef_paint_element_type_t type,
                             size_t dirty_rects_count,
                             const cef_rect_t *dirty_rects,
                             const void *buffer,
                             int width,
                             int height) {
            (void)browser;
            (void)dirty_rects_count;
            (void)dirty_rects;
            if (type != PET_VIEW || buffer == nullptr || width <= 0 || height <= 0) {
                return;
            }

            RenderHandler *owner = from(self);
            const auto *src = static_cast<const uint8_t *>(buffer);
            const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
            std::vector<uint8_t> converted(pixel_count * 4);
            for (size_t i = 0; i < pixel_count; i++) {
                converted[i * 4 + 0] = src[i * 4 + 2];
                converted[i * 4 + 1] = src[i * 4 + 1];
                converted[i * 4 + 2] = src[i * 4 + 0];
                converted[i * 4 + 3] = src[i * 4 + 3];
            }

            std::lock_guard<std::mutex> lock(owner->mutex);
            owner->rgba = std::move(converted);
            owner->pending_width = width;
            owner->pending_height = height;
            owner->dirty = true;
        }
    };

    struct Client {
        cef_client_t client;
        RenderHandler *render_handler = nullptr;

        explicit Client(RenderHandler *handler) :
                render_handler(handler) {
            client.base = make_base(sizeof(cef_client_t));
            client.get_render_handler = get_render_handler;
        }

        static Client *from(cef_client_t *client_ptr) {
            return reinterpret_cast<Client *>(client_ptr);
        }

        static cef_render_handler_t *get_render_handler(cef_client_t *self) {
            Client *owner = from(self);
            return owner->render_handler != nullptr ? &owner->render_handler->handler : nullptr;
        }
    };

    LoadResult runtime;
    RenderHandler render_handler;
    Client client{ &render_handler };
    cef_browser_t *browser = nullptr;
    int width = 1;
    int height = 1;
    bool initialized = false;
    bool hidden = false;
    input::MouseState mouse_state;

    const cef_api_t &api() const {
        return runtime.runtime->api();
    }

    cef_browser_host_t *host() const {
        if (browser == nullptr || browser->get_host == nullptr) {
            return nullptr;
        }
        return browser->get_host(browser);
    }

    void close_browser() {
        cef_browser_host_t *browser_host = host();
        if (browser_host == nullptr) {
            browser = nullptr;
            return;
        }

        if (browser_host->close_browser != nullptr) {
            browser_host->close_browser(browser_host, 1);
        }
        browser = nullptr;
    }

    void notify_resized() {
        cef_browser_host_t *browser_host = host();
        if (browser_host != nullptr && browser_host->was_resized != nullptr) {
            browser_host->was_resized(browser_host);
        }
    }
};

LinuxCefOsrWebview::LinuxCefOsrWebview() = default;

LinuxCefOsrWebview::~LinuxCefOsrWebview() {
    stop();
}

godot::Control *LinuxCefOsrWebview::create_control() {
    if (texture_rect == nullptr) {
        texture_rect = memnew(godot::TextureRect);
        texture_rect->set_expand_mode(godot::TextureRect::EXPAND_IGNORE_SIZE);
        texture_rect->set_stretch_mode(godot::TextureRect::STRETCH_SCALE);
        texture_rect->set_clip_contents(true);
    }
    return texture_rect;
}

bool LinuxCefOsrWebview::start(godot::Control *owner, const godot::String &url) {
    if (started) {
        return true;
    }
    if (owner == nullptr) {
        webview_backend::output_error("Web chat Linux CEF OSR cannot start without a Godot owner control");
        return false;
    }

    create_control();
    cef = std::make_unique<CefObjects>();
    cef->runtime = linux_cef_runtime::load();
    if (!cef->runtime.ok()) {
        webview_backend::output_error("Web chat Linux CEF loader unavailable: " + cef->runtime.status.message);
        cef.reset();
        return false;
    }

    const godot::String runtime_dir = cef->runtime.status.runtime_dir;
    const godot::String session_name = process_profile_name();
    const godot::String profile_dir =
        fennara::app_paths::webview_profile_dir().path_join("cef").path_join(session_name);
    const godot::String cache_dir = profile_dir.path_join("cache");
    const godot::String log_dir =
        fennara::app_paths::webview_log_dir().path_join("cef").path_join(session_name);
    ensure_dir(profile_dir);
    ensure_dir(cache_dir);
    ensure_dir(log_dir);

    const cef_api_t &api = cef->api();
    cef_settings_t settings;
    settings.no_sandbox = 1;
    settings.windowless_rendering_enabled = 1;
    settings.disable_signal_handlers = 1;
    settings.log_severity = LOGSEVERITY_WARNING;

    CefString subprocess_path(api, runtime_dir.path_join("fennara_cef_helper"));
    CefString resources_path(api, runtime_dir);
    CefString locales_path(api, runtime_dir.path_join("locales"));
    CefString root_cache_path(api, profile_dir);
    CefString cache_path(api, cache_dir);
    CefString log_file(api, log_dir.path_join("cef_debug.log"));
    settings.browser_subprocess_path = subprocess_path.str;
    settings.resources_dir_path = resources_path.str;
    settings.locales_dir_path = locales_path.str;
    settings.root_cache_path = root_cache_path.str;
    settings.cache_path = cache_path.str;
    settings.log_file = log_file.str;

    cef_main_args_t args;
    if (api.cef_initialize(&args, &settings, nullptr, nullptr) == 0) {
        webview_backend::output_error("Web chat Linux CEF initialization failed");
        cef.reset();
        return false;
    }
    cef->initialized = true;

    godot::Vector2 size = owner->get_size();
    cef->width = clamp_dimension(size.x);
    cef->height = clamp_dimension(size.y);
    cef->render_handler.set_size(cef->width, cef->height);

    cef_window_info_t window_info;
    window_info.bounds.width = cef->width;
    window_info.bounds.height = cef->height;
    window_info.windowless_rendering_enabled = 1;
    window_info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;

    cef_browser_settings_t browser_settings;
    browser_settings.windowless_frame_rate = 30;
    browser_settings.background_color = 0xffffffff;

    CefString cef_url(api, url);
    cef->browser = api.cef_browser_host_create_browser_sync(
        &window_info,
        &cef->client.client,
        cef_url.ptr(),
        &browser_settings,
        nullptr,
        nullptr);

    if (cef->browser == nullptr) {
        webview_backend::output_error("Web chat Linux CEF browser creation failed");
        stop();
        return false;
    }

    started = true;
    webview_backend::output_log("Web chat Linux CEF OSR browser started at " + runtime_dir);
    return true;
}

void LinuxCefOsrWebview::resize_to(godot::Control *owner) {
    if (!started || cef == nullptr || owner == nullptr) {
        return;
    }

    const godot::Vector2 size = owner->get_size();
    const int width = clamp_dimension(size.x);
    const int height = clamp_dimension(size.y);
    if (width == cef->width && height == cef->height) {
        return;
    }

    cef->width = width;
    cef->height = height;
    cef->render_handler.set_size(width, height);
    cef->notify_resized();
}

void LinuxCefOsrWebview::set_visible(bool visible) {
    if (texture_rect != nullptr) {
        texture_rect->set_visible(visible);
    }
    if (!started || cef == nullptr || cef->hidden == !visible) {
        return;
    }

    cef->hidden = !visible;
    if (!visible) {
        set_focused(false);
    }

    cef_browser_host_t *host = cef->host();
    if (host != nullptr && host->was_hidden != nullptr) {
        host->was_hidden(host, visible ? 0 : 1);
    }
}

void LinuxCefOsrWebview::process(double delta) {
    (void)delta;
    if (!started || cef == nullptr || !cef->runtime.ok()) {
        return;
    }

    cef->api().cef_do_message_loop_work();

    std::vector<uint8_t> frame;
    int width = 0;
    int height = 0;
    if (!cef->render_handler.take_frame(frame, width, height) || frame.empty()) {
        return;
    }

    godot::PackedByteArray bytes;
    bytes.resize(static_cast<int64_t>(frame.size()));
    std::memcpy(bytes.ptrw(), frame.data(), frame.size());

    godot::Ref<godot::Image> image =
        godot::Image::create_from_data(width, height, false, godot::Image::FORMAT_RGBA8, bytes);
    if (image.is_null()) {
        return;
    }

    if (texture.is_null()) {
        texture = godot::ImageTexture::create_from_image(image);
        if (texture_rect != nullptr) {
            texture_rect->set_texture(texture);
        }
    } else {
        texture->update(image);
    }
}

bool LinuxCefOsrWebview::handle_input(const godot::Ref<godot::InputEvent> &event) {
    if (!started || cef == nullptr || event.is_null()) {
        return false;
    }

    cef_browser_host_t *host = cef->host();
    if (host == nullptr) {
        return false;
    }

    bool request_focus = false;
    const bool handled = input::handle_input(event, host, texture_rect, cef->width, cef->height, cef->mouse_state, request_focus);
    if (request_focus) {
        set_focused(true);
    }
    return handled;
}

void LinuxCefOsrWebview::set_focused(bool next_focused) {
    if (!started || cef == nullptr || focused == next_focused) {
        return;
    }

    cef_browser_host_t *host = cef->host();
    if (host == nullptr) {
        return;
    }

    if (host->send_focus_event != nullptr) {
        host->send_focus_event(host, next_focused ? 1 : 0);
    } else if (host->set_focus != nullptr) {
        host->set_focus(host, next_focused ? 1 : 0);
    } else {
        return;
    }
    focused = next_focused;
}

void LinuxCefOsrWebview::notify_mouse_leave() {
    if (!started || cef == nullptr) {
        return;
    }

    input::notify_mouse_leave(cef->host(), cef->mouse_state);
}

void LinuxCefOsrWebview::stop() {
    if (cef != nullptr) {
        if (cef->initialized) {
            set_focused(false);
            cef->close_browser();
            cef->api().cef_do_message_loop_work();
            cef->api().cef_shutdown();
        }
        cef.reset();
    }
    started = false;
    focused = false;
    texture.unref();
    if (texture_rect != nullptr) {
        texture_rect->set_texture(godot::Ref<godot::Texture2D>());
    }
}

bool LinuxCefOsrWebview::is_started() const {
    return started;
}

} // namespace fennara::linux_cef_osr

#endif
