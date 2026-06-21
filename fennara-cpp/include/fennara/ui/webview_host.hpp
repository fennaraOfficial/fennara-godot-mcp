#pragma once

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/variant/string.hpp>

#include <string>

namespace fennara {

class WebviewHost {
public:
    WebviewHost() = default;
    ~WebviewHost();

    bool start(godot::Control *owner, const godot::String &url);
    void resize_to(godot::Control *owner);
    void set_visible(bool visible);
    void stop();
    bool is_started() const;

private:
    void *webview = nullptr;
    void *widget = nullptr;
    void *parent_window = nullptr;
    godot::String current_url;
    godot::String helper_state_path;
    godot::String helper_log_path;
    bool started = false;
    int32_t helper_pid = 0;
    int current_window_id = -1;
    int last_x = -1;
    int last_y = -1;
    int last_width = -1;
    int last_height = -1;
    int last_visible_state = -1;
};

} // namespace fennara
