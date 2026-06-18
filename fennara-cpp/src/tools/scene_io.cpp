#include "fennara/tools/scene_io.hpp"

#include "fennara/helpers.hpp"

#include <godot_cpp/classes/editor_file_system.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>

namespace fennara::scene_io {

godot::Ref<godot::PackedScene> load_packed_scene(
    const godot::String &scene_path,
    godot::ResourceLoader::CacheMode cache_mode) {
    return godot::ResourceLoader::get_singleton()->load(
        scene_path, "PackedScene", cache_mode);
}

godot::Node *instantiate_scene(const godot::Ref<godot::PackedScene> &packed) {
    if (!packed.is_valid()) {
        return nullptr;
    }
    return packed->instantiate();
}

godot::Error save_scene(godot::Node *root, const godot::String &scene_path) {
    if (root == nullptr) {
        return godot::ERR_INVALID_PARAMETER;
    }

    godot::Ref<godot::PackedScene> packed;
    packed.instantiate();
    godot::Error pack_err = packed->pack(root);
    if (pack_err != godot::OK) {
        return pack_err;
    }
    godot::Error save_err =
        godot::ResourceSaver::get_singleton()->save(packed, scene_path);
    if (save_err == godot::OK) {
        notify_editor_filesystem(scene_path);
    }
    return save_err;
}

void notify_editor_filesystem(const godot::String &path) {
    fennara::notify_editor_filesystem(path);
}

} // namespace fennara::scene_io
