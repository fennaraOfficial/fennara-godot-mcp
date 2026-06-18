#include "fennara/ui/dock.hpp"

#include "fennara/local_bridge.hpp"
#include "fennara/logger.hpp"

#include <godot_cpp/classes/project_settings.hpp>

namespace fennara {

namespace {

godot::Label *make_label(const godot::String &text) {
    godot::Label *label = memnew(godot::Label);
    label->set_text(text);
    label->set_autowrap_mode(godot::TextServer::AUTOWRAP_WORD_SMART);
    return label;
}

godot::Button *make_button(const godot::String &text) {
    godot::Button *button = memnew(godot::Button);
    button->set_text(text);
    button->set_h_size_flags(godot::Control::SIZE_EXPAND_FILL);
    return button;
}

} // namespace

void FennaraDock::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("_on_set_target_pressed"),
        &FennaraDock::_on_set_target_pressed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("_on_refresh_pressed"),
        &FennaraDock::_on_refresh_pressed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("_on_mcp_target_state_changed", "active"),
        &FennaraDock::_on_mcp_target_state_changed);
}

FennaraDock::FennaraDock() {
    set_v_size_flags(godot::Control::SIZE_EXPAND_FILL);
    set_h_size_flags(godot::Control::SIZE_EXPAND_FILL);
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
    _refresh_status();
    set_process(true);
    FLOG_CTX("UI", "Local MCP dock ready", godot::Dictionary());
}

void FennaraDock::_process(double delta) {
    refresh_timer -= delta;
    if (refresh_timer <= 0.0) {
        refresh_timer = 1.0;
        _refresh_status();
    }
}

void FennaraDock::_build_ui() {
    godot::VBoxContainer *layout = memnew(godot::VBoxContainer);
    layout->set_anchors_preset(godot::Control::PRESET_FULL_RECT);
    layout->set_offset(godot::Side::SIDE_LEFT, 12);
    layout->set_offset(godot::Side::SIDE_TOP, 12);
    layout->set_offset(godot::Side::SIDE_RIGHT, -12);
    layout->set_offset(godot::Side::SIDE_BOTTOM, -12);
    layout->add_theme_constant_override("separation", 10);
    add_child(layout);

    godot::Label *title = make_label("Fennara MCP");
    title->add_theme_font_size_override("font_size", 22);
    layout->add_child(title);

    daemon_status_label = make_label("");
    target_status_label = make_label("");
    project_label = make_label("");
    version_label = make_label("");

    layout->add_child(daemon_status_label);
    layout->add_child(target_status_label);
    layout->add_child(project_label);
    layout->add_child(version_label);

    set_target_button = make_button("Set This Project as MCP Target");
    refresh_button = make_button("Refresh Status");
    layout->add_child(set_target_button);
    layout->add_child(refresh_button);

    set_target_button->connect("pressed", callable_mp(this, &FennaraDock::_on_set_target_pressed));
    refresh_button->connect("pressed", callable_mp(this, &FennaraDock::_on_refresh_pressed));
}

void FennaraDock::_refresh_status() {
    bool daemon_connected = local_bridge && local_bridge->is_daemon_connected();
    bool active_target = local_bridge && local_bridge->is_active_mcp_target();

    if (daemon_status_label) {
        daemon_status_label->set_text(
            godot::String("Local daemon: ") +
            (daemon_connected ? "connected" : "waiting for fennara-daemon"));
    }
    if (target_status_label) {
        target_status_label->set_text(_target_status_text(daemon_connected, active_target));
    }
    if (project_label) {
        project_label->set_text("Project: " + _project_name() + "\nPath: " + _project_path());
    }
    if (version_label) {
        version_label->set_text(godot::String("Plugin version: ") + FennaraLocalBridge::PLUGIN_VERSION);
    }
    if (set_target_button) {
        set_target_button->set_disabled(!daemon_connected || active_target);
    }
}

void FennaraDock::_on_set_target_pressed() {
    if (!local_bridge) {
        FLOG_UI("Set MCP target ignored: local bridge missing");
        return;
    }
    bool ok = local_bridge->set_as_active_project();
    godot::Dictionary details;
    details["success"] = ok;
    FLOG_CTX("UI", "Set MCP target requested", details);
    _refresh_status();
}

void FennaraDock::_on_refresh_pressed() {
    _refresh_status();
}

void FennaraDock::_on_mcp_target_state_changed(bool active) {
    (void)active;
    _refresh_status();
}

godot::String FennaraDock::_project_name() const {
    godot::ProjectSettings *settings = godot::ProjectSettings::get_singleton();
    if (settings == nullptr) {
        return "";
    }
    return settings->get_setting("application/config/name", "");
}

godot::String FennaraDock::_project_path() const {
    godot::ProjectSettings *settings = godot::ProjectSettings::get_singleton();
    if (settings == nullptr) {
        return "";
    }
    return settings->globalize_path("res://");
}

godot::String FennaraDock::_target_status_text(bool daemon_connected, bool active_target) const {
    if (!daemon_connected) {
        return "MCP target: waiting for daemon";
    }
    if (active_target) {
        return "MCP target: this project";
    }
    if (local_bridge) {
        godot::String target_name = local_bridge->get_active_mcp_target_name();
        godot::String target_path = local_bridge->get_active_mcp_target_path();
        if (!target_name.is_empty() || !target_path.is_empty()) {
            godot::String text = "MCP target: ";
            text += target_name.is_empty() ? "another project" : target_name;
            if (!target_path.is_empty()) {
                text += "\nTarget path: " + target_path;
            }
            return text;
        }
    }
    return "MCP target: not selected";
}

} // namespace fennara
