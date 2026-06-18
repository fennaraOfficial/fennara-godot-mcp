#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/scene_state.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace fennara {

class FennaraValidateSceneTool : public godot::RefCounted {
    GDCLASS(FennaraValidateSceneTool, godot::RefCounted);

protected:
    static void _bind_methods();

public:
    static godot::Dictionary execute(const godot::Dictionary &args);

private:
    // Individual check functions — each appends to `issues` Array
    static void _check_missing_ext_resources(
        const godot::String &scene_path, godot::Array &issues);
    static void _check_script_extends_mismatch(
        const godot::Ref<godot::SceneState> &state, godot::Array &issues);
    static void _check_unset_export_vars(
        const godot::Ref<godot::SceneState> &state, godot::Array &issues);
    static void _check_duplicate_siblings(
        const godot::String &scene_path, godot::Array &issues);
    static void _check_invalid_node_paths(
        const godot::String &scene_path, godot::Array &issues);
    static void _check_script_node_references(
        const godot::String &scene_path, godot::Array &issues);
    static void _check_cyclic_dependencies(
        const godot::String &scene_path, godot::Array &issues);

    // Helpers
    static godot::String _build_node_path(
        const godot::Ref<godot::SceneState> &state, int node_idx);
    static bool _inherits_class(const godot::StringName &class_name,
                                const godot::StringName &base_class);
    static void _add_issue(godot::Array &issues,
                           const godot::String &node_path,
                           const godot::String &check_name,
                           const godot::String &severity,
                           const godot::String &message,
                           const godot::Dictionary &extra = godot::Dictionary());
    static godot::String _get_script_path(
        const godot::Ref<godot::SceneState> &state, int node_idx);

    // Cyclic dependency recursive helper
    static bool _detect_cycle(const godot::String &scene_path,
                              godot::Array &chain,
                              godot::Dictionary &visited,
                              godot::Array &issues);
};

} // namespace fennara
