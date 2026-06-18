#include "fennara/tools/get_node_properties/sprite_frames.hpp"

#include <godot_cpp/classes/animated_sprite2d.hpp>
#include <godot_cpp/classes/atlas_texture.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/sprite_frames.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/rect2.hpp>

namespace fennara::get_node_properties {

namespace {

godot::String num(double v) {
    godot::String s = godot::String::num(v, 4);
    if (s.contains(".")) {
        while (s.ends_with("0")) s = s.left(s.length() - 1);
        if (s.ends_with(".")) s = s.left(s.length() - 1);
    }
    return s;
}

godot::String bool_text(bool value) {
    return value ? "true" : "false";
}

godot::String format_texture_ref(const godot::Ref<godot::Texture2D> &texture) {
    if (texture.is_null()) {
        return "null";
    }

    auto *atlas = godot::Object::cast_to<godot::AtlasTexture>(texture.ptr());
    if (atlas != nullptr) {
        godot::String out = "<AtlasTexture>";

        godot::Ref<godot::Texture2D> atlas_texture = atlas->get_atlas();
        if (atlas_texture.is_valid()) {
            auto *atlas_resource =
                godot::Object::cast_to<godot::Resource>(atlas_texture.ptr());
            godot::String atlas_label =
                (atlas_resource != nullptr && !atlas_resource->get_path().is_empty())
                    ? atlas_resource->get_path()
                    : godot::String("<") + atlas_texture->get_class() + ">";
            out += " atlas:" + atlas_label;
        } else {
            out += " atlas:null";
        }

        godot::Rect2 region = atlas->get_region();
        out += " region:Rect2(" + num(region.position.x) + ", " +
               num(region.position.y) + ", " + num(region.size.x) + ", " +
               num(region.size.y) + ")";
        return out;
    }

    auto *resource = godot::Object::cast_to<godot::Resource>(texture.ptr());
    if (resource != nullptr && !resource->get_path().is_empty()) {
        return resource->get_path();
    }

    return godot::String("<") + texture->get_class() + ">";
}

} // namespace

godot::String format_sprite_frames(godot::Node *target) {
    auto *sprite = godot::Object::cast_to<godot::AnimatedSprite2D>(target);
    if (sprite == nullptr) {
        return godot::String();
    }

    godot::Ref<godot::SpriteFrames> frames = sprite->get_sprite_frames();
    if (frames.is_null()) {
        return "no SpriteFrames resource\n";
    }

    godot::PackedStringArray animation_names = frames->get_animation_names();
    if (animation_names.is_empty()) {
        return "no animations\n";
    }

    godot::String out;
    for (int i = 0; i < animation_names.size(); i++) {
        godot::String anim_name = godot::String(animation_names[i]);
        int frame_count = frames->get_frame_count(animation_names[i]);
        out += "animation[" + anim_name + "] fps:" +
               num(frames->get_animation_speed(animation_names[i])) +
               " loop:" + bool_text(frames->get_animation_loop(animation_names[i])) +
               " frames:" + godot::String::num_int64(frame_count) + "\n";

        for (int frame_index = 0; frame_index < frame_count; frame_index++) {
            godot::Ref<godot::Texture2D> texture =
                frames->get_frame_texture(animation_names[i], frame_index);
            out += "  frame[" + godot::String::num_int64(frame_index) +
                   "] duration:" +
                   num(frames->get_frame_duration(animation_names[i], frame_index)) +
                   " texture:" + format_texture_ref(texture) + "\n";
        }
    }

    return out;
}

} // namespace fennara::get_node_properties
