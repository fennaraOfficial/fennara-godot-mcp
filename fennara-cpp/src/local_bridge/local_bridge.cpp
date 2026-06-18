#include "fennara/local_bridge.hpp"

#include "fennara/app_paths.hpp"
#include "fennara/logger.hpp"

#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace fennara {

void FennaraLocalBridge::_bind_methods() {
    ADD_SIGNAL(godot::MethodInfo(
        "mcp_target_state_changed",
        godot::PropertyInfo(godot::Variant::BOOL, "active")));
    godot::ClassDB::bind_method(
        godot::D_METHOD("_on_async_tool_call_completed", "results", "request_id", "tool_name", "input", "started_at_ms", "executor"),
        &FennaraLocalBridge::_on_async_tool_call_completed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("request_get_class_info_warmup"),
        &FennaraLocalBridge::request_get_class_info_warmup);
}

void FennaraLocalBridge::_ready() {
    _session_id = _make_session_id();
    set_process(true);
    _connect_socket();
}

void FennaraLocalBridge::_process(double delta) {
    if (!_ws.is_valid()) {
        _reconnect_timer -= delta;
        if (!_intentional_close && _reconnect_timer <= 0.0) {
            _connect_socket();
        }
        return;
    }

    godot::WebSocketPeer::State state = _ws->get_ready_state();
    if (state != godot::WebSocketPeer::STATE_CLOSED) {
        _ws->poll();
        state = _ws->get_ready_state();
    }

    // A connect attempt can fail straight into STATE_CLOSED without ever
    // transitioning through OPEN, so recover from any closed socket here
    // instead of relying only on state-change detection.
    if (state == godot::WebSocketPeer::STATE_CLOSED) {
        if (!_intentional_close) {
            if (_last_state != godot::WebSocketPeer::STATE_CLOSED) {
                FLOG_NET("Local bridge disconnected");
            }
            _start_daemon_if_available();
            _reconnect_timer = _daemon_spawn_attempted ? 0.5 : RECONNECT_DELAY_SECONDS;
        }
            _ws.unref();
            _sent_hello = false;
            _sent_get_class_info_warmup = false;
            if (_is_active_mcp_target) {
                _is_active_mcp_target = false;
                _active_mcp_target_name = "";
                _active_mcp_target_path = "";
                emit_signal("mcp_target_state_changed", false);
            }
            _last_state = state;
            return;
    }

    if (state != _last_state) {
        if (state == godot::WebSocketPeer::STATE_OPEN) {
            _ws->set_no_delay(true);
            _ws->set_outbound_buffer_size(LOCAL_BRIDGE_WS_BUFFER_SIZE_BYTES);
            _sent_hello = false;
            _daemon_spawn_attempted = false;
            FLOG_NET("Local bridge connected");
        }
        _last_state = state;
    }

    if (state == godot::WebSocketPeer::STATE_OPEN && !_sent_hello) {
        _send_hello();
    }
    if (state == godot::WebSocketPeer::STATE_OPEN) {
        _maybe_send_get_class_info_warmup();
    }

    if (state == godot::WebSocketPeer::STATE_OPEN) {
        while (_ws->get_available_packet_count() > 0) {
            godot::PackedByteArray packet = _ws->get_packet();
            if (!_ws->was_string_packet()) {
                continue;
            }

            godot::Variant parsed = godot::JSON::parse_string(packet.get_string_from_utf8());
            if (parsed.get_type() != godot::Variant::DICTIONARY) {
                continue;
            }

            _handle_message(parsed);
        }
    }
}

bool FennaraLocalBridge::is_daemon_connected() const {
    return _ws.is_valid() && _ws->get_ready_state() == godot::WebSocketPeer::STATE_OPEN;
}

bool FennaraLocalBridge::is_active_mcp_target() const {
    return _is_active_mcp_target;
}

godot::String FennaraLocalBridge::get_active_mcp_target_name() const {
    return _active_mcp_target_name;
}

godot::String FennaraLocalBridge::get_active_mcp_target_path() const {
    return _active_mcp_target_path;
}

godot::String FennaraLocalBridge::get_device_id() const {
    godot::String used_path;
    godot::Dictionary state = app_paths::read_json_first_existing(
        app_paths::runtime_state_read_paths(), &used_path);
    return godot::String(state.get("device_id", "")).strip_edges();
}

void FennaraLocalBridge::_exit_tree() {
    _close_socket();
}

void FennaraLocalBridge::_connect_socket() {
    if (_ws.is_valid()) {
        godot::WebSocketPeer::State state = _ws->get_ready_state();
        if (state == godot::WebSocketPeer::STATE_OPEN || state == godot::WebSocketPeer::STATE_CONNECTING) {
            return;
        }
    }

    _ws.instantiate();
    if (!_ws.is_valid()) {
        FLOG_ERR("Local bridge failed to initialize WebSocketPeer");
        return;
    }

    _intentional_close = false;
    _sent_hello = false;
    _last_state = godot::WebSocketPeer::STATE_CLOSED;

    godot::Error err = _ws->connect_to_url(LOCAL_DAEMON_WS_URL);
    if (err != godot::OK) {
        FLOG_NET("Local bridge daemon unavailable");
        _ws.unref();
        _start_daemon_if_available();
        _reconnect_timer = _daemon_spawn_attempted ? 0.5 : RECONNECT_DELAY_SECONDS;
    }
}

void FennaraLocalBridge::_close_socket() {
    _intentional_close = true;
    if (_ws.is_valid()) {
        godot::WebSocketPeer::State state = _ws->get_ready_state();
        if (state == godot::WebSocketPeer::STATE_OPEN || state == godot::WebSocketPeer::STATE_CONNECTING) {
            _ws->close();
        }
    }
}

} // namespace fennara
