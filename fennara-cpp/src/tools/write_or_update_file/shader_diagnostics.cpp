#include "fennara/tools/write_or_update_file.hpp"

#include "fennara/warning_capture.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/shader.hpp>

namespace fennara {

void FennaraWriteOrUpdateFileTool::_append_shader_diagnostics(
    godot::Dictionary &result, const godot::String &file_path,
    const godot::String &content) {
    if (!file_path.ends_with(".gdshader")) {
        return;
    }

    godot::Ref<FennaraWarningCapture> capture;
    capture.instantiate();
    godot::OS::get_singleton()->add_logger(capture);

    godot::Ref<godot::Shader> shader;
    shader.instantiate();
    shader->set_code(content);
    godot::RID shader_rid = shader->get_rid();
    godot::List<godot::PropertyInfo> shader_params;
    shader->get_shader_uniform_list(&shader_params);

    godot::OS::get_singleton()->remove_logger(capture);

    godot::Array captured = capture->get_captured();
    godot::Array diagnostics;
    int total_errors = 0;
    int total_warnings = 0;
    bool has_actionable_error = false;

    for (int i = 0; i < captured.size(); i++) {
        if (captured[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }

        godot::Dictionary entry = captured[i];
        godot::String type = entry.get("type", "");
        if (type != "shader_error" && type != "error" && type != "warning") {
            continue;
        }

        godot::String message = entry.get("message", "");
        bool is_wrapper_error =
            message.find("Shader compilation failed") != -1 ||
            message.find("Method/function failed") != -1;
        if (!is_wrapper_error && type != "warning") {
            has_actionable_error = true;
        }
    }

    for (int i = 0; i < captured.size(); i++) {
        if (captured[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }

        godot::Dictionary entry = captured[i];
        godot::String type = entry.get("type", "");
        if (type != "shader_error" && type != "error" && type != "warning") {
            continue;
        }

        godot::String severity = type == "warning" ? "warning" : "error";
        godot::String message = entry.get("message", "");
        bool is_wrapper_error =
            message.find("Shader compilation failed") != -1 ||
            message.find("Method/function failed") != -1;
        if (is_wrapper_error && has_actionable_error) {
            continue;
        }

        godot::Dictionary diagnostic;
        diagnostic["severity"] = severity;
        diagnostic["message"] = message;
        diagnostic["file"] = file_path;
        diagnostic["line"] = entry.get("line", 0);
        diagnostic["column"] = 0;
        diagnostic["source"] = "shader_parser";
        diagnostic["type"] = type;
        diagnostics.append(diagnostic);

        if (severity == "warning") {
            total_warnings++;
        } else {
            total_errors++;
        }
    }

    result["diagnostics"] = diagnostics;
    result["diagnostic_success"] = true;
    result["diagnostic_mode"] = "shader_parser";
    result["total_errors"] = total_errors;
    result["total_warnings"] = total_warnings;
    result["total_info"] = 0;
    result["total_hints"] = 0;
    result["total_diagnostics"] = diagnostics.size();
}

} // namespace fennara
