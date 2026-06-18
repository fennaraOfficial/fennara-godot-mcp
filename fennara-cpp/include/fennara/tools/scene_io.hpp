#pragma once

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara::scene_io {

godot::Ref<godot::PackedScene> load_packed_scene(
    const godot::String &scene_path,
    godot::ResourceLoader::CacheMode cache_mode);
godot::Node *instantiate_scene(const godot::Ref<godot::PackedScene> &packed);
godot::Error save_scene(godot::Node *root, const godot::String &scene_path);
void notify_editor_filesystem(const godot::String &path);

} // namespace fennara::scene_io
