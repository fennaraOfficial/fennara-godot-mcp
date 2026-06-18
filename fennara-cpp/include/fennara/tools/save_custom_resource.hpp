#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara {

class FennaraSaveCustomResourceTool : public godot::RefCounted {
    GDCLASS(FennaraSaveCustomResourceTool, godot::RefCounted)

  protected:
    static void _bind_methods();

  public:
    static godot::Dictionary execute(const godot::Dictionary &args);

  private:
    static godot::Dictionary _stamp_result(godot::Dictionary result,
                                           const godot::Dictionary &args);
    static godot::String
    _get_script_path_for_class_name(const godot::String &class_type);
};

} // namespace fennara
