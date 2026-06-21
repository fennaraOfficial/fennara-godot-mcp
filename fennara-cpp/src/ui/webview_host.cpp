#include "fennara/ui/webview_host.hpp"

#include "webview_backend.hpp"

#include "fennara/logger.hpp"

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace fennara {

namespace {

bool editor_is_headless() {
    godot::DisplayServer *display = godot::DisplayServer::get_singleton();
    godot::OS *os = godot::OS::get_singleton();
    return (os != nullptr && os->has_feature("headless")) ||
           (display != nullptr && display->get_name().to_lower() == "headless");
}

} // namespace

namespace webview_backend {

void output_log(const godot::String &message) {
    FLOG_UI(message);
    godot::UtilityFunctions::print(godot::String("[Fennara] ") + message);
}

void output_error(const godot::String &message) {
    FLOG_ERR(message);
    godot::UtilityFunctions::push_error(godot::String("[Fennara] ") + message);
}

} // namespace webview_backend

WebviewHost::WebviewHost() :
        backend(webview_backend::create_backend()) {
}

WebviewHost::~WebviewHost() {
    stop();
}

bool WebviewHost::start(godot::Control *owner, const godot::String &url) {
    if (is_started()) {
        webview_backend::output_log("Web chat host already started");
        return true;
    }

    if (editor_is_headless()) {
        webview_backend::output_log("Web chat host skipped: headless editor has no display surface");
        return false;
    }

    if (backend == nullptr) {
        webview_backend::output_error("Web chat native webview is not wired for this platform build yet");
        return false;
    }
    return backend->start(owner, url);
}

bool WebviewHost::uses_internal_surface() const {
    return backend != nullptr &&
           backend->surface_mode() == webview_backend::WebviewSurfaceMode::InternalGodotSurface;
}

godot::Control *WebviewHost::create_internal_control() {
    if (!uses_internal_surface()) {
        return nullptr;
    }
    if (internal_control == nullptr) {
        internal_control = backend->create_internal_control();
        if (internal_control != nullptr) {
            internal_control->set_visible(false);
        }
    }
    return internal_control;
}

void WebviewHost::resize_to(godot::Control *owner) {
    if (backend != nullptr) {
        backend->resize_to(owner);
    }
}

void WebviewHost::set_visible(bool visible) {
    if (backend != nullptr) {
        backend->set_visible(visible);
    }
    if (internal_control != nullptr) {
        internal_control->set_visible(visible);
    }
}

void WebviewHost::process(double delta) {
    if (backend != nullptr) {
        backend->process(delta);
    }
}

bool WebviewHost::handle_input(const godot::Ref<godot::InputEvent> &event) {
    if (backend != nullptr) {
        return backend->handle_input(event);
    }
    return false;
}

void WebviewHost::set_focused(bool focused) {
    if (backend != nullptr) {
        backend->set_focused(focused);
    }
}

void WebviewHost::notify_mouse_leave() {
    if (backend != nullptr) {
        backend->notify_mouse_leave();
    }
}

void WebviewHost::stop() {
    if (backend != nullptr) {
        backend->stop();
    }
    if (internal_control != nullptr) {
        internal_control->set_visible(false);
    }
}

bool WebviewHost::is_started() const {
    return backend != nullptr && backend->is_started();
}

} // namespace fennara
