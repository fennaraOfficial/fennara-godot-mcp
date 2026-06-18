#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara {

class FennaraRuntimeScriptTool : public godot::RefCounted {
    GDCLASS(FennaraRuntimeScriptTool, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    static godot::Dictionary submit(const godot::Dictionary &args);
};

} // namespace fennara
