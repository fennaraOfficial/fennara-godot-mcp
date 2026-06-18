#include "fennara/lsp/gdscript_lsp.hpp"

#include "fennara/file_utils.hpp"
#include "fennara/lsp/lsp_client.hpp"
#include "fennara/logger.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>

namespace fennara {
namespace gdscript_lsp {

namespace {

constexpr int kLspInitAttempts = 3;
constexpr int kLspInitRetryDelayUsec = 500000;

godot::String severity_to_string(int severity) {
    switch (severity) {
    case 1:
        return "error";
    case 2:
        return "warning";
    case 3:
        return "info";
    default:
        return "hint";
    }
}

godot::Ref<godot::StreamPeerTCP> connect_and_initialize(
    const godot::String &client_name, godot::Dictionary &result) {
    godot::Dictionary last_init_response;
    godot::String last_connect_error;

    for (int attempt = 1; attempt <= kLspInitAttempts; attempt++) {
        godot::String connect_error;
        godot::Ref<godot::StreamPeerTCP> peer = lsp::connect(connect_error);
        if (!peer.is_valid()) {
            last_connect_error = connect_error;
        } else {
            godot::Dictionary init_response = lsp::initialize(peer, client_name);
            if (init_response.has("result")) {
                return peer;
            }

            last_init_response = init_response;
            peer->disconnect_from_host();
        }

        if (attempt < kLspInitAttempts) {
            godot::OS::get_singleton()->delay_usec(kLspInitRetryDelayUsec);
        }
    }

    godot::Dictionary context;
    context["attempts"] = static_cast<int64_t>(kLspInitAttempts);
    context["client_name"] = client_name;
    if (!last_connect_error.is_empty()) {
        context["last_connect_error"] = last_connect_error;
    }
    context["last_init_response"] = godot::JSON::stringify(last_init_response);
    FLOG_ERR_CTX("Diag: LSP initialization failed", context);

    result["success"] = false;
    result["error"] = last_connect_error.is_empty()
        ? godot::String("LSP initialization failed after retries")
        : last_connect_error;
    return godot::Ref<godot::StreamPeerTCP>();
}

} // namespace

godot::Dictionary diagnose_files(const godot::Array &file_paths,
                                 const godot::String &client_name) {
    godot::Dictionary result;
    godot::Dictionary per_file_results;
    godot::Array files_to_check;
    godot::Dictionary target_filenames; // lowercase filename -> absolute path

    for (int i = 0; i < file_paths.size(); i++) {
        godot::String resolved = file_utils::resolve_path(file_paths[i]);
        if (!godot::FileAccess::file_exists(resolved)) {
            continue;
        }

        files_to_check.append(resolved);
        target_filenames[resolved.get_file().to_lower()] = resolved;

        godot::Dictionary file_result;
        file_result["diagnostics"] = godot::Array();
        file_result["total_errors"] = 0;
        file_result["total_warnings"] = 0;
        file_result["total_info"] = 0;
        file_result["total_hints"] = 0;
        per_file_results[resolved] = file_result;
    }

    result["file_count"] = files_to_check.size();
    result["per_file"] = per_file_results;

    if (files_to_check.is_empty()) {
        result["success"] = true;
        return result;
    }

    godot::Ref<godot::StreamPeerTCP> peer =
        connect_and_initialize(client_name, result);
    if (!peer.is_valid()) {
        return result;
    }

    for (int i = 0; i < files_to_check.size(); i++) {
        godot::String gd_file = files_to_check[i];
        godot::String content = file_utils::read_file_content(gd_file);
        if (content.is_empty()) {
            continue;
        }

        godot::String uri = file_utils::path_to_uri(gd_file);

        godot::Dictionary text_doc;
        text_doc["uri"] = uri;
        text_doc["languageId"] = "gdscript";
        text_doc["version"] = 1;
        text_doc["text"] = content;

        godot::Dictionary open_params;
        open_params["textDocument"] = text_doc;
        lsp::send_notification(peer, "textDocument/didOpen", open_params);
    }

    godot::OS::get_singleton()->delay_usec(500000);

    godot::Dictionary files_seen;
    int64_t poll_start = godot::Time::get_singleton()->get_ticks_msec();

    while (godot::Time::get_singleton()->get_ticks_msec() - poll_start <
           lsp::DEFAULT_TIMEOUT_MS) {
        int remaining = lsp::DEFAULT_TIMEOUT_MS -
                        static_cast<int>(godot::Time::get_singleton()->get_ticks_msec() -
                                         poll_start);
        if (remaining <= 0) {
            break;
        }

        godot::Dictionary msg = lsp::read_one_message(peer, remaining);
        if (msg.is_empty()) {
            break;
        }

        godot::String method = msg.get("method", "");
        if (method != "textDocument/publishDiagnostics") {
            continue;
        }

        godot::Dictionary params = msg.get("params", godot::Dictionary());
        godot::String msg_uri = params.get("uri", "");
        godot::String msg_filename = msg_uri.get_file().to_lower();
        godot::String matched_abs_path;
        if (target_filenames.has(msg_filename)) {
            matched_abs_path = target_filenames[msg_filename];
        }

        if (matched_abs_path.is_empty()) {
            continue;
        }

        files_seen[matched_abs_path] = true;

        godot::Dictionary file_result = per_file_results.get(matched_abs_path, godot::Dictionary());
        godot::Array diags = file_result.get("diagnostics", godot::Array());
        int file_errors = file_result.get("total_errors", 0);
        int file_warnings = file_result.get("total_warnings", 0);
        int file_info = file_result.get("total_info", 0);
        int file_hints = file_result.get("total_hints", 0);

        godot::Array raw_diags = params.get("diagnostics", godot::Array());
        for (int d = 0; d < raw_diags.size(); d++) {
            godot::Dictionary raw = raw_diags[d];
            godot::Dictionary range_obj = raw.get("range", godot::Dictionary());
            godot::Dictionary start_pos = range_obj.get("start", godot::Dictionary());

            int severity = static_cast<int>(raw.get("severity", 4));

            godot::Dictionary diag;
            diag["line"] = static_cast<int>(start_pos.get("line", 0)) + 1;
            diag["column"] = static_cast<int>(start_pos.get("character", 0)) + 1;
            diag["severity"] = severity_to_string(severity);
            diag["message"] = raw.get("message", "");
            diags.append(diag);

            if (severity == 1) {
                file_errors++;
            } else if (severity == 2) {
                file_warnings++;
            } else if (severity == 3) {
                file_info++;
            } else {
                file_hints++;
            }
        }

        file_result["diagnostics"] = diags;
        file_result["total_errors"] = file_errors;
        file_result["total_warnings"] = file_warnings;
        file_result["total_info"] = file_info;
        file_result["total_hints"] = file_hints;
        per_file_results[matched_abs_path] = file_result;

        if (files_seen.size() >= files_to_check.size()) {
            break;
        }
    }

    for (int i = 0; i < files_to_check.size(); i++) {
        godot::String uri = file_utils::path_to_uri(files_to_check[i]);
        godot::Dictionary close_doc;
        close_doc["uri"] = uri;
        godot::Dictionary close_params;
        close_params["textDocument"] = close_doc;
        lsp::send_notification(peer, "textDocument/didClose", close_params);
    }

    lsp::shutdown(peer);
    peer->disconnect_from_host();

    result["success"] = true;
    result["per_file"] = per_file_results;
    return result;
}

} // namespace gdscript_lsp
} // namespace fennara
