#include "fennara/local_bridge.hpp"

#include "fennara/addon_access.hpp"
#include "fennara/executor.hpp"
#include "fennara/logger.hpp"
#include "fennara/snapshot_manager.hpp"
#include "fennara/tool_call_log.hpp"
#include "fennara/tool_results/formatters.hpp"
#include "fennara/update_notice.hpp"

#include <godot_cpp/classes/time.hpp>

namespace fennara {

namespace {

bool _is_local_bridge_tool(const godot::String &tool) {
    return tool == "fennara_status" ||
           tool == "read_file" ||
           tool == "file_ops" ||
           tool == "write_or_update_file" ||
           tool == "run_scene_edit_script" ||
           tool == "get_scene_tree" ||
           tool == "save_custom_resource" ||
           tool == "script_diagnostics" ||
           tool == "screenshot_scene" ||
           tool == "get_node_properties" ||
           tool == "get_class_info" ||
           tool == "validate_scene" ||
           tool == "project_settings" ||
           tool == "runtime_session" ||
           tool == "runtime_script" ||
           tool == "scrape_editor";
}

godot::String _friendly_mcp_tool_action(const godot::String &tool,
                                        const godot::Dictionary &args) {
    if (tool == "write_or_update_file") {
        godot::String path = args.get("file_path", "");
        godot::String mode = args.get("mode", "");
        if (path.ends_with(".gd")) {
            return (mode == "write" ? "Writing script: " : "Editing script: ") + path;
        }
        return (mode == "write" ? "Writing file: " : "Editing file: ") + path;
    }
    if (tool == "read_file") {
        return "Reading project file";
    }
    if (tool == "file_ops") {
        return "Exploring project files";
    }
    if (tool == "run_scene_edit_script") {
        godot::String scene_path = args.get("scene_path", "");
        return scene_path.is_empty()
            ? godot::String("Applying scene edit script")
            : godot::String("Editing scene: ") + scene_path;
    }
    if (tool == "screenshot_scene") {
        godot::String scene_path = args.get("scene_path", "");
        return scene_path.is_empty()
            ? godot::String("Capturing scene screenshot")
            : godot::String("Capturing scene: ") + scene_path;
    }
    if (tool == "script_diagnostics") {
        return "Checking script diagnostics";
    }
    if (tool == "validate_scene") {
        return "Validating scene";
    }
    if (tool == "get_scene_tree") {
        return "Inspecting scene tree";
    }
    if (tool == "get_node_properties") {
        return "Inspecting node properties";
    }
    if (tool == "get_class_info") {
        return "Looking up Godot class info";
    }
    if (tool == "project_settings") {
        return "Updating project settings";
    }
    if (tool == "save_custom_resource") {
        return "Saving custom resource";
    }
    if (tool == "runtime_session") {
        godot::String action = args.get("action", "status");
        if (action == "start") {
            return "Starting runtime session";
        }
        if (action == "stop") {
            return "Stopping runtime session";
        }
        return "Checking runtime session";
    }
    if (tool == "runtime_script") {
        return "Running runtime script";
    }
    if (tool == "scrape_editor") {
        return "Scraping editor debugger";
    }
    if (tool == "fennara_status") {
        return "Checking Fennara status";
    }
    return "Running MCP tool: " + tool;
}

void _log_mcp_tool_start(const godot::String &tool,
                         const godot::Dictionary &args) {
    Logger::log_activity("Running 1 action...");
    Logger::log_activity(_friendly_mcp_tool_action(tool, args));
}

void _log_mcp_tool_complete() {
    Logger::log_activity("Actions complete.");
}

godot::Dictionary _fennara_status_result() {
    godot::Dictionary result;
    result["success"] = true;
    result["tool_name"] = "fennara_status";
    result["godot_connected"] = true;
    result["mcp_mode"] = "local";
    result["version"] = update_notice::status();
    result["addon_access"] = addon_access::status();
    godot::Array local_tools;
    local_tools.append("read_file");
    local_tools.append("file_ops");
    local_tools.append("write_or_update_file");
    local_tools.append("run_scene_edit_script");
    local_tools.append("get_scene_tree");
    local_tools.append("save_custom_resource");
    local_tools.append("script_diagnostics");
    local_tools.append("screenshot_scene");
    local_tools.append("get_node_properties");
    local_tools.append("get_class_info");
    local_tools.append("validate_scene");
    local_tools.append("project_settings");
    local_tools.append("runtime_session");
    local_tools.append("runtime_script");
    local_tools.append("scrape_editor");
    result["available_tools"] = local_tools;
    result["policy_note"] =
        "Fennara tools can access the whole project except Fennara's own protected addon folder.";
    return result;
}

void _queue_free_executor(godot::Object *executor) {
    if (godot::Node *node = godot::Object::cast_to<godot::Node>(executor)) {
        node->queue_free();
    }
}

godot::Dictionary _prepare_mcp_screenshot_result(const godot::Dictionary &result) {
    if (!(bool)result.get("success", false)) {
        return result;
    }

    if (result.has("images")) {
        godot::Dictionary transformed = result;
        godot::Array images = result.get("images", godot::Array());
        godot::Array transformed_images;
        for (int i = 0; i < images.size(); i++) {
            godot::Dictionary image = images[i];
            godot::Dictionary transformed_image = image;
            transformed_image.erase("image_base64");
            transformed_images.append(transformed_image);
        }
        transformed["images"] = transformed_images;
    }

    if (!result.has("image_base64")) {
        return result;
    }

    godot::Dictionary transformed = result;
    transformed.erase("image_base64");
    return transformed;
}

godot::Variant _mcp_model_facing_result(const godot::Dictionary &result) {
    godot::Variant content = result.get("content", godot::Variant());
    godot::Variant metadata = result.get("metadata", godot::Variant());
    if (content.get_type() == godot::Variant::STRING &&
        metadata.get_type() == godot::Variant::DICTIONARY) {
        return content;
    }
    return result;
}

} // namespace

void FennaraLocalBridge::_handle_message(const godot::Dictionary &message) {
    godot::String type = message.get("type", "");
    if (type == "tool_call") {
        _handle_tool_call(message);
    } else if (type == "snapshot_begin_turn") {
        _handle_snapshot_begin_turn(message);
    } else if (type == "snapshot_revert") {
        _handle_snapshot_revert(message);
    } else if (type == "active_project_changed") {
        bool active = message.get("is_active", false);
        _active_mcp_target_name = godot::String(message.get("active_project_name", "")).strip_edges();
        _active_mcp_target_path = godot::String(message.get("active_project_path", "")).strip_edges();
        if (active != _is_active_mcp_target) {
            _is_active_mcp_target = active;
            emit_signal("mcp_target_state_changed", active);
        }
    }
}

void FennaraLocalBridge::_handle_tool_call(const godot::Dictionary &message) {
    uint64_t started_at_ms = godot::Time::get_singleton()->get_ticks_msec();
    godot::String request_id = message.get("request_id", "");
    godot::String tool = message.get("tool", "");

    godot::Dictionary response;
    response["type"] = "tool_result";
    response["request_id"] = request_id;

    godot::Variant args_variant = message.get("args", godot::Dictionary());
    godot::Dictionary args;
    if (args_variant.get_type() == godot::Variant::DICTIONARY) {
        args = args_variant;
    }

    godot::Dictionary start_details;
    start_details["request_id"] = request_id;
    start_details["tool"] = tool;
    start_details["arg_keys"] = args.keys();
    FLOG_CTX("TOOL", "Local bridge tool call received", start_details);
    tool_call_log::log_received(_session_id, request_id, tool, args);

    if (request_id.is_empty()) {
        response["ok"] = false;
        response["error"] = "Missing request_id.";
        FLOG_ERR("Local bridge tool call rejected: missing request_id");
        tool_call_log::log_failed(_session_id, "missing-request-id", tool, args,
                                  "Missing request_id.", started_at_ms);
        _send_json(response);
        return;
    }

    tool_call_log::log_started(_session_id, request_id, tool);

    if (tool == "fennara_status") {
        response["ok"] = true;
        response["result"] = _fennara_status_result();
        tool_call_log::log_completed(_session_id, request_id, tool, args,
                                     godot::Dictionary(response["result"]), true,
                                     started_at_ms);
        _send_json(response);
        return;
    }

    if (!_is_local_bridge_tool(tool)) {
        response["ok"] = false;
        response["error"] = "Unsupported local bridge tool: " + tool;
        FLOG_ERR(godot::String("Local bridge unsupported tool: ") + tool);
        tool_call_log::log_failed(_session_id, request_id, tool, args,
                                  response["error"], started_at_ms);
        _send_json(response);
        return;
    }

    if (tool == "write_or_update_file" ||
        tool == "script_diagnostics" || tool == "screenshot_scene" ||
        tool == "run_scene_edit_script" || tool == "runtime_session" ||
        tool == "runtime_script" ||
        tool == "validate_scene") {
        FennaraExecutor *executor = memnew(FennaraExecutor);
        if (executor == nullptr) {
            response["ok"] = false;
            response["error"] = "Local bridge failed to create an executor for async tool execution.";
            FLOG_ERR(godot::String("Local bridge failed to create async executor tool=") + tool);
            tool_call_log::log_failed(_session_id, request_id, tool, args,
                                      response["error"], started_at_ms);
            _send_json(response);
            return;
        }
        executor->set_name("FennaraLocalBridgeExecutor");
        executor->set_execution_context("mcp:" + request_id, -1);
        if (_snapshot_mgr.is_valid()) {
            executor->set_snapshot_manager(_snapshot_mgr.ptr());
        }
        add_child(executor);

        godot::Array tool_calls;
        godot::Dictionary tool_call;
        godot::Dictionary execution_args = args;
        if (tool == "validate_scene" ||
            tool == "screenshot_scene" || tool == "runtime_session") {
            execution_args["_fennara_tool_artifact_dir"] =
                tool_call_log::result_artifact_dir(_session_id, request_id, tool);
        }
        tool_call["name"] = tool;
        tool_call["args"] = execution_args;
        tool_calls.append(tool_call);

        executor->connect(
            "all_tools_completed",
            callable_mp(this, &FennaraLocalBridge::_on_async_tool_call_completed).bind(request_id, tool, args, started_at_ms, executor),
            godot::Object::CONNECT_ONE_SHOT
        );
        godot::Dictionary async_details = start_details;
        FLOG_CTX("TOOL", "Local bridge async tool started", async_details);
        executor->execute_tool_calls_async(tool_calls);
        return;
    }

    _log_mcp_tool_start(tool, args);
    FennaraSnapshotManager::set_active(_snapshot_mgr.is_valid() ? _snapshot_mgr.ptr() : nullptr);
    godot::Dictionary raw_result = FennaraExecutor::execute_tool(tool, args);
    FennaraSnapshotManager::set_active(nullptr);
    _log_mcp_tool_complete();
    godot::Dictionary result = tool_results::format_for_model(tool, args, raw_result);
    response["ok"] = result.get("success", false);
    response["result"] = _mcp_model_facing_result(result);
    response["raw_result"] = raw_result;
    response["formatted_result"] = result;
    tool_call_log::log_completed(_session_id, request_id, tool, args,
                                 godot::Dictionary(response["result"]),
                                 response["ok"], started_at_ms);
    godot::Dictionary done_details = start_details;
    done_details["ok"] = response["ok"];
    FLOG_CTX("TOOL", "Local bridge sync tool completed", done_details);
    _send_json(response);
}

void FennaraLocalBridge::_handle_snapshot_begin_turn(const godot::Dictionary &message) {
    godot::Dictionary response;
    response["type"] = "snapshot_result";
    response["request_id"] = message.get("request_id", "");
    if (!_snapshot_mgr.is_valid()) {
        _snapshot_mgr.instantiate();
    }
    if (!_snapshot_mgr.is_valid()) {
        response["ok"] = false;
        response["error"] = "Snapshot manager is unavailable.";
        _send_json(response);
        return;
    }
    _snapshot_mgr->begin_turn(message.get("user_message", ""),
                              message.get("chat_id", ""));
    response["ok"] = true;
    response["action"] = "begin_turn";
    response["revert_count"] = _snapshot_mgr->revert_count();
    _send_json(response);
}

void FennaraLocalBridge::_handle_snapshot_revert(const godot::Dictionary &message) {
    godot::Dictionary response;
    response["type"] = "snapshot_result";
    response["request_id"] = message.get("request_id", "");
    godot::String chat_id = message.get("chat_id", "");
    if (!_snapshot_mgr.is_valid() || _snapshot_mgr->revert_count() <= 0) {
        response["ok"] = true;
        response["action"] = "revert";
        response["restored_message"] = "";
        response["revert_count"] = 0;
        _send_json(response);
        return;
    }
    if (!_snapshot_mgr->can_revert_chat(chat_id)) {
        response["ok"] = false;
        response["error"] = "Latest revert snapshot belongs to a different chat.";
        response["action"] = "revert";
        response["revert_count"] = _snapshot_mgr->revert_count();
        _send_json(response);
        return;
    }
    godot::String restored_message = _snapshot_mgr->revert(chat_id);
    response["ok"] = true;
    response["action"] = "revert";
    response["restored_message"] = restored_message;
    response["revert_count"] = _snapshot_mgr->revert_count();
    _send_json(response);
}

void FennaraLocalBridge::_on_async_tool_call_completed(const godot::Array &results,
                                                godot::String request_id,
                                                godot::String tool_name,
                                                godot::Dictionary input,
                                                uint64_t started_at_ms,
                                                godot::Object *executor) {
    godot::Dictionary response;
    response["type"] = "tool_result";
    response["request_id"] = request_id;

    if (results.is_empty()) {
        response["ok"] = false;
        response["error"] = "Async tool execution returned no results.";
        FLOG_ERR(godot::String("Local bridge async tool returned no results tool=") + tool_name);
        tool_call_log::log_failed(_session_id, request_id, tool_name, input,
                                  response["error"], started_at_ms);
        _send_json(response);
        _queue_free_executor(executor);
        return;
    }

    godot::Variant first = results[0];
    if (first.get_type() != godot::Variant::DICTIONARY) {
        response["ok"] = false;
        response["error"] = "Async tool execution returned an invalid result payload.";
        FLOG_ERR(godot::String("Local bridge async tool invalid result tool=") + tool_name);
        tool_call_log::log_failed(_session_id, request_id, tool_name, input,
                                  response["error"], started_at_ms);
        _send_json(response);
        _queue_free_executor(executor);
        return;
    }

    godot::Dictionary wrapped = first;
    godot::Dictionary result = wrapped.get("result", godot::Dictionary());
    godot::Dictionary raw_result = wrapped.get("raw_result", godot::Dictionary());
    if (tool_name == "screenshot_scene") {
        result = _prepare_mcp_screenshot_result(result);
    }
    response["ok"] = result.get("success", false);
    response["result"] = _mcp_model_facing_result(result);
    response["raw_result"] = raw_result;
    response["formatted_result"] = result;
    if (wrapped.has("plugin_metadata")) {
        response["plugin_metadata"] = wrapped["plugin_metadata"];
    }
    tool_call_log::log_completed(_session_id, request_id, tool_name, input,
                                 godot::Dictionary(response["result"]),
                                 response["ok"], started_at_ms);
    godot::Dictionary done_details;
    done_details["request_id"] = request_id;
    done_details["tool"] = tool_name;
    done_details["ok"] = response["ok"];
    done_details["result_count"] = results.size();
    FLOG_CTX("TOOL", "Local bridge async tool completed", done_details);
    _send_json(response);
    _queue_free_executor(executor);
}

} // namespace fennara
