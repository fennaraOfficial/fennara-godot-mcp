#include "fennara/tool_results/get_node_properties.hpp"

#include "fennara/tool_results/envelope.hpp"
#include "fennara/tool_results/markdown.hpp"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara::tool_results {

namespace {

int node_properties_budget_tokens(int target_count) {
    if (target_count <= 1) return 10000;
    if (target_count == 2) return 14000;
    if (target_count == 3) return 18000;
    if (target_count == 4) return 22000;
    return 26000;
}

godot::String target_label(const godot::Dictionary &node, int index) {
    godot::String scene_path = node.get("scene_path", "");
    godot::String node_path = node.get("node_path", "");
    if (!scene_path.is_empty() || !node_path.is_empty()) {
        return scene_path + " :: " + (node_path.is_empty() ? "." : node_path);
    }
    return "targets[" + godot::String::num_int64(index) + "]";
}

godot::String scope_for_nodes(const godot::Array &nodes) {
    godot::PackedStringArray labels;
    for (int i = 0; i < nodes.size(); i++) {
        if (nodes[i].get_type() != godot::Variant::DICTIONARY) {
            labels.append("targets[" + godot::String::num_int64(i) + "]");
            continue;
        }
        godot::Dictionary node = nodes[i];
        labels.append(target_label(node, i));
    }
    return godot::String::num_int64(nodes.size()) +
           (nodes.size() == 1 ? " target: " : " targets: ") +
           godot::String(", ").join(labels);
}

godot::Dictionary target_metadata(const godot::Dictionary &node) {
    godot::Dictionary target;
    godot::String text = node.get("properties_text", "");
    target["scene_path"] = node.get("scene_path", "");
    target["node_path"] = node.get("node_path", "");
    target["resolved_path"] = node.get("resolved_path", "");
    target["status"] = node.get("status", "");
    target["node_name"] = node.get("node_name", "");
    target["node_type"] = node.get("node_type", "");
    target["properties_line_count"] = node.get("properties_line_count", 0);
    target["shown_property_tokens"] = 0;
    target["omitted_property_tokens"] = estimate_tokens(text);
    if (node.has("error")) {
        target["error"] = node["error"];
    }
    return target;
}

godot::String success_section(const godot::Dictionary &node,
                              int index,
                              const godot::String &text,
                              bool previewed,
                              int shown_tokens,
                              int total_tokens) {
    godot::PackedStringArray lines;
    lines.append("## " + target_label(node, index));
    lines.append(godot::String("Status: ") + (previewed ? "partial" : "success"));

    godot::String node_name = node.get("node_name", "");
    godot::String node_type = node.get("node_type", "");
    if (!node_name.is_empty() || !node_type.is_empty()) {
        godot::String node_line = "Node:";
        if (!node_name.is_empty()) {
            node_line += " " + node_name;
        }
        if (!node_type.is_empty()) {
            node_line += " (" + node_type + ")";
        }
        lines.append(node_line);
    }

    godot::String resolved_path = node.get("resolved_path", "");
    if (!resolved_path.is_empty()) {
        lines.append("Resolved path: " + resolved_path);
    }

    int64_t property_lines = static_cast<int64_t>(node.get("properties_line_count", 0));
    if (property_lines > 0) {
        lines.append("Property lines: " + godot::String::num_int64(property_lines));
    }
    if (previewed) {
        lines.append(
            "Shown: preview, about " + godot::String::num_int64(shown_tokens) +
            " of " + godot::String::num_int64(total_tokens) + " estimated tokens"
        );
        lines.append("Omitted: remaining node properties exceeded model-facing size limit");
    }

    lines.append("");
    lines.append(code_fence(text, "text"));
    return godot::String("\n").join(lines);
}

godot::String failed_section(const godot::Dictionary &node, int index) {
    godot::PackedStringArray lines;
    lines.append("## " + target_label(node, index));
    lines.append("Status: failed");
    if (node.has("error")) {
        lines.append("Error:\n" + godot::String(node.get("error", "")));
    }
    return godot::String("\n").join(lines);
}

} // namespace

