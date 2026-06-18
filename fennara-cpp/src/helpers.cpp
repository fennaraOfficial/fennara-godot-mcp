#include "fennara/helpers.hpp"
#include "fennara/logger.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_file_system.hpp>
#include <godot_cpp/classes/script_editor.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace fennara {

namespace {

void notify_text_shader_editors(godot::Node *node) {
    if (node == nullptr) {
        return;
    }

    if (node->get_class() == "TextShaderEditor") {
        node->notification(godot::Node::NOTIFICATION_APPLICATION_FOCUS_IN);
    }

    int child_count = node->get_child_count();
    for (int i = 0; i < child_count; i++) {
        notify_text_shader_editors(node->get_child(i));
    }
}

} // namespace

godot::Array safe_get_array(const godot::Dictionary &args, const godot::String &key) {
    godot::Variant val = args.get(key, godot::Array());
    if (val.get_type() == godot::Variant::ARRAY) {
        return val;
    }
    if (val.get_type() == godot::Variant::STRING) {
        godot::Variant parsed = godot::JSON::parse_string(val);
        if (parsed.get_type() == godot::Variant::ARRAY) {
            return parsed;
        }
    }
    return godot::Array();
}

godot::Variant parse_value(const godot::Variant &value, godot::Variant::Type expected_type) {
    if (expected_type == godot::Variant::COLOR) {
        return parse_color(value);
    }
    if (expected_type == godot::Variant::VECTOR3) {
        return parse_vector3(value);
    }
    if (expected_type == godot::Variant::VECTOR2 && value.get_type() == godot::Variant::ARRAY) {
        godot::Array arr = value;
        if (arr.size() >= 2) {
            return godot::Vector2(
                godot::Variant(arr[0]).operator double(),
                godot::Variant(arr[1]).operator double());
        }
    }

    if (value.get_type() == godot::Variant::DICTIONARY) {
        godot::Dictionary dict = value;

        bool has_x = dict.has("x");
        bool has_y = dict.has("y");
        bool has_z = dict.has("z");

        // Dict{x,y} → Vector2
        if (has_x && has_y && !has_z) {
            return godot::Vector2(
                godot::Variant(dict["x"]).operator double(),
                godot::Variant(dict["y"]).operator double());
        }

        // Dict{x,y,z} → Vector3
        if (has_x && has_y && has_z) {
            return godot::Vector3(
                godot::Variant(dict["x"]).operator double(),
                godot::Variant(dict["y"]).operator double(),
                godot::Variant(dict["z"]).operator double());
        }

        // Dict{r,g,b[,a]} → Color
        if (dict.has("r") && dict.has("g") && dict.has("b")) {
            double a = dict.has("a") ? godot::Variant(dict["a"]).operator double() : 1.0;
            return godot::Color(
                godot::Variant(dict["r"]).operator double(),
                godot::Variant(dict["g"]).operator double(),
                godot::Variant(dict["b"]).operator double(), a);
        }

        // Dict{position,size} → Rect2
        if (dict.has("position") && dict.has("size")) {
            godot::Variant pos = parse_value(dict["position"]);
            godot::Variant size = parse_value(dict["size"]);
            if (pos.get_type() == godot::Variant::VECTOR2 &&
                size.get_type() == godot::Variant::VECTOR2) {
                return godot::Rect2(pos.operator godot::Vector2(), size.operator godot::Vector2());
            }
        }

        return value;
    }

    // Recurse into arrays
    if (value.get_type() == godot::Variant::ARRAY) {
        godot::Array arr = value;
        godot::Array result;
        for (int i = 0; i < arr.size(); i++) {
            result.append(parse_value(arr[i]));
        }
        return result;
    }

    // String → numeric conversion
    if (value.get_type() == godot::Variant::STRING) {
        godot::String s = value;
        if (s.is_valid_int()) {
            return s.to_int();
        }
        if (s.is_valid_float()) {
            return s.to_float();
        }
    }

    return value;
}

godot::Variant serialize_value(const godot::Variant &value) {
    switch (value.get_type()) {
    case godot::Variant::VECTOR2: {
        godot::Vector2 v = value;
        godot::Dictionary d;
        d["x"] = v.x;
        d["y"] = v.y;
        return d;
    }
    case godot::Variant::VECTOR3: {
        godot::Vector3 v = value;
        godot::Dictionary d;
        d["x"] = v.x;
        d["y"] = v.y;
        d["z"] = v.z;
        return d;
    }
    case godot::Variant::COLOR: {
        godot::Color c = value;
        godot::Dictionary d;
        d["r"] = c.r;
        d["g"] = c.g;
        d["b"] = c.b;
        d["a"] = c.a;
        return d;
    }
    case godot::Variant::OBJECT: {
        godot::Object *obj = value;
        auto *tex = godot::Object::cast_to<godot::Texture2D>(obj);
        if (tex) {
            return tex->get_path();
        }
        return value;
    }
    default:
        return value;
    }
}

godot::String normalize_path(const godot::String &path) {
    if (path.begins_with("res://")) {
        return path;
    }
    if (path.begins_with("user://")) {
        return path;
    }
    if (path.begins_with("./")) {
        return godot::String("res://") + path.substr(2, path.length() - 2);
    }
    if (path.begins_with("/")) {
        return godot::String("res://") + path.substr(1, path.length() - 1);
    }
    return godot::String("res://") + path;
}

