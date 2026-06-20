#include "fennara/ui/dock.hpp"

#include "fennara/local_bridge.hpp"
#include "fennara/logger.hpp"
#include "fennara/ui/webview_host.hpp"

#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <chrono>

namespace fennara {

namespace {

constexpr double STARTUP_GEOMETRY_DELAY_SECONDS = 0.25;
constexpr int REQUIRED_STABLE_GEOMETRY_FRAMES = 2;

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
}

FennaraDock::FennaraDock() {
    set_v_size_flags(godot::Control::SIZE_EXPAND_FILL);
    set_h_size_flags(godot::Control::SIZE_EXPAND_FILL);
    set_custom_minimum_size(godot::Vector2(420, 520));
    set_clip_contents(true);
    webview_host = std::make_unique<WebviewHost>();
}

FennaraDock::~FennaraDock() {
    if (webview_host) {
        webview_host->stop();
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
    _output_log("Web chat dock ready");
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
    if (webview_host && webview_host->is_started() && webview_region) {
        webview_host->resize_to(webview_region);
    }
}

void FennaraDock::_build_ui() {
    godot::PanelContainer *root = memnew(godot::PanelContainer);
    root->set_anchors_preset(godot::Control::PRESET_FULL_RECT);
    root->set_h_size_flags(godot::Control::SIZE_EXPAND_FILL);
    root->set_v_size_flags(godot::Control::SIZE_EXPAND_FILL);
    root->set_custom_minimum_size(godot::Vector2(420, 520));
    add_child(root);

    godot::MarginContainer *margin = memnew(godot::MarginContainer);
    margin->set_anchors_preset(godot::Control::PRESET_FULL_RECT);
    margin->set_h_size_flags(godot::Control::SIZE_EXPAND_FILL);
    margin->set_v_size_flags(godot::Control::SIZE_EXPAND_FILL);
    margin->set_custom_minimum_size(godot::Vector2(420, 520));
    margin->add_theme_constant_override("margin_left", 0);
    margin->add_theme_constant_override("margin_top", 0);
    margin->add_theme_constant_override("margin_right", 0);
    margin->add_theme_constant_override("margin_bottom", 0);
    root->add_child(margin);
    webview_region = margin;

    fallback_label = make_fallback_label("Starting Fennara chat...");
    fallback_label->set_anchors_preset(godot::Control::PRESET_FULL_RECT);
    margin->add_child(fallback_label);
}

void FennaraDock::_try_start_webview() {
    if (attempted_webview || (webview_host && webview_host->is_started())) {
        return;
    }

    godot::OS *os = godot::OS::get_singleton();
    bool native_webview_disabled =
        os != nullptr && os->get_environment("FENNARA_DISABLE_NATIVE_WEBVIEW") == "1";
    if (native_webview_disabled) {
        attempted_webview = true;
        if (fallback_label) {
            fallback_label->set_text(
                "Fennara chat UI is packaged, but the native webview host is disabled by environment.");
        }
        _output_log("Web chat native host disabled by FENNARA_DISABLE_NATIVE_WEBVIEW=1");
        return;
    }

    godot::Control *owner = webview_region ? webview_region : this;
    godot::Vector2 owner_size = owner->get_size();
    if (owner_size.x < 240 || owner_size.y < 240) {
        if (!logged_waiting_for_size) {
            logged_waiting_for_size = true;
            _output_log("Web chat waiting for dock size, current=" +
                        godot::String::num(owner_size.x) + "x" +
                        godot::String::num(owner_size.y));
        }
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
    _output_log("Web chat starting url=" + url);
    if (!webview_host->start(owner, url)) {
        if (fallback_label) {
            fallback_label->set_text("Fennara chat could not start. Check the Godot Output panel for details.");
        }
        return;
    }

    if (fallback_label) {
        fallback_label->set_visible(false);
    }
    _output_log("Web chat started");
}

void FennaraDock::_refresh_status() {
    if (fallback_label == nullptr) {
        return;
    }
    _try_start_webview();
}

void FennaraDock::_on_mcp_target_state_changed(bool active) {
    (void)active;
    _refresh_status();
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

bool FennaraDock::_webview_region_is_stable() {
    godot::Control *owner = webview_region ? webview_region : this;
    godot::Vector2 position = owner->get_screen_position();
    godot::Vector2 size = owner->get_size();
    bool changed =
        position.distance_to(last_region_position) > 1.0 ||
        size.distance_to(last_region_size) > 1.0;

    if (changed) {
        last_region_position = position;
        last_region_size = size;
        stable_geometry_frames = 0;
        _output_log("Web chat waiting for stable dock geometry x=" +
                    godot::String::num(position.x) +
                    " y=" + godot::String::num(position.y) +
                    " w=" + godot::String::num(size.x) +
                    " h=" + godot::String::num(size.y));
        return false;
    }

    stable_geometry_frames++;
    return stable_geometry_frames >= REQUIRED_STABLE_GEOMETRY_FRAMES;
}

void FennaraDock::_output_log(const godot::String &message) const {
    FLOG_UI(message);
    godot::UtilityFunctions::print(godot::String("[Fennara] ") + message);
}

} // namespace fennara
