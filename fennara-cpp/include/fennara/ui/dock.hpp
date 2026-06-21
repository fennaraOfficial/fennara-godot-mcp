#pragma once

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <memory>

namespace fennara {

class FennaraLocalBridge;
class WebviewHost;

class FennaraDock : public godot::Control {
    GDCLASS(FennaraDock, godot::Control)

protected:
    static void _bind_methods();
    void _notification(int what);

private:
    FennaraLocalBridge *local_bridge = nullptr;
    godot::Control *webview_region = nullptr;
    godot::Control *internal_webview_surface = nullptr;
    godot::Label *fallback_label = nullptr;
    std::unique_ptr<WebviewHost> webview_host;
    double refresh_timer = 0.0;
    double startup_delay = 0.0;
    bool attempted_webview = false;
    bool logged_waiting_for_size = false;
    int stable_geometry_frames = 0;
    godot::Vector2 last_region_position;
    godot::Vector2 last_region_size;

    void _build_ui();
    void _try_start_webview();
    void _sync_webview_bounds();
    void _refresh_status();
    void _on_mcp_target_state_changed(bool active);
    godot::String _chat_url() const;
    bool _webview_region_is_stable();
    void _output_log(const godot::String &message) const;

public:
    FennaraDock();
    ~FennaraDock();

    void set_local_bridge(FennaraLocalBridge *bridge);
    void _ready() override;
    void _process(double delta) override;
    void _gui_input(const godot::Ref<godot::InputEvent> &event) override;
};

} // namespace fennara
