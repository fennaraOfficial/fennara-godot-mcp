#pragma once

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/v_box_container.hpp>

namespace fennara {

class FennaraLocalBridge;

class FennaraDock : public godot::Control {
    GDCLASS(FennaraDock, godot::Control)

protected:
    static void _bind_methods();

private:
    FennaraLocalBridge *local_bridge = nullptr;
    godot::Label *daemon_status_label = nullptr;
    godot::Label *target_status_label = nullptr;
    godot::Label *project_label = nullptr;
    godot::Label *version_label = nullptr;
    godot::Button *set_target_button = nullptr;
    godot::Button *refresh_button = nullptr;
    double refresh_timer = 0.0;

    void _build_ui();
    void _refresh_status();
    void _on_set_target_pressed();
    void _on_refresh_pressed();
    void _on_mcp_target_state_changed(bool active);
    godot::String _project_name() const;
    godot::String _project_path() const;
    godot::String _target_status_text(bool daemon_connected, bool active_target) const;

public:
    FennaraDock();
    ~FennaraDock() = default;

    void set_local_bridge(FennaraLocalBridge *bridge);
    void _ready() override;
    void _process(double delta) override;
};

} // namespace fennara
