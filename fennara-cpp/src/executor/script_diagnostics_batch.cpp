#include "fennara/executor.hpp"
#include "fennara/lsp/csharp_lsp.hpp"
#include "fennara/file_utils.hpp"
#include "fennara/lsp/gdscript_lsp.hpp"

#include "fennara/tools/run_scene_edit_script.hpp"
#include "fennara/tools/validate_scene.hpp"

#include <godot_cpp/classes/file_access.hpp>

namespace fennara {
namespace {

void merge_per_file(godot::Dictionary &into, const godot::Dictionary &from) {
    godot::Array keys = from.keys();
    for (int i = 0; i < keys.size(); i++) {
        into[keys[i]] = from[keys[i]];
    }
}

godot::Dictionary run_mixed_script_diagnostics(const godot::Array &files_to_check) {
    godot::Array gd_files;
    godot::Array cs_files;
    for (int i = 0; i < files_to_check.size(); i++) {
        godot::String path = files_to_check[i];
        if (path.ends_with(".gd")) {
            gd_files.append(path);
        } else if (path.ends_with(".cs")) {
            cs_files.append(path);
        }
    }

    godot::Dictionary per_file;
    if (!gd_files.is_empty()) {
        godot::Dictionary gd_result =
            gdscript_lsp::diagnose_files(gd_files, "fennara-batch-diagnostics");
        if (!(bool)gd_result.get("success", false)) {
            return gd_result;
        }
        merge_per_file(per_file, gd_result.get("per_file", godot::Dictionary()));
    }

    if (!cs_files.is_empty()) {
        godot::Dictionary cs_result =
            csharp_lsp::diagnose_files(cs_files, "fennara-csharp-batch-diagnostics");
        if (!(bool)cs_result.get("success", false)) {
            return cs_result;
        }
        merge_per_file(per_file, cs_result.get("per_file", godot::Dictionary()));
    }

    godot::Dictionary result;
    result["success"] = true;
    result["per_file"] = per_file;
    return result;
}

} // namespace

void FennaraExecutor::_maybe_append_scene_validation(godot::Dictionary &res,
                                              const godot::String &scene_path) {
    if (res.has("validation")) {
        return;
    }

    godot::Dictionary val_args;
    godot::Array scene_paths;
    scene_paths.append(scene_path);
    val_args["scene_paths"] = scene_paths;
    godot::Dictionary validation = FennaraValidateSceneTool::execute(val_args);
    godot::Array validation_results =
        validation.get("scenes", godot::Array());
    if (!validation_results.is_empty()) {
        godot::Dictionary first = validation_results[0];
        if (godot::String(first.get("status", "")) != "success") return;
        godot::Dictionary val_summary;
        val_summary["issues"] = first.get("issues", godot::Array());
        val_summary["checks_run"] = first.get("checks_run", 0);
        val_summary["total_issues"] = first.get("total_issues", 0);
        val_summary["errors"] = first.get("errors", 0);
        val_summary["warnings"] = first.get("warnings", 0);
        res["validation"] = val_summary;
    }
}

void FennaraExecutor::_track_modified_scene(const godot::String &scene_path,
                                     int tool_index) {
    _modified_scenes.push_back({scene_path, tool_index});
}

void FennaraExecutor::_run_batch_diagnostics(uint64_t batch_generation) {
    godot::Dictionary per_file_results;
    bool batch_success = false;
    godot::String batch_error;

    godot::Array files_to_check;
    for (const auto &pw : _pending_script_writes) {
        godot::String abs_path = file_utils::resolve_path(pw.file_path);
        if (godot::FileAccess::file_exists(abs_path)) {
            files_to_check.append(abs_path);
        }
    }
    for (const auto &pending : _pending_run_scene_edit_scripts) {
        if (godot::FileAccess::file_exists(pending.resolved_script_path)) {
            bool already_added = false;
            for (int i = 0; i < files_to_check.size(); i++) {
                if (godot::String(files_to_check[i]) ==
                    pending.resolved_script_path) {
                    already_added = true;
                    break;
                }
            }
            if (!already_added) {
                files_to_check.append(pending.resolved_script_path);
            }
        }
    }

    if (files_to_check.is_empty()) {
        for (const auto &pw : _pending_script_writes) {
            godot::Dictionary file_result;
            file_result["diagnostics"] = godot::Array();
            file_result["total_errors"] = 0;
            file_result["total_warnings"] = 0;
            file_result["total_info"] = 0;
            file_result["total_hints"] = 0;
            file_result["total_diagnostics"] = 0;
            per_file_results[pw.file_path] = file_result;
        }
        for (const auto &pending : _pending_run_scene_edit_scripts) {
            godot::Dictionary file_result;
            file_result["diagnostics"] = godot::Array();
            file_result["total_errors"] = 0;
            file_result["total_warnings"] = 0;
            file_result["total_info"] = 0;
            file_result["total_hints"] = 0;
            file_result["total_diagnostics"] = 0;
            per_file_results[pending.resolved_script_path] = file_result;
        }
        batch_success = true;
        goto done;
    }

    {
        godot::Dictionary diag_result =
            run_mixed_script_diagnostics(files_to_check);
        if (!(bool)diag_result.get("success", false)) {
            batch_success = false;
            batch_error = diag_result.get("error", "Diagnostics failed");
            goto done;
        }

        godot::Dictionary abs_per_file =
            diag_result.get("per_file", godot::Dictionary());

        for (const auto &pw : _pending_script_writes) {
            godot::String resolved = file_utils::resolve_path(pw.file_path);
            godot::Dictionary file_result;
            if (abs_per_file.has(resolved)) {
                file_result = abs_per_file[resolved];
            } else {
                file_result["diagnostics"] = godot::Array();
                file_result["total_errors"] = 0;
                file_result["total_warnings"] = 0;
                file_result["total_info"] = 0;
                file_result["total_hints"] = 0;
                file_result["total_diagnostics"] = 0;
            }
            per_file_results[pw.file_path] = file_result;
        }

        for (const auto &pending : _pending_run_scene_edit_scripts) {
            godot::Dictionary file_result;
            if (abs_per_file.has(pending.resolved_script_path)) {
                file_result = abs_per_file[pending.resolved_script_path];
            } else {
                file_result["diagnostics"] = godot::Array();
                file_result["total_errors"] = 0;
                file_result["total_warnings"] = 0;
                file_result["total_info"] = 0;
                file_result["total_hints"] = 0;
                file_result["total_diagnostics"] = 0;
            }
            per_file_results[pending.resolved_script_path] = file_result;
        }

        batch_success = true;
    }

done:
    {
        std::lock_guard<std::mutex> lock(_batch_diag_mutex);
        _batch_diag_results = godot::Dictionary();
        _batch_diag_results["success"] = batch_success;
        _batch_diag_results["per_file"] = per_file_results;
        if (!batch_error.is_empty()) {
            _batch_diag_results["error"] = batch_error;
        }
    }
    godot::Dictionary diag_done = _batch_log_context();
    diag_done["success"] = batch_success;
    diag_done["pending_script_writes"] =
        static_cast<int64_t>(_pending_script_writes.size());
    diag_done["pending_run_scene_edit_scripts"] =
        static_cast<int64_t>(_pending_run_scene_edit_scripts.size());
    if (!batch_error.is_empty()) {
        diag_done["error"] = batch_error;
    }
    _log_tool_event("Batch diagnostics finished", diag_done);
    call_deferred("_on_batch_diagnostics_complete", batch_generation);
}

void FennaraExecutor::_on_batch_diagnostics_complete(uint64_t batch_generation) {
    if (_batch_diag_thread.joinable()) {
        _batch_diag_thread.join();
    }

    if (_batch_cancelled || batch_generation != _async_batch_generation) {
        _pending_script_writes.clear();
        _pending_run_scene_edit_scripts.clear();
        _batch_diag_results = godot::Dictionary();
        return;
    }

    godot::Dictionary batch_results;
    {
        std::lock_guard<std::mutex> lock(_batch_diag_mutex);
        batch_results = _batch_diag_results;
    }

    bool batch_success = batch_results.get("success", false);
    godot::Dictionary per_file =
        batch_results.get("per_file", godot::Dictionary());
    godot::String batch_error =
        batch_results.get("error", "Diagnostics failed");
    _focus_best_edited_script(per_file);

    for (const auto &pw : _pending_script_writes) {
        godot::Dictionary merged = pw.write_result;

        if (per_file.has(pw.file_path)) {
            godot::Dictionary file_diag = per_file[pw.file_path];
            merged["diagnostics"] = file_diag.get("diagnostics", godot::Array());
            merged["total_errors"] = file_diag.get("total_errors", 0);
            merged["total_warnings"] = file_diag.get("total_warnings", 0);
            merged["total_info"] = file_diag.get("total_info", 0);
            merged["total_hints"] = file_diag.get("total_hints", 0);
            merged["total_diagnostics"] = file_diag.get(
                "total_diagnostics",
                godot::Array(file_diag.get("diagnostics", godot::Array())).size());
        } else {
            merged["diagnostics"] = godot::Array();
            merged["total_errors"] = 0;
            merged["total_warnings"] = 0;
            merged["total_info"] = 0;
            merged["total_hints"] = 0;
            merged["total_diagnostics"] = 0;
        }
        merged["diagnostic_success"] = batch_success;
        merged["diagnostic_mode"] = "lsp";
        godot::Dictionary summary = merged.get("summary", godot::Dictionary());
        summary["diagnostic_success"] = batch_success;
        summary["diagnostic_mode"] = "lsp";
        summary["total_errors"] = merged.get("total_errors", 0);
        summary["total_warnings"] = merged.get("total_warnings", 0);
        summary["total_info"] = merged.get("total_info", 0);
        summary["total_hints"] = merged.get("total_hints", 0);
        summary["diagnostic_count"] = merged.get("total_diagnostics", 0);
        merged["summary"] = summary;

        _on_async_tool_complete(merged, pw.tool_index, "write_or_update_file", pw.tool_args, batch_generation);
    }

    _pending_script_writes.clear();

    for (const auto &pending : _pending_run_scene_edit_scripts) {
        godot::Dictionary merged = pending.prepared_args;

        if (per_file.has(pending.resolved_script_path)) {
            godot::Dictionary file_diag = per_file[pending.resolved_script_path];
            merged["script_diagnostics"] =
                file_diag.get("diagnostics", godot::Array());
            merged["total_errors"] = file_diag.get("total_errors", 0);
            merged["total_warnings"] = file_diag.get("total_warnings", 0);
        } else {
            merged["script_diagnostics"] = godot::Array();
            merged["total_errors"] = 0;
            merged["total_warnings"] = 0;
        }
        merged["diagnostic_success"] = batch_success;

        if (!batch_success) {
            merged["diagnostic_error"] = batch_error;
            merged["diagnostic_mode"] = "direct_script_load";
            merged["diagnostic_fallback"] = "direct_script_load";
            godot::Dictionary executed = FennaraRunSceneEditScriptTool::execute_prepared(merged);
            if ((bool)executed.get("success", false)) {
                godot::String scene_path =
                    executed.get("scene_path", merged.get("scene_path", ""));
                if (!scene_path.is_empty()) {
                    _track_modified_scene(scene_path, pending.tool_index);
                    _maybe_append_scene_validation(executed, scene_path);
                }
            }
            _on_async_tool_complete(executed, pending.tool_index, "run_scene_edit_script", godot::Dictionary(), batch_generation);
            continue;
        }

        merged["diagnostic_mode"] = "lsp";

        if ((int)merged.get("total_errors", 0) > 0) {
            merged["success"] = false;
            merged["error"] = "Script diagnostics reported errors. Patch the saved script_path and rerun.";
            merged["runtime_errors"] = godot::Array();
            merged["logs"] = godot::Array();
            _on_async_tool_complete(merged, pending.tool_index, "run_scene_edit_script", godot::Dictionary(), batch_generation);
            continue;
        }

        godot::Dictionary executed = FennaraRunSceneEditScriptTool::execute_prepared(merged);
        if ((bool)executed.get("success", false)) {
            godot::String scene_path =
                executed.get("scene_path", merged.get("scene_path", ""));
            if (!scene_path.is_empty()) {
                _track_modified_scene(scene_path, pending.tool_index);
                _maybe_append_scene_validation(executed, scene_path);
            }
        }
        _on_async_tool_complete(executed, pending.tool_index, "run_scene_edit_script", godot::Dictionary(), batch_generation);
    }

    _pending_run_scene_edit_scripts.clear();
    _start_next_validate_scene();
}

} // namespace fennara
