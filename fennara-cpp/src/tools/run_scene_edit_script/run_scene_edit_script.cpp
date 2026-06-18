#include "fennara/tools/run_scene_edit_script.hpp"

#include "fennara/helpers.hpp"
#include "fennara/logger.hpp"
#include "fennara/warning_capture.hpp"
#include "fennara/tools/run_scene_edit_script/internal.hpp"
#include "fennara/tools/validate_scene.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace fennara {

using namespace run_scene_edit_script_internal;

namespace {

void stamp_result(godot::Dictionary &result) {
    result["tool_name"] = "run_scene_edit_script";
    result["format_version"] = "run-scene-edit-script-result-v1";
}

void finalize_summary(godot::Dictionary &result) {
    godot::Dictionary summary;
    bool success = result.get("success", false);
    summary["status"] = success ? "success" : "failed";
    summary["scene_path"] = result.get("scene_path", "");
    summary["script_path"] = result.get("script_path", "");
    summary["scene_created"] = result.get("scene_created", false);
    summary["scene_saved"] = result.get("scene_saved", false);
    summary["modified"] = result.get("modified", false);
    summary["diagnostic_success"] = result.get("diagnostic_success", false);
    summary["diagnostic_mode"] = result.get("diagnostic_mode", "");
    summary["diagnostic_fallback"] = result.get("diagnostic_fallback", "");
    summary["total_errors"] = result.get("total_errors", 0);
    summary["total_warnings"] = result.get("total_warnings", 0);
    summary["log_count"] = godot::Array(result.get("logs", godot::Array())).size();
    summary["runtime_error_count"] =
        godot::Array(result.get("runtime_errors", godot::Array())).size();
    if (result.has("validation") &&
        result["validation"].get_type() == godot::Variant::DICTIONARY) {
        godot::Dictionary validation = result["validation"];
        summary["validation_total_issues"] = validation.get("total_issues", 0);
        summary["validation_errors"] = validation.get("errors", 0);
        summary["validation_warnings"] = validation.get("warnings", 0);
        summary["runtime_safe"] =
            static_cast<int>(validation.get("errors", 0)) == 0;
    }
    result["status"] = summary["status"];
    result["summary"] = summary;
}

void append_saved_scene_validation(godot::Dictionary &result,
                                   const godot::String &scene_path) {
    godot::Dictionary val_args;
    godot::Array scene_paths;
    scene_paths.append(scene_path);
    val_args["scene_paths"] = scene_paths;

    godot::Dictionary validation = FennaraValidateSceneTool::execute(val_args);
    godot::Array validation_results =
        validation.get("scenes", godot::Array());
    if (validation_results.is_empty()) return;

    godot::Dictionary first = validation_results[0];
    if (godot::String(first.get("status", "")) != "success") return;

    godot::Dictionary val_summary;
    val_summary["source"] = "saved_scene_reload";
    val_summary["issues"] = first.get("issues", godot::Array());
    val_summary["checks_run"] = first.get("checks_run", 0);
    val_summary["total_issues"] = first.get("total_issues", 0);
    val_summary["errors"] = first.get("errors", 0);
    val_summary["warnings"] = first.get("warnings", 0);
    result["validation"] = val_summary;
    if (static_cast<int>(val_summary["errors"]) > 0) {
        result["runtime_safe"] = false;
        result["scene_status_note"] =
            "Scene saved, but saved-scene validation found blocking issues.";
    } else {
        result["runtime_safe"] = true;
    }
}

} // namespace

void FennaraRunSceneEditScriptTool::_bind_methods() {
    godot::ClassDB::bind_static_method(
        "FennaraRunSceneEditScriptTool", godot::D_METHOD("execute", "args"),
        &FennaraRunSceneEditScriptTool::execute);
}

godot::Dictionary FennaraRunSceneEditScriptTool::execute(const godot::Dictionary &args) {
    godot::Dictionary prepared = prepare_execution(args);
    if (!(bool)prepared.get("success", false)) {
        finalize_summary(prepared);
        return prepared;
    }

    godot::String effective_script_path = prepared.get("script_path", "");
    godot::Dictionary diagnostics = collect_script_diagnostics(effective_script_path);
    apply_diagnostics_to_result(diagnostics, prepared);

    if (!(bool)diagnostics.get("diagnostic_success", false)) {
        prepared["diagnostic_mode"] = "direct_script_load";
        prepared["diagnostic_fallback"] = "direct_script_load";
    } else {
        prepared["diagnostic_mode"] = "lsp";
    }

    if ((bool)diagnostics.get("diagnostic_success", false) &&
        (int)diagnostics.get("total_errors", 0) > 0) {
        prepared["success"] = false;
        prepared["error"] = "Script diagnostics reported errors. Patch the saved script_path and rerun.";
        prepared["runtime_errors"] = godot::Array();
        prepared["logs"] = godot::Array();
        finalize_summary(prepared);
        return prepared;
    }

    godot::Dictionary executed = execute_prepared(prepared);
    finalize_summary(executed);
    return executed;
}

