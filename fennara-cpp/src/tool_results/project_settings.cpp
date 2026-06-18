#include "fennara/tool_results/project_settings.hpp"

#include "fennara/tool_results/envelope.hpp"
#include "fennara/tool_results/markdown.hpp"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace fennara::tool_results {

namespace {

constexpr int kBudgetTokens = 12000;

int append_budgeted_items(godot::PackedStringArray &lines,
                          const godot::Array &items,
                          int &remaining_tokens) {
    int shown = 0;
    for (int i = 0; i < items.size(); i++) {
        godot::String line = "- " + godot::String(items[i]);
        int tokens = estimate_tokens(line);
        if (remaining_tokens - tokens < 0) {
            continue;
        }
        lines.append(line);
        remaining_tokens -= tokens;
        shown++;
    }
    return shown;
}

} // namespace

godot::Dictionary format_project_settings(const godot::Dictionary &raw_result) {
    bool raw_success = raw_result.get("success", false);
    godot::String status = raw_success ? "success" : "failed";
    godot::String action = raw_result.get("action", "");
    godot::Array settings = raw_result.get("settings", godot::Array());

    godot::PackedStringArray lines;
    lines.append("Tool: project_settings");
    lines.append("Status: " + status);
    lines.append("Action: " + action);
    if (raw_result.has("key")) {
        lines.append("Key: " + godot::String(raw_result.get("key", "")));
    }
    if (raw_result.has("prefix") && !godot::String(raw_result.get("prefix", "")).is_empty()) {
        lines.append("Prefix: " + godot::String(raw_result.get("prefix", "")));
    }
    if (raw_result.has("query") && !godot::String(raw_result.get("query", "")).is_empty()) {
        lines.append("Query: " + godot::String(raw_result.get("query", "")));
    }
    if (raw_result.has("source")) {
        lines.append("Source: " + godot::String(raw_result.get("source", "")));
    }
    if (raw_result.has("value")) {
        lines.append("Value: " + godot::String(raw_result.get("value", "")));
    }
    if (raw_result.has("event_count")) {
        lines.append("Input events: " + godot::String::num_int64(
            static_cast<int64_t>(raw_result.get("event_count", 0))));
    }
    if (raw_result.has("deadzone")) {
        lines.append("Deadzone: " + godot::String::num(
            static_cast<double>(raw_result.get("deadzone", 0.0)), 2));
    }
    if (raw_result.has("count")) {
        lines.append("Settings matched: " + godot::String::num_int64(
            static_cast<int64_t>(raw_result.get("count", 0))));
    }
    if (raw_result.has("total_count") &&
        static_cast<int64_t>(raw_result.get("total_count", 0)) !=
            static_cast<int64_t>(raw_result.get("count", 0))) {
        lines.append("Total matches: " + godot::String::num_int64(
            static_cast<int64_t>(raw_result.get("total_count", 0))));
    }
    if (raw_result.has("truncated_message")) {
        lines.append("Truncated: " + godot::String(raw_result.get("truncated_message", "")));
    }
    if (raw_result.has("output")) {
        lines.append("Output: " + godot::String(raw_result.get("output", "")));
    }
    if (raw_result.has("error")) {
        lines.append("Error: " + godot::String(raw_result.get("error", "")));
    }

    int remaining_tokens = kBudgetTokens - estimate_tokens(godot::String("\n").join(lines));
    if (remaining_tokens < 1) {
        remaining_tokens = 1;
    }

    bool previewed = false;
    int shown_settings = 0;
    if (!settings.is_empty()) {
        lines.append("");
        lines.append("## Settings");
        shown_settings = append_budgeted_items(lines, settings, remaining_tokens);
        if (shown_settings < settings.size()) {
            previewed = true;
            lines.append("");
            lines.append("Omitted: additional settings exceeded model-facing size limit.");
        }
    }

    if (previewed && status == "success") {
        status = "partial";
        lines.set(1, "Status: partial");
    }

    godot::Dictionary metadata = make_base_metadata(
        "project_settings", "project_settings-md-v1", status);
    metadata["action"] = action;
    metadata["key"] = raw_result.get("key", "");
    metadata["prefix"] = raw_result.get("prefix", "");
    metadata["query"] = raw_result.get("query", "");
    metadata["count"] = raw_result.get("count", settings.size());
    metadata["total_count"] = raw_result.get("total_count", raw_result.get("count", settings.size()));
    metadata["source"] = raw_result.get("source", "");
    metadata["shown_settings"] = shown_settings;
    metadata["omitted_settings"] = settings.size() - shown_settings;
    metadata["budget_tokens"] = kBudgetTokens;
    metadata["previewed"] = previewed;
    return make_envelope(godot::String("\n").join(lines), metadata, raw_success);
}

} // namespace fennara::tool_results
