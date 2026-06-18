#include "fennara/tools/run_scene_edit_script/internal.hpp"

#include "fennara/file_utils.hpp"
#include "fennara/lsp/gdscript_lsp.hpp"

#include <godot_cpp/classes/file_access.hpp>

namespace fennara::run_scene_edit_script_internal {

godot::Dictionary collect_script_diagnostics(const godot::String &file_path) {
    godot::Dictionary result;
    result["diagnostic_success"] = false;
    result["diagnostics"] = godot::Array();
    result["total_errors"] = 0;
    result["total_warnings"] = 0;

    godot::String resolved = file_utils::resolve_path(file_path);
    if (!godot::FileAccess::file_exists(resolved)) {
        result["diagnostic_error"] = "File not found: " + file_path;
        return result;
    }

    godot::Array files_to_check;
    files_to_check.append(resolved);

    godot::Dictionary diag_result =
        gdscript_lsp::diagnose_files(files_to_check, "fennara-run-scene-edit-script");
    if (!(bool)diag_result.get("success", false)) {
        result["diagnostic_error"] = diag_result.get("error", "Diagnostics failed");
        return result;
    }

    godot::Dictionary per_file = diag_result.get("per_file", godot::Dictionary());
    godot::Dictionary file_result = per_file.get(resolved, godot::Dictionary());

    result["diagnostic_success"] = true;
    result["diagnostics"] = file_result.get("diagnostics", godot::Array());
    result["total_errors"] = file_result.get("total_errors", 0);
    result["total_warnings"] = file_result.get("total_warnings", 0);
    return result;
}

void apply_diagnostics_to_result(const godot::Dictionary &diagnostics,
                                 godot::Dictionary &result) {
    result["script_diagnostics"] = diagnostics.get("diagnostics", godot::Array());
    result["diagnostic_success"] = diagnostics.get("diagnostic_success", false);
    result["total_errors"] = diagnostics.get("total_errors", 0);
    result["total_warnings"] = diagnostics.get("total_warnings", 0);
    if (diagnostics.has("diagnostic_error")) {
        result["diagnostic_error"] = diagnostics["diagnostic_error"];
    }
}

} // namespace fennara::run_scene_edit_script_internal
