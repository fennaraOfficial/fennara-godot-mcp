#include "fennara/tool_results/get_scene_tree.hpp"

#include "fennara/tool_results/envelope.hpp"
#include "fennara/tool_results/markdown.hpp"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara::tool_results {

namespace {

int scene_tree_budget_tokens(int target_count) {
    if (target_count <= 1) return 10000;
    if (target_count == 2) return 14000;
    if (target_count == 3) return 18000;
    if (target_count == 4) return 22000;
    return 26000;
}

godot::String scene_path_for_heading(const godot::Dictionary &scene, int index) {
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
        paths.append(scene_path_for_heading(scene, i));
    }
    return godot::String::num_int64(scenes.size()) +
           (scenes.size() == 1 ? " scene: " : " scenes: ") +
           godot::String(", ").join(paths);
}

godot::Dictionary target_metadata(const godot::Dictionary &scene) {
    godot::Dictionary target;
    target["scene_path"] = scene.get("scene_path", "");
    target["status"] = scene.get("status", "");
    target["root_name"] = scene.get("root_name", "");
    target["root_type"] = scene.get("root_type", "");
    target["node_count"] = scene.get("node_count", 0);
    target["tree_line_count"] = scene.get("tree_line_count", 0);
    target["shown_tree_tokens"] = 0;
    target["omitted_tree_tokens"] = estimate_tokens(godot::String(scene.get("tree", "")));
    if (scene.has("error")) {
        target["error"] = scene["error"];
    }
    return target;
}

godot::String success_section(const godot::Dictionary &scene,
                              int index,
                              const godot::String &tree,
                              bool previewed,
                              int shown_tokens,
                              int total_tokens) {
    godot::PackedStringArray lines;
    lines.append("## " + scene_path_for_heading(scene, index));
    lines.append(godot::String("Status: ") + (previewed ? "partial" : "success"));

    godot::String root_name = scene.get("root_name", "");
    godot::String root_type = scene.get("root_type", "");
    if (!root_name.is_empty() || !root_type.is_empty()) {
        godot::String root = "Root:";
        if (!root_name.is_empty()) {
            root += " " + root_name;
        }
        if (!root_type.is_empty()) {
            root += " (" + root_type + ")";
        }
        lines.append(root);
    }

    int64_t node_count = static_cast<int64_t>(scene.get("node_count", 0));
    if (node_count > 0) {
        lines.append("Nodes: " + godot::String::num_int64(node_count));
    }
    int64_t tree_lines = static_cast<int64_t>(scene.get("tree_line_count", 0));
    if (tree_lines > 0) {
        lines.append("Tree lines: " + godot::String::num_int64(tree_lines));
    }
    if (previewed) {
        lines.append(
            "Shown: preview, about " + godot::String::num_int64(shown_tokens) +
            " of " + godot::String::num_int64(total_tokens) + " estimated tokens"
        );
        lines.append("Omitted: remaining scene tree exceeded model-facing size limit");
    }

    lines.append("");
    lines.append(code_fence(tree, "text"));
    return godot::String("\n").join(lines);
}

godot::String failed_section(const godot::Dictionary &scene, int index) {
    godot::PackedStringArray lines;
    lines.append("## " + scene_path_for_heading(scene, index));
    lines.append("Status: failed");
    if (scene.has("error")) {
        lines.append("Error:\n" + godot::String(scene.get("error", "")));
    }
    return godot::String("\n").join(lines);
}

} // namespace

