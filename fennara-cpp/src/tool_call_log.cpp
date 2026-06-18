#include "fennara/tool_call_log.hpp"

#include "fennara/logger.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/time.hpp>

namespace fennara::tool_call_log {
namespace {

constexpr const char *TOOL_LOG_ROOT = "user://.fennara/tool_logs";

godot::String now_iso() {
    godot::Time *time = godot::Time::get_singleton();
    return time == nullptr ? godot::String() : time->get_datetime_string_from_system(true, true);
}

uint64_t ticks_ms() {
    godot::Time *time = godot::Time::get_singleton();
    return time == nullptr ? 0 : time->get_ticks_msec();
}

godot::String safe_segment(const godot::String &value) {
    godot::String out = value.strip_edges();
    if (out.is_empty()) {
        return "unknown";
    }
    out = out.replace("://", "_");
    out = out.replace(" ", "_");
    out = out.replace("/", "_");
    out = out.replace("\\", "_");
    out = out.replace(":", "_");
    out = out.replace("#", "_");
    out = out.replace("*", "_");
    out = out.replace("?", "_");
    out = out.replace("\"", "_");
    out = out.replace("<", "_");
    out = out.replace(">", "_");
    out = out.replace("|", "_");
    return out;
}

godot::String compact_request_id(const godot::String &request_id) {
    godot::String value = safe_segment(request_id);
    if (value.begins_with("local-tool-")) {
        return "tool_" + value.trim_prefix("local-tool-");
    }
    return value;
}

bool tool_uses_artifact_folder(const godot::String &tool) {
    return tool == "runtime_session" ||
           tool == "screenshot_scene" ||
           tool == "validate_scene";
}

godot::String result_folder_name(const godot::String &request_id,
                                 const godot::String &tool) {
    return compact_request_id(request_id) + "_" + safe_segment(tool);
}

godot::String result_path(const godot::String &session_id,
                          const godot::String &request_id,
                          const godot::String &tool) {
    if (tool_uses_artifact_folder(tool)) {
        return result_artifact_dir(session_id, request_id, tool)
            .path_join("result.json");
    }
    return session_log_dir(session_id)
        .path_join("results")
        .path_join(result_folder_name(request_id, tool) + ".json");
}

void ensure_dir(const godot::String &path) {
    if (path.is_empty()) {
        return;
    }
    godot::DirAccess::make_dir_recursive_absolute(path);
}

void write_json_file(const godot::String &path, const godot::Dictionary &data) {
    ensure_dir(path.get_base_dir());
    godot::Ref<godot::FileAccess> file =
        godot::FileAccess::open(path, godot::FileAccess::WRITE);
    if (file.is_null()) {
        godot::Dictionary details;
        details["path"] = path;
        FLOG_CTX("TOOL", "Failed to write MCP tool result log", details);
        return;
    }
    file->store_string(godot::JSON::stringify(data, "\t"));
    file->store_string("\n");
}

void append_event(const godot::String &session_id, godot::Dictionary event) {
    const godot::String dir = session_log_dir(session_id);
    ensure_dir(dir);

    godot::Ref<godot::FileAccess> file =
        godot::FileAccess::open(dir.path_join("calls.jsonl"), godot::FileAccess::READ_WRITE);
    if (file.is_null()) {
        file = godot::FileAccess::open(dir.path_join("calls.jsonl"), godot::FileAccess::WRITE);
    }
    if (file.is_null()) {
        godot::Dictionary details;
        details["session_id"] = session_id;
        details["log_dir"] = dir;
        FLOG_CTX("TOOL", "Failed to append MCP tool call log", details);
        return;
    }

    file->seek_end();
    event["ts"] = now_iso();
    event["ts_ms"] = static_cast<int64_t>(ticks_ms());
    file->store_string(godot::JSON::stringify(event));
    file->store_string("\n");
}

godot::Dictionary base_event(const godot::String &event_name,
                             const godot::String &request_id,
                             const godot::String &tool,
                             const godot::String &status) {
    godot::Dictionary event;
    event["event"] = event_name;
    event["request_id"] = request_id;
    event["tool"] = tool;
    event["tool_type"] = tool;
    event["status"] = status;
    return event;
}

} // namespace

godot::String session_log_dir(const godot::String &session_id) {
    return godot::String(TOOL_LOG_ROOT).path_join(safe_segment(session_id));
}

godot::String result_artifact_dir(const godot::String &session_id,
                                  const godot::String &request_id,
                                  const godot::String &tool) {
    return session_log_dir(session_id)
        .path_join("results")
        .path_join(result_folder_name(request_id, tool));
}

void log_received(const godot::String &session_id,
                  const godot::String &request_id,
                  const godot::String &tool,
                  const godot::Dictionary &input) {
    godot::Dictionary event = base_event("received", request_id, tool, "received");
    event["input"] = input;
    append_event(session_id, event);
}

void log_started(const godot::String &session_id,
                 const godot::String &request_id,
                 const godot::String &tool) {
    append_event(session_id, base_event("started", request_id, tool, "running"));
}

void log_completed(const godot::String &session_id,
                   const godot::String &request_id,
                   const godot::String &tool,
                   const godot::Dictionary &input,
                   const godot::Dictionary &result,
                   bool ok,
                   uint64_t started_at_ms) {
    const godot::String status = ok ? "completed" : "failed";
    const bool has_artifact_folder = tool_uses_artifact_folder(tool);
    const godot::String artifact_path =
        has_artifact_folder ? result_artifact_dir(session_id, request_id, tool) : godot::String();
    const godot::String path = result_path(session_id, request_id, tool);

    godot::Dictionary result_file;
    result_file["request_id"] = request_id;
    result_file["tool"] = tool;
    result_file["tool_type"] = tool;
    result_file["status"] = status;
    result_file["ok"] = ok;
    result_file["input"] = input;
    result_file["result"] = result;
    if (has_artifact_folder) {
        result_file["artifact_path"] = artifact_path;
    }
    result_file["completed_at"] = now_iso();
    result_file["duration_ms"] =
        started_at_ms > 0 ? static_cast<int64_t>(ticks_ms() - started_at_ms) : 0;
    write_json_file(path, result_file);

    godot::Dictionary event = base_event(status, request_id, tool, status);
    event["ok"] = ok;
    event["duration_ms"] = result_file["duration_ms"];
    event["result_path"] = path;
    if (has_artifact_folder) {
        event["artifact_path"] = artifact_path;
    }
    append_event(session_id, event);
}

void log_failed(const godot::String &session_id,
                const godot::String &request_id,
                const godot::String &tool,
                const godot::Dictionary &input,
                const godot::String &error,
                uint64_t started_at_ms) {
    godot::Dictionary result;
    result["success"] = false;
    result["error"] = error;
    result["ok"] = false;
    log_completed(session_id, request_id, tool, input, result, false, started_at_ms);
}

} // namespace fennara::tool_call_log
