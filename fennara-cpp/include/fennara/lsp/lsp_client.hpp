#pragma once

#include <godot_cpp/classes/stream_peer_tcp.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara {
namespace lsp {

static constexpr const char *HOST = "127.0.0.1";
static constexpr int PORT = 6005;
static constexpr int CONNECT_TIMEOUT_MS = 2000;
static constexpr int DEFAULT_TIMEOUT_MS = 5000;
static constexpr int POLL_SLEEP_USEC = 500;

// Connect to Godot's built-in LSP server. Returns connected peer, or invalid
// ref on failure (error message written to out_error).
godot::Ref<godot::StreamPeerTCP> connect(godot::String &out_error);

// LSP initialize handshake (sends initialize + initialized).
godot::Dictionary initialize(godot::Ref<godot::StreamPeerTCP> peer,
                             godot::String client_name);

// LSP shutdown + exit.
void shutdown(godot::Ref<godot::StreamPeerTCP> peer);

// Send a full JSON-RPC message with Content-Length framing.
void send_message(godot::Ref<godot::StreamPeerTCP> peer,
                  godot::Dictionary msg);

// Send a JSON-RPC notification (no id).
void send_notification(godot::Ref<godot::StreamPeerTCP> peer,
                       godot::String method, godot::Dictionary params);

// Read one Content-Length-framed JSON-RPC message. Returns empty Dict on
// timeout or malformed data.
godot::Dictionary read_one_message(godot::Ref<godot::StreamPeerTCP> peer,
                                   int timeout_ms);

// Read messages until one with matching id arrives. Returns empty Dict on
// timeout.
godot::Dictionary wait_for_response(godot::Ref<godot::StreamPeerTCP> peer,
                                    int id, int timeout_ms);

} // namespace lsp
} // namespace fennara
