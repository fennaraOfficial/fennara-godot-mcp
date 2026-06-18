#include "fennara/tool_results/write_or_update_file.hpp"

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

godot::String severity_heading(const godot::String &severity) {
    if (severity == "error") return "### Errors";
    if (severity == "warning") return "### Warnings";
    if (severity == "info") return "### Info";
    return "### Hints";
}

godot::Array diagnostics_for_severity(const godot::Array &diagnostics,
                                      const godot::String &severity) {
    godot::Array out;
    for (int i = 0; i < diagnostics.size(); i++) {
        if (diagnostics[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary diag = diagnostics[i];
        if (godot::String(diag.get("severity", "")) == severity) {
            out.append(diag);
        }
    }
    return out;
}

int append_diagnostics(godot::PackedStringArray &lines,
                       const godot::Array &diagnostics,
                       int &remaining_tokens) {
    int shown = 0;
    for (int severity_index = 0; severity_index < 4; severity_index++) {
        godot::String severity = severity_index == 0 ? "error" :
            (severity_index == 1 ? "warning" :
             (severity_index == 2 ? "info" : "hint"));
        godot::Array matching = diagnostics_for_severity(diagnostics, severity);
        if (matching.is_empty()) {
            continue;
        }

        godot::PackedStringArray bullets;
        for (int i = 0; i < matching.size(); i++) {
            godot::Dictionary diag = matching[i];
            godot::String bullet = diagnostic_line(diag);
            int tokens = estimate_tokens(bullet);
            if (remaining_tokens - tokens < 0) {
                continue;
            }
            bullets.append(bullet);
            remaining_tokens -= tokens;
            shown++;
        }

        if (!bullets.is_empty()) {
            lines.append("");
            lines.append(severity_heading(severity));
            lines.append(godot::String("\n").join(bullets));
        }
    }
    return shown;
}

} // namespace

godot::Dictionary format_write_or_update_file(
    const godot::Dictionary &raw_result) {
    bool raw_success = raw_result.get("success", false);
    godot::String status = raw_result.get("status", raw_success ? "success" : "failed");
    bool diagnostic_success = raw_result.get("diagnostic_success", false);
    godot::Array diagnostics = raw_result.get("diagnostics", godot::Array());
    int total_errors = static_cast<int>(raw_result.get("total_errors", 0));
    int total_warnings = static_cast<int>(raw_result.get("total_warnings", 0));
    int total_info = static_cast<int>(raw_result.get("total_info", 0));
    int total_hints = static_cast<int>(raw_result.get("total_hints", 0));

    if (raw_success && diagnostics.size() > 0) {
        status = "partial";
    }

    godot::PackedStringArray lines;
    lines.append("Tool: write_or_update_file");
    lines.append("Status: " + status);
    lines.append("Mode: " + godot::String(raw_result.get("mode", "")));
    lines.append("File: " + godot::String(raw_result.get("file_path", "")));

    if (raw_result.has("line_count")) {
        lines.append("Lines: " + godot::String::num_int64(
            static_cast<int64_t>(raw_result.get("line_count", 0))));
    }
    if (raw_result.has("created")) {
        lines.append("Created: " + bool_text(raw_result.get("created", false)));
    }
    if (raw_result.has("replacements_made")) {
        lines.append("Replacements made: " + godot::String::num_int64(
            static_cast<int64_t>(raw_result.get("replacements_made", 0))));
    }
    if (raw_result.has("attached_to_scene")) {
        lines.append("Attached to: " +
                     godot::String(raw_result.get("attached_to_scene", "")) +
                     " node " +
                     godot::String(raw_result.get("attached_to_node", "")));
    }
    if (raw_result.has("attach_warning")) {
        lines.append("Attach warning: " +
                     godot::String(raw_result.get("attach_warning", "")));
    }
    if (raw_result.has("attach_error")) {
        lines.append("Attach error: " +
                     godot::String(raw_result.get("attach_error", "")));
    }
    if (raw_result.has("error")) {
        lines.append("Error: " + godot::String(raw_result.get("error", "")));
    }
    if (raw_result.has("resolution")) {
        lines.append("Resolution: " + godot::String(raw_result.get("resolution", "")));
    }

    if (diagnostic_success || raw_result.has("diagnostics")) {
        godot::String diagnostic_label =
            godot::String(raw_result.get("diagnostic_mode", "")) == "shader_parser"
                ? "Shader diagnostics"
                : "Script diagnostics";
        lines.append(diagnostic_label + ": " +
                     godot::String::num_int64(total_errors) + " errors, " +
                     godot::String::num_int64(total_warnings) + " warnings, " +
                     godot::String::num_int64(total_info) + " info, " +
                     godot::String::num_int64(total_hints) + " hints");
    }

    int remaining_tokens = kBudgetTokens - estimate_tokens(godot::String("\n").join(lines));
    if (remaining_tokens < 1) {
        remaining_tokens = 1;
    }

    bool previewed = false;
    int shown_diagnostics = 0;
    if (!diagnostics.is_empty()) {
        lines.append("");
        lines.append(godot::String(raw_result.get("diagnostic_mode", "")) == "shader_parser"
                         ? "## Shader diagnostics"
                         : "## Script diagnostics");
        shown_diagnostics = append_diagnostics(lines, diagnostics, remaining_tokens);
        if (shown_diagnostics < diagnostics.size()) {
            previewed = true;
            lines.append("");
            lines.append("Omitted: additional diagnostics exceeded model-facing size limit.");
        }
    }

    if (previewed && status == "success") {
        status = "partial";
        lines.set(1, "Status: partial");
    }

    godot::Dictionary metadata = make_base_metadata(
        "write_or_update_file", "write_or_update_file-md-v1", status);
    metadata["file_path"] = raw_result.get("file_path", "");
    metadata["block_reason"] = raw_result.get("block_reason", "");
    metadata["blocked_path"] = raw_result.get("blocked_path", "");
    metadata["blocked_addon_root"] = raw_result.get("blocked_addon_root", "");
    metadata["mode"] = raw_result.get("mode", "");
    metadata["line_count"] = raw_result.get("line_count", 0);
    metadata["created"] = raw_result.get("created", false);
    metadata["replacements_made"] = raw_result.get("replacements_made", 0);
    metadata["diagnostic_success"] = diagnostic_success;
    metadata["total_errors"] = total_errors;
    metadata["total_warnings"] = total_warnings;
    metadata["total_info"] = total_info;
    metadata["total_hints"] = total_hints;
    metadata["diagnostic_count"] = diagnostics.size();
    metadata["shown_diagnostics"] = shown_diagnostics;
    metadata["omitted_diagnostics"] = diagnostics.size() - shown_diagnostics;
    metadata["budget_tokens"] = kBudgetTokens;
    metadata["previewed"] = previewed;
    return make_envelope(godot::String("\n").join(lines), metadata, raw_success);
}

} // namespace fennara::tool_results
