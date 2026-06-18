#include "fennara/tools/run_scene_edit_script.hpp"

#include "fennara/tools/run_scene_edit_script/internal.hpp"

#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace fennara {

namespace {

godot::Node *resolve_node_path(godot::Node *root, const godot::String &node_path) {
    if (root == nullptr) {
        return nullptr;
    }

    godot::String path = node_path.strip_edges();
    if (path.is_empty() || path == "." || path == "/") {
        return root;
    }

    if (path.begins_with("/")) {
        path = path.substr(1);
    }

    int slash = path.find("/");
    godot::String head = (slash == -1) ? path : path.left(slash);
    if (head == godot::String(root->get_name())) {
        path = (slash == -1) ? godot::String() : path.substr(slash + 1);
    }

    if (path.is_empty()) {
        return root;
    }

    return root->get_node_or_null(path);
}

void assign_owner_recursive(godot::Node *node, godot::Node *scene_root) {
    if (node == nullptr || scene_root == nullptr) {
        return;
    }

    if (node != scene_root) {
        node->set_owner(scene_root);
    }

    for (int i = 0; i < node->get_child_count(); i++) {
        assign_owner_recursive(node->get_child(i), scene_root);
    }
}

godot::String build_debug_node_path(godot::Node *node,
                                    godot::Node *stop_at) {
    if (node == nullptr) {
        return "<null>";
    }

    godot::PackedStringArray parts;
    godot::Node *cursor = node;
    while (cursor != nullptr) {
        parts.insert(0, godot::String(cursor->get_name()));
        if (cursor == stop_at) {
            break;
        }
        cursor = cursor->get_parent();
    }
    return godot::String("/").join(parts);
}

godot::Node *find_instanced_scene_ancestor(godot::Node *node,
                                           godot::Node *scene_root) {
    godot::Node *cursor = node;
    while (cursor != nullptr && cursor != scene_root) {
        if (!cursor->get_scene_file_path().is_empty()) {
            return cursor;
        }
        cursor = cursor->get_parent();
    }
    return nullptr;
}

void find_nodes_by_name_recursive(godot::Node *node, const godot::String &name,
                                  godot::Array &results) {
    if (node == nullptr) {
        return;
    }

    if (godot::String(node->get_name()) == name) {
        results.append(node);
    }

    for (int i = 0; i < node->get_child_count(); i++) {
        find_nodes_by_name_recursive(node->get_child(i), name, results);
    }
}

} // namespace

void FennaraRunSceneEditScriptContext::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("get_scene_root"),
                                &FennaraRunSceneEditScriptContext::get_scene_root);
    godot::ClassDB::bind_method(godot::D_METHOD("get_scene_path"),
                                &FennaraRunSceneEditScriptContext::get_scene_path);
    godot::ClassDB::bind_method(godot::D_METHOD("get_scene_exists"),
                                &FennaraRunSceneEditScriptContext::get_scene_exists);
    godot::ClassDB::bind_method(godot::D_METHOD("set_scene_root", "root"),
                                &FennaraRunSceneEditScriptContext::set_scene_root);
    godot::ClassDB::bind_method(godot::D_METHOD("own", "node"),
                                &FennaraRunSceneEditScriptContext::own);
    godot::ClassDB::bind_method(
        godot::D_METHOD("instance_scene", "parent", "scene_path", "desired_name"),
        &FennaraRunSceneEditScriptContext::instance_scene);
    godot::ClassDB::bind_method(godot::D_METHOD("log", "message"),
                                &FennaraRunSceneEditScriptContext::log);
    godot::ClassDB::bind_method(godot::D_METHOD("error", "message"),
                                &FennaraRunSceneEditScriptContext::error);
    godot::ClassDB::bind_method(godot::D_METHOD("get_node_or_null", "node_path"),
                                &FennaraRunSceneEditScriptContext::get_node_or_null);
    godot::ClassDB::bind_method(godot::D_METHOD("find_nodes_by_name", "name"),
                                &FennaraRunSceneEditScriptContext::find_nodes_by_name);
    godot::ClassDB::bind_method(
        godot::D_METHOD("ensure_unique_child_name", "parent", "desired_name"),
        &FennaraRunSceneEditScriptContext::ensure_unique_child_name);
    godot::ClassDB::bind_method(godot::D_METHOD("remove_node", "node_path"),
                                &FennaraRunSceneEditScriptContext::remove_node);
    godot::ClassDB::bind_method(godot::D_METHOD("clear_children", "node"),
                                &FennaraRunSceneEditScriptContext::clear_children);
    godot::ClassDB::bind_method(godot::D_METHOD("mark_modified"),
                                &FennaraRunSceneEditScriptContext::mark_modified);
    godot::ClassDB::bind_method(godot::D_METHOD("has_errors"),
                                &FennaraRunSceneEditScriptContext::has_errors);
    godot::ClassDB::bind_method(godot::D_METHOD("get_logs"),
                                &FennaraRunSceneEditScriptContext::get_logs);
    godot::ClassDB::bind_method(godot::D_METHOD("get_edit_errors"),
                                &FennaraRunSceneEditScriptContext::get_edit_errors);
    godot::ClassDB::bind_method(godot::D_METHOD("was_modified"),
                                &FennaraRunSceneEditScriptContext::was_modified);
}

