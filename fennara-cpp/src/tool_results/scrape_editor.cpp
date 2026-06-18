#include "fennara/tool_results/scrape_editor.hpp"

#include "fennara/tool_results/envelope.hpp"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara::tool_results {
namespace {

void append_issues(godot::PackedStringArray &lines, const godot::Array &errors) {
    if (errors.is_empty()) {
        lines.append("Debugger issues: none found");
        return;
    }

    lines.append("Debugger issues:");
    int count = errors.size() < 6 ? errors.size() : 6;
    for (int i = 0; i < count; i++) {
        if (errors[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary issue = errors[i];
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

    if (errors.size() > count) {
        lines.append(
            "- ... " +
            godot::String::num_int64(errors.size() - count) +
            " more debugger issue(s) omitted");
    }
}

} // namespace

godot::Dictionary format_scrape_editor(const godot::Dictionary &raw_result) {
    godot::String status = raw_result.get("status", "unknown");
    godot::String target = raw_result.get("target", "");
    godot::Dictionary summary = raw_result.get("summary", godot::Dictionary());
    godot::Array errors = raw_result.get("errors", godot::Array());

    godot::PackedStringArray lines;
    lines.append("Tool: scrape_editor");
    lines.append("Status: " + status);
    lines.append("Target: " + target);
    lines.append("Source: " + godot::String(raw_result.get("source", "")));

    godot::String tree_path = raw_result.get("tree_path", "");
    if (!tree_path.is_empty()) {
        lines.append("Tree path: " + tree_path);
    }

    lines.append(
        "Events: " +
        godot::String::num_int64(static_cast<int64_t>(
            summary.get("total_event_count", errors.size()))) +
        ", unique issues: " +
        godot::String::num_int64(static_cast<int64_t>(
            summary.get("unique_issue_count", errors.size()))));

    if ((bool)summary.get("truncated", false)) {
        lines.append("Note: debugger snapshot was truncated.");
    }

    lines.append("");
    append_issues(lines, errors);

    godot::Dictionary metadata =
        make_base_metadata("scrape_editor", "scrape_editor-md-v1", status);
    metadata["summary"] = summary;
    metadata["target"] = target;
    metadata["source"] = raw_result.get("source", "");
    metadata["tree_path"] = tree_path;
    metadata["errors"] = errors;
    metadata["error_count"] = errors.size();

    return make_envelope(
        godot::String("\n").join(lines),
        metadata,
        raw_result.get("success", false));
}

} // namespace fennara::tool_results
