#include "fennara/tools/run_scene_edit_script/internal.hpp"

#include "fennara/helpers.hpp"
#include "fennara/tools/write_or_update_file.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace fennara::run_scene_edit_script_internal {

namespace {

godot::String make_safe_name_part(const godot::String &value,
                                  const godot::String &fallback) {
    godot::String safe = value.strip_edges().to_lower();
    safe = safe.replace(" ", "_")
               .replace("/", "_")
               .replace("\\", "_")
               .replace(":", "_")
               .replace("@", "_")
               .replace(".", "_");
    if (safe.is_empty()) {
        safe = fallback;
    }
    return safe;
}

} // namespace

godot::String make_temp_script_path(const godot::String &scene_path) {
    uint64_t ticks = godot::Time::get_singleton()->get_ticks_usec();
    godot::String scene_name = scene_path.get_file().get_basename();
    scene_name = make_safe_name_part(scene_name, "scene");
    return "res://.fennara/tmp/editor_scripts/run_" + scene_name + "_" +
           godot::String::num_uint64(ticks) + ".gd";
}

godot::String write_or_resolve_script_path(const godot::String &normalized_scene,
                                           const godot::String &code,
                                           const godot::String &script_path,
                                           godot::Dictionary &result) {
    if (!code.is_empty()) {
        godot::String effective_script_path = make_temp_script_path(normalized_scene);

        godot::Dictionary write_args;
        write_args["mode"] = "write";
        write_args["file_path"] = effective_script_path;
        write_args["content"] = code;

        godot::Dictionary write_result = FennaraWriteOrUpdateFileTool::execute(write_args);
        if (!(bool)write_result.get("success", false)) {
            result = write_result;
            result["scene_path"] = normalized_scene;
            result["script_path"] = effective_script_path;
            return godot::String();
        }
        return write_result.get("file_path", effective_script_path);
    }

    godot::String effective_script_path = normalize_path(script_path);
    if (!effective_script_path.ends_with(".gd")) {
        result["success"] = false;
        result["error"] = "script_path must point to a .gd file.";
        result["scene_path"] = normalized_scene;
        result["script_path"] = effective_script_path;
        return godot::String();
    }
    if (!godot::FileAccess::file_exists(effective_script_path)) {
        result["success"] = false;
        result["error"] = "Script file not found: " + effective_script_path;
        result["scene_path"] = normalized_scene;
        result["script_path"] = effective_script_path;
        return godot::String();
    }
    return effective_script_path;
}

godot::Ref<godot::GDScript> load_script(const godot::String &script_path,
                                        godot::Dictionary &result) {
    godot::Ref<godot::GDScript> script = godot::ResourceLoader::get_singleton()->load(
        script_path, "GDScript", godot::ResourceLoader::CACHE_MODE_IGNORE);
    if (!script.is_valid()) {
        result["success"] = false;
        result["error"] = "Failed to load script: " + script_path;
        result["runtime_errors"] = godot::Array();
        result["logs"] = godot::Array();
        return script;
    }

    godot::Error reload_err = script->reload();
    if (reload_err != godot::OK) {
        godot::Array runtime_errors;
        runtime_errors.append(make_runtime_error(
            "Script reload failed before execution. Patch the saved script_path and rerun.",
            "reload"));
        result["success"] = false;
        result["error"] = "Script validation failed";
        result["runtime_errors"] = runtime_errors;
        result["logs"] = godot::Array();
        script.unref();
    }

    return script;
}

bool validate_script_contract(const godot::Ref<godot::GDScript> &script,
                              godot::Dictionary &result) {
    godot::StringName base_type = script->get_instance_base_type();
    godot::StringName ref_counted_type("RefCounted");
    if (base_type == ref_counted_type ||
        godot::ClassDB::is_parent_class(base_type, ref_counted_type)) {
        return true;
    }

    result["success"] = false;
    result["error"] = "run_scene_edit_script requires the script to extend RefCounted.";
    result["runtime_errors"] =
        godot::Array::make(make_runtime_error(
            "Expected `@tool extends RefCounted` for run_scene_edit_script v1.",
            "contract"));
    result["logs"] = godot::Array();
    return false;
}

godot::Variant instantiate_runner(const godot::Ref<godot::GDScript> &script,
                                  godot::Node *root_node,
                                  godot::Dictionary &result) {
    godot::Variant instance_variant = script->new_();
    godot::Object *runner = instance_variant;
    if (runner == nullptr) {
        if (root_node != nullptr) {
            root_node->queue_free();
        }
        result["success"] = false;
        result["error"] = "Failed to instantiate script.";
        result["runtime_errors"] =
            godot::Array::make(make_runtime_error("Script instantiation returned null.",
                                                  "instantiate"));
        result["logs"] = godot::Array();
        return godot::Variant();
    }

    if (!runner->has_method("run")) {
        if (root_node != nullptr) {
            root_node->queue_free();
        }
        result["success"] = false;
        result["error"] = "Script must define func run(ctx).";
        result["runtime_errors"] =
            godot::Array::make(make_runtime_error("Missing required run(ctx) entrypoint.",
                                                  "contract"));
        result["logs"] = godot::Array();
        return godot::Variant();
    }

    return instance_variant;
}

} // namespace fennara::run_scene_edit_script_internal
