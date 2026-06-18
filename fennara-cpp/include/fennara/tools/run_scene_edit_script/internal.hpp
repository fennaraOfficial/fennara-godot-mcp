#pragma once

#include "fennara/tools/run_scene_edit_script.hpp"

#include <godot_cpp/classes/gd_script.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara::run_scene_edit_script_internal {

inline godot::Dictionary make_runtime_error(const godot::String &message,
                                            const godot::String &source) {
    godot::Dictionary entry;
    entry["source"] = source;
    entry["message"] = message;
    return entry;
}

godot::Dictionary collect_script_diagnostics(const godot::String &file_path);
void apply_diagnostics_to_result(const godot::Dictionary &diagnostics,
                                 godot::Dictionary &result);

godot::String make_temp_script_path(const godot::String &scene_path);
godot::String write_or_resolve_script_path(const godot::String &normalized_scene,
                                           const godot::String &code,
                                           const godot::String &script_path,
                                           godot::Dictionary &result);
godot::Ref<godot::GDScript> load_script(const godot::String &script_path,
                                        godot::Dictionary &result);
bool validate_script_contract(const godot::Ref<godot::GDScript> &script,
                              godot::Dictionary &result);
godot::Variant instantiate_runner(const godot::Ref<godot::GDScript> &script,
                                  godot::Node *root_node,
                                  godot::Dictionary &result);

bool load_or_prepare_scene(const godot::String &normalized_scene,
                           godot::Dictionary &result,
                           godot::Node *&root_node,
                           bool &created_new_scene,
                           bool &inherited_root_scene);
void append_capture_errors(const godot::Array &captured,
                           godot::Array &runtime_errors);
bool save_scene(godot::Node *root_node,
                const godot::String &normalized_scene,
                bool created_new_scene,
                bool inherited_root_scene,
                godot::Dictionary &result);

} // namespace fennara::run_scene_edit_script_internal
