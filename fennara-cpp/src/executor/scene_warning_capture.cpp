#include "fennara/executor.hpp"
#include "fennara/snapshot_manager.hpp"
#include "fennara/warning_capture.hpp"

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/tree.hpp>
#include <godot_cpp/classes/tree_item.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace fennara {

namespace {

struct SceneViewHints {
    int node3d_count = 0;
    int control_count = 0;
    bool has_camera3d = false;
};

void collect_scene_view_hints(godot::Node *node, SceneViewHints &hints) {
    if (!node) {
        return;
    }

    if (godot::Object::cast_to<godot::Node3D>(node)) {
        hints.node3d_count++;
    }
    if (godot::Object::cast_to<godot::Control>(node)) {
        hints.control_count++;
    }
    if (godot::Object::cast_to<godot::Camera3D>(node)) {
        hints.has_camera3d = true;
    }

    for (int i = 0; i < node->get_child_count(); i++) {
        collect_scene_view_hints(node->get_child(i), hints);
    }
}

bool should_open_3d_editor(godot::Node *root, const SceneViewHints &hints) {
    if (!root) {
        return false;
    }
    if (godot::Object::cast_to<godot::Node3D>(root)) {
        return true;
    }
    if (hints.has_camera3d) {
        return true;
    }
    return hints.node3d_count > 0 && hints.control_count == 0;
}

void switch_main_screen_for_open_scene() {
    godot::EditorInterface *editor = godot::EditorInterface::get_singleton();
    if (!editor) {
        return;
    }

    godot::Node *root = editor->get_edited_scene_root();
    SceneViewHints hints;
    collect_scene_view_hints(root, hints);
    godot::String screen_name = should_open_3d_editor(root, hints) ? "3D" : "2D";
    editor->set_main_screen_editor(screen_name);
    editor->call_deferred("set_main_screen_editor", screen_name);
}

godot::String warning_entry_text(const godot::Variant &entry_var) {
    if (entry_var.get_type() != godot::Variant::DICTIONARY) {
        return godot::String(entry_var);
    }

    godot::Dictionary entry = entry_var;
    godot::String type = entry.get("type", "");
    godot::String message = entry.get("message", "");
    godot::String file = entry.get("file", "");
    int64_t line = static_cast<int64_t>(entry.get("line", 0));
    godot::String function = entry.get("function", "");

    godot::String text;
    if (!type.is_empty()) {
        text += type + ": ";
    }
    text += message;
    if (!file.is_empty()) {
        text += " (" + file;
        if (line > 0) {
            text += ":" + godot::String::num_int64(line);
        }
        if (!function.is_empty()) {
            text += ", " + function;
        }
        text += ")";
    }
    return text;
}

godot::PackedStringArray deduped_warning_lines(const godot::Array &warnings) {
    godot::PackedStringArray lines;
    godot::Dictionary counts;
    godot::Array order;

    for (int i = 0; i < warnings.size(); i++) {
        godot::String text = warning_entry_text(warnings[i]);
        if (counts.has(text)) {
            counts[text] = static_cast<int>(counts[text]) + 1;
        } else {
            counts[text] = 1;
            order.append(text);
        }
    }

    for (int i = 0; i < order.size(); i++) {
        godot::String text = order[i];
        int count = static_cast<int>(counts[text]);
        godot::String line = "- " + text;
        if (count > 1) {
            line += " (repeated " + godot::String::num_int64(count) + " times)";
        }
        lines.append(line);
    }

    return lines;
}

void append_scene_warnings_to_result(godot::Dictionary &result,
                                     const godot::String &scene_path,
                                     const godot::Array &engine_warnings,
                                     const godot::Array &configuration_warnings) {
    if (result.has("content") && result.has("metadata")) {
        godot::String content = result.get("content", "");
        godot::PackedStringArray warning_lines;
        if (engine_warnings.size() > 0 || configuration_warnings.size() > 0) {
            warning_lines.append("## Editor warnings for " + scene_path);
            if (engine_warnings.size() > 0) {
                warning_lines.append("Engine warnings: " +
                                     godot::String::num_int64(engine_warnings.size()));
                warning_lines.append_array(deduped_warning_lines(engine_warnings));
            }
            if (configuration_warnings.size() > 0) {
                warning_lines.append("Configuration warnings: " +
                                     godot::String::num_int64(configuration_warnings.size()));
                for (int i = 0; i < configuration_warnings.size(); i++) {
                    godot::Variant entry_var = configuration_warnings[i];
                    if (entry_var.get_type() == godot::Variant::DICTIONARY) {
                        godot::Dictionary entry = entry_var;
                        warning_lines.append("- " +
                                             godot::String(entry.get("node", "")) +
                                             ": " +
                                             godot::String(entry.get("warning", "")));
                    } else {
                        warning_lines.append("- " + godot::String(entry_var));
                    }
                }
            }
        }
        if (!warning_lines.is_empty()) {
            result["content"] = content + "\n\n" + godot::String("\n").join(warning_lines);
        }

        godot::Dictionary metadata = result.get("metadata", godot::Dictionary());
        godot::Array targets = metadata.get("targets", godot::Array());
        for (int i = 0; i < targets.size(); i++) {
            if (targets[i].get_type() != godot::Variant::DICTIONARY) {
                continue;
            }
            godot::Dictionary target = targets[i];
            if (godot::String(target.get("scene_path", "")) != scene_path) {
                continue;
            }
            target["engine_warning_count"] = engine_warnings.size();
            target["configuration_warning_count"] = configuration_warnings.size();
            targets[i] = target;
        }
        metadata["targets"] = targets;
        result["metadata"] = metadata;
        return;
    }

    if (result.has("results")) {
        godot::Array items = result.get("results", godot::Array());
        bool changed = false;
        for (int i = 0; i < items.size(); i++) {
            godot::Variant item_variant = items[i];
            if (item_variant.get_type() != godot::Variant::DICTIONARY) {
                continue;
            }

            godot::Dictionary item = item_variant;
            if (godot::String(item.get("scene_path", "")) != scene_path) {
                continue;
            }

            item["engine_warnings"] = engine_warnings;
            item["configuration_warnings"] = configuration_warnings;
            items[i] = item;
            changed = true;
        }

        if (changed) {
            result["results"] = items;
        }
        return;
    }

    if (result.has("scenes")) {
        godot::Array items = result.get("scenes", godot::Array());
        bool changed = false;
        for (int i = 0; i < items.size(); i++) {
            godot::Variant item_variant = items[i];
            if (item_variant.get_type() != godot::Variant::DICTIONARY) {
                continue;
            }

            godot::Dictionary item = item_variant;
            if (godot::String(item.get("scene_path", "")) != scene_path) {
                continue;
            }

            item["engine_warnings"] = engine_warnings;
            item["configuration_warnings"] = configuration_warnings;
            items[i] = item;
            changed = true;
        }

        if (changed) {
            result["scenes"] = items;
        }
        return;
    }

    if (engine_warnings.size() > 0) {
        result["engine_warnings"] = engine_warnings;
    }
    if (configuration_warnings.size() > 0) {
        result["configuration_warnings"] = configuration_warnings;
    }
}

godot::Tree *find_scene_tree_control(godot::Node *node) {
    auto *tree = godot::Object::cast_to<godot::Tree>(node);
    if (tree) {
        godot::Node *parent = tree->get_parent();
        if (parent && parent->get_class() == "SceneTreeEditor") {
            return tree;
        }
    }
    for (int i = 0; i < node->get_child_count(); i++) {
        auto *result = find_scene_tree_control(node->get_child(i));
        if (result) return result;
    }
    return nullptr;
}

void collect_tree_warnings(godot::TreeItem *item, godot::Array &out) {
    static const int BUTTON_WARNING = 5;

    int warning_idx = item->get_button_by_id(0, BUTTON_WARNING);
    if (warning_idx >= 0) {
        godot::String tooltip = item->get_button_tooltip_text(0, warning_idx);
        godot::PackedStringArray parts =
            tooltip.split(godot::String::utf8("\xe2\x80\xa2"));
        for (int i = 0; i < parts.size(); i++) {
            godot::String trimmed = parts[i].strip_edges();
            if (trimmed.is_empty() ||
                trimmed.begins_with("Node configuration warning")) {
                continue;
            }
            trimmed = trimmed.replace("\n    ", " ");
            godot::Dictionary entry;
            entry["node"] = item->get_text(0);
            entry["warning"] = trimmed;
            out.append(entry);
        }
    }

    godot::TreeItem *child = item->get_first_child();
    while (child) {
        collect_tree_warnings(child, out);
        child = child->get_next();
    }
}

} // namespace

