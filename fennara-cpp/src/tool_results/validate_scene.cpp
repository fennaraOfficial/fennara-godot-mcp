#include "fennara/tool_results/validate_scene.hpp"

#include "fennara/tool_results/envelope.hpp"
#include "fennara/tool_results/markdown.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara::tool_results {

namespace {

int validate_scene_budget_tokens(int target_count) {
    if (target_count <= 1) return 10000;
    if (target_count == 2) return 14000;
    if (target_count == 3) return 18000;
    if (target_count == 4) return 22000;
    return 26000;
}

godot::String scene_label(const godot::Dictionary &scene, int index) {
    godot::String path = scene.get("scene_path", "");
    if (!path.is_empty()) {
        return path;
    }
    return "scene_paths[" + godot::String::num_int64(index) + "]";
}

godot::String scope_for_scenes(const godot::Array &scenes) {
    godot::PackedStringArray paths;
    for (int i = 0; i < scenes.size(); i++) {
        if (scenes[i].get_type() != godot::Variant::DICTIONARY) {
            paths.append("scene_paths[" + godot::String::num_int64(i) + "]");
            continue;
        }
        godot::Dictionary scene = scenes[i];
        paths.append(scene_label(scene, i));
    }
    return godot::String::num_int64(scenes.size()) +
           (scenes.size() == 1 ? " scene: " : " scenes: ") +
           godot::String(", ").join(paths);
}

godot::Dictionary target_metadata(const godot::Dictionary &scene) {
    godot::Dictionary target;
    target["scene_path"] = scene.get("scene_path", "");
    target["status"] = scene.get("status", "");
    target["checks_run"] = scene.get("checks_run", 0);
    target["total_issues"] = scene.get("total_issues", 0);
    target["errors"] = scene.get("errors", 0);
    target["warnings"] = scene.get("warnings", 0);
    target["notes"] = scene.get("notes", 0);
    target["shown_issues"] = 0;
    target["omitted_issues"] =
        static_cast<int>(scene.get("total_issues", 0)) +
        static_cast<int>(scene.get("notes", 0));
    if (scene.has("error")) {
        target["error"] = scene["error"];
    }
    if (scene.has("runtime_check")) {
        target["runtime_check"] = scene["runtime_check"];
    }
    return target;
}

godot::String issue_extra_text(const godot::Dictionary &issue) {
    static const char *skip_keys[] = {
        "node", "node_path", "check", "severity", "message"
    };

    godot::PackedStringArray parts;
    godot::Array keys = issue.keys();
    for (int i = 0; i < keys.size(); i++) {
        godot::String key = keys[i];
        bool skip = false;
        for (const char *skip_key : skip_keys) {
            if (key == skip_key) {
                skip = true;
                break;
            }
        }
        if (!skip) {
            parts.append(key + "=" + godot::String(issue[key]));
        }
    }
    if (parts.is_empty()) {
        return "";
    }
    return " (" + godot::String(", ").join(parts) + ")";
}

godot::String issue_bullet(const godot::Dictionary &issue) {
    godot::String severity = issue.get("severity", "");
    godot::String check = issue.get("check", "");
    godot::String node = issue.get("node_path", issue.get("node", ""));
    godot::String message = issue.get("message", "");

    godot::String out = "- " + severity;
    if (!check.is_empty()) {
        out += " (" + check + ")";
    }
    if (!node.is_empty()) {
        out += " " + node + ":";
    }
    out += " " + message;
    out += issue_extra_text(issue);
    return out;
}

godot::Array issues_for_severity(const godot::Dictionary &scene,
                                 const godot::String &severity) {
    godot::Array out;
    godot::Array issues = scene.get("issues", godot::Array());
    for (int i = 0; i < issues.size(); i++) {
        if (issues[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary issue = issues[i];
        if (godot::String(issue.get("severity", "")) == severity) {
            out.append(issue);
        }
    }
    return out;
}

godot::String severity_heading(const godot::String &severity) {
    if (severity == "error") {
        return "### Structural errors";
    }
    if (severity == "warning") {
        return "### Structural warnings";
    }
    return "### Structural notes";
}

godot::String failed_section(const godot::Dictionary &scene, int index) {
    godot::PackedStringArray lines;
    lines.append("## " + scene_label(scene, index));
    lines.append("Status: failed");
    if (scene.has("error")) {
        lines.append("Error:\n" + godot::String(scene.get("error", "")));
    }
    return godot::String("\n").join(lines);
}

godot::String read_text_file(const godot::String &path) {
    if (path.is_empty()) {
        return "";
    }
    godot::Ref<godot::FileAccess> file =
        godot::FileAccess::open(path, godot::FileAccess::READ);
    if (file.is_null()) {
        return "";
    }
    godot::String text = file->get_as_text();
    file->close();
    return text;
}

bool is_runtime_metadata_line(const godot::String &line) {
    return line.begins_with("# Fennara daemon") ||
           line.begins_with("Scene: ") ||
           line.begins_with("Executable: ") ||
           line.begins_with("Working directory: ") ||
           line.begins_with("Args: ") ||
           line == "## Fennara daemon process result" ||
           line.begins_with("Status: ") ||
           line.begins_with("Exit code: ") ||
           line.begins_with("Duration: ") ||
           line.begins_with("Godot Engine ") ||
           line.begins_with("Vulkan ") ||
           line.begins_with("OpenGL ");
}

bool starts_runtime_issue_block(const godot::String &line) {
    return line.begins_with("WARNING:") ||
           line.begins_with("ERROR:") ||
           line.begins_with("SCRIPT ERROR:");
}

bool is_runtime_issue_continuation(const godot::String &line) {
    return line.begins_with("   at:") ||
           line.begins_with("          at:") ||
           line.begins_with("   GDScript backtrace") ||
           line.begins_with("       [") ||
           line.begins_with("          GDScript backtrace") ||
           line.begins_with("              [");
}

godot::String bool_text(bool value) {
    return value ? "yes" : "no";
}

godot::String runtime_status_label(const godot::Dictionary &runtime) {
    godot::String status = runtime.get("status", "");
    if (status == "stopped_after_run_seconds") {
        return "stopped after 3s validation window";
    }
    if (status == "cancelled") {
        return "cancelled";
    }
    if (status == "timeout") {
        return "timed out";
    }
    if (status == "crashed") {
        return "crashed";
    }
    if (status == "failed") {
        return "failed";
    }
    if (status.is_empty() && (bool)runtime.get("killed", false)) {
        return "stopped by Fennara";
    }
    return status.is_empty() ? godot::String("unknown") : status.replace("_", " ");
}

godot::String exit_code_note(const godot::Dictionary &runtime) {
    godot::String status = runtime.get("status", "");
    if (status == "stopped_after_run_seconds" ||
        (bool)runtime.get("killed", false)) {
        return " (process was stopped by Fennara after the validation window; this is not by itself a failure)";
    }
    return "";
}

godot::String block_text(const godot::PackedStringArray &block) {
    return godot::String("\n").join(block);
}

godot::String compact_runtime_log_for_model(const godot::String &raw) {
    godot::PackedStringArray lines = raw.split("\n");
    godot::PackedStringArray out;
    godot::PackedStringArray previous_block;
    int repeat_count = 0;
    bool in_native_backtrace = false;
    int native_frames = 0;
    const int max_blocks = 120;

    auto flush_repeat = [&]() {
        if (previous_block.is_empty()) {
            return;
        }
        godot::String text = block_text(previous_block);
        if (repeat_count > 1 && starts_runtime_issue_block(previous_block[0])) {
            out.append(text + "\n[repeated " +
                       godot::String::num_int64(repeat_count) + "x]");
        } else {
            for (int i = 0; i < repeat_count; i++) {
                out.append(text);
            }
        }
        previous_block.clear();
        repeat_count = 0;
    };

    for (int i = 0; i < lines.size();) {
        godot::String line = lines[i].rstrip("\r");
        if (line.strip_edges().is_empty() || is_runtime_metadata_line(line)) {
            i++;
            continue;
        }
        if (line.begins_with("[") &&
            line.contains("no debug info in PE/COFF executable")) {
            in_native_backtrace = true;
            native_frames++;
            i++;
            continue;
        }
        if (in_native_backtrace) {
            if (line.contains("-- END OF C++ BACKTRACE --")) {
                godot::PackedStringArray block;
                block.append("[native backtrace omitted: " +
                             godot::String::num_int64(native_frames) +
                             " frames without symbols]");
                if (block_text(block) == block_text(previous_block)) {
                    repeat_count++;
                } else {
                    flush_repeat();
                    previous_block = block;
                    repeat_count = 1;
                }
                in_native_backtrace = false;
                native_frames = 0;
            }
            i++;
            continue;
        }

        godot::PackedStringArray block;
        if (line.length() > 1000) {
            line = line.substr(0, 1000) +
                   " ... [line truncated; full output in raw log]";
        }
        block.append(line);
        i++;

        if (starts_runtime_issue_block(line)) {
            while (i < lines.size()) {
                godot::String next = lines[i].rstrip("\r");
                if (starts_runtime_issue_block(next) ||
                    !is_runtime_issue_continuation(next)) {
                    break;
                }
                block.append(next);
                i++;
            }
        }

        if (block_text(block) == block_text(previous_block)) {
            repeat_count++;
            continue;
        }
        flush_repeat();
        previous_block = block;
        repeat_count = 1;
        if (out.size() >= max_blocks) {
            out.append("[runtime output truncated; full output in raw log]");
            break;
        }
    }
    flush_repeat();
    return godot::String("\n").join(out);
}

} // namespace

godot::Dictionary format_validate_scene(const godot::Dictionary &raw_result) {
    bool raw_success = raw_result.get("success", false);
    godot::Array scenes = raw_result.get("scenes", godot::Array());
    godot::Dictionary summary = raw_result.get("summary", godot::Dictionary());
    godot::Dictionary runtime_batch = raw_result.get("runtime_batch", godot::Dictionary());
    int budget_tokens = validate_scene_budget_tokens(scenes.size());

    godot::Array targets;
    godot::PackedStringArray sections;
    int raw_success_count = 0;
    int raw_failure_count = 0;
    bool previewed = false;

    godot::PackedStringArray header;
    header.append("Tool: validate_scene");
    header.append("Status: pending");
    header.append(scenes.size() > 0 ? "Scope: " + scope_for_scenes(scenes) : "Scope: unknown");
    if (!summary.is_empty()) {
        godot::String totals_line =
            "Totals: " +
            godot::String::num_int64(static_cast<int64_t>(summary.get("success_count", 0))) +
            " succeeded, " +
            godot::String::num_int64(static_cast<int64_t>(summary.get("failure_count", 0))) +
            " failed, " +
            godot::String::num_int64(static_cast<int64_t>(summary.get("total_issues", 0))) +
            " issues (" +
            godot::String::num_int64(static_cast<int64_t>(summary.get("errors", 0))) +
            " errors, " +
            godot::String::num_int64(static_cast<int64_t>(summary.get("warnings", 0))) +
            " warnings)";
        int64_t note_total =
            static_cast<int64_t>(summary.get("notes", 0));
        if (note_total > 0) {
            totals_line += ", " + godot::String::num_int64(note_total) + " notes";
        }
        header.append(totals_line);
        if (summary.has("runtime_checked_count")) {
            godot::String runtime_line = "Runtime: ";
            if ((bool)summary.get("runtime_skipped", false)) {
                runtime_line += "skipped";
            } else {
                runtime_line += godot::String(summary.get("runtime_status", "unknown")) +
                    ", ran " +
                    godot::String::num_int64(static_cast<int64_t>(summary.get("runtime_checked_count", 0))) +
                    " scenes headlessly for 3s each";
                int64_t runtime_crashes =
                    static_cast<int64_t>(summary.get("runtime_crash_count", 0));
                int64_t runtime_errors =
                    static_cast<int64_t>(summary.get("runtime_error_count", 0));
                int64_t runtime_warnings =
                    static_cast<int64_t>(summary.get("runtime_warning_count", 0));
                runtime_line += " (" +
                    godot::String::num_int64(runtime_crashes) + " crashes, " +
                    godot::String::num_int64(runtime_errors) + " errors, " +
                    godot::String::num_int64(runtime_warnings) + " warnings)";
            }
            header.append(runtime_line);
        }
    }
    if (scenes.size() == 0 && raw_result.has("error")) {
        header.append("");
        header.append("Error:\n" + godot::String(raw_result.get("error", "")));
    }

    int used_tokens = estimate_tokens(godot::String("\n").join(header));
    int remaining_tokens = budget_tokens - used_tokens;
    int per_scene_budget = scenes.size() > 0 ? remaining_tokens / scenes.size() : remaining_tokens;
    if (per_scene_budget < 1) {
        per_scene_budget = 1;
    }

    for (int i = 0; i < scenes.size(); i++) {
        if (scenes[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary scene = scenes[i];
        godot::String scene_status = scene.get("status", "");
        if (scene_status == "success") {
            raw_success_count++;
        } else {
            raw_failure_count++;
        }

        int target_index = targets.size();
        targets.append(target_metadata(scene));

        if (scene_status != "success") {
            sections.append(failed_section(scene, i));
            if (targets[target_index].get_type() == godot::Variant::DICTIONARY) {
                godot::Dictionary target = targets[target_index];
                target["shown_issues"] = 0;
                target["omitted_issues"] = 0;
                targets[target_index] = target;
            }
            continue;
        }

        godot::PackedStringArray lines;
        lines.append("## " + scene_label(scene, i));
        lines.append("Status: success");
        lines.append(
            "Checks: " + godot::String::num_int64(static_cast<int64_t>(scene.get("checks_run", 0)))
        );
        lines.append(
            "Structural issues: " + godot::String::num_int64(static_cast<int64_t>(scene.get("total_issues", 0))) +
            " (" + godot::String::num_int64(static_cast<int64_t>(scene.get("errors", 0))) +
            " errors, " + godot::String::num_int64(static_cast<int64_t>(scene.get("warnings", 0))) +
            " warnings)"
        );
        int note_count = static_cast<int>(scene.get("notes", 0));
        if (note_count > 0) {
            lines.append(
                "Structural notes: " +
                godot::String::num_int64(static_cast<int64_t>(note_count))
            );
        }

        int detail_budget = per_scene_budget - estimate_tokens(godot::String("\n").join(lines));
        if (detail_budget < 1) {
            detail_budget = 1;
        }

        int shown = 0;
        for (int severity_index = 0; severity_index < 3; severity_index++) {
            godot::String severity = severity_index == 0 ? "error" :
                (severity_index == 1 ? "warning" : "info");
            godot::Array issues = issues_for_severity(scene, severity);
            if (issues.is_empty()) {
                continue;
            }

            godot::PackedStringArray bullets;
            for (int issue_index = 0; issue_index < issues.size(); issue_index++) {
                godot::Dictionary issue = issues[issue_index];
                godot::String bullet = issue_bullet(issue);
                int tokens = estimate_tokens(bullet);
                if (detail_budget - tokens < 0) {
                    previewed = true;
                    continue;
                }
                bullets.append(bullet);
                detail_budget -= tokens;
                shown++;
            }
            if (!bullets.is_empty()) {
                lines.append("");
                lines.append(severity_heading(severity));
                lines.append(godot::String("\n").join(bullets));
            }
        }

        int total_issues = static_cast<int>(scene.get("total_issues", 0));
        int total_reported_items = total_issues + note_count;
        if (shown < total_reported_items) {
            previewed = true;
            lines.append("");
            lines.append("Omitted: additional validation issues or notes exceeded model-facing size limit.");
        }

        godot::Dictionary target = targets[target_index];
        target["shown_issues"] = shown;
        target["omitted_issues"] =
            total_reported_items > shown ? total_reported_items - shown : 0;
        targets[target_index] = target;

        if (scene.has("runtime_check")) {
            godot::Variant runtime_var = scene["runtime_check"];
            lines.append("");
            lines.append("### 3s headless runtime check");
            if (runtime_var.get_type() == godot::Variant::DICTIONARY) {
                godot::Dictionary runtime = runtime_var;
                lines.append("Runtime status: " + runtime_status_label(runtime));
                lines.append("Crash detected: " +
                             bool_text((bool)runtime.get("crashed", false)));
                lines.append("Runtime errors detected: " +
                             bool_text((bool)runtime.get("has_error", false)));
                lines.append("Runtime warnings detected: " +
                             bool_text((bool)runtime.get("has_warning", false)));
                lines.append("Process exit code: " +
                             godot::String::num_int64(static_cast<int>(
                                 runtime.get("exit_code", 0))) +
                             exit_code_note(runtime));
                lines.append("Observed duration: " +
                             godot::String::num(static_cast<double>(
                                 runtime.get("duration_seconds", 0.0)), 3) +
                             "s");
                godot::String raw_log = runtime.get("raw_log_path", "");
                godot::String compacted =
                    compact_runtime_log_for_model(read_text_file(raw_log));
                if (compacted.strip_edges().is_empty()) {
                    lines.append(
                        "During this brief 3s headless run, Fennara captured no runtime errors, warnings, or crash output.");
                } else {
                    lines.append("Captured output from the 3s headless run:");
                    lines.append(compacted);
                }
            } else if (godot::String(runtime_var) == "skipped") {
                lines.append("Skipped: " +
                             godot::String(scene.get("runtime_skip_reason", "")));
            } else if (godot::String(runtime_var) == "failed") {
                lines.append("Failed: " +
                             godot::String(scene.get("runtime_error", "")));
            }
        }
        sections.append(godot::String("\n").join(lines));
    }

    godot::PackedStringArray saved_lines;
    godot::String artifact_dir = summary.get("artifact_dir", "");
    godot::String artifact_abs = summary.get("artifact_absolute_dir", "");
    godot::String result_json = summary.get("result_json_path", "");
    godot::String result_json_abs = summary.get("result_json_absolute_path", "");
    godot::String raw_logs = summary.get("runtime_raw_logs_dir", "");
    godot::String raw_logs_abs = summary.get("runtime_raw_logs_absolute_dir", "");
    if (!artifact_dir.is_empty() || !result_json.is_empty() || !raw_logs.is_empty()) {
        saved_lines.append("## Saved result/logs");
        if (!artifact_dir.is_empty()) {
            saved_lines.append("- Result artifacts: " + artifact_dir);
        }
        if (!artifact_abs.is_empty()) {
            saved_lines.append("- Result artifacts absolute: " + artifact_abs);
        }
        if (!result_json.is_empty()) {
            saved_lines.append("- Full raw result JSON: " + result_json);
        }
        if (!result_json_abs.is_empty()) {
            saved_lines.append("- Full raw result JSON absolute: " + result_json_abs);
        }
        if (!raw_logs.is_empty()) {
            saved_lines.append("- Runtime raw logs: " + raw_logs);
        }
        if (!raw_logs_abs.is_empty()) {
            saved_lines.append("- Runtime raw logs absolute: " + raw_logs_abs);
        }
        sections.append(godot::String("\n").join(saved_lines));
    }

    godot::String status = summary.get("status", "");
    if (status.is_empty()) {
        status = "success";
    }
    if (scenes.size() == 0 && raw_result.has("error")) {
        status = "failed";
    } else if (status == "success") {
        if (raw_failure_count > 0 && raw_success_count == 0) {
            status = "failed";
        } else if (raw_failure_count > 0 || previewed) {
            status = "partial";
        }
    }
    header.set(1, "Status: " + status);
    sections.insert(0, godot::String("\n").join(header));

    godot::Dictionary metadata = make_base_metadata("validate_scene", "validate_scene-md-v1", status);
    metadata["targets"] = targets;
    metadata["budget_tokens"] = validate_scene_budget_tokens(scenes.size());
    metadata["previewed"] = previewed;
    if (summary.has("runtime_compacted_log_path")) {
        metadata["runtime_compacted_log_path"] = summary.get("runtime_compacted_log_path", "");
        metadata["runtime_compacted_log_absolute_path"] =
            summary.get("runtime_compacted_log_absolute_path", "");
        metadata["runtime_results_path"] = summary.get("runtime_results_path", "");
        metadata["runtime_results_absolute_path"] =
            summary.get("runtime_results_absolute_path", "");
        metadata["runtime_raw_logs_dir"] = summary.get("runtime_raw_logs_dir", "");
        metadata["runtime_raw_logs_absolute_dir"] =
            summary.get("runtime_raw_logs_absolute_dir", "");
    }
    metadata["artifact_dir"] = summary.get("artifact_dir", "");
    metadata["artifact_absolute_dir"] = summary.get("artifact_absolute_dir", "");
    metadata["result_json_path"] = summary.get("result_json_path", "");
    metadata["result_json_absolute_path"] = summary.get("result_json_absolute_path", "");
    return make_envelope(godot::String("\n\n").join(sections), metadata, raw_success);
}

} // namespace fennara::tool_results