void FennaraRunSceneEditScriptContext::configure(godot::Node *scene_root,
                                          const godot::String &scene_path,
                                          bool scene_exists) {
    _scene_root = scene_root;
    _scene_path = scene_path;
    _scene_exists = scene_exists;
    _modified = false;
    _logs.clear();
    _edit_errors.clear();
}

godot::Node *FennaraRunSceneEditScriptContext::get_scene_root() const { return _scene_root; }

godot::String FennaraRunSceneEditScriptContext::get_scene_path() const { return _scene_path; }

bool FennaraRunSceneEditScriptContext::get_scene_exists() const { return _scene_exists; }

void FennaraRunSceneEditScriptContext::set_scene_root(godot::Node *root) {
    if (_scene_exists) {
        add_edit_error("set_scene_root() is only allowed when scene_path does not exist yet.");
        return;
    }
    if (root == nullptr) {
        add_edit_error("set_scene_root() received a null root node.");
        return;
    }
    if (_scene_root != nullptr) {
        add_edit_error("set_scene_root() can only be called once per run.");
        return;
    }
    if (root->get_parent() != nullptr) {
        add_edit_error("set_scene_root() requires a detached root node with no parent.");
        return;
    }

    _scene_root = root;
    _modified = true;
}

void FennaraRunSceneEditScriptContext::own(godot::Node *node) {
    if (_scene_root == nullptr) {
        add_edit_error("own() requires a scene root. Create one with set_scene_root() first.");
        return;
    }
    if (node == nullptr) {
        add_edit_error("own() received a null node.");
        return;
    }
    godot::Node *instanced_ancestor =
        find_instanced_scene_ancestor(node, _scene_root);
    if (instanced_ancestor != nullptr) {
        godot::String node_path = build_debug_node_path(node, _scene_root);
        godot::String instance_path =
            build_debug_node_path(instanced_ancestor, _scene_root);
        add_edit_error(
            "ctx.own() refused: '" + node_path +
            "' is inside instanced scene '" + instance_path +
            "' from '" + instanced_ancestor->get_scene_file_path() +
            "'. Do not own PackedScene instances or their children. Use ctx.instance_scene(parent, scene_path, desired_name) for reusable scenes. Only call ctx.own() on raw nodes created with Node.new(). If recursively owning a raw subtree, stop before any child where child.scene_file_path is not empty.");
        return;
    }

    assign_owner_recursive(node, _scene_root);
    _modified = true;
}

