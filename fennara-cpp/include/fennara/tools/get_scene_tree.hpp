#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace fennara {

class FennaraGetSceneTreeTool : public godot::RefCounted {
    GDCLASS(FennaraGetSceneTreeTool, godot::RefCounted);

protected:
    static void _bind_methods();

public:
    static godot::Dictionary execute(const godot::Dictionary &args);
    static godot::String _build_tree_structure(godot::Node *node,
                                               const godot::String &indent,
                                               bool is_last);
    static godot::String _build_grouped_node_line(
        const godot::Dictionary &group_data, const godot::String &indent,
        bool is_last);
    static godot::Array _group_children(const godot::TypedArray<godot::Node> &children);
    static godot::Variant _parse_node_name(const godot::String &node_name);
    static int _get_script_line_count(const godot::String &script_path);
};

} // namespace fennara
