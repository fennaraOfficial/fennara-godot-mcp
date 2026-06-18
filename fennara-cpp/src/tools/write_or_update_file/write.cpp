#include "fennara/tools/write_or_update_file.hpp"

#include "fennara/addon_access.hpp"
#include "fennara/helpers.hpp"
#include "fennara/logger.hpp"

#include <godot_cpp/classes/file_access.hpp>

namespace fennara {

godot::Dictionary FennaraWriteOrUpdateFileTool::_execute_write(
    const godot::Dictionary &args) {
    godot::Dictionary result;

    godot::String file_path = args.get("file_path", "");
    godot::String content = args.get("content", "");
    godot::String attach_to_scene = args.get("attach_to_scene", "");
    godot::String attach_to_node = args.get("attach_to_node", "");

    if (content.is_empty()) {
        result["success"] = false;
        result["error"] = "content required for write mode";
        return result;
    }

    godot::String normalized_path = _normalize_file_path(file_path);
    FLOG_TOOL(godot::String("Write: path=") + normalized_path + " mode=create");

    godot::Dictionary addon_block;
    if (!addon_access::is_path_allowed(normalized_path, false, addon_block)) {
        FLOG_ERR(godot::String("Write: blocked addon path ") + normalized_path);
        result = addon_block;
        result["mode"] = "write";
        result["file_path"] = normalized_path;
        return result;
    }

    if (_is_protected_path(normalized_path)) {
        FLOG_ERR(godot::String("Write: protected path ") + normalized_path);
        result["success"] = false;
        result["error"] = fennara::protected_path_error(normalized_path);
        return result;
    }

    bool file_exists = godot::FileAccess::file_exists(normalized_path);
    if (!_ensure_parent_dir(normalized_path, result)) {
        return result;
    }

    _snapshot_before_write(normalized_path, file_exists);

    godot::Dictionary write_result =
        _write_content(normalized_path, content, file_path, file_exists);
    if (!(bool)write_result.get("success", false)) {
        return write_result;
    }

    fennara::notify_editor_filesystem(normalized_path);
    if (normalized_path.ends_with(".gdshader")) {
        _refresh_cached_shader_resource(normalized_path, content);
        _reserialize_shader_owners(normalized_path, result);
    }

    FLOG_TOOL(godot::String("Write: done, lines=") +
              godot::String::num_int64(content.split("\n").size()) +
              " created=" + (!file_exists ? "true" : "false"));
    result["success"] = true;
    result["mode"] = "write";
    result["file_path"] = normalized_path;
    result["line_count"] = content.split("\n").size();
    result["created"] = !file_exists;
    _append_shader_diagnostics(result, normalized_path, content);

    if (!attach_to_scene.is_empty() && !attach_to_node.is_empty()) {
        if (!normalized_path.ends_with(".gd")) {
            result["attach_warning"] =
                "Can only attach .gd scripts to scenes. Skipping attachment.";
        } else {
            godot::Dictionary attach_result = _attach_script_to_scene(
                normalized_path, attach_to_scene, attach_to_node);
            if (bool(attach_result.get("success", false))) {
                result["attached_to_scene"] = attach_to_scene;
                result["attached_to_node"] = attach_to_node;
            } else {
                result["attach_error"] = attach_result.get(
                    "error", "Unknown error during attachment");
            }
        }
    }

    return result;
}

} // namespace fennara
