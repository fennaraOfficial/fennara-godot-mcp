#include "fennara/tools/write_or_update_file.hpp"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara {

godot::Dictionary FennaraWriteOrUpdateFileTool::_stamp_result(
    godot::Dictionary result, const godot::Dictionary &args) {
    result["tool_name"] = "write_or_update_file";
    result["format_version"] = "write-or-update-file-result-v1";
    bool success = result.get("success", false);
    result["status"] = success ? "success" : "failed";
    if (!result.has("mode") && args.has("mode")) {
        result["mode"] = args.get("mode", "");
    }

    godot::String file_path =
        result.get("file_path", args.get("file_path", ""));
    if (!file_path.is_empty() && !file_path.begins_with("res://")) {
        file_path = "res://" + file_path;
    }
    if (!file_path.is_empty()) {
        result["file_path"] = file_path;
    }

    godot::Dictionary summary;
    summary["status"] = result.get("status", "failed");
    summary["mode"] = result.get("mode", args.get("mode", ""));
    summary["file_path"] = result.get("file_path", file_path);
    summary["line_count"] = result.get("line_count", 0);
    summary["created"] = result.get("created", false);
    summary["replacements_made"] = result.get("replacements_made", 0);
    summary["diagnostic_success"] = result.get("diagnostic_success", false);
    summary["total_errors"] = result.get("total_errors", 0);
    summary["total_warnings"] = result.get("total_warnings", 0);
    summary["diagnostic_count"] =
        result.get("diagnostics", godot::Array()).get_type() ==
                godot::Variant::ARRAY
            ? godot::Array(result.get("diagnostics", godot::Array())).size()
            : 0;
    summary["shader_owner_count"] = result.get("shader_owner_count", 0);
    summary["reserialized_resource_count"] =
        result.get("reserialized_resources", godot::Array()).get_type() ==
                godot::Variant::ARRAY
            ? godot::Array(result.get("reserialized_resources", godot::Array())).size()
            : 0;
    if (result.has("attached_to_scene")) {
        summary["attached_to_scene"] = result["attached_to_scene"];
        summary["attached_to_node"] = result.get("attached_to_node", "");
    }
    result["summary"] = summary;
    return result;
}

} // namespace fennara