godot::Dictionary FennaraRunSceneEditScriptTool::prepare_execution(const godot::Dictionary &args) {
    godot::Dictionary result;
    stamp_result(result);
    result["scene_created"] = false;
    result["scene_saved"] = false;
    result["scene_status_note"] =
        "Scene has not been created or saved yet. The scene will only be created or updated if execution succeeds.";

    godot::String scene_path = args.get("scene_path", "");
    godot::String code = args.get("code", "");
    godot::String script_path = args.get("script_path", "");

    if (scene_path.is_empty()) {
        result["success"] = false;
        result["error"] = "scene_path required";
        return result;
    }
    if (code.is_empty() == script_path.is_empty()) {
        result["success"] = false;
        result["error"] = "Provide exactly one of code or script_path.";
        return result;
    }

    godot::String normalized_scene = normalize_path(scene_path);
    if (!normalized_scene.ends_with(".tscn") && !normalized_scene.ends_with(".scn")) {
        result["success"] = false;
        result["error"] = "scene_path must point to a .tscn or .scn file.";
        return result;
    }

    godot::String effective_script_path =
        write_or_resolve_script_path(normalized_scene, code, script_path, result);
    if (effective_script_path.is_empty()) {
        return result;
    }

    result["scene_path"] = normalized_scene;
    result["script_path"] = effective_script_path;
    result["success"] = true;
    return result;
}

godot::Dictionary FennaraRunSceneEditScriptTool::execute_prepared(
    const godot::Dictionary &prepared_args) {
    godot::Dictionary result = prepared_args;
    stamp_result(result);
    godot::String normalized_scene = prepared_args.get("scene_path", "");
    godot::String effective_script_path = prepared_args.get("script_path", "");
    result["scene_created"] = false;
    result["scene_saved"] = false;
    result["scene_status_note"] =
        "Scene has not been created or saved yet. The scene will only be created or updated if execution succeeds.";

    godot::Ref<godot::GDScript> script = load_script(effective_script_path, result);
    if (!script.is_valid() || !validate_script_contract(script, result)) {
        finalize_summary(result);
        return result;
    }

    godot::Node *root_node = nullptr;
    bool created_new_scene = false;
    bool inherited_root_scene = false;
    if (!load_or_prepare_scene(normalized_scene, result, root_node, created_new_scene, inherited_root_scene)) {
        finalize_summary(result);
        return result;
    }
    bool scene_exists = !created_new_scene;

    godot::Ref<FennaraRunSceneEditScriptContext> ctx;
    ctx.instantiate();
    ctx->configure(root_node, normalized_scene, scene_exists);

    godot::Variant runner_variant = instantiate_runner(script, root_node, result);
    godot::Object *runner = runner_variant;
    if (runner == nullptr) {
        finalize_summary(result);
        return result;
    }

    godot::Ref<FennaraWarningCapture> capture;
    capture.instantiate();
    godot::OS::get_singleton()->add_logger(capture);
    runner->call("run", ctx.ptr());
    godot::OS::get_singleton()->remove_logger(capture);

    godot::Array runtime_errors = ctx->get_edit_errors();
    append_capture_errors(capture->get_captured(), runtime_errors);

    root_node = ctx->get_scene_root();
    if (runtime_errors.is_empty() && root_node == nullptr) {
        runtime_errors.append(make_runtime_error(
            "Script did not produce a scene root. For a new scene, call ctx.set_scene_root(root).",
            "contract"));
    }

    result["logs"] = ctx->get_logs();
    result["runtime_errors"] = runtime_errors;
    result["modified"] = ctx->was_modified();

    if (!runtime_errors.is_empty()) {
        if (root_node != nullptr) {
            root_node->queue_free();
        }
        result["success"] = false;
        result["error"] = "Editor script execution failed.";
        finalize_summary(result);
        return result;
    }

    if (!created_new_scene && !(bool)result.get("modified", false)) {
        if (root_node != nullptr) {
            root_node->queue_free();
        }
        result["scene_saved"] = false;
        result["scene_created"] = false;
        result["scene_status_note"] =
            "Scene was inspected but not saved because the script did not mark it modified.";
        result["runtime_safe"] = true;
        result["success"] = true;
        finalize_summary(result);
        return result;
    }

    if (!save_scene(root_node, normalized_scene, created_new_scene, inherited_root_scene, result)) {
        finalize_summary(result);
        return result;
    }

    FLOG_TOOL(godot::String("run_scene_edit_script: scene=") + normalized_scene +
              " script=" + effective_script_path +
              " created=" + (created_new_scene ? "true" : "false"));

    append_saved_scene_validation(result, normalized_scene);

    result["success"] = true;
    finalize_summary(result);
    return result;
}

} // namespace fennara