godot::Node *FennaraRunSceneEditScriptContext::instance_scene(
    godot::Node *parent, const godot::String &scene_path,
    const godot::String &desired_name) {
    if (_scene_root == nullptr) {
        add_edit_error("instance_scene() requires a scene root. Create one with set_scene_root() first.");
        return nullptr;
    }
    if (parent == nullptr) {
        add_edit_error("instance_scene() received a null parent node.");
        return nullptr;
    }
    if (!_scene_root->is_ancestor_of(parent) && parent != _scene_root) {
        add_edit_error("instance_scene() parent must be the scene root or one of its descendants.");
        return nullptr;
    }

    godot::String normalized_path = scene_path.strip_edges();
    if (normalized_path.is_empty()) {
        add_edit_error("instance_scene() received an empty scene_path.");
        return nullptr;
    }

    godot::Ref<godot::PackedScene> packed =
        godot::ResourceLoader::get_singleton()->load(
            normalized_path, "PackedScene",
            godot::ResourceLoader::CACHE_MODE_IGNORE);
    if (!packed.is_valid()) {
        add_edit_error("instance_scene() failed to load PackedScene: " + normalized_path);
        return nullptr;
    }

    godot::Node *instance =
        packed->instantiate(godot::PackedScene::GEN_EDIT_STATE_INSTANCE);
    if (instance == nullptr) {
        add_edit_error("instance_scene() failed to instantiate PackedScene: " + normalized_path);
        return nullptr;
    }

    godot::String name = desired_name.strip_edges();
    if (name.is_empty()) {
        name = godot::String(instance->get_name());
    }
    instance->set_name(ensure_unique_child_name(parent, name));
    parent->add_child(instance);

    // Own only the instance root. Owning the imported children flattens the
    // instance into the parent scene and can duplicate its internal nodes.
    instance->set_owner(_scene_root);
    _modified = true;
    return instance;
}

void FennaraRunSceneEditScriptContext::log(const godot::String &message) { _logs.append(message); }

void FennaraRunSceneEditScriptContext::error(const godot::String &message) {
    add_edit_error(message);
}

godot::Node *FennaraRunSceneEditScriptContext::get_node_or_null(
    const godot::String &node_path) const {
    return resolve_node_path(_scene_root, node_path);
}

godot::Array FennaraRunSceneEditScriptContext::find_nodes_by_name(
    const godot::String &name) const {
    godot::Array results;
    find_nodes_by_name_recursive(_scene_root, name, results);
    return results;
}

godot::String FennaraRunSceneEditScriptContext::ensure_unique_child_name(
    godot::Node *parent, const godot::String &desired_name) const {
    if (parent == nullptr) {
        return desired_name;
    }

    godot::String candidate = desired_name.is_empty() ? godot::String("Node") : desired_name;
    if (parent->get_node_or_null(candidate) == nullptr) {
        return candidate;
    }

    int suffix = 2;
    while (true) {
        godot::String next = candidate + "_" + godot::String::num_int64(suffix);
        if (parent->get_node_or_null(next) == nullptr) {
            return next;
        }
        suffix++;
    }
}

bool FennaraRunSceneEditScriptContext::remove_node(const godot::String &node_path) {
    godot::Node *target = resolve_node_path(_scene_root, node_path);
    if (target == nullptr) {
        add_edit_error("remove_node(): node not found: " + node_path);
        return false;
    }
    if (target == _scene_root) {
        add_edit_error("remove_node(): refusing to remove the scene root.");
        return false;
    }

    godot::Node *parent = target->get_parent();
    if (parent != nullptr) {
        parent->remove_child(target);
    }
    target->queue_free();
    _modified = true;
    return true;
}

void FennaraRunSceneEditScriptContext::clear_children(godot::Node *node) {
    if (node == nullptr) {
        add_edit_error("clear_children() received a null node.");
        return;
    }

    while (node->get_child_count() > 0) {
        godot::Node *child = node->get_child(0);
        node->remove_child(child);
        child->queue_free();
    }
    _modified = true;
}

void FennaraRunSceneEditScriptContext::mark_modified() { _modified = true; }

bool FennaraRunSceneEditScriptContext::has_errors() const { return !_edit_errors.is_empty(); }

godot::Array FennaraRunSceneEditScriptContext::get_logs() const { return _logs; }

godot::Array FennaraRunSceneEditScriptContext::get_edit_errors() const {
    return _edit_errors;
}

bool FennaraRunSceneEditScriptContext::was_modified() const { return _modified; }

void FennaraRunSceneEditScriptContext::add_edit_error(const godot::String &message,
                                               const godot::String &source) {
    _edit_errors.append(run_scene_edit_script_internal::make_runtime_error(message, source));
}

} // namespace fennara
