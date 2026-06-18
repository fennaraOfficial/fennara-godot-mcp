#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara {

class FennaraRunSceneEditScriptContext : public godot::RefCounted {
    GDCLASS(FennaraRunSceneEditScriptContext, godot::RefCounted);

protected:
    static void _bind_methods();

public:
    void configure(godot::Node *scene_root, const godot::String &scene_path,
                   bool scene_exists);

    godot::Node *get_scene_root() const;
    godot::String get_scene_path() const;
    bool get_scene_exists() const;

    void set_scene_root(godot::Node *root);
    void own(godot::Node *node);
    godot::Node *instance_scene(godot::Node *parent,
                                const godot::String &scene_path,
                                const godot::String &desired_name);
    void log(const godot::String &message);
    void error(const godot::String &message);
    godot::Node *get_node_or_null(const godot::String &node_path) const;
    godot::Array find_nodes_by_name(const godot::String &name) const;
    godot::String ensure_unique_child_name(godot::Node *parent,
                                           const godot::String &desired_name) const;
    bool remove_node(const godot::String &node_path);
    void clear_children(godot::Node *node);
    void mark_modified();

    bool has_errors() const;
    godot::Array get_logs() const;
    godot::Array get_edit_errors() const;
    bool was_modified() const;

    void add_edit_error(const godot::String &message,
                        const godot::String &source = "ctx");

private:
    godot::Node *_scene_root = nullptr;
    godot::String _scene_path;
    bool _scene_exists = false;
    bool _modified = false;
    godot::Array _logs;
    godot::Array _edit_errors;
};

class FennaraRunSceneEditScriptTool : public godot::RefCounted {
    GDCLASS(FennaraRunSceneEditScriptTool, godot::RefCounted);

protected:
    static void _bind_methods();

public:
    static godot::Dictionary execute(const godot::Dictionary &args);
    static godot::Dictionary prepare_execution(const godot::Dictionary &args);
    static godot::Dictionary execute_prepared(const godot::Dictionary &prepared_args);
};

} // namespace fennara
