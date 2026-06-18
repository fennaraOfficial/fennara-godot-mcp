#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara {

class FennaraWriteOrUpdateFileTool : public godot::RefCounted {
    GDCLASS(FennaraWriteOrUpdateFileTool, godot::RefCounted);

protected:
    static void _bind_methods();

public:
    static godot::Dictionary execute(const godot::Dictionary &args);

private:
    static godot::Dictionary _execute_write(const godot::Dictionary &args);
    static godot::Dictionary _execute_update(const godot::Dictionary &args);
    static godot::String _normalize_file_path(const godot::String &path);
    static bool _ensure_parent_dir(const godot::String &path,
                                   godot::Dictionary &result);
    static godot::Dictionary _read_content(const godot::String &path,
                                           const godot::String &input_path);
    static godot::Dictionary _write_content(const godot::String &path,
                                            const godot::String &content,
                                            const godot::String &input_path,
                                            bool file_exists);
    static void _snapshot_before_write(const godot::String &path,
                                       bool file_exists);
    static void _refresh_cached_shader_resource(const godot::String &path,
                                                const godot::String &content);
    static void _reserialize_shader_owners(const godot::String &shader_path,
                                           godot::Dictionary &result);
    static void _append_shader_diagnostics(godot::Dictionary &result,
                                           const godot::String &file_path,
                                           const godot::String &content);
    static godot::Dictionary _stamp_result(godot::Dictionary result,
                                           const godot::Dictionary &args);
    static godot::Dictionary _attach_script_to_scene(
        const godot::String &script_path, const godot::String &scene_path,
        const godot::String &node_path);
    static godot::Node *_resolve_node(godot::Node *root,
                                      const godot::String &node_path);
    static bool _is_protected_path(const godot::String &path);
};

} // namespace fennara
