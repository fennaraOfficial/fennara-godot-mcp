#include "fennara/tool_results/run_scene_edit_script.hpp"

#include "fennara/tool_results/envelope.hpp"
#include "fennara/tool_results/markdown.hpp"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara::tool_results {

namespace {

constexpr int kBudgetTokens = 12000;

godot::String bool_text(bool value) {
    return value ? "true" : "false";
}

godot::String diagnostic_line(const godot::Dictionary &diag) {
    int64_t line = static_cast<int64_t>(diag.get("line", 0));
    int64_t column = static_cast<int64_t>(diag.get("column", 0));
    godot::String severity = diag.get("severity", "");
    godot::String message = diag.get("message", "");
    return "- line " + godot::String::num_int64(line) + ":" +
           godot::String::num_int64(column) + " " + severity + ": " + message;
}

godot::String runtime_error_line(const godot::Dictionary &entry) {
    godot::String source = entry.get("source", "");
    godot::String message = entry.get("message", "");
    if (source.is_empty()) {
        return "- " + message;
    }
    return "- " + source + ": " + message;
}

int append_budgeted_lines(godot::PackedStringArray &lines,
                          const godot::PackedStringArray &items,
                          int &remaining_tokens) {
    int shown = 0;
    for (int i = 0; i < items.size(); i++) {
        int tokens = estimate_tokens(items[i]);
        if (remaining_tokens - tokens < 0) {
            continue;
        }
        lines.append(items[i]);
        remaining_tokens -= tokens;
        shown++;
    }
    return shown;
}

godot::PackedStringArray diagnostics_lines(const godot::Array &diagnostics) {
    godot::PackedStringArray out;
    for (int i = 0; i < diagnostics.size(); i++) {
        if (diagnostics[i].get_type() == godot::Variant::DICTIONARY) {
            godot::Dictionary diag = diagnostics[i];
            out.append(diagnostic_line(diag));
        } else {
            out.append("- " + godot::String(diagnostics[i]));
        }
    }
    return out;
}

godot::PackedStringArray runtime_error_lines(const godot::Array &errors) {
    godot::PackedStringArray out;
    for (int i = 0; i < errors.size(); i++) {
        if (errors[i].get_type() == godot::Variant::DICTIONARY) {
            godot::Dictionary entry = errors[i];
            out.append(runtime_error_line(entry));
        } else {
            out.append("- " + godot::String(errors[i]));
        }
    }
    return out;
}

godot::PackedStringArray log_lines(const godot::Array &logs) {
    godot::PackedStringArray out;
    for (int i = 0; i < logs.size(); i++) {
        out.append("- " + godot::String(logs[i]));
    }
    return out;
}

godot::PackedStringArray validation_lines(const godot::Dictionary &validation) {
    godot::PackedStringArray out;
    godot::Array issues = validation.get("issues", godot::Array());
    for (int i = 0; i < issues.size(); i++) {
        if (issues[i].get_type() != godot::Variant::DICTIONARY) {
            out.append("- " + godot::String(issues[i]));
            continue;
        }
        godot::Dictionary issue = issues[i];
        godot::String severity = issue.get("severity", "");
        godot::String check = issue.get("check", "");
        godot::String node = issue.get("node_path", issue.get("node", ""));
        godot::String message = issue.get("message", "");
        godot::String line = "- " + severity;
        if (!check.is_empty()) {
            line += " [" + check + "]";
        }
        if (!node.is_empty()) {
            line += " " + node + ":";
        }
        line += " " + message;
        out.append(line);
    }
    return out;
}

} // namespace

