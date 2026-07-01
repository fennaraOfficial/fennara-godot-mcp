#include "fennara/ui/dock.hpp"

#include "fennara/app_paths.hpp"
#include "fennara/local_bridge.hpp"
#include "fennara/logger.hpp"
#include "fennara/ui/webview_host.hpp"

#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <chrono>

namespace fennara {

namespace {

constexpr double STARTUP_GEOMETRY_DELAY_SECONDS = 0.25;
constexpr int REQUIRED_STABLE_GEOMETRY_FRAMES = 2;

FennaraDock *active_dock = nullptr;

godot::Label *make_fallback_label(const godot::String &text) {
    godot::Label *label = memnew(godot::Label);
    label->set_text(text);
    label->set_autowrap_mode(godot::TextServer::AUTOWRAP_WORD_SMART);
    label->set_horizontal_alignment(godot::HorizontalAlignment::HORIZONTAL_ALIGNMENT_CENTER);
    label->set_vertical_alignment(godot::VerticalAlignment::VERTICAL_ALIGNMENT_CENTER);
    label->add_theme_color_override("font_color", godot::Color("#a8b0ba"));
    label->set_h_size_flags(godot::Control::SIZE_EXPAND_FILL);
    label->set_v_size_flags(godot::Control::SIZE_EXPAND_FILL);
    return label;
}

} // namespace

void FennaraDock::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("_on_mcp_target_state_changed", "active"),
        &FennaraDock::_on_mcp_target_state_changed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("_on_open_browser_pressed"),
        &FennaraDock::_on_open_browser_pressed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("_on_use_embedded_toggled", "pressed"),
        &FennaraDock::_on_use_embedded_toggled);
}

FennaraDock::FennaraDock() {
    set_v_size_flags(godot::Control::SIZE_EXPAND_FILL);
    set_h_size_flags(godot::Control::SIZE_EXPAND_FILL);
    set_custom_minimum_size(godot::Vector2(420, 520));
    set_clip_contents(true);
    set_focus_mode(godot::Control::FOCUS_ALL);
    webview_host = std::make_unique<WebviewHost>();
    active_dock = this;
}

FennaraDock::~FennaraDock() {
    _release_webview_keyboard_focus();
    if (webview_host) {
        webview_host->stop();
    }
    if (active_dock == this) {
        active_dock = nullptr;
    }
}

void FennaraDock::set_local_bridge(FennaraLocalBridge *bridge) {
    local_bridge = bridge;
    if (local_bridge &&
        !local_bridge->is_connected("mcp_target_state_changed",
                                    callable_mp(this, &FennaraDock::_on_mcp_target_state_changed))) {
        local_bridge->connect("mcp_target_state_changed",
                              callable_mp(this, &FennaraDock::_on_mcp_target_state_changed));
    }
    _refresh_status();
}

void FennaraDock::_ready() {
    _build_ui();
    startup_delay = STARTUP_GEOMETRY_DELAY_SECONDS;
    set_process(true);
    set_process_input(true);
}

void FennaraDock::_process(double delta) {
    if (startup_delay > 0.0) {
        startup_delay -= delta;
    } else {
        refresh_timer -= delta;
        if (refresh_timer <= 0.0) {
            refresh_timer = 0.25;
            _refresh_status();
        }
    }
    _sync_webview_bounds();
    if (webview_keyboard_focused && !has_focus()) {
        _release_webview_keyboard_focus();
    }
    if (webview_host) {
        webview_host->process(delta);
    }
}

void FennaraDock::_notification(int what) {
    if (!webview_host || !webview_host->is_started()) {
        return;
    }

    switch (what) {
        case NOTIFICATION_VISIBILITY_CHANGED:
        case NOTIFICATION_RESIZED:
        case NOTIFICATION_THEME_CHANGED:
            _sync_webview_bounds();
            break;
        case NOTIFICATION_FOCUS_EXIT:
            _release_webview_keyboard_focus();
            break;
        case NOTIFICATION_MOUSE_EXIT:
        case NOTIFICATION_MOUSE_EXIT_SELF:
            webview_host->notify_mouse_leave();
            break;
        default:
            break;
    }
}

void FennaraDock::_sync_webview_bounds() {
    if (!webview_host || !webview_host->is_started()) {
        return;
    }

    if (!is_visible_in_tree()) {
        _release_webview_keyboard_focus();
        webview_host->set_visible(false);
        return;
    }

    webview_host->resize_to(webview_region ? webview_region : this);
    if (webview_host->uses_internal_surface()) {
        webview_host->set_visible(true);
    }
}

