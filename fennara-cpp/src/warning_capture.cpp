#include "fennara/warning_capture.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace fennara {

void FennaraWarningCapture::_bind_methods() {
}

void FennaraWarningCapture::_log_error(const godot::String &p_function, const godot::String &p_file,
                                int32_t p_line, const godot::String &p_code,
                                const godot::String &p_rationale, bool p_editor_notify,
                                int32_t p_error_type,
                                const godot::TypedArray<godot::Ref<godot::ScriptBacktrace>> &p_script_backtraces) {
    godot::Dictionary entry;

    switch (p_error_type) {
        case ERROR_TYPE_WARNING:
            entry["type"] = "warning";
            break;
        case ERROR_TYPE_ERROR:
            entry["type"] = "error";
            break;
        case ERROR_TYPE_SCRIPT:
            entry["type"] = "script_error";
            break;
        case ERROR_TYPE_SHADER:
            entry["type"] = "shader_error";
            break;
        default:
            entry["type"] = "unknown";
            break;
    }

    godot::String message = p_code;
    if (!p_rationale.is_empty()) {
        message += " - " + p_rationale;
    }
    entry["message"] = message;
    entry["file"] = p_file;
    entry["line"] = p_line;
    entry["function"] = p_function;

    godot::Array backtrace_frames;
    for (int i = 0; i < p_script_backtraces.size(); i++) {
        godot::Ref<godot::ScriptBacktrace> backtrace = p_script_backtraces[i];
        if (!backtrace.is_valid() || backtrace->is_empty()) {
            continue;
        }
        int frame_count = backtrace->get_frame_count();
        for (int frame_idx = 0; frame_idx < frame_count; frame_idx++) {
            godot::Dictionary frame;
            frame["language"] = backtrace->get_language_name();
            frame["file"] = backtrace->get_frame_file(frame_idx);
            frame["line"] = backtrace->get_frame_line(frame_idx);
            frame["function"] = backtrace->get_frame_function(frame_idx);
            backtrace_frames.append(frame);
        }
    }
    if (!backtrace_frames.is_empty()) {
        entry["script_backtrace"] = backtrace_frames;
    }

    _captured.append(entry);
}

void FennaraWarningCapture::_log_message(const godot::String &p_message, bool p_error) {
    if (!p_error) {
        return;
    }

    godot::Dictionary entry;
    entry["type"] = "error";
    entry["message"] = p_message;
    entry["file"] = "";
    entry["line"] = 0;
    entry["function"] = "";
    _captured.append(entry);
}

godot::Array FennaraWarningCapture::get_captured() const {
    return _captured;
}

void FennaraWarningCapture::clear() {
    _captured.clear();
}

} // namespace fennara
