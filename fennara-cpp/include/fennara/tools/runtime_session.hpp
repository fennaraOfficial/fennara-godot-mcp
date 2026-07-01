#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara {

class FennaraRuntimeSessionTool : public godot::RefCounted {
    GDCLASS(FennaraRuntimeSessionTool, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    static godot::Dictionary execute(const godot::Dictionary &args);
    static godot::Dictionary execute_start_after_preflight(
        const godot::Dictionary &args,
        const godot::Dictionary &build_result,
        const godot::Dictionary &preflight,
        const godot::Dictionary &script_preflight);
};

} // namespace fennara