void FennaraDock::_build_ui() {
    godot::PanelContainer *root = memnew(godot::PanelContainer);
    root->set_anchors_preset(godot::Control::PRESET_FULL_RECT);
    root->set_h_size_flags(godot::Control::SIZE_EXPAND_FILL);
    root->set_v_size_flags(godot::Control::SIZE_EXPAND_FILL);
    root->set_custom_minimum_size(godot::Vector2(420, 520));
    root->set_mouse_filter(godot::Control::MOUSE_FILTER_PASS);
    add_child(root);

    godot::MarginContainer *margin = memnew(godot::MarginContainer);
    margin->set_anchors_preset(godot::Control::PRESET_FULL_RECT);
    margin->set_h_size_flags(godot::Control::SIZE_EXPAND_FILL);
    margin->set_v_size_flags(godot::Control::SIZE_EXPAND_FILL);
    margin->set_custom_minimum_size(godot::Vector2(420, 520));
    margin->set_mouse_filter(godot::Control::MOUSE_FILTER_PASS);
    margin->add_theme_constant_override("margin_left", 0);
    margin->add_theme_constant_override("margin_top", 0);
    margin->add_theme_constant_override("margin_right", 0);
    margin->add_theme_constant_override("margin_bottom", 0);
    root->add_child(margin);
    webview_region = margin;

    fallback_label = make_fallback_label("Starting Fennara chat...");
    fallback_label->set_anchors_preset(godot::Control::PRESET_FULL_RECT);
    margin->add_child(fallback_label);

    browser_fallback_panel = memnew(godot::VBoxContainer);
    browser_fallback_panel->set_anchors_preset(godot::Control::PRESET_FULL_RECT);
    browser_fallback_panel->set_h_size_flags(godot::Control::SIZE_EXPAND_FILL);
    browser_fallback_panel->set_v_size_flags(godot::Control::SIZE_EXPAND_FILL);
    browser_fallback_panel->set_mouse_filter(godot::Control::MOUSE_FILTER_PASS);
    godot::VBoxContainer *browser_vbox = godot::Object::cast_to<godot::VBoxContainer>(browser_fallback_panel);
    if (browser_vbox != nullptr) {
        browser_vbox->set_alignment(godot::BoxContainer::ALIGNMENT_CENTER);
        browser_vbox->add_theme_constant_override("separation", 8);
    }

    browser_fallback_message = make_fallback_label(
        "Fennara chat is using your system browser to reduce Godot GPU and memory usage.");
    browser_fallback_message->set_v_size_flags(godot::Control::SIZE_SHRINK_CENTER);
    browser_fallback_message->set_custom_minimum_size(godot::Vector2(320, 70));
    browser_fallback_panel->add_child(browser_fallback_message);

    godot::HBoxContainer *browser_buttons = memnew(godot::HBoxContainer);
    browser_buttons->set_h_size_flags(godot::Control::SIZE_SHRINK_CENTER);
    browser_buttons->add_theme_constant_override("separation", 8);
    browser_fallback_panel->add_child(browser_buttons);

    open_browser_button = memnew(godot::Button);
    open_browser_button->set_text("Open chat");
    open_browser_button->set_tooltip_text("Open Fennara chat in your system browser");
    open_browser_button->connect("pressed", callable_mp(this, &FennaraDock::_on_open_browser_pressed));
    browser_buttons->add_child(open_browser_button);

    use_embedded_checkbox = memnew(godot::CheckBox);
    use_embedded_checkbox->set_text("Open embedded chat in Godot next time");
    use_embedded_checkbox->set_h_size_flags(godot::Control::SIZE_SHRINK_CENTER);
    use_embedded_checkbox->connect("toggled", callable_mp(this, &FennaraDock::_on_use_embedded_toggled));
    browser_fallback_panel->add_child(use_embedded_checkbox);

    browser_restart_label = make_fallback_label("Restart Godot for this change to take effect.");
    browser_restart_label->set_v_size_flags(godot::Control::SIZE_SHRINK_CENTER);
    browser_restart_label->set_custom_minimum_size(godot::Vector2(320, 36));
    browser_restart_label->set_visible(false);
    browser_fallback_panel->add_child(browser_restart_label);

    browser_fallback_panel->set_visible(false);
    margin->add_child(browser_fallback_panel);

    if (webview_host && webview_host->uses_internal_surface()) {
        internal_webview_surface = webview_host->create_internal_control();
        if (internal_webview_surface != nullptr) {
            internal_webview_surface->set_anchors_preset(godot::Control::PRESET_FULL_RECT);
            internal_webview_surface->set_h_size_flags(godot::Control::SIZE_EXPAND_FILL);
            internal_webview_surface->set_v_size_flags(godot::Control::SIZE_EXPAND_FILL);
            internal_webview_surface->set_mouse_filter(godot::Control::MOUSE_FILTER_PASS);
            margin->add_child(internal_webview_surface);
            margin->move_child(fallback_label, margin->get_child_count() - 1);
            margin->move_child(browser_fallback_panel, margin->get_child_count() - 1);
        }
    }
}