godot::Dictionary format_run_scene_edit_script(const godot::Dictionary &raw_result) {
    bool raw_success = raw_result.get("success", false);
    bool runtime_safe = raw_result.get("runtime_safe", true);
    bool validation_failed = raw_success && !runtime_safe;
    godot::String status = validation_failed ? "validation_failed" :
        (raw_success ? "success" : "failed");
    godot::Array logs = raw_result.get("logs", godot::Array());
    godot::Array runtime_errors = raw_result.get("runtime_errors", godot::Array());
    godot::Array diagnostics = raw_result.get("script_diagnostics", godot::Array());
    int total_errors = static_cast<int>(raw_result.get("total_errors", 0));
    int total_warnings = static_cast<int>(raw_result.get("total_warnings", 0));

    godot::PackedStringArray lines;
    lines.append("Tool: run_scene_edit_script");
    lines.append("Status: " + status);
    lines.append("Scene: " + godot::String(raw_result.get("scene_path", "")));
    lines.append("Script: " + godot::String(raw_result.get("script_path", "")));
    lines.append("Scene created: " + bool_text(raw_result.get("scene_created", false)));
    lines.append("Scene saved: " + bool_text(raw_result.get("scene_saved", false)));
    lines.append("Modified: " + bool_text(raw_result.get("modified", false)));
    if (raw_result.has("runtime_safe")) {
        lines.append("Runtime safe: " + bool_text(runtime_safe));
    }
    godot::String diagnostic_mode = raw_result.get("diagnostic_mode", "");
    if (!diagnostic_mode.is_empty()) {
        lines.append("Diagnostic mode: " + diagnostic_mode);
    }
    godot::String note = raw_result.get("scene_status_note", "");
    if (!note.is_empty()) {
        lines.append("Scene status: " + note);
    }
    if (raw_result.has("error")) {
        lines.append("Error: " + godot::String(raw_result.get("error", "")));
    }
    lines.append("Script diagnostics: " +
                 godot::String::num_int64(total_errors) + " errors, " +
                 godot::String::num_int64(total_warnings) + " warnings");
    godot::String diagnostic_fallback = raw_result.get("diagnostic_fallback", "");
    if (!diagnostic_fallback.is_empty()) {
        lines.append("Diagnostic fallback: " + diagnostic_fallback);
    }
    godot::String diagnostic_error = raw_result.get("diagnostic_error", "");
    if (!diagnostic_error.is_empty()) {
        lines.append("Diagnostic note: " + diagnostic_error);
    }

    int remaining_tokens = kBudgetTokens - estimate_tokens(godot::String("\n").join(lines));
    if (remaining_tokens < 1) {
        remaining_tokens = 1;
    }

    bool previewed = false;
    int shown_diagnostics = 0;
    int shown_runtime_errors = 0;
    int shown_logs = 0;
    int shown_validation_issues = 0;

    godot::PackedStringArray diag_lines = diagnostics_lines(diagnostics);
    if (!diag_lines.is_empty()) {
        lines.append("");
        lines.append("## Script diagnostics");
        shown_diagnostics = append_budgeted_lines(lines, diag_lines, remaining_tokens);
        if (shown_diagnostics < diag_lines.size()) {
            previewed = true;
            lines.append("Omitted: additional diagnostics exceeded model-facing size limit.");
        }
    }

    godot::PackedStringArray error_lines = runtime_error_lines(runtime_errors);
    if (!error_lines.is_empty()) {
        lines.append("");
        lines.append("## Runtime errors");
        shown_runtime_errors = append_budgeted_lines(lines, error_lines, remaining_tokens);
        if (shown_runtime_errors < error_lines.size()) {
            previewed = true;
            lines.append("Omitted: additional runtime errors exceeded model-facing size limit.");
        }
    }

    godot::PackedStringArray run_logs = log_lines(logs);
    if (!run_logs.is_empty()) {
        lines.append("");
        lines.append("## Logs");
        shown_logs = append_budgeted_lines(lines, run_logs, remaining_tokens);
        if (shown_logs < run_logs.size()) {
            previewed = true;
            lines.append("Omitted: additional logs exceeded model-facing size limit.");
        }
    }

    if (raw_result.has("validation") &&
        raw_result["validation"].get_type() == godot::Variant::DICTIONARY) {
        godot::Dictionary validation = raw_result["validation"];
        lines.append("");
        lines.append("## Scene validation");
        godot::String source = validation.get("source", "");
        if (!source.is_empty()) {
            lines.append("Source: " + source);
        }
        lines.append("Checks: " + godot::String::num_int64(static_cast<int64_t>(validation.get("checks_run", 0))));
        lines.append("Issues: " + godot::String::num_int64(static_cast<int64_t>(validation.get("total_issues", 0))) +
                     " (" + godot::String::num_int64(static_cast<int64_t>(validation.get("errors", 0))) +
                     " errors, " + godot::String::num_int64(static_cast<int64_t>(validation.get("warnings", 0))) +
                     " warnings)");
        godot::PackedStringArray issue_lines = validation_lines(validation);
        shown_validation_issues = append_budgeted_lines(lines, issue_lines, remaining_tokens);
        if (shown_validation_issues < issue_lines.size()) {
            previewed = true;
            lines.append("Omitted: additional validation issues exceeded model-facing size limit.");
        }
    }

    godot::Dictionary metadata = make_base_metadata(
        "run_scene_edit_script", "run_scene_edit_script-md-v1", status);
    metadata["scene_path"] = raw_result.get("scene_path", "");
    metadata["script_path"] = raw_result.get("script_path", "");
    metadata["scene_created"] = raw_result.get("scene_created", false);
    metadata["scene_saved"] = raw_result.get("scene_saved", false);
    metadata["modified"] = raw_result.get("modified", false);
    metadata["runtime_safe"] = runtime_safe;
    metadata["total_errors"] = total_errors;
    metadata["total_warnings"] = total_warnings;
    metadata["diagnostic_mode"] = diagnostic_mode;
    metadata["diagnostic_fallback"] = diagnostic_fallback;
    metadata["runtime_error_count"] = runtime_errors.size();
    metadata["shown_runtime_errors"] = shown_runtime_errors;
    metadata["log_count"] = logs.size();
    metadata["shown_logs"] = shown_logs;
    metadata["diagnostic_count"] = diagnostics.size();
    metadata["shown_diagnostics"] = shown_diagnostics;
    metadata["shown_validation_issues"] = shown_validation_issues;
    metadata["budget_tokens"] = kBudgetTokens;
    metadata["previewed"] = previewed;
    return make_envelope(godot::String("\n").join(lines), metadata, raw_success);
}

} // namespace fennara::tool_results
