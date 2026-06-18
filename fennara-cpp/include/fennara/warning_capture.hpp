#pragma once

#include <godot_cpp/classes/logger.hpp>
#include <godot_cpp/classes/script_backtrace.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace fennara {

class FennaraWarningCapture : public godot::Logger {
    GDCLASS(FennaraWarningCapture, godot::Logger)

protected:
    static void _bind_methods();

public:
    void _log_error(const godot::String &p_function, const godot::String &p_file,
                    int32_t p_line, const godot::String &p_code,
                    const godot::String &p_rationale, bool p_editor_notify,
                    int32_t p_error_type,
                    const godot::TypedArray<godot::Ref<godot::ScriptBacktrace>> &p_script_backtraces) override;

    void _log_message(const godot::String &p_message, bool p_error) override;

    godot::Array get_captured() const;
    void clear();

private:
    godot::Array _captured;
};

} // namespace fennara