void FennaraExecutor::_capture_scene_warnings() {
    _scene_to_indices = godot::Dictionary();
    for (const auto &ms : _modified_scenes) {
        godot::Array indices;
        if (_scene_to_indices.has(ms.scene_path)) {
            indices = _scene_to_indices[ms.scene_path];
        }
        indices.append(ms.tool_index);
        _scene_to_indices[ms.scene_path] = indices;
    }
    _modified_scenes.clear();

    _scene_paths_for_warnings = _scene_to_indices.keys();
    _engine_warnings_per_scene = godot::Array();
    _engine_warnings_per_scene.resize(_scene_paths_for_warnings.size());

    for (int s = 0; s < _scene_paths_for_warnings.size(); s++) {
        godot::String scene_path = _scene_paths_for_warnings[s];

        godot::Dictionary warning_context = _batch_log_context();
        warning_context["scene_path"] = scene_path;
        _log_tool_event("Capture engine warnings", warning_context);

        godot::Ref<FennaraWarningCapture> wc;
        wc.instantiate();
        godot::OS::get_singleton()->add_logger(wc);

        godot::EditorInterface::get_singleton()->open_scene_from_path(scene_path);
        switch_main_screen_for_open_scene();

        godot::OS::get_singleton()->remove_logger(wc);
        _engine_warnings_per_scene[s] = wc->get_captured();
    }

    call_deferred("_scrape_configuration_warnings");
}

