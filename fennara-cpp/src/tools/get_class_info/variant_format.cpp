#include "fennara/tools/get_class_info/get_class_info.hpp"

#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/plane.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector4.hpp>

namespace fennara {
godot::String FennaraGetClassInfoTool::_type_to_string(int type) {
    switch (type) {
    case 0:  return "null";
    case 1:  return "bool";
    case 2:  return "int";
    case 3:  return "float";
    case 4:  return "String";
    case 5:  return "Vector2";
    case 6:  return "Vector2i";
    case 7:  return "Rect2";
    case 8:  return "Rect2i";
    case 9:  return "Vector3";
    case 10: return "Vector3i";
    case 11: return "Transform2D";
    case 12: return "Vector4";
    case 13: return "Vector4i";
    case 14: return "Plane";
    case 15: return "Quaternion";
    case 16: return "AABB";
    case 17: return "Basis";
    case 18: return "Transform3D";
    case 19: return "Projection";
    case 20: return "Color";
    case 21: return "StringName";
    case 22: return "NodePath";
    case 23: return "RID";
    case 24: return "Object";
    case 25: return "Callable";
    case 26: return "Signal";
    case 27: return "Dictionary";
    case 28: return "Array";
    case 29: return "PackedByteArray";
    case 30: return "PackedInt32Array";
    case 31: return "PackedInt64Array";
    case 32: return "PackedFloat32Array";
    case 33: return "PackedFloat64Array";
    case 34: return "PackedStringArray";
    case 35: return "PackedVector2Array";
    case 36: return "PackedVector3Array";
    case 37: return "PackedColorArray";
    case 38: return "PackedVector4Array";
    default: return "unknown";
    }
}

godot::String FennaraGetClassInfoTool::_hint_to_string(int hint) {
    switch (hint) {
    case 1:  return "range";
    case 2:  return "enum";
    case 3:  return "enum_suggestion";
    case 4:  return "exp_easing";
    case 5:  return "link";
    case 6:  return "flags";
    case 7:  return "layers_2d_render";
    case 8:  return "layers_2d_physics";
    case 9:  return "layers_2d_navigation";
    case 10: return "layers_3d_render";
    case 11: return "layers_3d_physics";
    case 12: return "layers_3d_navigation";
    case 13: return "file";
    case 14: return "dir";
    case 15: return "global_file";
    case 16: return "global_dir";
    case 17: return "resource_type";
    case 18: return "multiline_text";
    case 19: return "expression";
    case 20: return "placeholder_text";
    case 23: return "color_no_alpha";
    case 24: return "object_id";
    case 25: return "type_string";
    case 26: return "node_path_to_edited_node";
    case 27: return "object_too_big";
    case 28: return "node_path_valid_types";
    case 29: return "save_file";
    case 30: return "global_save_file";
    case 31: return "int_is_objectid";
    case 32: return "int_is_pointer";
    case 34: return "array_type";
    case 35: return "locale_id";
    case 36: return "localizable_string";
    case 37: return "node_type";
    case 38: return "hide_quaternion_edit";
    case 39: return "password";
    default: return "other";
    }
}

godot::Variant FennaraGetClassInfoTool::_serialize_default(
    const godot::Variant &value) {
    switch (value.get_type()) {
    case godot::Variant::NIL:
        return godot::Variant();

    case godot::Variant::BOOL:
    case godot::Variant::INT:
    case godot::Variant::FLOAT:
    case godot::Variant::STRING:
        return value;

    case godot::Variant::STRING_NAME:
        return godot::String(godot::StringName(value));

    case godot::Variant::NODE_PATH:
        return godot::String(godot::NodePath(value));

    case godot::Variant::VECTOR2:
    case godot::Variant::VECTOR2I: {
        godot::Vector2 v = value;
        godot::Dictionary d;
        d["x"] = v.x;
        d["y"] = v.y;
        return d;
    }

    case godot::Variant::VECTOR3:
    case godot::Variant::VECTOR3I: {
        godot::Vector3 v = value;
        godot::Dictionary d;
        d["x"] = v.x;
        d["y"] = v.y;
        d["z"] = v.z;
        return d;
    }

    case godot::Variant::VECTOR4:
    case godot::Variant::VECTOR4I: {
        godot::Vector4 v = value;
        godot::Dictionary d;
        d["x"] = v.x;
        d["y"] = v.y;
        d["z"] = v.z;
        d["w"] = v.w;
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

    case godot::Variant::RECT2:
    case godot::Variant::RECT2I: {
        godot::Rect2 r = value;
        godot::Dictionary d;
        godot::Dictionary pos;
        pos["x"] = r.position.x;
        pos["y"] = r.position.y;
        godot::Dictionary sz;
        sz["x"] = r.size.x;
        sz["y"] = r.size.y;
        d["position"] = pos;
        d["size"] = sz;
        return d;
    }

    case godot::Variant::TRANSFORM2D:
        return godot::String(value);

    case godot::Variant::TRANSFORM3D:
        return godot::String(value);

    case godot::Variant::QUATERNION: {
        godot::Quaternion q = value;
        godot::Dictionary d;
        d["x"] = q.x;
        d["y"] = q.y;
        d["z"] = q.z;
        d["w"] = q.w;
        return d;
    }

    case godot::Variant::BASIS:
    case godot::Variant::AABB:
    case godot::Variant::PLANE:
    case godot::Variant::PROJECTION:
        return godot::String(value);

    case godot::Variant::OBJECT:
        return godot::Variant();

    case godot::Variant::ARRAY:
    case godot::Variant::DICTIONARY:
        return value;

    default:
        return godot::String(value);
    }
}

} // namespace fennara
