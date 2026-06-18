#include "fennara/tools/get_node_properties/animation_mixer.hpp"
#include "fennara/tools/get_node_properties/common.hpp"

#include <godot_cpp/classes/animation_mixer.hpp>
#include <godot_cpp/classes/animation_library.hpp>
#include <godot_cpp/classes/animation.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace fennara::get_node_properties {

namespace {

const char *loop_mode_labels[] = {"None", "Linear", "Ping-Pong"};
const char *track_type_labels[] = {
    "value", "position_3d", "rotation_3d", "scale_3d",
    "blend_shape", "method", "bezier", "audio", "animation"};
const char *interp_labels[] = {
    "nearest", "linear", "cubic", "linear_angle", "cubic_angle"};
const char *update_mode_labels[] = {"continuous", "discrete", "capture"};

godot::String enum_label(const char *labels[], int count, int value) {
    if (value >= 0 && value < count) return labels[value];
    return godot::String::num_int64(value);
}

godot::String format_track(godot::Ref<godot::Animation> anim, int t) {
    int type = anim->track_get_type(t);
    godot::String type_str = enum_label(track_type_labels, 9, type);
    godot::String path = godot::String(anim->track_get_path(t));

    godot::String line = godot::String("    track[") +
                         godot::String::num_int64(t) + "] " + type_str +
                         " path:" + path;

    bool enabled = anim->track_is_enabled(t);
    if (!enabled) line += " DISABLED";

    int interp = anim->track_get_interpolation_type(t);
    if (interp != 1) line += " interp:" + enum_label(interp_labels, 5, interp);

    if (!anim->track_get_interpolation_loop_wrap(t))
        line += " no_loop_wrap";

    if (type == 0) { // VALUE
        int update_mode = anim->value_track_get_update_mode(t);
        if (update_mode != 0)
            line += " update:" + enum_label(update_mode_labels, 3, update_mode);
    }

    line += "\n";

    int key_count = anim->track_get_key_count(t);
    if (key_count > 0) {
        line += "      keys: ";
        for (int k = 0; k < key_count; k++) {
            if (k > 0) line += " | ";
            double time = anim->track_get_key_time(t, k);
            line += "@" + godot::String::num(time, 2) + ": ";

            switch (type) {
            case 0: // VALUE
            case 1: // POSITION_3D
            case 2: // ROTATION_3D
            case 3: // SCALE_3D
            case 4: { // BLEND_SHAPE
                line += format_variant(anim->track_get_key_value(t, k));
                double transition = anim->track_get_key_transition(t, k);
                if (transition != 1.0)
                    line += " t:" + godot::String::num(transition, 2);
                break;
            }
            case 5: { // METHOD
                line += godot::String(anim->method_track_get_name(t, k)) + "(";
                godot::Array params = anim->method_track_get_params(t, k);
                for (int p = 0; p < params.size(); p++) {
                    if (p > 0) line += ",";
                    line += format_variant(params[p]);
                }
                line += ")";
                break;
            }
            case 6: { // BEZIER
                line += godot::String::num(
                    anim->bezier_track_get_key_value(t, k), 3);
                godot::Vector2 in_h =
                    anim->bezier_track_get_key_in_handle(t, k);
                godot::Vector2 out_h =
                    anim->bezier_track_get_key_out_handle(t, k);
                if (in_h.x != 0 || in_h.y != 0)
                    line += " in(" + godot::String::num(in_h.x, 2) + "," +
                            godot::String::num(in_h.y, 2) + ")";
                if (out_h.x != 0 || out_h.y != 0)
                    line += " out(" + godot::String::num(out_h.x, 2) + "," +
                            godot::String::num(out_h.y, 2) + ")";
                break;
            }
            case 7: { // AUDIO
                godot::Ref<godot::Resource> stream =
                    anim->audio_track_get_key_stream(t, k);
                if (stream.is_valid()) {
                    godot::String spath = stream->get_path();
                    line += spath.is_empty() ? stream->get_class() : spath;
                }
                double start = anim->audio_track_get_key_start_offset(t, k);
                double end = anim->audio_track_get_key_end_offset(t, k);
                if (start != 0.0)
                    line += " start:" + godot::String::num(start, 2);
                if (end != 0.0)
                    line += " end:" + godot::String::num(end, 2);
                break;
            }
            case 8: { // ANIMATION
                line += godot::String(
                    anim->animation_track_get_key_animation(t, k));
                break;
            }
            }
        }
        line += "\n";
    }

    return line;
}

} // namespace

godot::String format_animation_libraries(godot::Node *target) {
    auto *mixer = godot::Object::cast_to<godot::AnimationMixer>(target);
    if (!mixer) return godot::String();

    godot::String out;

    godot::TypedArray<godot::StringName> lib_names =
        mixer->get_animation_library_list();

    for (int i = 0; i < lib_names.size(); i++) {
        godot::StringName lib_name = lib_names[i];
        godot::Ref<godot::AnimationLibrary> lib =
            mixer->get_animation_library(lib_name);
        if (!lib.is_valid()) continue;

        godot::String lib_key = godot::String(lib_name);
        if (lib_key.is_empty()) lib_key = "(default)";

        godot::TypedArray<godot::StringName> anim_names =
            lib->get_animation_list();

        for (int a = 0; a < anim_names.size(); a++) {
            godot::StringName anim_name = anim_names[a];
            godot::Ref<godot::Animation> anim =
                lib->get_animation(anim_name);
            if (!anim.is_valid()) continue;

            godot::String header = lib_key + "/" +
                                   godot::String(anim_name) +
                                   godot::String(" [length:") +
                                   godot::String::num(anim->get_length(), 2) +
                                   "s";

            int loop_mode = anim->get_loop_mode();
            header += ", loop:" +
                      enum_label(loop_mode_labels, 3, loop_mode);

            int track_count = anim->get_track_count();
            header += ", tracks:" + godot::String::num_int64(track_count);
            header += "]\n";
            out += header;

            for (int t = 0; t < track_count; t++) {
                out += format_track(anim, t);
            }
        }
    }

    return out;
}

} // namespace fennara::get_node_properties
