#ifdef __linux__

#include "linux_cef_osr.hpp"

#include "linux_cef_bridge_loader.hpp"
#include "linux_cef_bridge_api.hpp"
#include "linux_cef_input.hpp"
#include "webview_backend.hpp"

#include "fennara/app_paths.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>
namespace fennara::linux_cef_osr {
namespace {

using namespace fennara::linux_cef_runtime;

constexpr int kMinimumDimension = 1;

std::string utf8(const godot::String &value) {
    return value.utf8().get_data();
}

bool debug_logging_enabled() {
    const char *generic = std::getenv("FENNARA_WEBVIEW_DEBUG");
    const char *linux_cef = std::getenv("FENNARA_LINUX_CEF_DEBUG");
    return (generic != nullptr && std::string(generic) == "1") ||
           (linux_cef != nullptr && std::string(linux_cef) == "1");
}

void debug_log(const godot::String &message) {
    if (debug_logging_enabled()) {
        webview_backend::output_log("Web chat Linux CEF OSR: " + message);
    }
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
    static std::atomic<uint32_t> counter{0};
    godot::OS *os = godot::OS::get_singleton();
    const int32_t pid = os != nullptr ? os->get_process_id() : 0;
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    const uint32_t nonce = counter.fetch_add(1, std::memory_order_relaxed);
    return "godot-" + godot::String::num_int64(pid) +
           "-" + godot::String::num_int64(millis) +
           "-" + godot::String::num_int64(nonce);
}

void bridge_log(const char *message, void *user_data) {
    (void)user_data;
    if (message != nullptr) {
        debug_log(message);
    }
}

} // namespace

struct LinuxCefOsrWebview::CefObjects {
    LoadResult runtime;
    linux_cef_bridge_loader::BridgeLibrary bridge;
    fennara_cef_bridge_browser *browser = nullptr;
    std::mutex frame_mutex;
    std::vector<uint8_t> rgba;
    int pending_width = 0;
    int pending_height = 0;
    int width = 1;
    int height = 1;
    bool initialized = false;
    bool hidden = false;
    bool dirty = false;
    input::MouseState mouse_state;
    input::KeyboardState keyboard_state;

    const fennara_cef_bridge_api *api() const {
        return bridge.api();
    }

    void close_browser() {
        if (browser != nullptr && api() != nullptr && api()->close_browser != nullptr) {
            api()->close_browser(browser);
        }
        browser = nullptr;
    }

    void notify_resized() {
        if (browser != nullptr && api() != nullptr && api()->resize_browser != nullptr) {
            api()->resize_browser(browser, width, height);
        }
    }

    bool take_frame(std::vector<uint8_t> &out, int &out_width, int &out_height) {
        std::lock_guard<std::mutex> lock(frame_mutex);
        if (!dirty) {
            return false;
        }
        out = std::move(rgba);
        out_width = pending_width;
        out_height = pending_height;
        dirty = false;
        return true;
    }

    void set_frame(const uint8_t *bytes, int frame_width, int frame_height) {
        if (bytes == nullptr || frame_width <= 0 || frame_height <= 0) {
            return;
        }

        const size_t byte_count = static_cast<size_t>(frame_width) * static_cast<size_t>(frame_height) * 4;
        std::vector<uint8_t> next(byte_count);
        std::memcpy(next.data(), bytes, byte_count);

        std::lock_guard<std::mutex> lock(frame_mutex);
        rgba = std::move(next);
        pending_width = frame_width;
        pending_height = frame_height;
        dirty = true;
    }

