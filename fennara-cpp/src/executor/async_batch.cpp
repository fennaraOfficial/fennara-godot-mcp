#include "fennara/executor.hpp"
#include "fennara/addon_access.hpp"
#include "fennara/file_utils.hpp"
#include "fennara/logger.hpp"
#include "fennara/snapshot_manager.hpp"
#include "fennara/tool_results/formatters.hpp"

#include "fennara/tools/script_diagnostics.hpp"
#include "fennara/tools/run_scene_edit_script.hpp"
#include "fennara/tools/runtime_script.hpp"
#include "fennara/tools/runtime_session.hpp"
#include "fennara/tools/validate_scene.hpp"

#include <godot_cpp/classes/json.hpp>

#include <thread>

namespace fennara {

namespace {

godot::String tool_name_from_call(const godot::Dictionary &tool_call) {
    if (tool_call.has("function")) {
        return godot::Dictionary(tool_call["function"]).get("name", "");
    }
    return tool_call.get("name", "");
}

godot::Dictionary tool_args_from_call(const godot::Dictionary &tool_call) {
    if (tool_call.has("function")) {
        godot::Dictionary func = tool_call["function"];
        godot::String args_str = func.get("arguments", "{}");
        godot::Variant parsed = godot::JSON::parse_string(args_str);
        if (parsed.get_type() == godot::Variant::DICTIONARY) {
            return parsed;
        }
        return godot::Dictionary();
    }
    return tool_call.get("args", godot::Dictionary());
}

godot::String dictionary_keys_csv(const godot::Dictionary &dict) {
    godot::String keys_csv;
    godot::Array keys = dict.keys();
    for (int i = 0; i < keys.size(); i++) {
        if (i > 0) {
            keys_csv += ",";
        }
        keys_csv += godot::String(keys[i]);
    }
    return keys_csv;
}

bool complete_if_blocked(FennaraExecutor *executor,
                         const godot::String &tool_name,
                         const godot::Dictionary &args,
                         int tool_index,
                         const godot::String &path,
                         uint64_t batch_generation) {
    if (path.strip_edges().is_empty()) {
        return false;
    }

    godot::Dictionary addon_block;
    if (addon_access::is_path_allowed(path, false, addon_block)) {
        return false;
    }

    addon_block["tool_name"] = tool_name;
    executor->_on_async_tool_complete(addon_block, tool_index, tool_name, args, batch_generation);
    return true;
}

} // namespace

void FennaraExecutor::_cancel_active_async_tools() {
    for (int i = 0; i < _active_async_tools.size(); i++) {
        godot::Ref<FennaraScriptDiagnosticsTool> tool = _active_async_tools[i];
        if (tool.is_valid()) {
            tool->cancel();
        }
    }
}

void FennaraExecutor::execute_tool_calls_async(const godot::Array &tool_calls) {
    int count = tool_calls.size();
    _async_batch_generation++;
    _execution_batch_id = _make_batch_id();
    _batch_cancelled = false;
    _cancel_active_async_tools();
    _validate_scene_runtime_cancelled.store(true);

    godot::String tool_names;
    for (int i = 0; i < count; i++) {
        godot::Dictionary tc = tool_calls[i];
        godot::String n = tool_name_from_call(tc);
        if (i > 0) tool_names += ",";
        tool_names += n;
    }
    godot::Dictionary batch_start = _batch_log_context();
    batch_start["tool_count"] = count;
    if (!tool_names.is_empty()) {
        batch_start["tools"] = tool_names;
    }
    _log_tool_event("Batch start", batch_start);
    if (count > 0) {
        _print_fennara_activity(
            "Running " + godot::String::num_int64(count) +
            (count == 1 ? " action..." : " actions..."));
    }
    uint64_t batch_generation = _async_batch_generation;

    _async_results.resize(count);
    _active_async_tools.clear();
    _pending_async_tools = count;
    _pending_script_writes.clear();
    _pending_run_scene_edit_scripts.clear();
    _pending_screenshot_scenes.clear();
    _pending_validate_scenes.clear();
    _pending_runtime_sessions.clear();
    _pending_runtime_scripts.clear();
    _edited_script_paths.clear();
    _screenshot_running = false;
    _validate_scene_running = false;
    _validate_scene_tool_index = -1;
    _validate_scene_args = godot::Dictionary();
    _validate_scene_paths = godot::Array();
    _validate_scene_results = godot::Array();
    _validate_scene_runtime_eligible_paths = godot::Array();
    _validate_scene_index = 0;
    _validate_scene_thread_done = false;
    _validate_scene_thread_result = godot::Dictionary();
    _runtime_session_running = false;
    _runtime_script_running = false;
    _modified_scenes.clear();

    FennaraSnapshotManager::set_active(_snapshot_mgr);

    if (_batch_diag_thread.joinable()) {
        _batch_diag_thread.join();
    }
    if (_validate_scene_thread.joinable()) {
        _validate_scene_thread.join();
    }
    _validate_scene_runtime_cancelled.store(false);
    if (_runtime_script_thread.joinable()) {
        _runtime_script_thread.join();
    }
    if (_runtime_session_thread.joinable()) {
        _runtime_session_thread.join();
    }
    _validate_scene_thread_done = false;
    _validate_scene_thread_result = godot::Dictionary();

    if (count == 0) {
        _clear_execution_context();
        _check_completion();
        return;
    }

    for (int i = 0; i < count; i++) {
        godot::Dictionary tc = tool_calls[i];
        godot::String name = tool_name_from_call(tc);
        godot::Dictionary args = tool_args_from_call(tc);
        godot::Dictionary tool_context = _tool_log_context(name, i);
        godot::String arg_keys = dictionary_keys_csv(args);
        if (!arg_keys.is_empty()) {
            tool_context["arg_keys"] = arg_keys;
        }
        _log_tool_event("Tool scheduled", tool_context);
        _print_fennara_activity(_friendly_tool_action(name, args));

        if (name == "script_diagnostics") {
            godot::Ref<FennaraScriptDiagnosticsTool> tool;
            tool.instantiate();
            tool->connect("complete", callable_mp(this, &FennaraExecutor::_on_async_tool_complete).bind(i, name, args, batch_generation));
            _active_async_tools.append(tool);
            tool->execute(args);
        } else if (name == "write_or_update_file") {
            godot::Dictionary write_res = execute_tool(name, args);
            godot::String file_path = args.get("file_path", "");
            bool is_success = write_res.get("success", false);

            if (is_success && (file_path.ends_with(".gd") || file_path.ends_with(".cs"))) {
                godot::String resolved = write_res.get("file_path", file_path);
                _track_edited_script(resolved);
                _pending_script_writes.push_back({i, resolved, args, write_res});
            } else {
                _on_async_tool_complete(write_res, i, name, args, batch_generation);
            }
        } else if (name == "screenshot_scene") {
            if (complete_if_blocked(this, name, args, i, args.get("scene_path", ""), batch_generation)) {
                continue;
            }
            _pending_screenshot_scenes.push_back({i, args});
        } else if (name == "validate_scene") {
            godot::Array scene_paths = args.get("scene_paths", godot::Array());
            bool blocked = false;
            for (int scene_i = 0; scene_i < scene_paths.size(); scene_i++) {
                if (complete_if_blocked(this, name, args, i, scene_paths[scene_i], batch_generation)) {
                    blocked = true;
                    break;
                }
            }
            if (blocked) {
                continue;
            }
            _pending_validate_scenes.push_back({i, args});
        } else if (name == "runtime_script") {
            if (complete_if_blocked(this, name, args, i, args.get("script_path", ""), batch_generation)) {
                continue;
            }
            _pending_runtime_scripts.push_back({i, args});
        } else if (name == "runtime_session") {
            godot::String action = godot::String(args.get("action", "status")).strip_edges().to_lower();
            if (action == "start" &&
                complete_if_blocked(this, name, args, i, args.get("scene_path", ""), batch_generation)) {
                continue;
            }
            _pending_runtime_sessions.push_back({i, args});
        } else if (name == "explore_project") {
            godot::Dictionary res;
            res["success"] = false;
            res["error"] = "Tool '" + name + "' is not available in this local plugin build.";
            _on_async_tool_complete(res, i, name, args, batch_generation);
        } else if (name == "run_scene_edit_script") {
            godot::Dictionary addon_block;
            if (!addon_access::is_path_allowed(args.get("scene_path", ""), false, addon_block) ||
                (!godot::String(args.get("script_path", "")).strip_edges().is_empty() &&
                 !addon_access::is_path_allowed(args.get("script_path", ""), false, addon_block))) {
                addon_block["tool_name"] = name;
                _on_async_tool_complete(addon_block, i, name, args, batch_generation);
                continue;
            }
            godot::Dictionary prepared = FennaraRunSceneEditScriptTool::prepare_execution(args);
            if (!(bool)prepared.get("success", false)) {
                _on_async_tool_complete(prepared, i, name, args, batch_generation);
            } else {
                godot::String script_path = prepared.get("script_path", "");
                godot::String resolved = file_utils::resolve_path(script_path);
                _pending_run_scene_edit_scripts.push_back({i, script_path, resolved, prepared});
            }
        } else {
            godot::Dictionary res = execute_tool(name, args);
            _on_async_tool_complete(res, i, name, args, batch_generation);
        }
    }

    bool has_batch_diagnostics =
        !_pending_script_writes.empty() || !_pending_run_scene_edit_scripts.empty();
    if (has_batch_diagnostics) {
        godot::String diag_files;
        for (size_t i = 0; i < _pending_script_writes.size(); i++) {
            if (i > 0) diag_files += ",";
            diag_files += _pending_script_writes[i].file_path;
        }
        for (size_t i = 0; i < _pending_run_scene_edit_scripts.size(); i++) {
            if (!diag_files.is_empty()) diag_files += ",";
            diag_files += _pending_run_scene_edit_scripts[i].resolved_script_path;
        }
        godot::Dictionary diag_context = _batch_log_context();
        if (!diag_files.is_empty()) {
            diag_context["files"] = diag_files;
        }
        _log_tool_event("Batch diagnostics start", diag_context);
        _print_fennara_activity("Checking edited scripts before continuing...");
        _batch_diag_thread = std::thread(
            [this, batch_generation]() { _run_batch_diagnostics(batch_generation); });
    } else {
        _start_next_validate_scene();
    }
}

void FennaraExecutor::cancel() {
    _log_tool_event("Batch cancelled", _batch_log_context());
    _async_batch_generation++;
    _batch_cancelled = true;
    _pending_async_tools = 0;
    _cancel_active_async_tools();
    _active_async_tools.clear();
    _modified_scenes.clear();
    _scene_to_indices = godot::Dictionary();
    _engine_warnings_per_scene = godot::Array();
    _scene_paths_for_warnings = godot::Array();
    _screenshot_tool_index = -1;
    _screenshot_args = godot::Dictionary();
    _screenshot_nav_result = godot::Dictionary();
    _screenshot_views = godot::Array();
    _screenshot_captures = godot::Array();
    _screenshot_view_index = 0;
    _pending_screenshot_scenes.clear();
    _screenshot_running = false;
    _pending_validate_scenes.clear();
    _validate_scene_running = false;
    _validate_scene_tool_index = -1;
    _validate_scene_args = godot::Dictionary();
    _validate_scene_paths = godot::Array();
    _validate_scene_results = godot::Array();
    _validate_scene_runtime_eligible_paths = godot::Array();
    _validate_scene_index = 0;
    _validate_scene_runtime_cancelled.store(true);
    if (_validate_scene_thread.joinable()) {
        _validate_scene_thread.join();
    }
    _validate_scene_runtime_cancelled.store(false);
    _validate_scene_thread_done = false;
    _validate_scene_thread_result = godot::Dictionary();
    _pending_runtime_sessions.clear();
    _runtime_session_running = false;
    _runtime_session_tool_index = -1;
    _runtime_session_args = godot::Dictionary();
    if (_runtime_session_thread.joinable()) {
        _runtime_session_thread.join();
    }
    _runtime_session_thread_done = false;
    _runtime_session_thread_result = godot::Dictionary();
    _runtime_session_phase = "";
    _runtime_session_build_result = godot::Dictionary();
    _runtime_session_preflight_result = godot::Dictionary();
    _runtime_session_script_context = godot::Dictionary();
    _pending_runtime_scripts.clear();
    _runtime_script_running = false;
    _runtime_script_tool_index = -1;
    _runtime_script_run_id = "";
    _runtime_script_args = godot::Dictionary();
    if (_runtime_script_thread.joinable()) {
        _runtime_script_thread.join();
    }
    _runtime_script_thread_done = false;
    _runtime_script_thread_result = godot::Dictionary();
    _async_results = godot::Array();
    FennaraSnapshotManager::set_active(nullptr);
    _clear_execution_context();
}

void FennaraExecutor::_on_async_tool_complete(const godot::Dictionary &result,
                                        int tool_index,
                                        const godot::String &tool_name,
                                        const godot::Dictionary &tool_args,
                                        uint64_t batch_generation) {
    if (_batch_cancelled ||
        (batch_generation != 0 && batch_generation != _async_batch_generation)) {
        godot::Dictionary ignored_context = _tool_log_context(tool_name, tool_index);
        ignored_context["reason"] = _batch_cancelled ? "batch_cancelled" : "stale_batch_generation";
        _log_tool_event("Async tool completion ignored", ignored_context);
        return;
    }

    bool success = result.get("success", false);
    godot::Dictionary completion_context = _tool_log_context(tool_name, tool_index);
    completion_context["success"] = success;
    completion_context["remaining_after_complete"] = _pending_async_tools - 1;
    godot::String result_keys = dictionary_keys_csv(result);
    if (!result_keys.is_empty()) {
        completion_context["result_keys"] = result_keys;
    }
    _log_tool_event("Async tool complete", completion_context);
    if (!success && result.has("error")) {
        godot::Dictionary error_context = _tool_log_context(tool_name, tool_index);
        error_context["error"] = result["error"];
        FLOG_ERR_CTX("Tool failed", error_context);
    }

    godot::String plugin_incident_id;
    if (!success) {
        godot::Dictionary incident_context = _tool_log_context(tool_name, tool_index);
        if (result.has("error")) {
            incident_context["error"] = result["error"];
        }
        if (!result_keys.is_empty()) {
            incident_context["result_keys"] = result_keys;
        }
        plugin_incident_id = Logger::record_incident(
            "plugin_tool_execution",
            godot::String(result.get("error_code", "tool_failed")),
            godot::String("Plugin tool failed: ") + tool_name,
            incident_context
        );
    }

    godot::Dictionary wrapped;
    wrapped["tool"] = tool_name;
    wrapped["raw_result"] = result;
    wrapped["result"] = tool_results::format_for_model(tool_name, tool_args, result);
    godot::Dictionary plugin_metadata = _tool_result_metadata(tool_name, tool_index);
    if (!plugin_incident_id.is_empty()) {
        plugin_metadata["plugin_incident_id"] = plugin_incident_id;
    }
    wrapped["plugin_metadata"] = plugin_metadata;

    _async_results[tool_index] = wrapped;
    _pending_async_tools--;
    _check_completion();
}

void FennaraExecutor::_check_completion() {
    if (_batch_cancelled) {
        return;
    }

    if (_pending_async_tools <= 0) {
        if (!_modified_scenes.empty()) {
            _capture_scene_warnings();
            return;
        }

        godot::Dictionary complete_context = _batch_log_context();
        complete_context["total"] = static_cast<int64_t>(_async_results.size());
        _log_tool_event("Batch complete", complete_context);
        _print_fennara_activity("Actions complete.");
        _active_async_tools.clear();
        FennaraSnapshotManager::set_active(nullptr);
        emit_signal("all_tools_completed", _async_results);
        _clear_execution_context();
    }
}

} // namespace fennara
