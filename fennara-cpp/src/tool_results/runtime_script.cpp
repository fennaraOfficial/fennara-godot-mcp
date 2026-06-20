#include "fennara/tool_results/runtime_script.hpp"

#include "fennara/tool_results/envelope.hpp"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace fennara::tool_results {

namespace {

godot::String normalize_path_for_model(const godot::String &path) {
    godot::String normalized = path.replace("\\", "/");
    if (normalized.begins_with("res://") || normalized.begins_with("user://")) {
        return normalized;
    }

    godot::ProjectSettings *settings = godot::ProjectSettings::get_singleton();
    if (settings == nullptr) {
        return normalized;
    }

    godot::String user_root =
        settings->globalize_path("user://").replace("\\", "/");
    if (!user_root.ends_with("/")) {
        user_root += "/";
    }
    if (normalized.begins_with(user_root)) {
        return "user://" + normalized.substr(user_root.length());
    }
    return normalized;
}

} // namespace

godot::Dictionary format_runtime_script(const godot::Dictionary &raw_result) {
    godot::String status = raw_result.get("status", "unknown");
    godot::PackedStringArray lines;
    lines.append("Tool: runtime_script");
    lines.append("Status: " + status);
    if (raw_result.has("error")) {
        lines.append("");
        lines.append(godot::String(raw_result.get("error", "")));
    }
    godot::String session_id = raw_result.get("session_id", "");
    godot::String script_run_id = raw_result.get("script_run_id", "");
    godot::String script_path = raw_result.get("script_path", "");
    godot::String log_path = raw_result.get("log_path", "");
    godot::String visible_log_path = normalize_path_for_model(log_path);
    godot::String runtime_session_artifact_dir =
        raw_result.get("runtime_session_artifact_dir", "");
    godot::String visible_runtime_session_artifact_dir =
        normalize_path_for_model(runtime_session_artifact_dir);
    godot::String captures_dir = raw_result.get("captures_dir", "");
    godot::String visible_captures_dir = normalize_path_for_model(captures_dir);
    godot::Array captures = raw_result.get("captures", godot::Array());
    godot::Dictionary script_result = raw_result.get("result", godot::Dictionary());
    godot::Dictionary runtime_findings =
        raw_result.get("runtime_findings", godot::Dictionary());
    if (!session_id.is_empty()) lines.append("Session id: " + session_id);
    if (!script_run_id.is_empty()) lines.append("Script run id: " + script_run_id);
    if (!script_path.is_empty()) lines.append("Script path: " + script_path);
    if (raw_result.has("diagnostic_success")) {
        godot::String error_count =
            godot::String::num_int64((int)raw_result.get("total_errors", 0));
        godot::String warning_count =
            godot::String::num_int64((int)raw_result.get("total_warnings", 0));
        lines.append("Diagnostics: " + error_count + " errors, " +
                     warning_count + " warnings");
        if (raw_result.has("diagnostic_error")) {
            lines.append("Diagnostic note: " +
                         godot::String(raw_result.get("diagnostic_error", "")));
        }
    }
    if (!visible_log_path.is_empty()) {
        lines.append("Log file: " + visible_log_path);
        if (visible_log_path != log_path) {
            lines.append("Log file absolute: " + log_path.replace("\\", "/"));
        }
    }
    if (!runtime_session_artifact_dir.is_empty()) {
        lines.append("Runtime session artifacts: " + visible_runtime_session_artifact_dir);
        if (visible_runtime_session_artifact_dir != runtime_session_artifact_dir) {
            lines.append("Runtime session artifacts absolute: " +
                         runtime_session_artifact_dir.replace("\\", "/"));
        }
    }
    if (script_result.has("scene_closed")) {
        lines.append(
            godot::String("Scene closed: ") +
            ((bool)script_result.get("scene_closed", false) ? "true" : "false"));
    }
    if (script_result.has("session_active")) {
        lines.append(
            godot::String("Session active: ") +
            ((bool)script_result.get("session_active", false) ? "true" : "false"));
    }
    if (!visible_captures_dir.is_empty()) {
        lines.append("Captures dir: " + visible_captures_dir);
        if (visible_captures_dir != captures_dir) {
            lines.append("Captures dir absolute: " + captures_dir.replace("\\", "/"));
        }
    }
    if (!captures.is_empty()) {
        lines.append("Captures: " + godot::String::num_int64(captures.size()));
        for (int i = 0; i < captures.size(); i++) {
            godot::Dictionary capture = captures[i];
            godot::String image_path =
                normalize_path_for_model(godot::String(capture.get("image_path", "")));
            lines.append("- " + godot::String(capture.get("label", "capture")) +
                         ": " + image_path);
        }
    }
    if ((bool)runtime_findings.get("has_findings", false)) {
        lines.append("");
        lines.append("## Runtime findings during script");
        lines.append(
            "Errors: " +
            godot::String::num_int64((int)runtime_findings.get("error_count", 0)) +
            ", warnings: " +
            godot::String::num_int64((int)runtime_findings.get("warning_count", 0)) +
            ", crashes: " +
            godot::String::num_int64((int)runtime_findings.get("crash_count", 0)));
        godot::String compacted = runtime_findings.get("compacted", "");
        if (!compacted.strip_edges().is_empty()) {
            lines.append("");
            lines.append(compacted);
        }
    }
    if (!log_path.is_empty()) {
        lines.append(
            "Read the session log for script logs, runtime errors, and follow-up state.");
    }

    godot::Dictionary metadata =
        make_base_metadata("runtime_script", "runtime_script-md-v1", status);
    metadata["summary"] = raw_result;
    metadata["session_id"] = session_id;
    metadata["script_run_id"] = script_run_id;
    metadata["script_path"] = script_path;
    metadata["log_path"] = visible_log_path;
    metadata["raw_log_path"] = log_path;
    metadata["runtime_session_artifact_dir"] = visible_runtime_session_artifact_dir;
    metadata["raw_runtime_session_artifact_dir"] = runtime_session_artifact_dir;
    metadata["captures_dir"] = visible_captures_dir;
    metadata["raw_captures_dir"] = captures_dir;
    metadata["captures"] = captures;
    metadata["runtime_findings"] = runtime_findings;
    return make_envelope(godot::String("\n").join(lines),
                         metadata,
                         raw_result.get("success", false));
}

} // namespace fennara::tool_results
