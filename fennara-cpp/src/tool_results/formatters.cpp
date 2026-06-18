#include "fennara/tool_results/formatters.hpp"

#include "fennara/tool_results/envelope.hpp"
#include "fennara/tool_results/get_class_info.hpp"
#include "fennara/tool_results/get_node_properties.hpp"
#include "fennara/tool_results/get_scene_tree.hpp"
#include "fennara/tool_results/script_diagnostics.hpp"
#include "fennara/tool_results/project_settings.hpp"
#include "fennara/tool_results/run_scene_edit_script.hpp"
#include "fennara/tool_results/runtime_script.hpp"
#include "fennara/tool_results/runtime_session.hpp"
#include "fennara/tool_results/save_custom_resource.hpp"
#include "fennara/tool_results/scrape_editor.hpp"
#include "fennara/tool_results/screenshot_scene.hpp"
#include "fennara/tool_results/validate_scene.hpp"
#include "fennara/tool_results/write_or_update_file.hpp"

namespace fennara::tool_results {

namespace {

godot::Dictionary pass_through(const godot::Dictionary &result) {
    return result;
}

} // namespace

godot::Dictionary format_for_model(const godot::String &tool_name,
                                   const godot::Dictionary &args,
                                   const godot::Dictionary &raw_result) {
    (void)args;
    if (is_envelope(raw_result)) {
        return pass_through(raw_result);
    }
    if (tool_name == "script_diagnostics") {
        return pass_through(format_script_diagnostics(args, raw_result));
    }
    if (tool_name == "get_scene_tree") {
        return pass_through(format_get_scene_tree(raw_result));
    }
    if (tool_name == "get_node_properties") {
        return pass_through(format_get_node_properties(raw_result));
    }
    if (tool_name == "validate_scene") {
        return pass_through(format_validate_scene(raw_result));
    }
    if (tool_name == "get_class_info") {
        return pass_through(format_get_class_info(raw_result));
    }
    if (tool_name == "run_scene_edit_script") {
        return pass_through(format_run_scene_edit_script(raw_result));
    }
    if (tool_name == "write_or_update_file") {
        return pass_through(format_write_or_update_file(raw_result));
    }
    if (tool_name == "project_settings") {
        return pass_through(format_project_settings(raw_result));
    }
    if (tool_name == "runtime_session") {
        return pass_through(format_runtime_session(raw_result));
    }
    if (tool_name == "runtime_script") {
        return pass_through(format_runtime_script(raw_result));
    }
    if (tool_name == "scrape_editor") {
        return pass_through(format_scrape_editor(raw_result));
    }
    if (tool_name == "save_custom_resource") {
        return pass_through(format_save_custom_resource(raw_result));
    }
    if (tool_name == "screenshot_scene") {
        return pass_through(format_screenshot_scene(raw_result));
    }
    return raw_result;
}

} // namespace fennara::tool_results