bool is_protected_path(const godot::String &path) {
    static const char *protected_paths[] = {
        "res://project.godot",
        "res://addons/fennara",
        "res://.godot",
        "res://.git",
    };

    for (const char *pp : protected_paths) {
        godot::String protected_str(pp);
        if (path == protected_str || path.begins_with(protected_str + "/")) {
            return true;
        }
    }
    return false;
}

godot::String protected_path_error(const godot::String &path) {
    return godot::String("BLOCKED: '") + path +
        "' is a protected path. Protected paths: "
        "res://.godot/ (editor cache), res://.git/, res://addons/fennara/ (plugin). "
        "You can only access user project files like res://scripts/, res://scenes/, "
        "res://shaders/, res://Assets/, res://project.godot, etc.";
}

bool path_exists(const godot::String &path) {
    return godot::FileAccess::file_exists(path) ||
           godot::DirAccess::dir_exists_absolute(path);
}

godot::Color parse_color(const godot::Variant &value) {
    if (value.get_type() == godot::Variant::COLOR) {
        return value;
    }
    if (value.get_type() == godot::Variant::STRING) {
        return godot::Color(value.operator godot::String());
    }
    if (value.get_type() == godot::Variant::ARRAY) {
        // Done by Gemini: support parsing [r, g, b, a] float arrays into Colors
        godot::Array arr = value;
        if (arr.size() >= 3) {
            double a = arr.size() >= 4 ? godot::Variant(arr[3]).operator double() : 1.0;
            return godot::Color(
                godot::Variant(arr[0]).operator double(),
                godot::Variant(arr[1]).operator double(),
                godot::Variant(arr[2]).operator double(), a);
        }
    }
    if (value.get_type() == godot::Variant::DICTIONARY) {
        godot::Dictionary dict = value;
        double a = dict.has("a") ? godot::Variant(dict["a"]).operator double() : 1.0;
        return godot::Color(
            godot::Variant(dict.get("r", 1.0)).operator double(),
            godot::Variant(dict.get("g", 1.0)).operator double(),
            godot::Variant(dict.get("b", 1.0)).operator double(), a);
    }
    return godot::Color(1, 1, 1, 1);
}

godot::Vector3 parse_vector3(const godot::Variant &value) {
    if (value.get_type() == godot::Variant::VECTOR3) {
        return value.operator godot::Vector3();
    }
    if (value.get_type() == godot::Variant::ARRAY) {
        godot::Array arr = value;
        if (arr.size() >= 3) {
            return godot::Vector3(
                godot::Variant(arr[0]).operator double(),
                godot::Variant(arr[1]).operator double(),
                godot::Variant(arr[2]).operator double());
        }
    }
    if (value.get_type() == godot::Variant::DICTIONARY) {
        godot::Dictionary dict = value;
        return godot::Vector3(
            godot::Variant(dict.get("x", 0.0)).operator double(),
            godot::Variant(dict.get("y", 0.0)).operator double(),
            godot::Variant(dict.get("z", 0.0)).operator double());
    }
    return godot::Vector3(0, 0, 0);
}

void notify_editor_filesystem(const godot::String &path) {
    if (!godot::Engine::get_singleton()->is_editor_hint()) {
        return;
    }

    godot::EditorFileSystem *fs =
        godot::EditorInterface::get_singleton()->get_resource_filesystem();
    if (fs) {
        godot::Dictionary details;
        details["path"] = path;
        details["is_scanning"] = fs->is_scanning();
        FLOG_CTX("TOOL", "EditorFileSystem notify", details);
        fs->update_file(path);
        if (!fs->is_scanning()) {
            FLOG_TOOL(godot::String("EditorFileSystem scan requested from notify path=") + path);
            fs->scan();
        }
    }

    // If this is a scene file currently open in the editor, reload it
    // to prevent the "Files Modified on Disk" dialog.
    // However, only do this if the file actually exists. If it was just deleted
    // (e.g. by a revert action), attempting to reload it will freeze the editor.
    if ((path.ends_with(".tscn") || path.ends_with(".scn")) && godot::FileAccess::file_exists(path)) {
        godot::PackedStringArray open =
            godot::EditorInterface::get_singleton()->get_open_scenes();
        for (int i = 0; i < open.size(); i++) {
            if (open[i] == path) {
                FLOG_TOOL(godot::String("EditorFileSystem reload open scene path=") + path);
                godot::EditorInterface::get_singleton()
                    ->reload_scene_from_path(path);
                break;
            }
        }
    }

    // If this is a script, trigger Godot's built-in "auto_reload_scripts_on_external_change"
    // immediately by simulating an editor window focus gain, rather than waiting for the user
    // to actually alt-tab into the window.
    if (path.ends_with(".gd")) {
        auto *script_editor = godot::EditorInterface::get_singleton()->get_script_editor();
        if (script_editor) {
            FLOG_TOOL(godot::String("EditorFileSystem script reload focus notification path=") + path);
            script_editor->notification(godot::Node::NOTIFICATION_APPLICATION_FOCUS_IN);
            script_editor->notification(godot::Node::NOTIFICATION_WM_WINDOW_FOCUS_IN);
        }
    }

    if (path.ends_with(".gdshader")) {
        godot::Control *base = godot::EditorInterface::get_singleton()->get_base_control();
        if (base) {
            FLOG_TOOL(godot::String("EditorFileSystem shader reload focus notification path=") + path);
            notify_text_shader_editors(base);
        }
    }
}

} // namespace fennara
