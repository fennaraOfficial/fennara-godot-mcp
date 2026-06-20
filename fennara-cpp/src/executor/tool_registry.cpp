#include "fennara/executor.hpp"
#include "fennara/addon_access.hpp"
#include "fennara/logger.hpp"

#include "fennara/tools/file_ops/file_ops.hpp"
#include "fennara/tools/get_class_info/get_class_info.hpp"
#include "fennara/tools/get_node_properties/get_node_properties.hpp"
#include "fennara/tools/get_scene_tree.hpp"
#include "fennara/tools/project_settings.hpp"
#include "fennara/tools/read_file.hpp"
#include "fennara/tools/run_scene_edit_script.hpp"
#include "fennara/tools/runtime_script.hpp"
#include "fennara/tools/runtime_session.hpp"
#include "fennara/tools/save_custom_resource.hpp"
#include "fennara/tools/scrape_editor.hpp"
#include "fennara/tools/validate_scene.hpp"
#include "fennara/tools/write_or_update_file.hpp"

namespace fennara {

namespace {

bool append_if_blocked(const godot::String &path,
                       godot::Array &blocked) {
    if (path.strip_edges().is_empty()) {
        return false;
    }
    godot::Dictionary block;
    if (!addon_access::is_path_allowed(path, false, block)) {
        blocked.append(block);
        return true;
    }
    return false;
}

void append_array_blocks(const godot::Dictionary &args,
                         const godot::String &key,
                         godot::Array &blocked) {
    godot::Variant value = args.get(key, godot::Variant());
    if (value.get_type() != godot::Variant::ARRAY) {
        return;
    }
    godot::Array paths = value;
    for (int i = 0; i < paths.size(); i++) {
        if (paths[i].get_type() == godot::Variant::STRING) {
            append_if_blocked(paths[i], blocked);
        }
    }
}

godot::Dictionary blocked_tool_result(const godot::String &name,
                                      const godot::Dictionary &args) {
    godot::Array blocked;
    if (name == "script_diagnostics") {
        append_array_blocks(args, "file_paths", blocked);
    } else if (name == "read_file") {
        append_array_blocks(args, "file_paths", blocked);
    } else if (name == "write_or_update_file") {
        append_if_blocked(args.get("file_path", ""), blocked);
        append_if_blocked(args.get("attach_to_scene", ""), blocked);
    } else if (name == "get_scene_tree" || name == "validate_scene") {
        append_array_blocks(args, "scene_paths", blocked);
    } else if (name == "run_scene_edit_script") {
        append_if_blocked(args.get("scene_path", ""), blocked);
        append_if_blocked(args.get("script_path", ""), blocked);
    } else if (name == "save_custom_resource") {
        append_if_blocked(args.get("resource_path", ""), blocked);
        append_if_blocked(args.get("script_path", ""), blocked);
        append_if_blocked(args.get("resource_type", ""), blocked);
    } else if (name == "screenshot_scene") {
        append_if_blocked(args.get("scene_path", ""), blocked);
    } else if (name == "runtime_session") {
        append_if_blocked(args.get("scene_path", ""), blocked);
    } else if (name == "runtime_script") {
        append_if_blocked(args.get("script_path", ""), blocked);
    } else if (name == "get_node_properties") {
        append_if_blocked(args.get("scene_path", ""), blocked);
    }

    if (blocked.is_empty()) {
        return godot::Dictionary();
    }

    godot::Dictionary first = blocked[0];
    godot::Dictionary result = first;
    result["tool_name"] = name;
    result["blocked_paths"] = blocked;
    return result;
}

} // namespace

bool FennaraExecutor::has_tool(const godot::String &name) {
    return name == "read_file" ||
           name == "file_ops" ||
           name == "write_or_update_file" ||
           name == "get_scene_tree" ||
           name == "save_custom_resource" || name == "run_scene_edit_script" ||
           name == "script_diagnostics" ||
           name == "screenshot_scene" ||
           name == "get_node_properties" || name == "get_class_info" ||
           name == "validate_scene" || name == "project_settings" ||
           name == "runtime_session" ||
           name == "runtime_script" ||
           name == "scrape_editor";
}

bool FennaraExecutor::_is_thread_safe(const godot::String &name) {
    return name == "write_or_update_file";
}

godot::Dictionary FennaraExecutor::execute_tool(const godot::String &name,
                                         const godot::Dictionary &args) {
    godot::String arg_keys;
    godot::Array keys = args.keys();
    for (int i = 0; i < keys.size(); i++) {
        if (i > 0) arg_keys += ",";
        arg_keys += godot::String(keys[i]);
    }
    FLOG_TOOL(godot::String("Execute: name=") + name + " args=[" + arg_keys + "]");

    godot::Dictionary blocked = blocked_tool_result(name, args);
    if (!blocked.is_empty()) {
        FLOG_ERR(godot::String("Tool blocked by addon policy: name=") + name);
        return blocked;
    }

    godot::Dictionary result;
    if (name == "read_file") result = FennaraReadFileTool::execute(args);
    else if (name == "file_ops") result = FennaraFileOpsTool::execute(args);
    else if (name == "write_or_update_file") result = FennaraWriteOrUpdateFileTool::execute(args);
    else if (name == "get_scene_tree") result = FennaraGetSceneTreeTool::execute(args);
    else if (name == "save_custom_resource") result = FennaraSaveCustomResourceTool::execute(args);
    else if (name == "run_scene_edit_script") result = FennaraRunSceneEditScriptTool::execute(args);
    else if (name == "get_node_properties") result = FennaraGetNodePropertiesTool::execute(args);
    else if (name == "get_class_info") result = FennaraGetClassInfoTool::execute(args);
    else if (name == "validate_scene") result = FennaraValidateSceneTool::execute(args);
    else if (name == "project_settings") result = FennaraProjectSettingsTool::execute(args);
    else if (name == "runtime_session") result = FennaraRuntimeSessionTool::execute(args);
    else if (name == "runtime_script") result = FennaraRuntimeScriptTool::submit(args);
    else if (name == "scrape_editor") result = FennaraScrapeEditorTool::execute(args);
    else {
        FLOG_ERR(godot::String("Unknown tool: name=") + name);
        godot::Dictionary err;
        err["success"] = false;
        err["error"] = "Unknown C++ sync tool: " + name + ". Make sure async tools are executed via execute_tool_calls_async.";
        return err;
    }

    bool success = result.get("success", false);
    if (success) {
        godot::String result_keys;
        godot::Array rkeys = result.keys();
        for (int i = 0; i < rkeys.size(); i++) {
            if (i > 0) result_keys += ",";
            result_keys += godot::String(rkeys[i]);
        }
        FLOG_TOOL(godot::String("Done: name=") + name + " success=true result_keys=[" + result_keys + "]");
    } else {
        godot::String error_msg = result.get("error", "unknown error");
        FLOG_ERR(godot::String("Tool failed: name=") + name + " error=" + error_msg);
    }

    return result;
}

} // namespace fennara