void FennaraDock::_try_start_webview() {
    if (attempted_webview || (webview_host && webview_host->is_started())) {
        return;
    }

    godot::OS *os = godot::OS::get_singleton();
    bool native_webview_disabled =
        os != nullptr && os->get_environment("FENNARA_DISABLE_NATIVE_WEBVIEW") == "1";

    if (_chat_surface_prefers_browser() || native_webview_disabled) {
        godot::String url = _browser_chat_url();
        if (url.is_empty()) {
            return;
        }
        attempted_webview = true;
        browser_chat_url = url;
        _show_browser_fallback(
            native_webview_disabled
                ? "Fennara native webview is disabled. Use the system browser for this session."
                : "Fennara chat is using your system browser to reduce Godot GPU and memory usage.");
        if (native_webview_disabled) {
            _output_log("Web chat native host disabled by FENNARA_DISABLE_NATIVE_WEBVIEW=1");
        }
        return;
    }

    godot::Control *owner = webview_region ? webview_region : this;
    godot::Vector2 owner_size = owner->get_size();
    if (owner_size.x < 240 || owner_size.y < 240) {
        return;
    }
    if (!_webview_region_is_stable()) {
        return;
    }

    attempted_webview = true;

    if (webview_host == nullptr) {
        if (fallback_label) {
            fallback_label->set_text("Fennara chat could not start: native host is unavailable.");
        }
        _output_log("Web chat native host is unavailable");
        return;
    }

    godot::String url = _chat_url();
    if (!webview_host->start(owner, url)) {
        if (internal_webview_surface) {
            internal_webview_surface->set_visible(false);
        }
        godot::String browser_url = _browser_chat_url();
        if (!browser_url.is_empty()) {
            browser_chat_url = browser_url;
            _show_browser_fallback("Fennara native chat could not start. Use the system browser for this session.");
        } else if (fallback_label) {
            fallback_label->set_text(
                "Fennara chat could not start. Check the Godot Output panel for details.");
        }
        return;
    }

    if (internal_webview_surface) {
        internal_webview_surface->set_visible(true);
    }
    if (fallback_label) {
        fallback_label->set_visible(false);
    }
    if (browser_fallback_panel) {
        browser_fallback_panel->set_visible(false);
    }
}

void FennaraDock::_refresh_status() {
    if (fallback_label == nullptr) {
        return;
    }
    _try_start_webview();
}

void FennaraDock::_set_webview_keyboard_focus(bool focused) {
    if (webview_keyboard_focused == focused && focused) {
        return;
    }
    webview_keyboard_focused = focused;
    if (webview_host && webview_host->is_started()) {
        webview_host->set_focused(focused);
    }
}

void FennaraDock::_release_webview_keyboard_focus() {
    webview_keyboard_focused = false;
    if (webview_host && webview_host->is_started()) {
        webview_host->set_focused(false);
    }
}

bool FennaraDock::_event_is_inside_webview_region(const godot::Ref<godot::InputEvent> &event) {
    auto *button = godot::Object::cast_to<godot::InputEventMouseButton>(event.ptr());
    if (button == nullptr || !button->is_pressed()) {
        return false;
    }
    godot::Control *owner = webview_region ? webview_region : this;
    godot::Rect2 rect(owner->get_global_position(), owner->get_size());
    return rect.has_point(button->get_global_position());
}

void FennaraDock::release_active_webview_keyboard_focus() {
    if (active_dock != nullptr) {
        active_dock->_release_webview_keyboard_focus();
    }
}

void FennaraDock::_on_mcp_target_state_changed(bool active) {
    (void)active;
    _refresh_status();
}

void FennaraDock::_on_open_browser_pressed() {
    if (browser_chat_url.is_empty()) {
        return;
    }

    godot::OS *os = godot::OS::get_singleton();
    if (os != nullptr) {
        os->shell_open(browser_chat_url);
    }
    if (browser_fallback_message != nullptr) {
        browser_fallback_message->set_text("Opened chat in your system browser.");
    }
}

void FennaraDock::_on_use_embedded_toggled(bool pressed) {
    const godot::String next_surface = pressed ? "embedded" : "browser";
    if (!_save_chat_surface(next_surface)) {
        if (use_embedded_checkbox != nullptr) {
            use_embedded_checkbox->set_pressed_no_signal(!pressed);
        }
        if (browser_fallback_message != nullptr) {
            browser_fallback_message->set_text("Could not save chat display setting.");
        }
        return;
    }

    if (browser_fallback_message != nullptr) {
        browser_fallback_message->set_text(
            pressed
                ? "Embedded chat will open in Godot next time."
                : "Fennara chat will keep using your system browser.");
    }
    if (browser_restart_label != nullptr) {
        browser_restart_label->set_visible(pressed);
    }
}

