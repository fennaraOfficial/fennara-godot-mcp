#include "fennara/runtime/runtime_scene_preflight.hpp"

#include "fennara/file_utils.hpp"
#include "fennara/lsp/gdscript_lsp.hpp"
#include "fennara/tools/scene_io.hpp"

#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_state.hpp>

namespace fennara::runtime_scene_preflight {
namespace {

bool is_gdscript_path(const godot::String &path) {
    return path.begins_with("res://") && path.ends_with(".gd");
}

void append_unique(godot::Array &items, const godot::String &value) {
    if (value.is_empty() || items.has(value)) {
        return;
    }
    items.append(value);
}

godot::String script_path_from_variant(const godot::Variant &value) {
    if (value.get_type() != godot::Variant::OBJECT) {
        return "";
    }
    godot::Object *obj = value;
    auto *res = godot::Object::cast_to<godot::Resource>(obj);
    if (res == nullptr) {
        return "";
    }
    godot::String path = res->get_path();
    return is_gdscript_path(path) ? path : godot::String();
}

void collect_state_scripts(const godot::Ref<godot::SceneState> &state,
                           godot::Array &script_paths,
                           godot::Array &scene_paths,
                           godot::Dictionary &visited_scenes);

void collect_scene_scripts(const godot::String &scene_path,
                           godot::Array &script_paths,
                           godot::Array &scene_paths,
                           godot::Dictionary &visited_scenes) {
    if (scene_path.is_empty() || visited_scenes.has(scene_path)) {
        return;
    }
    visited_scenes[scene_path] = true;
    append_unique(scene_paths, scene_path);

    godot::Ref<godot::PackedScene> packed =
        scene_io::load_packed_scene(
            scene_path, godot::ResourceLoader::CACHE_MODE_IGNORE);
    if (!packed.is_valid()) {
        return;
    }
    godot::Ref<godot::SceneState> state = packed->get_state();
    collect_state_scripts(state, script_paths, scene_paths, visited_scenes);
}

void collect_instance_scripts(const godot::Ref<godot::PackedScene> &instance,
                              godot::Array &script_paths,
                              godot::Array &scene_paths,
                              godot::Dictionary &visited_scenes) {
    if (!instance.is_valid()) {
        return;
    }
    godot::String path = instance->get_path();
    if (path.begins_with("res://") && path.ends_with(".tscn")) {
        collect_scene_scripts(path, script_paths, scene_paths, visited_scenes);
        return;
    }
    godot::Ref<godot::SceneState> state = instance->get_state();
    collect_state_scripts(state, script_paths, scene_paths, visited_scenes);
}

void collect_state_scripts(const godot::Ref<godot::SceneState> &state,
                           godot::Array &script_paths,
                           godot::Array &scene_paths,
                           godot::Dictionary &visited_scenes) {
    if (!state.is_valid()) {
        return;
    }
    for (int node_idx = 0; node_idx < state->get_node_count(); node_idx++) {
        int prop_count = state->get_node_property_count(node_idx);
        for (int prop_idx = 0; prop_idx < prop_count; prop_idx++) {
            godot::String prop_name = state->get_node_property_name(node_idx, prop_idx);
            if (prop_name != "script") {
                continue;
            }
            godot::String script_path =
                script_path_from_variant(state->get_node_property_value(node_idx, prop_idx));
            append_unique(script_paths, script_path);
        }
        collect_instance_scripts(
            state->get_node_instance(node_idx),
            script_paths,
            scene_paths,
            visited_scenes);
    }
}

godot::Array collect_autoload_scripts() {
    godot::Array scripts;
    godot::ProjectSettings *settings = godot::ProjectSettings::get_singleton();
    if (settings == nullptr) {
        return scripts;
    }
    godot::Array props = settings->get_property_list();
    for (int i = 0; i < props.size(); i++) {
        if (props[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary prop = props[i];
        godot::String name = prop.get("name", "");
        if (!name.begins_with("autoload/")) {
            continue;
        }
        godot::String value = settings->get_setting(name, "");
        if (value.begins_with("*")) {
            value = value.substr(1);
        }
        if (is_gdscript_path(value)) {
            append_unique(scripts, value);
        }
    }
    return scripts;
}

godot::Array resolved_paths(const godot::Array &res_paths) {
    godot::Array resolved;
    for (int i = 0; i < res_paths.size(); i++) {
        godot::String path = res_paths[i];
        godot::String abs = file_utils::resolve_path(path);
        if (!abs.is_empty() && !resolved.has(abs)) {
            resolved.append(abs);
        }
    }
    return resolved;
}

godot::Dictionary make_result() {
    godot::Dictionary result;
    result["success"] = true;
    result["status"] = "success";
    result["autoload_scripts"] = godot::Array();
    result["scene_scripts"] = godot::Array();
    result["checked_scripts"] = godot::Array();
    result["dependency_scenes"] = godot::Array();
    result["diagnostics"] = godot::Array();
    result["error_count"] = 0;
    result["warning_count"] = 0;
    return result;
}

} // namespace

godot::Dictionary collect_scene_script_context(const godot::String &scene_path) {
    godot::Dictionary result = make_result();
    result["scene_path"] = scene_path;

    godot::Array autoload_scripts = collect_autoload_scripts();
    godot::Array scene_scripts;
    godot::Array dependency_scenes;
    godot::Dictionary visited_scenes;
    collect_scene_scripts(scene_path, scene_scripts, dependency_scenes, visited_scenes);

    godot::Array checked_scripts = autoload_scripts.duplicate();
    for (int i = 0; i < scene_scripts.size(); i++) {
        append_unique(checked_scripts, scene_scripts[i]);
    }

    result["autoload_scripts"] = autoload_scripts;
    result["scene_scripts"] = scene_scripts;
    result["checked_scripts"] = checked_scripts;
    result["dependency_scenes"] = dependency_scenes;
    result["checked_script_count"] = checked_scripts.size();

    return result;
}

godot::Dictionary diagnose_collected_scripts(const godot::Dictionary &context) {
    godot::Dictionary result = context.duplicate();
    godot::Array checked_scripts = result.get("checked_scripts", godot::Array());
    godot::Array files_to_check = resolved_paths(checked_scripts);
    if (files_to_check.is_empty()) {
        return result;
    }

    godot::Dictionary diag_result =
        gdscript_lsp::diagnose_files(files_to_check, "fennara-runtime-scene-preflight");
    result["diagnostic_result"] = diag_result;
    if (!(bool)diag_result.get("success", false)) {
        result["success"] = false;
        result["status"] = "failed";
        result["error"] = diag_result.get("error", "GDScript diagnostics failed");
        return result;
    }

    godot::Dictionary per_file = diag_result.get("per_file", godot::Dictionary());
    godot::Array diagnostics;
    int errors = 0;
    int warnings = 0;
    godot::Array keys = per_file.keys();
    for (int i = 0; i < keys.size(); i++) {
        godot::Dictionary file_result = per_file[keys[i]];
        errors += static_cast<int>(file_result.get("total_errors", 0));
        warnings += static_cast<int>(file_result.get("total_warnings", 0));
        godot::Array file_diags = file_result.get("diagnostics", godot::Array());
        for (int j = 0; j < file_diags.size(); j++) {
            if (file_diags[j].get_type() != godot::Variant::DICTIONARY) {
                continue;
            }
            godot::Dictionary diagnostic = file_diags[j];
            diagnostic["resolved_path"] = keys[i];
            diagnostics.append(diagnostic);
        }
    }

    result["diagnostics"] = diagnostics;
    result["error_count"] = errors;
    result["warning_count"] = warnings;
    if (errors > 0) {
        result["success"] = false;
        result["status"] = "blocked";
        result["error"] = "Scene/autoload script diagnostics have errors.";
    }
    return result;
}

godot::Dictionary check_scene_scripts(const godot::String &scene_path) {
    return diagnose_collected_scripts(collect_scene_script_context(scene_path));
}

} // namespace fennara::runtime_scene_preflight
