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
    void stop();
    bool is_started() const;

private:
    void *webview = nullptr;
    void *widget = nullptr;
    void *parent_window = nullptr;
    godot::String current_url;
    bool started = false;
    int current_window_id = -1;
    int last_x = -1;
    int last_y = -1;
    int last_width = -1;
    int last_height = -1;
};

} // namespace fennara
