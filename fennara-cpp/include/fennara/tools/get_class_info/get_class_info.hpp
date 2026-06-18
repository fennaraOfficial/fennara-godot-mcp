#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara {

class FennaraGetClassInfoTool : public godot::RefCounted {
    GDCLASS(FennaraGetClassInfoTool, godot::RefCounted);

protected:
    static void _bind_methods();

public:
    static godot::Dictionary execute(const godot::Dictionary &args);
    static godot::String _type_to_string(int type);
    static godot::String _hint_to_string(int hint);
    static godot::Variant _serialize_default(const godot::Variant &value);
};

} // namespace fennara
