#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara {

class FennaraProjectSettingsTool : public godot::RefCounted {
    GDCLASS(FennaraProjectSettingsTool, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    static godot::Dictionary execute(const godot::Dictionary &args);

private:
    static godot::Dictionary _action_get(const godot::String &key);
    static godot::Dictionary _action_set(const godot::String &key, const godot::Variant &value);
    static godot::Dictionary _action_remove(const godot::String &key);
    static godot::Dictionary _action_list(const godot::String &prefix);
    static godot::Dictionary _action_find_setting(const godot::String &prefix,
                                                  const godot::String &query);
    static godot::Dictionary _set_input_action(const godot::String &action_name, const godot::Dictionary &config);
    static godot::Dictionary _stamp_result(godot::Dictionary result,
                                           const godot::Dictionary &args);
    static godot::Variant _build_input_event(const godot::Dictionary &evt);
};

} // namespace fennara
