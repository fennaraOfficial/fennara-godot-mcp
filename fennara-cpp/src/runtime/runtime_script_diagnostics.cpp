#include "fennara/runtime/runtime_script_diagnostics.hpp"

#include "fennara/file_utils.hpp"
#include "fennara/lsp/gdscript_lsp.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/gd_script.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

namespace fennara::runtime_script_diagnostics {
namespace {

godot::Dictionary make_base() {
    godot::Dictionary result;
    result["diagnostic_success"] = false;
    result["diagnostic_mode"] = "gdscript_lsp";
    result["script_diagnostics"] = godot::Array();
    result["total_errors"] = 0;
    result["total_warnings"] = 0;
    return result;
}

void add_fallback_load_result(const godot::String &script_path,
                              godot::Dictionary &result) {
    result["fallback_mode"] = "direct_script_load";

    godot::Ref<godot::GDScript> script =
        godot::ResourceLoader::get_singleton()->load(
            script_path, "GDScript", godot::ResourceLoader::CACHE_MODE_IGNORE);
    if (!script.is_valid()) {
        result["fallback_success"] = false;
        result["blocking_error"] = true;
        result["diagnostic_error"] =
            "Runtime script diagnostics were unavailable and direct script load failed.";
        return;
    }

    godot::Error reload_err = script->reload();
    if (reload_err != godot::OK) {
        result["fallback_success"] = false;
        result["blocking_error"] = true;
        result["diagnostic_error"] =
            "Runtime script reload failed before dispatch.";
        return;
    }

    result["fallback_success"] = true;
    result["blocking_error"] = false;
}

} // namespace

godot::Dictionary check(const godot::String &script_path) {
    godot::Dictionary result = make_base();
    result["script_path"] = script_path;

    godot::String resolved = file_utils::resolve_path(script_path);
    if (!godot::FileAccess::file_exists(resolved)) {
        result["blocking_error"] = true;
        result["diagnostic_error"] = "File not found: " + script_path;
        return result;
    }

    godot::Array files_to_check;
    files_to_check.append(resolved);

    godot::Dictionary diag_result =
        gdscript_lsp::diagnose_files(files_to_check, "fennara-runtime-script");
    if (!(bool)diag_result.get("success", false)) {
        result["diagnostic_error"] = diag_result.get("error", "Diagnostics failed");
        add_fallback_load_result(script_path, result);
        return result;
    }

    godot::Dictionary per_file = diag_result.get("per_file", godot::Dictionary());
    godot::Dictionary file_result = per_file.get(resolved, godot::Dictionary());
    int total_errors = (int)file_result.get("total_errors", 0);

    result["diagnostic_success"] = true;
    result["script_diagnostics"] =
        file_result.get("diagnostics", godot::Array());
    result["total_errors"] = total_errors;
    result["total_warnings"] = file_result.get("total_warnings", 0);
    result["blocking_error"] = total_errors > 0;
    return result;
}

void apply_to_result(const godot::Dictionary &diagnostics,
                     godot::Dictionary &result) {
    result["diagnostic_success"] = diagnostics.get("diagnostic_success", false);
    result["diagnostic_mode"] = diagnostics.get("diagnostic_mode", "gdscript_lsp");
    result["script_diagnostics"] =
        diagnostics.get("script_diagnostics", godot::Array());
    result["total_errors"] = diagnostics.get("total_errors", 0);
    result["total_warnings"] = diagnostics.get("total_warnings", 0);
    if (diagnostics.has("diagnostic_error")) {
        result["diagnostic_error"] = diagnostics["diagnostic_error"];
    }
    if (diagnostics.has("fallback_mode")) {
        result["fallback_mode"] = diagnostics["fallback_mode"];
    }
    if (diagnostics.has("fallback_success")) {
        result["fallback_success"] = diagnostics["fallback_success"];
    }
}

bool has_blocking_error(const godot::Dictionary &diagnostics) {
    return (bool)diagnostics.get("blocking_error", false);
}

} // namespace fennara::runtime_script_diagnostics
