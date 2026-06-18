#include "fennara/tools/write_or_update_file.hpp"

#include "fennara/addon_access.hpp"
#include "fennara/helpers.hpp"
#include "fennara/logger.hpp"

#include <godot_cpp/classes/file_access.hpp>

namespace fennara {

godot::Dictionary FennaraWriteOrUpdateFileTool::_execute_update(
    const godot::Dictionary &args) {
    godot::Dictionary result;

    godot::String file_path = args.get("file_path", "");
    godot::String old_string = args.get("old_string", "");
    godot::String new_string = args.get("new_string", "");
    bool replace_all = args.get("replace_all", false);

    if (old_string.is_empty()) {
        result["success"] = false;
        result["error"] = "old_string required for update mode";
        return result;
    }

    godot::String normalized_path = _normalize_file_path(file_path);
    FLOG_TOOL(godot::String("Write: path=") + normalized_path +
              " mode=update replace_all=" +
              (replace_all ? "true" : "false"));

    godot::Dictionary addon_block;
    if (!addon_access::is_path_allowed(normalized_path, false, addon_block)) {
        FLOG_ERR(godot::String("Write: blocked addon path ") + normalized_path);
        result = addon_block;
        result["mode"] = "update";
        result["file_path"] = normalized_path;
        return result;
    }

    if (_is_protected_path(normalized_path)) {
        FLOG_ERR(godot::String("Write: protected path ") + normalized_path);
        result["success"] = false;
        result["error"] = fennara::protected_path_error(normalized_path);
        return result;
    }

    if (!godot::FileAccess::file_exists(normalized_path)) {
        FLOG_ERR(godot::String("Write: file not found ") + normalized_path);
        result["success"] = false;
        result["error"] = "File does not exist: " + file_path;
        return result;
    }

    godot::Dictionary read_result = _read_content(normalized_path, file_path);
    if (!(bool)read_result.get("success", false)) {
        return read_result;
    }

    godot::String current_content = read_result.get("content", "");
    int occurrence_count = current_content.count(old_string);
    if (occurrence_count == 0) {
        godot::String old_preview = old_string.length() > 80
            ? old_string.substr(0, 80) + "..."
            : old_string;
        FLOG_ERR(godot::String("Write: old_string not found in ") +
                 normalized_path + " old_string=\"" + old_preview + "\"");
        result["success"] = false;
        result["error"] =
            "YOU MUST READ THIS FILE FIRST BEFORE EDITING! OLD_STRING NOT "
            "FOUND IN FILE: " +
            file_path;
        return result;
    }
    if (occurrence_count > 1 && !replace_all) {
        FLOG_ERR(godot::String("Write: old_string found ") +
                 godot::String::num_int64(occurrence_count) + " times in " +
                 normalized_path);
        result["success"] = false;
        result["error"] =
            "YOU MUST READ THIS FILE FIRST BEFORE EDITING! OLD_STRING APPEARS "
            + godot::String::num_int64(occurrence_count) +
            " TIMES IN FILE. PROVIDE MORE SURROUNDING CODE TO MAKE IT UNIQUE, "
            "OR SET REPLACE_ALL=TRUE";
        return result;
    }

    _snapshot_before_write(normalized_path, true);

    godot::String new_content;
    if (replace_all) {
        new_content = current_content.replace(old_string, new_string);
    } else {
        int pos = current_content.find(old_string);
        new_content = current_content.substr(0, pos) + new_string +
                      current_content.substr(pos + old_string.length());
    }

    godot::Dictionary write_result =
        _write_content(normalized_path, new_content, file_path, true);
    if (!(bool)write_result.get("success", false)) {
        return write_result;
    }

    fennara::notify_editor_filesystem(normalized_path);
    if (normalized_path.ends_with(".gdshader")) {
        _refresh_cached_shader_resource(normalized_path, new_content);
        _reserialize_shader_owners(normalized_path, result);
    }

    FLOG_TOOL(godot::String("Write: done, replacements=") +
              godot::String::num_int64(replace_all ? occurrence_count : 1) +
              " lines=" +
              godot::String::num_int64(new_content.split("\n").size()));
    result["success"] = true;
    result["mode"] = "update";
    result["file_path"] = normalized_path;
    result["line_count"] = new_content.split("\n").size();
    result["replacements_made"] = replace_all ? occurrence_count : 1;
    _append_shader_diagnostics(result, normalized_path, new_content);
    return result;
}

} // namespace fennara
