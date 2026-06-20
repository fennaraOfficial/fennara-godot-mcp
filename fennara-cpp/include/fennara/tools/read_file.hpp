#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace fennara {

class FennaraReadFileTool : public godot::RefCounted {
    GDCLASS(FennaraReadFileTool, godot::RefCounted);

protected:
    static void _bind_methods();

public:
    static godot::Dictionary execute(const godot::Dictionary &args);
};

} // namespace fennara
