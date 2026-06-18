#include "fennara/lsp/lsp_client.hpp"

#include "fennara/logger.hpp"

#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

namespace fennara {
namespace lsp {

namespace {

godot::String stringify_message(const godot::Dictionary &message) {
    if (message.is_empty()) {
        return "<empty>";
    }

    return godot::JSON::stringify(message);
}

} // namespace

godot::Ref<godot::StreamPeerTCP> connect(godot::String &out_error) {
    godot::Ref<godot::StreamPeerTCP> peer;
    peer.instantiate();

    godot::Error err = peer->connect_to_host(HOST, PORT);
    if (err != godot::OK) {
        out_error = godot::String(
            "Cannot connect to GDScript LSP on port {0}. "
            "Is Godot editor running?")
                        .format(godot::Array::make(PORT));
        return godot::Ref<godot::StreamPeerTCP>();
    }

    int64_t start = godot::Time::get_singleton()->get_ticks_msec();
    while (peer->get_status() ==
           godot::StreamPeerTCP::STATUS_CONNECTING) {
        peer->poll();
        if (godot::Time::get_singleton()->get_ticks_msec() - start >
            CONNECT_TIMEOUT_MS) {
            peer->disconnect_from_host();
            out_error = godot::String(
                "Connection timeout to GDScript LSP on port {0}. "
                "Is Godot editor running?")
                            .format(godot::Array::make(PORT));
            return godot::Ref<godot::StreamPeerTCP>();
        }
        godot::OS::get_singleton()->delay_usec(POLL_SLEEP_USEC);
    }

    if (peer->get_status() != godot::StreamPeerTCP::STATUS_CONNECTED) {
        peer->disconnect_from_host();
        out_error = godot::String(
            "Cannot connect to GDScript LSP on port {0}. "
            "Is Godot editor running?")
                        .format(godot::Array::make(PORT));
        return godot::Ref<godot::StreamPeerTCP>();
    }

    peer->set_no_delay(true);
    return peer;
}

godot::Dictionary initialize(godot::Ref<godot::StreamPeerTCP> peer,
                             godot::String client_name) {
    godot::String root_path =
        godot::ProjectSettings::get_singleton()->globalize_path("res://");
    godot::String root_uri =
        root_path.replace("\\", "/");
    if (!root_uri.begins_with("/")) {
        root_uri = "/" + root_uri;
    }
    root_uri = "file://" + root_uri;

    godot::Dictionary client_info;
    client_info["name"] = client_name;
    client_info["version"] = "1.0";

    godot::Dictionary params;
    params["processId"] = godot::Variant();
    params["clientInfo"] = client_info;
    params["rootUri"] = root_uri;
    params["capabilities"] = godot::Dictionary();

    godot::Dictionary msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = 1;
    msg["method"] = "initialize";
    msg["params"] = params;
    send_message(peer, msg);

    godot::Dictionary response = wait_for_response(peer, 1, DEFAULT_TIMEOUT_MS);

    if (response.has("result")) {
        // Send initialized notification only after a successful initialize response.
        send_notification(peer, "initialized", godot::Dictionary());
        godot::OS::get_singleton()->delay_usec(2000);
    } else {
        godot::Dictionary context;
        context["client_name"] = client_name;
        context["peer_status"] = static_cast<int64_t>(peer->get_status());
        context["response"] = stringify_message(response);
        context["root_uri"] = root_uri;
        context["timeout_ms"] = static_cast<int64_t>(DEFAULT_TIMEOUT_MS);
        FLOG_ERR_CTX("LSP initialize missing result", context);
    }

    return response;
}

void shutdown(godot::Ref<godot::StreamPeerTCP> peer) {
    godot::Dictionary msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = 9999;
    msg["method"] = "shutdown";
    send_message(peer, msg);

    wait_for_response(peer, 9999, 1000);
    send_notification(peer, "exit", godot::Dictionary());
}

void send_message(godot::Ref<godot::StreamPeerTCP> peer,
                  godot::Dictionary msg) {
    godot::String body = godot::JSON::stringify(msg);
    godot::PackedByteArray body_bytes = body.to_utf8_buffer();
    godot::String header =
        godot::String("Content-Length: {0}\r\n\r\n")
            .format(godot::Array::make(body_bytes.size()));
    godot::PackedByteArray full_bytes = header.to_ascii_buffer();
    full_bytes.append_array(body_bytes);
    peer->put_data(full_bytes);
}

void send_notification(godot::Ref<godot::StreamPeerTCP> peer,
                       godot::String method, godot::Dictionary params) {
    godot::Dictionary msg;
    msg["jsonrpc"] = "2.0";
    msg["method"] = method;
    msg["params"] = params;
    send_message(peer, msg);
}

godot::Dictionary read_one_message(godot::Ref<godot::StreamPeerTCP> peer,
                                   int timeout_ms) {
    int64_t start = godot::Time::get_singleton()->get_ticks_msec();

    // Phase 1: Read header byte-by-byte until \r\n\r\n
    godot::String header;
    while (godot::Time::get_singleton()->get_ticks_msec() - start <
           timeout_ms) {
        peer->poll();
        if (peer->get_available_bytes() > 0) {
            godot::Array result = peer->get_partial_data(1);
            if ((int)result[0] == godot::OK) {
                godot::PackedByteArray bytes = result[1];
                if (bytes.size() == 1) {
                    header += godot::String::chr(bytes[0]);
                    if (header.ends_with("\r\n\r\n")) {
                        break;
                    }
                }
            }
        } else {
            godot::OS::get_singleton()->delay_usec(POLL_SLEEP_USEC);
        }
    }

    if (!header.ends_with("\r\n\r\n")) {
        return godot::Dictionary();
    }

    // Parse Content-Length
    int content_length = -1;
    godot::PackedStringArray lines = header.split("\r\n");
    for (int i = 0; i < lines.size(); i++) {
        if (lines[i].to_lower().begins_with("content-length:")) {
            godot::PackedStringArray parts = lines[i].split(":");
            if (parts.size() >= 2) {
                content_length = parts[1].strip_edges().to_int();
            }
            break;
        }
    }

    if (content_length <= 0) {
        return godot::Dictionary();
    }

    // Phase 2: Read exactly content_length bytes
    godot::PackedByteArray body_buf;
    while (body_buf.size() < content_length &&
           godot::Time::get_singleton()->get_ticks_msec() - start <
               timeout_ms) {
        peer->poll();
        int available = peer->get_available_bytes();
        if (available > 0) {
            int remaining = content_length - body_buf.size();
            int to_read = available < remaining ? available : remaining;
            godot::Array result = peer->get_partial_data(to_read);
            if ((int)result[0] == godot::OK) {
                godot::PackedByteArray chunk = result[1];
                body_buf.append_array(chunk);
            }
        } else {
            godot::OS::get_singleton()->delay_usec(POLL_SLEEP_USEC);
        }
    }

    if (body_buf.size() < content_length) {
        return godot::Dictionary();
    }

    godot::String body = body_buf.get_string_from_utf8();
    godot::Variant parsed = godot::JSON::parse_string(body);
    if (parsed.get_type() == godot::Variant::DICTIONARY) {
        return parsed;
    }

    return godot::Dictionary();
}

godot::Dictionary wait_for_response(godot::Ref<godot::StreamPeerTCP> peer,
                                    int id, int timeout_ms) {
    int64_t start = godot::Time::get_singleton()->get_ticks_msec();
    while (godot::Time::get_singleton()->get_ticks_msec() - start <
           timeout_ms) {
        int remaining =
            timeout_ms -
            (int)(godot::Time::get_singleton()->get_ticks_msec() - start);
        if (remaining <= 0) {
            break;
        }
        godot::Dictionary m = read_one_message(peer, remaining);
        if (m.is_empty()) {
            if (peer->get_status() != godot::StreamPeerTCP::STATUS_CONNECTED) {
                break;
            }
            godot::OS::get_singleton()->delay_usec(POLL_SLEEP_USEC);
            continue;
        }
        if (m.has("id") && (int)m["id"] == id) {
            return m;
        }
    }
    return godot::Dictionary();
}

} // namespace lsp
} // namespace fennara