godot::Dictionary format_get_node_properties(const godot::Dictionary &raw_result) {
    bool raw_success = raw_result.get("success", false);
    godot::Array nodes = raw_result.get("nodes", godot::Array());
    godot::Dictionary summary = raw_result.get("summary", godot::Dictionary());
    int budget_tokens = node_properties_budget_tokens(nodes.size());
    int used_tokens = 0;

    godot::Array targets;
    godot::Array section_entries;
    godot::Array large_indices;
    godot::PackedStringArray sections;
    int raw_success_count = 0;
    int raw_failure_count = 0;
    bool previewed = false;

    for (int i = 0; i < nodes.size(); i++) {
        if (nodes[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary node = nodes[i];
        godot::String status = node.get("status", "");
        if (status == "success") {
            raw_success_count++;
        } else {
            raw_failure_count++;
        }

        int target_index = targets.size();
        targets.append(target_metadata(node));

        godot::String section;
        godot::String text;
        bool large_text = false;
        if (status == "success") {
            text = node.get("properties_text", "");
            section = success_section(node, i, text, false,
                                      estimate_tokens(text),
                                      estimate_tokens(text));
            large_text = estimate_tokens(section) > 2000;
            if (!large_text && targets[target_index].get_type() == godot::Variant::DICTIONARY) {
                godot::Dictionary target = targets[target_index];
                target["shown_property_tokens"] = estimate_tokens(text);
                target["omitted_property_tokens"] = 0;
                targets[target_index] = target;
            }
        } else {
            section = failed_section(node, i);
            if (targets[target_index].get_type() == godot::Variant::DICTIONARY) {
                godot::Dictionary target = targets[target_index];
                target["shown_property_tokens"] = 0;
                target["omitted_property_tokens"] = 0;
                targets[target_index] = target;
            }
        }

        godot::Dictionary entry;
        entry["tokens"] = estimate_tokens(section);
        entry["large_text"] = large_text;
        entry["section"] = section;
        entry["text"] = text;
        entry["node"] = node;
        entry["source_index"] = i;
        entry["target_index"] = target_index;
        section_entries.append(entry);
        if (large_text) {
            large_indices.append(section_entries.size() - 1);
        }
    }

    for (int i = 0; i < section_entries.size(); i++) {
        godot::Dictionary entry = section_entries[i];
        if ((bool)entry.get("large_text", false)) {
            continue;
        }
        sections.append(godot::String(entry.get("section", "")));
        used_tokens += static_cast<int>(entry.get("tokens", 0));
    }

    int remaining_tokens = budget_tokens - used_tokens;
    int large_count = large_indices.size();
    int per_large_budget = large_count > 0 ? remaining_tokens / large_count : 0;
    for (int i = 0; i < large_indices.size(); i++) {
        int entry_index = static_cast<int>(large_indices[i]);
        godot::Dictionary entry = section_entries[entry_index];
        godot::Dictionary node = entry["node"];
        godot::String text = entry.get("text", "");
        int section_budget = per_large_budget <= 0 ? 1 : per_large_budget;
        godot::String preview = preview_text_by_budget(text, section_budget);
        bool section_previewed = preview.length() < text.length();
        if (section_previewed) {
            previewed = true;
        }

        int target_index = static_cast<int>(entry.get("target_index", -1));
        if (target_index >= 0 && target_index < targets.size() &&
            targets[target_index].get_type() == godot::Variant::DICTIONARY) {
            godot::Dictionary target = targets[target_index];
            int shown = estimate_tokens(preview);
            int total = estimate_tokens(text);
            target["shown_property_tokens"] = shown;
            target["omitted_property_tokens"] = total > shown ? total - shown : 0;
            targets[target_index] = target;
        }

        sections.append(success_section(
            node,
            static_cast<int>(entry.get("source_index", 0)),
            preview,
            section_previewed,
            estimate_tokens(preview),
            estimate_tokens(text)
        ));
    }

    godot::String status = "success";
    if (nodes.size() == 0 && raw_result.has("error")) {
        status = "failed";
    } else if (raw_failure_count > 0 && raw_success_count == 0) {
        status = "failed";
    } else if (raw_failure_count > 0 || previewed) {
        status = "partial";
    }

    godot::PackedStringArray header;
    header.append("Tool: get_node_properties");
    header.append("Status: " + status);
    header.append(nodes.size() > 0 ? "Scope: " + scope_for_nodes(nodes) : "Scope: unknown");
    if (!summary.is_empty()) {
        header.append(
            "Totals: " +
            godot::String::num_int64(static_cast<int64_t>(summary.get("success_count", 0))) +
            " succeeded, " +
            godot::String::num_int64(static_cast<int64_t>(summary.get("failure_count", 0))) +
            " failed, " +
            godot::String::num_int64(static_cast<int64_t>(summary.get("total_property_lines", 0))) +
            " property lines"
        );
    }
    if (nodes.size() == 0 && raw_result.has("error")) {
        header.append("");
        header.append("Error:\n" + godot::String(raw_result.get("error", "")));
    }
    sections.insert(0, godot::String("\n").join(header));

    godot::Dictionary metadata = make_base_metadata("get_node_properties", "get_node_properties-md-v1", status);
    metadata["targets"] = targets;
    metadata["budget_tokens"] = node_properties_budget_tokens(nodes.size());
    metadata["previewed"] = previewed;
    return make_envelope(godot::String("\n\n").join(sections), metadata, raw_success);
}

} // namespace fennara::tool_results
