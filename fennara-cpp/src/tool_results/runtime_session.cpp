#include "fennara/tool_results/runtime_session.hpp"

#include "fennara/tool_results/envelope.hpp"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara::tool_results {

namespace {

godot::String status_value(const godot::Dictionary &raw_result) {
    return godot::String(raw_result.get("status", "unknown"));
}

void append_if_present(godot::PackedStringArray &lines,
                       const godot::String &label,
                       const godot::Dictionary &raw_result,
                       const godot::String &key) {
    godot::String value = raw_result.get(key, "");
    if (!value.is_empty()) {
        lines.append(label + godot::String(": ") + value);
    }
}

} // namespace

godot::Dictionary format_runtime_session(const godot::Dictionary &raw_result) {
    godot::String status = status_value(raw_result);
    godot::PackedStringArray lines;
    lines.append("Tool: runtime_session");
    lines.append("Status: " + status);

    if (raw_result.has("error")) {
        lines.append("");
        lines.append(godot::String(raw_result.get("error", "")));
    }

    append_if_present(lines, "Playing scene", raw_result, "playing_scene");
    append_if_present(lines, "Scene", raw_result, "scene_path");
    append_if_present(lines, "Session id", raw_result, "session_id");
    append_if_present(lines, "Log file", raw_result, "log_path");
    append_if_present(lines, "Captures dir", raw_result, "captures_dir");
    if (!godot::String(raw_result.get("log_path", "")).is_empty()) {
        lines.append(
            "Session log is the source of truth for runtime progress, debugger issues, script logs, and captures.");
    }

    if (raw_result.has("script_running")) {
        lines.append(
            "Script running: " +
            godot::String((bool)raw_result.get("script_running", false)
                              ? "true"
                              : "false"));
    }
    if (raw_result.has("max_run_seconds")) {
        double seconds = static_cast<double>(
            raw_result.get("max_run_seconds", 0.0));
        if (seconds > 0.0) {
            lines.append("Max run seconds: " + godot::String::num(seconds));
        }
    }

    godot::Array launch_errors =
        raw_result.get("launch_errors", godot::Array());
    godot::Array runtime_debugger_errors =
        raw_result.get("runtime_debugger_errors", godot::Array());
    godot::Array latest_runtime_issues =
        raw_result.get("latest_runtime_issues", godot::Array());
    godot::Array shown_runtime_issues =
        !runtime_debugger_errors.is_empty() ? runtime_debugger_errors : latest_runtime_issues;
    if (!shown_runtime_issues.is_empty()) {
        lines.append("");
        lines.append("Runtime debugger issues:");
        int count = shown_runtime_issues.size() < 6 ? shown_runtime_issues.size() : 6;
        for (int i = 0; i < count; i++) {
            if (shown_runtime_issues[i].get_type() != godot::Variant::DICTIONARY) {
                continue;
            }
            godot::Dictionary issue = shown_runtime_issues[i];
            godot::String message = issue.get("message", "");
            int repeated = static_cast<int>(issue.get("count", 1));
            lines.append(
                "- " + message +
                (repeated > 1
                     ? godot::String(" (repeated ") +
                           godot::String::num_int64(repeated) + " times)"
                     : godot::String()));

            godot::Array raw_lines = issue.get("raw_lines", godot::Array());
            int detail_count = raw_lines.size() < 4 ? raw_lines.size() : 4;
            for (int j = 1; j < detail_count; j++) {
                lines.append("  - " + godot::String(raw_lines[j]));
            }
        }
        if (shown_runtime_issues.size() > count) {
            lines.append(
                "- ... " +
                godot::String::num_int64(shown_runtime_issues.size() - count) +
                " more runtime issue(s) omitted");
        }
    }
    godot::Array msbuild_issues =
        raw_result.get("msbuild_issues", godot::Array());
    godot::Dictionary csharp_build =
        raw_result.get("csharp_build", godot::Dictionary());
    if (!csharp_build.is_empty() &&
        (bool)csharp_build.get("needed", false)) {
        lines.append("");
        lines.append("C# build:");
        lines.append("- Status: " + godot::String(csharp_build.get("status", "")));
        lines.append("- Command: " + godot::String(csharp_build.get("command", "dotnet build")));
        lines.append("- Duration: " +
                     godot::String::num((double)csharp_build.get("duration_seconds", 0.0)) +
                     "s");
        if (godot::String(csharp_build.get("status", "")) != "success") {
            godot::String output = csharp_build.get("output", "");
            if (!output.strip_edges().is_empty()) {
                lines.append("");
                lines.append(output.strip_edges());
            }
        }
    }

    godot::Dictionary preflight =
        raw_result.get("preflight", godot::Dictionary());
    if (!preflight.is_empty()) {
        godot::Dictionary preflight_summary =
            preflight.get("summary", godot::Dictionary());
        int preflight_errors =
            static_cast<int>(preflight_summary.get("errors", 0));
        int preflight_warnings =
            static_cast<int>(preflight_summary.get("warnings", 0));
        lines.append("");
        lines.append("Scene preflight:");
        lines.append("- Errors: " + godot::String::num_int64(preflight_errors) +
                     ", warnings: " + godot::String::num_int64(preflight_warnings));
        if (preflight_errors > 0) {
            godot::Array scenes = preflight.get("scenes", godot::Array());
            for (int i = 0; i < scenes.size(); i++) {
                if (scenes[i].get_type() != godot::Variant::DICTIONARY) {
                    continue;
                }
                godot::Dictionary scene = scenes[i];
                godot::Array issues = scene.get("issues", godot::Array());
                for (int j = 0; j < issues.size() && j < 8; j++) {
                    if (issues[j].get_type() != godot::Variant::DICTIONARY) {
                        continue;
                    }
                    godot::Dictionary issue = issues[j];
                    if (godot::String(issue.get("severity", "")) != "error") {
                        continue;
                    }
                    lines.append("- " + godot::String(issue.get("message", "")));
                }
            }
        }
    }

    godot::Dictionary script_preflight =
        raw_result.get("script_preflight", godot::Dictionary());
    if (!script_preflight.is_empty()) {
        int script_errors =
            static_cast<int>(script_preflight.get("error_count", 0));
        int script_warnings =
            static_cast<int>(script_preflight.get("warning_count", 0));
        int checked_scripts =
            static_cast<int>(script_preflight.get("checked_script_count", 0));
        lines.append("");
        lines.append("Script preflight:");
        lines.append("- Checked scripts: " +
                     godot::String::num_int64(checked_scripts));
        lines.append("- Errors: " + godot::String::num_int64(script_errors) +
                     ", warnings: " +
                     godot::String::num_int64(script_warnings));
        if (script_errors > 0) {
            godot::Array diagnostics =
                script_preflight.get("diagnostics", godot::Array());
            int shown = 0;
            for (int i = 0; i < diagnostics.size() && shown < 8; i++) {
                if (diagnostics[i].get_type() != godot::Variant::DICTIONARY) {
                    continue;
                }
                godot::Dictionary diagnostic = diagnostics[i];
                if (godot::String(diagnostic.get("severity", "")) != "error") {
                    continue;
                }
                lines.append("- " +
                             godot::String(diagnostic.get("message", "")));
                shown++;
            }
        }
    }

    if (!msbuild_issues.is_empty()) {
        lines.append("");
        lines.append("MSBuild issues:");
        int count = msbuild_issues.size() < 8 ? msbuild_issues.size() : 8;
        for (int i = 0; i < count; i++) {
            if (msbuild_issues[i].get_type() != godot::Variant::DICTIONARY) {
                continue;
            }
            godot::Dictionary issue = msbuild_issues[i];
            godot::String file = issue.get("file", "");
            int line = static_cast<int>(issue.get("line", 0));
            int column = static_cast<int>(issue.get("column", 0));
            godot::String code = issue.get("code", "");
            godot::String message = issue.get("message", "");
            lines.append(
                "- " + code + ": " + message + " (" + file + ":" +
                godot::String::num_int64(line) + ":" +
                godot::String::num_int64(column) + ")");
        }
        if (msbuild_issues.size() > count) {
            lines.append(
                "- ... " +
                godot::String::num_int64(msbuild_issues.size() - count) +
                " more MSBuild issue(s) omitted");
        }
    }
    append_if_present(lines, "MSBuild issues file", raw_result, "msbuild_issues_path");
    append_if_present(lines, "MSBuild log file", raw_result, "msbuild_log_path");

    if (!launch_errors.is_empty()) {
        lines.append("");
        lines.append("Launch debugger issues:");
        int count = launch_errors.size() < 5 ? launch_errors.size() : 5;
        for (int i = 0; i < count; i++) {
            if (launch_errors[i].get_type() != godot::Variant::DICTIONARY) {
                continue;
            }
            godot::Dictionary error = launch_errors[i];
            godot::String message = error.get("message", "");
            int repeated = static_cast<int>(error.get("count", 1));
            lines.append(
                "- " + message +
                (repeated > 1
                     ? godot::String(" (repeated ") +
                           godot::String::num_int64(repeated) + " times)"
                     : godot::String()));

            godot::Array raw_lines = error.get("raw_lines", godot::Array());
            int detail_count = raw_lines.size() < 4 ? raw_lines.size() : 4;
            for (int j = 1; j < detail_count; j++) {
                lines.append("  - " + godot::String(raw_lines[j]));
            }
        }
        if (launch_errors.size() > count) {
            lines.append(
                "- ... " +
                godot::String::num_int64(launch_errors.size() - count) +
                " more launch error(s) omitted");
        }
    }

    if (status == "started" || status == "managed_running") {
        lines.append("");
        lines.append(
            "Stop this session with `runtime_session` action `stop` as soon as runtime work is complete.");
    }
    if (status == "started_with_errors") {
        lines.append("");
        lines.append(
            "The scene entered play mode but debugger issues are already present. Inspect the session log before running runtime scripts.");
    }
    if (status == "unmanaged_running") {
        lines.append("");
        lines.append(
            "Next step: ask the user to stop the running scene in Godot, then call `runtime_session` with action `start` again.");
    }
    if (status == "blocked" || status == "managed_stale") {
        lines.append("");
        lines.append(
            "Resolve the active or stale runtime state before starting another scene.");
    }

    godot::Dictionary metadata = make_base_metadata(
        "runtime_session",
        "runtime_session-md-v1",
        status);
    metadata["summary"] = raw_result;
    metadata["scene_running"] = raw_result.get("scene_running", false);
    metadata["session_id"] = raw_result.get("session_id", "");
    metadata["scene_path"] = raw_result.get("scene_path", "");
    metadata["playing_scene"] = raw_result.get("playing_scene", "");
    metadata["log_path"] = raw_result.get("log_path", "");
    metadata["captures_dir"] = raw_result.get("captures_dir", "");
    metadata["script_running"] = raw_result.get("script_running", false);
    metadata["runtime_issue_count"] = raw_result.get("runtime_issue_count", 0);
    metadata["latest_runtime_issues"] = latest_runtime_issues;
    metadata["latest_runtime_summary"] = raw_result.get("latest_runtime_summary", godot::Dictionary());
    metadata["runtime_debugger_errors"] = runtime_debugger_errors;
    metadata["runtime_debugger_error_count"] =
        raw_result.get("runtime_debugger_error_count", runtime_debugger_errors.size());
    metadata["runtime_debugger_summary"] =
        raw_result.get("runtime_debugger_summary", godot::Dictionary());
    metadata["launch_errors"] = launch_errors;
    metadata["launch_error_count"] = raw_result.get("launch_error_count", launch_errors.size());
    metadata["msbuild_issues"] = msbuild_issues;
    metadata["csharp_build"] = csharp_build;
    metadata["preflight"] = preflight;
    metadata["script_preflight"] = script_preflight;
    metadata["msbuild_issue_count"] = raw_result.get("msbuild_issue_count", msbuild_issues.size());
    metadata["msbuild_issues_path"] = raw_result.get("msbuild_issues_path", "");
    metadata["msbuild_log_path"] = raw_result.get("msbuild_log_path", "");

    godot::Dictionary envelope = make_envelope(
        godot::String("\n").join(lines),
        metadata,
        raw_result.get("success", false));
    if (raw_result.has("log_path")) {
        envelope["log_path"] = raw_result["log_path"];
    }
    if (raw_result.has("captures_dir")) {
        envelope["captures_dir"] = raw_result["captures_dir"];
    }
    return envelope;
}

} // namespace fennara::tool_results
