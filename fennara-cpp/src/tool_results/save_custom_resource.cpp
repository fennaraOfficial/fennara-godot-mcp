#include "fennara/tool_results/save_custom_resource.hpp"

#include "fennara/tool_results/envelope.hpp"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace fennara::tool_results {

namespace {

void append_array_items(godot::PackedStringArray &lines,
                        const godot::Array &items) {
    for (int i = 0; i < items.size(); i++) {
        lines.append("- " + godot::String(items[i]));
    }
}

} // namespace

godot::Dictionary format_save_custom_resource(const godot::Dictionary &raw_result) {
    bool raw_success = raw_result.get("success", false);
    godot::String status = raw_success ? "success" : "failed";
    godot::Array properties_set = raw_result.get("properties_set", godot::Array());
    godot::Array warnings = raw_result.get("warnings", godot::Array());

    if (raw_success && !warnings.is_empty()) {
        status = "partial";
    }

    godot::PackedStringArray lines;
    lines.append("Tool: save_custom_resource");
    lines.append("Status: " + status);
    lines.append("Resource: " + godot::String(raw_result.get("resource_path", "")));
    lines.append("Type: " + godot::String(raw_result.get("resource_type", "")));
    if (raw_result.has("script_path") && !godot::String(raw_result.get("script_path", "")).is_empty()) {
        lines.append("Script: " + godot::String(raw_result.get("script_path", "")));
    }
    lines.append("Properties set: " + godot::String::num_int64(properties_set.size()));
    if (raw_result.has("message")) {
        lines.append("Message: " + godot::String(raw_result.get("message", "")));
    }
    if (raw_result.has("error")) {
        lines.append("Error: " + godot::String(raw_result.get("error", "")));
    }

    if (!properties_set.is_empty()) {
        lines.append("");
        lines.append("## Properties set");
        append_array_items(lines, properties_set);
    }

    if (!warnings.is_empty()) {
        lines.append("");
        lines.append("## Warnings");
        append_array_items(lines, warnings);
    }

    godot::Dictionary metadata = make_base_metadata(
        "save_custom_resource", "save_custom_resource-md-v1", status);
    metadata["resource_path"] = raw_result.get("resource_path", "");
    metadata["resource_type"] = raw_result.get("resource_type", "");
    metadata["script_path"] = raw_result.get("script_path", "");
    metadata["set_property_count"] = properties_set.size();
    metadata["warning_count"] = warnings.size();
    metadata["previewed"] = false;
    return make_envelope(godot::String("\n").join(lines), metadata, raw_success);
}

} // namespace fennara::tool_results
