#include "fennara/executor.hpp"
#include "fennara/logger.hpp"

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/time.hpp>

namespace fennara {

namespace {

bool is_fennara_scratch_script(const godot::String &script_path) {
    godot::String normalized = script_path.replace("\\", "/");
    return normalized.begins_with("res://.fennara/") ||
           normalized.contains("/.fennara/");
}

} // namespace

godot::String FennaraExecutor::_make_batch_id() {
    ++_batch_counter;
    uint64_t ticks = godot::Time::get_singleton()->get_ticks_usec();
    return godot::String("TB-") + godot::String::num_uint64(ticks) + "-" +
           godot::String::num_uint64(_batch_counter);
}

godot::Dictionary FennaraExecutor::_batch_log_context() const {
    godot::Dictionary context;
    if (!_execution_batch_id.is_empty()) {
        context["batch_id"] = _execution_batch_id;
    }
    if (!_execution_request_id.is_empty()) {
        context["request_id"] = _execution_request_id;
    }
    if (_execution_session_index >= 0) {
        context["session_index"] = _execution_session_index;
    }
    return context;
}

godot::Dictionary FennaraExecutor::_tool_log_context(const godot::String &tool_name,
                                              int tool_index) const {
    godot::Dictionary context = _batch_log_context();
    if (!tool_name.is_empty()) {
        context["tool_name"] = tool_name;
    }
    if (tool_index >= 0) {
        context["tool_index"] = tool_index;
    }
    return context;
}

godot::Dictionary FennaraExecutor::_tool_result_metadata(
    const godot::String &tool_name, int tool_index) const {
    godot::Dictionary metadata;
    if (!_execution_request_id.is_empty()) {
        metadata["source_request_id"] = _execution_request_id;
    }
    if (!_execution_batch_id.is_empty()) {
        metadata["batch_id"] = _execution_batch_id;
    }
    if (_execution_session_index >= 0) {
        metadata["session_index"] = _execution_session_index;
    }
    if (!tool_name.is_empty()) {
        metadata["tool_name"] = tool_name;
    }
    if (tool_index >= 0) {
        metadata["tool_index"] = tool_index;
    }
    return metadata;
}

void FennaraExecutor::_log_tool_event(const godot::String &message,
                               godot::Dictionary details) const {
    details["pending_async_tools"] = _pending_async_tools;
    details["pending_script_writes"] = static_cast<int64_t>(_pending_script_writes.size());
    details["pending_run_scene_edit_scripts"] =
        static_cast<int64_t>(_pending_run_scene_edit_scripts.size());
    FLOG_CTX("TOOL", message, details);
}

void FennaraExecutor::_clear_execution_context() {
    _execution_request_id = "";
    _execution_batch_id = "";
    _execution_session_index = -1;
}

bool FennaraExecutor::_has_execution_context() const {
    return !_execution_request_id.is_empty();
}

void FennaraExecutor::_print_fennara_activity(const godot::String &message) const {
    if (!_has_execution_context() || message.strip_edges().is_empty()) {
        return;
    }

    Logger::log_activity(message);
}

godot::String FennaraExecutor::_friendly_tool_action(
    const godot::String &tool_name,
    const godot::Dictionary &args) const {
    if (tool_name == "write_or_update_file") {
        godot::String path = args.get("file_path", "");
        godot::String mode = args.get("mode", "");
        if (path.ends_with(".gd")) {
            return (mode == "write" ? "Writing script: " : "Editing script: ") + path;
        }
        return (mode == "write" ? "Writing file: " : "Editing file: ") + path;
    }
    if (tool_name == "script_diagnostics") {
        return "Checking script diagnostics";
    }
    if (tool_name == "validate_scene") {
        return "Validating scene";
    }
    if (tool_name == "run_scene_edit_script") {
        return "Applying scene edit script";
    }
    if (tool_name == "screenshot_scene") {
        return "Capturing scene screenshot";
    }
    if (tool_name == "runtime_session") {
        godot::String action = args.get("action", "status");
        if (action == "start") {
            return "Starting runtime session";
        }
        if (action == "stop") {
            return "Stopping runtime session";
        }
        return "Checking runtime session";
    }
    if (tool_name == "get_scene_tree") {
        return "Inspecting scene tree";
    }
    if (tool_name == "get_node_properties") {
        return "Inspecting node properties";
    }
    if (tool_name == "get_class_info") {
        return "Looking up Godot class info";
    }
    return "Running tool: " + tool_name;
}

void FennaraExecutor::_track_edited_script(const godot::String &script_path) {
    if (!_has_execution_context() || script_path.is_empty() ||
        !script_path.ends_with(".gd") || is_fennara_scratch_script(script_path)) {
        return;
    }

    for (const godot::String &existing : _edited_script_paths) {
        if (existing == script_path) {
            return;
        }
    }
    _edited_script_paths.push_back(script_path);
}

void FennaraExecutor::_focus_best_edited_script(
    const godot::Dictionary &per_file_diagnostics) {
    if (!_has_execution_context() || _edited_script_paths.empty()) {
        return;
    }

    godot::String best_path;
    for (const godot::String &path : _edited_script_paths) {
        if (!per_file_diagnostics.has(path)) {
            continue;
        }
        godot::Dictionary diagnostics = per_file_diagnostics[path];
        if ((int)diagnostics.get("total_errors", 0) > 0) {
            best_path = path;
            break;
        }
    }
    if (best_path.is_empty()) {
        best_path = _edited_script_paths.front();
    }

    if (_edited_script_paths.size() == 1) {
        _print_fennara_activity("Focusing script: " + best_path);
    } else {
        _print_fennara_activity(
            "Focusing best script: " + best_path + " (" +
            godot::String::num_int64((int64_t)_edited_script_paths.size()) +
            " scripts edited)");
    }
    _focus_script(best_path);
}

void FennaraExecutor::_focus_script(const godot::String &script_path) const {
    godot::EditorInterface *editor = godot::EditorInterface::get_singleton();
    if (!editor || script_path.is_empty()) {
        return;
    }

    godot::Ref<godot::Resource> resource =
        godot::ResourceLoader::get_singleton()->load(script_path);
    godot::Ref<godot::Script> script = resource;
    if (!script.is_valid()) {
        return;
    }

    editor->edit_script(script, -1, 0, true);
    editor->set_main_screen_editor("Script");
}

} // namespace fennara