godot::String FennaraDock::_chat_url() const {
    godot::ProjectSettings *settings = godot::ProjectSettings::get_singleton();
    godot::String path = "res://addons/fennara/dist/index.html";
    if (settings != nullptr) {
        path = settings->globalize_path(path);
    }
    path = path.replace("\\", "/");
    godot::String url = path.begins_with("/") ? "file://" + path : "file:///" + path;
    const auto cache_bust = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
    url += "?v=" + godot::String::num_int64(cache_bust);
    if (local_bridge != nullptr && !local_bridge->get_chat_token().is_empty()) {
        url += "&chat_token=" + local_bridge->get_chat_token();
    }
    return url;
}

godot::String FennaraDock::_browser_chat_url() const {
    if (local_bridge == nullptr || local_bridge->get_chat_token().is_empty()) {
        return "";
    }
    return "http://127.0.0.1:41287/chat/?chat_token=" + local_bridge->get_chat_token();
}

bool FennaraDock::_chat_surface_prefers_browser() const {
    godot::Dictionary settings = app_paths::read_json_first_existing(
        app_paths::chat_settings_read_paths());
    const godot::String surface = godot::String(settings.get("chat_surface", "embedded")).to_lower();
    return surface == "browser";
}

bool FennaraDock::_save_chat_surface(const godot::String &surface) const {
    godot::Dictionary settings = app_paths::read_json_first_existing(
        app_paths::chat_settings_read_paths());
    settings["chat_surface"] = surface;
    return app_paths::write_json(app_paths::chat_settings_path(), settings);
}

void FennaraDock::_show_browser_fallback(const godot::String &message) {
    _release_webview_keyboard_focus();
    if (internal_webview_surface != nullptr) {
        internal_webview_surface->set_visible(false);
    }
    if (fallback_label != nullptr) {
        fallback_label->set_visible(false);
    }
    if (browser_fallback_panel != nullptr) {
        browser_fallback_panel->set_visible(true);
    }
    if (open_browser_button != nullptr) {
        open_browser_button->set_disabled(browser_chat_url.is_empty());
        open_browser_button->set_tooltip_text("Open Fennara chat in your system browser");
    }
    if (use_embedded_checkbox != nullptr) {
        use_embedded_checkbox->set_disabled(false);
        use_embedded_checkbox->set_pressed_no_signal(false);
    }
    if (browser_restart_label != nullptr) {
        browser_restart_label->set_visible(false);
    }
    if (browser_fallback_message != nullptr) {
        browser_fallback_message->set_text(message);
    }
}

bool FennaraDock::_webview_region_is_stable() {
    godot::Control *owner = webview_region ? webview_region : this;
    godot::Vector2 position = owner->get_global_position();
    godot::Vector2 size = owner->get_size();
    bool changed =
        position.distance_to(last_region_position) > 1.0 ||
        size.distance_to(last_region_size) > 1.0;

    if (changed) {
        last_region_position = position;
        last_region_size = size;
        stable_geometry_frames = 0;
        return false;
    }

    stable_geometry_frames++;
    return stable_geometry_frames >= REQUIRED_STABLE_GEOMETRY_FRAMES;
}

void FennaraDock::_output_log(const godot::String &message) const {
    FLOG_UI(message);
    godot::UtilityFunctions::print(godot::String("[Fennara] ") + message);
}

void FennaraDock::_input(const godot::Ref<godot::InputEvent> &event) {
    if (!webview_host || !webview_host->is_started()) {
        return;
    }
    auto *button = godot::Object::cast_to<godot::InputEventMouseButton>(event.ptr());
    if (button == nullptr || !button->is_pressed()) {
        return;
    }
    if (!_event_is_inside_webview_region(event)) {
        _release_webview_keyboard_focus();
    }
}

void FennaraDock::_gui_input(const godot::Ref<godot::InputEvent> &event) {
    if (webview_host && webview_host->uses_internal_surface() && webview_host->is_started()) {
        if (auto *button = godot::Object::cast_to<godot::InputEventMouseButton>(event.ptr())) {
            if (button->is_pressed()) {
                grab_focus();
                _set_webview_keyboard_focus(true);
            }
        }
        if (godot::Object::cast_to<godot::InputEventKey>(event.ptr()) != nullptr && !webview_keyboard_focused) {
            return;
        }
        if (webview_host->handle_input(event)) {
            accept_event();
        }
    }
}

} // namespace fennara