    static void paint_callback(const uint8_t *bytes, int frame_width, int frame_height, void *user_data) {
        auto *owner = static_cast<CefObjects *>(user_data);
        if (owner != nullptr) {
            owner->set_frame(bytes, frame_width, frame_height);
        }
    }
};

LinuxCefOsrWebview::LinuxCefOsrWebview() = default;

LinuxCefOsrWebview::~LinuxCefOsrWebview() {
    stop();
}

godot::Control *LinuxCefOsrWebview::create_control() {
    godot::TextureRect *texture_rect = current_texture_rect();
    if (texture_rect == nullptr) {
        texture_rect = memnew(godot::TextureRect);
        texture_rect_id = texture_rect->get_instance_id();
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
    debug_log("start requested owner_size=" +
              godot::String::num(owner->get_size().x) + "x" +
              godot::String::num(owner->get_size().y));
    cef = std::make_unique<CefObjects>();
    debug_log("loading shared runtime");
    cef->runtime = linux_cef_runtime::load();
    if (!cef->runtime.ok()) {
        webview_backend::output_error("Web chat Linux CEF loader unavailable: " + cef->runtime.status.message);
        cef.reset();
        return false;
    }

    const godot::String runtime_dir = cef->runtime.status.runtime_dir;
    debug_log("runtime ready version=" + cef->runtime.status.version +
              " dir=" + runtime_dir +
              " lib=" + cef->runtime.status.libcef_path);
    godot::String bridge_error;
    debug_log("loading Linux CEF bridge");
    if (!cef->bridge.load(bridge_error)) {
        webview_backend::output_error("Web chat Linux CEF bridge unavailable: " + bridge_error);
        cef.reset();
        return false;
    }
    debug_log("bridge ready path=" + cef->bridge.path());

    const godot::String session_name = process_profile_name();
    const godot::String profile_dir =
        fennara::app_paths::webview_profile_dir().path_join("cef").path_join(session_name);
    const godot::String cache_dir = profile_dir.path_join("cache");
    const godot::String log_dir =
        fennara::app_paths::webview_log_dir().path_join("cef").path_join(session_name);
    ensure_dir(profile_dir);
    ensure_dir(cache_dir);
    ensure_dir(log_dir);
    debug_log("profile=" + profile_dir);
    debug_log("cache=" + cache_dir);
    debug_log("log=" + log_dir);

    const fennara_cef_bridge_api *api = cef->api();
    if (api == nullptr) {
        webview_backend::output_error("Web chat Linux CEF bridge API is unavailable");
        cef.reset();
        return false;
    }

    const std::string subprocess_path = utf8(runtime_dir.path_join("fennara_cef_helper"));
    const std::string resources_path = utf8(runtime_dir);
    const std::string locales_path = utf8(runtime_dir.path_join("locales"));
    const std::string root_cache_path = utf8(profile_dir);
    const std::string cache_path = utf8(cache_dir);
    const std::string log_file = utf8(log_dir.path_join("cef_debug.log"));
    debug_log("settings prepared subprocess=" + runtime_dir.path_join("fennara_cef_helper") +
              " resources=" + runtime_dir +
              " locales=" + runtime_dir.path_join("locales"));

    std::string executable_path = utf8(godot::OS::get_singleton()->get_executable_path());
    char *argv[] = { executable_path.data(), nullptr };
    fennara_cef_bridge_callbacks callbacks{};
    callbacks.log = bridge_log;
    callbacks.paint = CefObjects::paint_callback;
    callbacks.user_data = cef.get();
    fennara_cef_bridge_settings settings{};
    settings.argc = 1;
    settings.argv = argv;
    settings.subprocess_path = subprocess_path.c_str();
    settings.resources_path = resources_path.c_str();
    settings.locales_path = locales_path.c_str();
    settings.root_cache_path = root_cache_path.c_str();
    settings.cache_path = cache_path.c_str();
    settings.log_file = log_file.c_str();
    const int process_exit_code = api->execute_process(&settings, &callbacks);
    if (process_exit_code >= 0) {
        webview_backend::output_error(
            "Web chat Linux CEF execute_process unexpectedly handled the Godot process");
        cef.reset();
        return false;
    }
    if (api->initialize(&settings, &callbacks) == 0) {
        webview_backend::output_error("Web chat Linux CEF initialization failed");
        cef.reset();
        return false;
    }
    cef->initialized = true;

    godot::Vector2 size = owner->get_size();
    cef->width = clamp_dimension(size.x);
    cef->height = clamp_dimension(size.y);
    debug_log("initial browser size " +
              godot::String::num_int64(cef->width) + "x" +
              godot::String::num_int64(cef->height));

    debug_log("creating CEF bridge browser url=" + url);
    const std::string browser_url = utf8(url);
    fennara_cef_bridge_browser_config browser_config{};
    browser_config.url = browser_url.c_str();
    browser_config.width = cef->width;
    browser_config.height = cef->height;
    cef->browser = api->create_browser(&browser_config, &callbacks);
    debug_log("CEF bridge browser create returned " +
              godot::String(cef->browser == nullptr ? "null" : "browser"));

    if (cef->browser == nullptr) {
        webview_backend::output_error("Web chat Linux CEF browser creation failed");
        stop();
        return false;
    }

    started = true;
    debug_log("browser started at " + runtime_dir);
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
    cef->notify_resized();
}

void LinuxCefOsrWebview::set_visible(bool visible) {
    godot::TextureRect *texture_rect = current_texture_rect();
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

    const fennara_cef_bridge_api *api = cef->api();
    if (api != nullptr && api->set_browser_hidden != nullptr && cef->browser != nullptr) {
        api->set_browser_hidden(cef->browser, visible ? 0 : 1);
    }
}

void LinuxCefOsrWebview::process(double delta) {
    (void)delta;
    if (!started || cef == nullptr || !cef->runtime.ok()) {
        return;
    }

    const fennara_cef_bridge_api *api = cef->api();
    if (api == nullptr || api->do_message_loop_work == nullptr) {
        return;
    }
    api->do_message_loop_work();

    std::vector<uint8_t> frame;
    int width = 0;
    int height = 0;
    if (!cef->take_frame(frame, width, height) || frame.empty()) {
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

    if (texture.is_null() || width != texture_width || height != texture_height) {
        texture = godot::ImageTexture::create_from_image(image);
        texture_width = width;
        texture_height = height;
        godot::TextureRect *texture_rect = current_texture_rect();
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

    const fennara_cef_bridge_api *api = cef->api();
    if (api == nullptr || cef->browser == nullptr) {
        return false;
    }

    bool request_focus = false;
    godot::TextureRect *texture_rect = current_texture_rect();
    const bool handled = input::handle_input(event,
                                            api,
                                            cef->browser,
                                            texture_rect,
                                            cef->width,
                                            cef->height,
                                            cef->mouse_state,
                                            cef->keyboard_state,
                                            request_focus);
    if (request_focus) {
        set_focused(true);
    }
    return handled;
}

void LinuxCefOsrWebview::set_focused(bool next_focused) {
    if (!started || cef == nullptr || focused == next_focused) {
        return;
    }

    const fennara_cef_bridge_api *api = cef->api();
    if (api == nullptr || api->set_browser_focus == nullptr || cef->browser == nullptr) {
        return;
    }

    api->set_browser_focus(cef->browser, next_focused ? 1 : 0);
    focused = next_focused;
}

void LinuxCefOsrWebview::notify_mouse_leave() {
    if (!started || cef == nullptr) {
        return;
    }

    input::notify_mouse_leave(cef->api(), cef->browser, cef->mouse_state);
}

void LinuxCefOsrWebview::stop() {
    if (cef != nullptr) {
        if (cef->initialized) {
            set_focused(false);
            cef->close_browser();
            const fennara_cef_bridge_api *api = cef->api();
            if (api != nullptr) {
                if (api->do_message_loop_work != nullptr) {
                    api->do_message_loop_work();
                }
                if (api->shutdown != nullptr) {
                    api->shutdown();
                }
            }
        }
        cef.reset();
    }
    started = false;
    focused = false;
    texture.unref();
    texture_width = 0;
    texture_height = 0;
    godot::TextureRect *texture_rect = current_texture_rect();
    if (texture_rect != nullptr) {
        texture_rect->set_texture(godot::Ref<godot::Texture2D>());
    }
}

bool LinuxCefOsrWebview::is_started() const {
    return started;
}

godot::TextureRect *LinuxCefOsrWebview::current_texture_rect() const {
    if (texture_rect_id == 0) {
        return nullptr;
    }
    return godot::Object::cast_to<godot::TextureRect>(
        godot::ObjectDB::get_instance(texture_rect_id));
}

} // namespace fennara::linux_cef_osr

#endif
