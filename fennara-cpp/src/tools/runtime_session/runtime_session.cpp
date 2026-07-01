#include "fennara/tools/runtime_session.hpp"

#include "fennara/lsp/csharp_build.hpp"
#include "fennara/runtime/runtime_scene_preflight.hpp"
#include "fennara/tools/validate_scene.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <chrono>
#include <mutex>
#include <thread>

namespace fennara {

namespace {

constexpr const char *kLocalDaemonHost = "127.0.0.1";
constexpr int kLocalDaemonPort = 41287;

godot::String &active_daemon_session_id() {
    static godot::String *session_id = memnew(godot::String);
    return *session_id;
}

std::mutex &active_daemon_session_mutex() {
    static std::mutex *mutex = new std::mutex;
    return *mutex;
}

godot::String get_active_daemon_session_id() {
    std::lock_guard<std::mutex> lock(active_daemon_session_mutex());
    return active_daemon_session_id();
}

void set_active_daemon_session_id(const godot::String &session_id) {
    std::lock_guard<std::mutex> lock(active_daemon_session_mutex());
    active_daemon_session_id() = session_id;
}

void clear_active_daemon_session_id_if_matches(const godot::String &session_id) {
    std::lock_guard<std::mutex> lock(active_daemon_session_mutex());
    if (active_daemon_session_id() == session_id) {
        active_daemon_session_id() = "";
    }
}

uint64_t daemon_now_ms() {
    godot::Time *time = godot::Time::get_singleton();
    return time == nullptr ? 0 : static_cast<uint64_t>(time->get_ticks_msec());
}

godot::String safe_file_component(godot::String value) {
    value = value.strip_edges().to_lower();
    value = value.replace(" ", "_")
                 .replace("/", "_")
                 .replace("\\", "_")
                 .replace(":", "_")
                 .replace("@", "_")
                 .replace(".", "_");
    return value.is_empty() ? godot::String("runtime") : value;
}

godot::String resolve_godot_executable() {
    godot::OS *os = godot::OS::get_singleton();
    if (os == nullptr) {
        return "";
    }
    godot::String current = os->get_executable_path();
    if (os->get_name() == "Windows" && !current.ends_with("_console.exe")) {
        godot::String console = current.trim_suffix(".exe") + "_console.exe";
        if (godot::FileAccess::file_exists(console)) {
            return console;
        }
    }
    return current;
}

godot::Dictionary post_daemon(const godot::String &path,
                              const godot::Dictionary &payload,
                              int timeout_ms = 10000) {
    godot::Dictionary result;
    godot::Ref<godot::HTTPClient> http;
    http.instantiate();
    if (http.is_null() ||
        http->connect_to_host(kLocalDaemonHost, kLocalDaemonPort) != godot::OK) {
        result["success"] = false;
        result["error"] = "Failed to connect to local Fennara daemon.";
        return result;
    }

    godot::PackedStringArray headers;
    headers.append("Content-Type: application/json");
    headers.append("Accept: application/json");
    godot::PackedByteArray body = godot::JSON::stringify(payload).to_utf8_buffer();
    uint64_t deadline = daemon_now_ms() + static_cast<uint64_t>(timeout_ms);
    bool sent = false;
    godot::String response_body;
    while (daemon_now_ms() < deadline) {
        http->poll();
        godot::HTTPClient::Status status = http->get_status();
        if (status == godot::HTTPClient::STATUS_CONNECTED && !sent) {
            if (http->request_raw(godot::HTTPClient::METHOD_POST, path, headers, body) != godot::OK) {
                result["success"] = false;
                result["error"] = "Failed to send daemon request.";
                return result;
            }
            sent = true;
        }
        if (status == godot::HTTPClient::STATUS_BODY) {
            godot::PackedByteArray chunk = http->read_response_body_chunk();
            if (!chunk.is_empty()) {
                response_body += chunk.get_string_from_utf8();
            }
            if (http->get_status() != godot::HTTPClient::STATUS_BODY && http->has_response()) {
                break;
            }
        } else if (sent && status == godot::HTTPClient::STATUS_CONNECTED && http->has_response()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    godot::Variant parsed = godot::JSON::parse_string(response_body);
    if (parsed.get_type() == godot::Variant::DICTIONARY) {
        result = parsed;
    } else {
        result["response_body"] = response_body;
    }
    result["success"] = (bool)result.get("ok", false);
    return result;
}

godot::String artifact_dir(const godot::Dictionary &args) {
    godot::String base = args.get("_fennara_tool_artifact_dir", "");
    if (base.strip_edges().is_empty()) {
        base = "user://.fennara/tool_logs/manual/runtime_session";
    }
    return base;
}

godot::Dictionary daemon_status(const godot::String &session_id) {
    godot::Dictionary payload;
    payload["session_id"] = session_id;
    return post_daemon("/runtime/session/status", payload);
}

godot::Dictionary make_runtime_session_error(const godot::String &status,
                                             const godot::String &message) {
    godot::Dictionary result;
    result["success"] = false;
    result["tool_name"] = "runtime_session";
    result["format_version"] = "runtime-session-result-v1";
    result["status"] = status;
    result["error"] = message;
    return result;
}

} // namespace

void FennaraRuntimeSessionTool::_bind_methods() {
    godot::ClassDB::bind_static_method(
        "FennaraRuntimeSessionTool",
        godot::D_METHOD("execute", "args"),
        &FennaraRuntimeSessionTool::execute);
}

godot::Dictionary FennaraRuntimeSessionTool::execute_start_after_preflight(
    const godot::Dictionary &args,
    const godot::Dictionary &build_result,
    const godot::Dictionary &preflight,
    const godot::Dictionary &script_preflight) {
    godot::String scene_path =
        godot::String(args.get("scene_path", "")).strip_edges();
    godot::String session_id = "runtime-" + godot::String::num_int64(
        static_cast<int64_t>(daemon_now_ms()));
    godot::String artifact_res_dir = artifact_dir(args);
    godot::ProjectSettings *settings = godot::ProjectSettings::get_singleton();
    godot::DirAccess::make_dir_recursive_absolute(
        settings->globalize_path(artifact_res_dir));

    godot::Dictionary payload;
    payload["session_id"] = session_id;
    payload["executable"] = resolve_godot_executable();
    payload["working_directory"] = settings->globalize_path("res://");
    payload["scene_path"] = scene_path;
    payload["artifact_dir"] = settings->globalize_path(artifact_res_dir);
    godot::Dictionary result = post_daemon("/runtime/session/start", payload);
    result["tool_name"] = "runtime_session";
    result["artifact_dir_res_path"] = artifact_res_dir;
    result["csharp_build"] = build_result;
    result["preflight"] = preflight;
    result["script_preflight"] = script_preflight;
    if (result.has("raw_log_path") && !result.has("log_path")) {
        result["log_path"] = result["raw_log_path"];
    }
    if ((bool)result.get("success", false)) {
        set_active_daemon_session_id(session_id);
    }
    return result;
}

godot::Dictionary FennaraRuntimeSessionTool::execute(
    const godot::Dictionary &args) {
    godot::String action =
        godot::String(args.get("action", "status")).strip_edges().to_lower();
    if (action.is_empty() || action == "status") {
        godot::String session_id = args.has("session_id")
            ? godot::String(args.get("session_id", ""))
            : get_active_daemon_session_id();
        session_id = session_id.strip_edges();
        if (session_id.is_empty()) {
            godot::Dictionary result;
            result["success"] = true;
            result["tool_name"] = "runtime_session";
            result["status"] = "idle";
            return result;
        }
        godot::Dictionary result = daemon_status(session_id);
        result["tool_name"] = "runtime_session";
        if (result.has("raw_log_path") && !result.has("log_path")) {
            result["log_path"] = result["raw_log_path"];
        }
        return result;
    }
    if (action == "start") {
        godot::String scene_path = godot::String(args.get("scene_path", "")).strip_edges();
        if (scene_path.is_empty()) {
            return make_runtime_session_error("blocked", "`scene_path` is required.");
        }
        godot::Dictionary build_result = csharp_build::run_dotnet_build_if_needed();
        if ((bool)build_result.get("needed", false) &&
            godot::String(build_result.get("status", "")) != "success") {
            godot::Dictionary result = make_runtime_session_error(
                "blocked",
                "C# project build failed. Runtime session was not started.");
            result["csharp_build"] = build_result;
            return result;
        }

        godot::Dictionary validate_args;
        godot::Array scene_paths;
        scene_paths.append(scene_path);
        validate_args["scene_paths"] = scene_paths;
        validate_args["skip_runtime"] = true;
        if (args.has("_fennara_tool_artifact_dir")) {
            validate_args["_fennara_tool_artifact_dir"] =
                godot::String(args["_fennara_tool_artifact_dir"]).path_join("preflight");
        }
        godot::Dictionary preflight = FennaraValidateSceneTool::execute(validate_args);
        godot::Dictionary summary = preflight.get("summary", godot::Dictionary());
        if (!(bool)preflight.get("success", false) ||
            static_cast<int>(summary.get("errors", 0)) > 0) {
            godot::Dictionary result = make_runtime_session_error(
                "blocked",
                "Scene preflight failed. Runtime session was not started.");
            result["csharp_build"] = build_result;
            result["preflight"] = preflight;
            return result;
        }

        godot::Dictionary script_preflight =
            runtime_scene_preflight::check_scene_scripts(scene_path);
        if (!(bool)script_preflight.get("success", false)) {
            godot::Dictionary result = make_runtime_session_error(
                "blocked",
                "Scene/autoload script diagnostics failed. Runtime session was not started.");
            result["csharp_build"] = build_result;
            result["preflight"] = preflight;
            result["script_preflight"] = script_preflight;
            return result;
        }

        return execute_start_after_preflight(
            args, build_result, preflight, script_preflight);
    }
    if (action == "stop") {
        godot::String session_id = args.has("session_id")
            ? godot::String(args.get("session_id", ""))
            : get_active_daemon_session_id();
        session_id = session_id.strip_edges();
        godot::Dictionary payload;
        payload["session_id"] = session_id;
        godot::Dictionary result = post_daemon("/runtime/session/stop", payload);
        result["tool_name"] = "runtime_session";
        if (result.has("raw_log_path") && !result.has("log_path")) {
            result["log_path"] = result["raw_log_path"];
        }
        if ((bool)result.get("success", false)) {
            clear_active_daemon_session_id_if_matches(session_id);
        }
        return result;
    }
    return make_runtime_session_error(
        "blocked",
        godot::String("Unsupported runtime_session action: ") + action +
            ". Use start, status, or stop.");
}

} // namespace fennara