godot::Dictionary format_get_scene_tree(const godot::Dictionary &raw_result) {
    bool raw_success = raw_result.get("success", false);
    godot::Array scenes = raw_result.get("scenes", godot::Array());
    godot::Dictionary summary = raw_result.get("summary", godot::Dictionary());
    int budget_tokens = scene_tree_budget_tokens(scenes.size());

    godot::Array targets;
    godot::Array section_entries;
    godot::Array large_indices;
    godot::PackedStringArray sections;
    int raw_success_count = 0;
    int raw_failure_count = 0;
    bool previewed = false;

    for (int i = 0; i < scenes.size(); i++) {
        if (scenes[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary scene = scenes[i];
        godot::String status = scene.get("status", "");
        if (status == "success") {
            raw_success_count++;
        } else {
            raw_failure_count++;
        }

        targets.append(target_metadata(scene));

        godot::String section;
        godot::String tree;
        bool large_tree = false;
        if (status == "success") {
            tree = scene.get("tree", "");
            section = success_section(scene, i, tree, false,
                                      estimate_tokens(tree),
                                      estimate_tokens(tree));
            large_tree = estimate_tokens(section) > 2000;
            if (!large_tree && targets[i].get_type() == godot::Variant::DICTIONARY) {
                godot::Dictionary target = targets[i];
                target["shown_tree_tokens"] = estimate_tokens(tree);
                target["omitted_tree_tokens"] = 0;
                targets[i] = target;
            }
        } else {
            section = failed_section(scene, i);
            if (targets[i].get_type() == godot::Variant::DICTIONARY) {
                godot::Dictionary target = targets[i];
                target["shown_tree_tokens"] = 0;
                target["omitted_tree_tokens"] = 0;
                targets[i] = target;
            }
        }

        godot::Dictionary entry;
        entry["tokens"] = estimate_tokens(section);
        entry["large_tree"] = large_tree;
        entry["section"] = section;
        entry["tree"] = tree;
        entry["scene"] = scene;
        entry["source_index"] = i;
        section_entries.append(entry);
        if (large_tree) {
            large_indices.append(section_entries.size() - 1);
        }
    }

    for (int i = 0; i < section_entries.size(); i++) {
        godot::Dictionary entry = section_entries[i];
        if ((bool)entry.get("large_tree", false)) {
            continue;
        }
        sections.append(godot::String(entry.get("section", "")));
        budget_tokens -= static_cast<int>(entry.get("tokens", 0));
    }

    int large_count = large_indices.size();
    int per_large_budget = large_count > 0 ? budget_tokens / large_count : 0;
    for (int i = 0; i < large_indices.size(); i++) {
        int entry_index = static_cast<int>(large_indices[i]);
        godot::Dictionary entry = section_entries[entry_index];
        godot::Dictionary scene = entry["scene"];
        godot::String tree = entry.get("tree", "");
        int section_budget = per_large_budget <= 0 ? 1 : per_large_budget;
        godot::String preview = preview_text_by_budget(tree, section_budget);
        bool section_previewed = preview.length() < tree.length();
        if (section_previewed) {
            previewed = true;
        }
        int target_index = static_cast<int>(entry.get("source_index", 0));
        if (target_index >= 0 && target_index < targets.size() &&
            targets[target_index].get_type() == godot::Variant::DICTIONARY) {
            godot::Dictionary target = targets[target_index];
            int shown = estimate_tokens(preview);
            int total = estimate_tokens(tree);
            target["shown_tree_tokens"] = shown;
            target["omitted_tree_tokens"] = total > shown ? total - shown : 0;
            targets[target_index] = target;
        }
        sections.append(success_section(
            scene,
            static_cast<int>(entry.get("source_index", 0)),
            preview,
            section_previewed,
            estimate_tokens(preview),
            estimate_tokens(tree)
        ));
    }

    godot::String status = "success";
    if (scenes.size() == 0 && raw_result.has("error")) {
        status = "failed";
    } else if (raw_failure_count > 0 && raw_success_count == 0) {
        status = "failed";
    } else if (raw_failure_count > 0 || previewed) {
        status = "partial";
    }

    godot::PackedStringArray header;
    header.append("Tool: get_scene_tree");
    header.append("Status: " + status);
    header.append(scenes.size() > 0 ? "Scope: " + scope_for_scenes(scenes) : "Scope: unknown");
    if (!summary.is_empty()) {
        header.append(
            "Totals: " +
            godot::String::num_int64(static_cast<int64_t>(summary.get("success_count", 0))) +
            " succeeded, " +
            godot::String::num_int64(static_cast<int64_t>(summary.get("failure_count", 0))) +
            " failed, " +
            godot::String::num_int64(static_cast<int64_t>(summary.get("total_nodes", 0))) +
            " nodes"
        );
    }
    if (scenes.size() == 0 && raw_result.has("error")) {
        header.append("");
        header.append("Error:\n" + godot::String(raw_result.get("error", "")));
    }
    sections.insert(0, godot::String("\n").join(header));

    godot::Dictionary metadata = make_base_metadata("get_scene_tree", "get_scene_tree-md-v1", status);
    metadata["targets"] = targets;
    metadata["budget_tokens"] = scene_tree_budget_tokens(scenes.size());
    metadata["previewed"] = previewed;
    return make_envelope(godot::String("\n\n").join(sections), metadata, raw_success);
}

} // namespace fennara::tool_results
