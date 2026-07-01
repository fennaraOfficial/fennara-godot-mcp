#pragma once

#include "fennara/snapshot_manager.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/web_socket_peer.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>

namespace fennara {

class FennaraExecutor;
class FennaraLocalBridge : public godot::Node {
    GDCLASS(FennaraLocalBridge, godot::Node)

protected:
    static void _bind_methods();

public:
    static constexpr const char *PLUGIN_VERSION = "0.3.4";

    void _ready() override;
    void _process(double delta) override;
    void _exit_tree() override;
    bool set_as_active_project();
    bool is_daemon_connected() const;
    bool is_active_mcp_target() const;
    godot::String get_active_mcp_target_name() const;
    godot::String get_active_mcp_target_path() const;
    godot::String get_device_id() const;
    godot::String get_chat_token() const;
    static godot::Dictionary collect_rendering_context();
    void request_get_class_info_warmup();
    bool send_chat_context_snippet(const godot::String &path,
                                   int32_t start_line,
                                   int32_t end_line,
                                   const godot::String &text);

private:
    static constexpr const char *LOCAL_DAEMON_WS_URL = "ws://127.0.0.1:41287/godot/ws";
    static constexpr double RECONNECT_DELAY_SECONDS = 2.0;
    static constexpr int LOCAL_BRIDGE_WS_BUFFER_SIZE_BYTES = 16 * 1024 * 1024;

    godot::Ref<godot::WebSocketPeer> _ws;
    godot::WebSocketPeer::State _last_state = godot::WebSocketPeer::STATE_CLOSED;
    double _reconnect_timer = 0.0;
    bool _sent_hello = false;
    bool _intentional_close = false;
    bool _daemon_spawn_attempted = false;
    bool _queued_get_class_info_warmup = false;
    bool _sent_get_class_info_warmup = false;
    bool _is_active_mcp_target = false;
    godot::String _session_id;
    godot::String _chat_token;
    godot::String _active_mcp_target_name;
    godot::String _active_mcp_target_path;
    godot::Ref<FennaraSnapshotManager> _snapshot_mgr;

    void _connect_socket();
    void _close_socket();
    void _start_daemon_if_available();
    void _send_hello();
    void _handle_message(const godot::Dictionary &message);
    void _handle_tool_call(const godot::Dictionary &message);
    void _handle_snapshot_begin_turn(const godot::Dictionary &message);
    void _handle_snapshot_revert(const godot::Dictionary &message);
    void _handle_open_project_file(const godot::Dictionary &message);
    godot::Dictionary _open_project_file_reference(const godot::String &path,
                                                   int32_t start_line,
                                                   int32_t end_line);
    void _focus_project_file_reference(godot::String path,
                                       int32_t start_line,
                                       int32_t end_line,
                                       int32_t attempt);
    void _on_async_tool_call_completed(const godot::Array &results, godot::String request_id, godot::String tool_name, godot::Dictionary input, uint64_t started_at_ms, godot::Object *executor);
    void _send_json(const godot::Dictionary &payload);
    void _maybe_send_get_class_info_warmup();
    godot::String _daemon_binary_path() const;
    godot::String _make_session_id() const;
    godot::String _make_chat_token() const;
    godot::String _project_name() const;
    godot::String _project_path() const;
    godot::String _godot_executable_path() const;
};

} // namespace fennara