void FennaraExecutor::_scrape_configuration_warnings() {
    if (_batch_cancelled) {
        return;
    }

    for (int s = 0; s < _scene_paths_for_warnings.size(); s++) {
        godot::String scene_path = _scene_paths_for_warnings[s];
        godot::Array captured = _engine_warnings_per_scene[s];

        if (_scene_paths_for_warnings.size() > 1 || s > 0) {
            godot::EditorInterface::get_singleton()->open_scene_from_path(scene_path);
            switch_main_screen_for_open_scene();
        }

        godot::Array config_warnings;
        godot::Control *base =
            godot::EditorInterface::get_singleton()->get_base_control();
        if (base) {
            godot::Tree *scene_tree = find_scene_tree_control(base);
            if (scene_tree) {
                godot::TreeItem *root_item = scene_tree->get_root();
                if (root_item) {
                    collect_tree_warnings(root_item, config_warnings);
                }
            }
        }

        godot::Dictionary captured_context = _batch_log_context();
        captured_context["scene_path"] = scene_path;
        captured_context["engine_messages"] = static_cast<int64_t>(captured.size());
        captured_context["configuration_warnings"] =
            static_cast<int64_t>(config_warnings.size());
        _log_tool_event("Captured scene warnings", captured_context);

        godot::Array indices = _scene_to_indices[scene_path];
        for (int i = 0; i < indices.size(); i++) {
            int tool_index = indices[i];
            godot::Dictionary wrapped = _async_results[tool_index];
            godot::Dictionary result = wrapped["result"];
            append_scene_warnings_to_result(result, scene_path, captured,
                                            config_warnings);
            wrapped["result"] = result;
            _async_results[tool_index] = wrapped;
        }
    }

    _scene_to_indices = godot::Dictionary();
    _engine_warnings_per_scene = godot::Array();
    _scene_paths_for_warnings = godot::Array();

    godot::Dictionary complete_context = _batch_log_context();
    complete_context["total"] = static_cast<int64_t>(_async_results.size());
    _log_tool_event("Batch complete", complete_context);
    _active_async_tools.clear();
    FennaraSnapshotManager::set_active(nullptr);
    emit_signal("all_tools_completed", _async_results);
    _clear_execution_context();
}

} // namespace fennara
