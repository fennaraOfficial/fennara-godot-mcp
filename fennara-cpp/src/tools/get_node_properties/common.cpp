#include "fennara/tools/get_node_properties/common.hpp"

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/plane.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/vector4.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace fennara::get_node_properties {

namespace {

godot::String num(double v) {
    // Trim trailing zeros for cleaner output
    godot::String s = godot::String::num(v, 4);
    if (s.contains(".")) {
        while (s.ends_with("0")) s = s.left(s.length() - 1);
        if (s.ends_with(".")) s = s.left(s.length() - 1);
    }
    return s;
}

godot::String format_packed_array(const godot::Variant &value);

} // namespace

// -------------------------------------------------------------------------
// format_variant
// -------------------------------------------------------------------------

godot::String format_variant(const godot::Variant &value) {
    switch (value.get_type()) {
    case godot::Variant::NIL:
        return "null";

    case godot::Variant::BOOL:
        return (bool)value ? "true" : "false";

    case godot::Variant::INT:
        return godot::String::num_int64((int64_t)value);

    case godot::Variant::FLOAT:
        return num((double)value);

    case godot::Variant::STRING:
        return "\"" + godot::String(value) + "\"";

    case godot::Variant::STRING_NAME:
        return godot::String(godot::StringName(value));

    case godot::Variant::NODE_PATH:
        return godot::String(godot::NodePath(value));

    case godot::Variant::VECTOR2:
    case godot::Variant::VECTOR2I: {
        godot::Vector2 v = value;
        return "(" + num(v.x) + "," + num(v.y) + ")";
    }

    case godot::Variant::VECTOR3:
    case godot::Variant::VECTOR3I: {
        godot::Vector3 v = value;
        return "(" + num(v.x) + "," + num(v.y) + "," + num(v.z) + ")";
    }

    case godot::Variant::VECTOR4:
    case godot::Variant::VECTOR4I: {
        godot::Vector4 v = value;
        return "(" + num(v.x) + "," + num(v.y) + "," + num(v.z) +
               "," + num(v.w) + ")";
    }

    case godot::Variant::COLOR: {
        godot::Color c = value;
        return "rgba(" + num(c.r) + "," + num(c.g) + "," + num(c.b) +
               "," + num(c.a) + ")";
    }

    case godot::Variant::RECT2:
    case godot::Variant::RECT2I: {
        godot::Rect2 r = value;
        return "rect((" + num(r.position.x) + "," + num(r.position.y) +
               "),(" + num(r.size.x) + "," + num(r.size.y) + "))";
    }

    case godot::Variant::TRANSFORM2D: {
        godot::Transform2D t = value;
        return "t2d(x(" + num(t[0].x) + "," + num(t[0].y) + ") y(" +
               num(t[1].x) + "," + num(t[1].y) + ") o(" +
               num(t[2].x) + "," + num(t[2].y) + "))";
    }

    case godot::Variant::BASIS: {
        godot::Basis b = value;
        return "basis((" + num(b[0].x) + "," + num(b[0].y) + "," +
               num(b[0].z) + "),(" + num(b[1].x) + "," + num(b[1].y) +
               "," + num(b[1].z) + "),(" + num(b[2].x) + "," +
               num(b[2].y) + "," + num(b[2].z) + "))";
    }

    case godot::Variant::TRANSFORM3D: {
        godot::Transform3D t = value;
        return "t3d(b((" + num(t.basis[0].x) + "," + num(t.basis[0].y) +
               "," + num(t.basis[0].z) + "),(" + num(t.basis[1].x) +
               "," + num(t.basis[1].y) + "," + num(t.basis[1].z) +
               "),(" + num(t.basis[2].x) + "," + num(t.basis[2].y) +
               "," + num(t.basis[2].z) + ")) o(" + num(t.origin.x) +
               "," + num(t.origin.y) + "," + num(t.origin.z) + "))";
    }

    case godot::Variant::QUATERNION: {
        godot::Quaternion q = value;
        return "quat(" + num(q.x) + "," + num(q.y) + "," + num(q.z) +
               "," + num(q.w) + ")";
    }

    case godot::Variant::PLANE: {
        godot::Plane p = value;
        return "plane((" + num(p.normal.x) + "," + num(p.normal.y) +
               "," + num(p.normal.z) + ")," + num(p.d) + ")";
    }

    case godot::Variant::AABB: {
        godot::AABB a = value;
        return "aabb((" + num(a.position.x) + "," + num(a.position.y) +
               "," + num(a.position.z) + "),(" + num(a.size.x) + "," +
               num(a.size.y) + "," + num(a.size.z) + "))";
    }

    case godot::Variant::OBJECT: {
        godot::Object *obj = value;
        if (!obj) return "null";
        return format_resource(obj);
    }

    case godot::Variant::ARRAY: {
        godot::Array arr = value;
        if (arr.size() > 32) {
            return "Array[" + godot::String::num_int64(arr.size()) + "]";
        }
        godot::String out = "[";
        for (int i = 0; i < arr.size(); i++) {
            if (i > 0) out += ", ";
            out += format_variant(arr[i]);
        }
        return out + "]";
    }

    case godot::Variant::DICTIONARY: {
        godot::Dictionary dict = value;
        godot::Array keys = dict.keys();
        godot::String out = "{";
        for (int i = 0; i < keys.size(); i++) {
            if (i > 0) out += ", ";
            out += format_variant(keys[i]) + ": " +
                   format_variant(dict[keys[i]]);
        }
        return out + "}";
    }

    case godot::Variant::PACKED_BYTE_ARRAY:
    case godot::Variant::PACKED_INT32_ARRAY:
    case godot::Variant::PACKED_INT64_ARRAY:
    case godot::Variant::PACKED_FLOAT32_ARRAY:
    case godot::Variant::PACKED_FLOAT64_ARRAY:
    case godot::Variant::PACKED_STRING_ARRAY:
    case godot::Variant::PACKED_VECTOR2_ARRAY:
    case godot::Variant::PACKED_VECTOR3_ARRAY:
    case godot::Variant::PACKED_COLOR_ARRAY:
        return format_packed_array(value);

    default:
        return godot::String(value);
    }
}

// -------------------------------------------------------------------------
// format_resource
// -------------------------------------------------------------------------

godot::String format_resource(godot::Object *resource) {
    auto *res = godot::Object::cast_to<godot::Resource>(resource);
    if (!res) return "<" + resource->get_class() + ">";

    godot::String path = res->get_path();
    if (!path.is_empty() && !path.contains("::")) {
        return path;
    }

    return "<" + res->get_class() + ">";
}

// -------------------------------------------------------------------------
// format_packed_array
// -------------------------------------------------------------------------

namespace {

godot::String format_packed_array(const godot::Variant &value) {
    switch (value.get_type()) {
    case godot::Variant::PACKED_BYTE_ARRAY: {
        godot::PackedByteArray arr = value;
        return "bytes[" + godot::String::num_int64(arr.size()) + "]";
    }
    case godot::Variant::PACKED_STRING_ARRAY: {
        godot::PackedStringArray arr = value;
        godot::String out = "[";
        for (int i = 0; i < arr.size(); i++) {
            if (i > 0) out += ", ";
            out += "\"" + arr[i] + "\"";
        }
        return out + "]";
    }
    case godot::Variant::PACKED_INT32_ARRAY: {
        godot::PackedInt32Array arr = value;
        if (arr.size() > 16) {
            return "int32[" + godot::String::num_int64(arr.size()) + "]";
        }
        godot::String out = "[";
        for (int i = 0; i < arr.size(); i++) {
            if (i > 0) out += ",";
            out += godot::String::num_int64(arr[i]);
        }
        return out + "]";
    }
    case godot::Variant::PACKED_INT64_ARRAY: {
        godot::PackedInt64Array arr = value;
        if (arr.size() > 16) {
            return "int64[" + godot::String::num_int64(arr.size()) + "]";
        }
        godot::String out = "[";
        for (int i = 0; i < arr.size(); i++) {
            if (i > 0) out += ",";
            out += godot::String::num_int64(arr[i]);
        }
        return out + "]";
    }
    case godot::Variant::PACKED_FLOAT32_ARRAY: {
        godot::PackedFloat32Array arr = value;
        if (arr.size() > 16) {
            return "float32[" + godot::String::num_int64(arr.size()) + "]";
        }
        godot::String out = "[";
        for (int i = 0; i < arr.size(); i++) {
            if (i > 0) out += ",";
            out += num(arr[i]);
        }
        return out + "]";
    }
    case godot::Variant::PACKED_FLOAT64_ARRAY: {
        godot::PackedFloat64Array arr = value;
        if (arr.size() > 16) {
            return "float64[" + godot::String::num_int64(arr.size()) + "]";
        }
        godot::String out = "[";
        for (int i = 0; i < arr.size(); i++) {
            if (i > 0) out += ",";
            out += num(arr[i]);
        }
        return out + "]";
    }
    case godot::Variant::PACKED_VECTOR2_ARRAY: {
        godot::PackedVector2Array arr = value;
        if (arr.size() > 16) {
            return "vec2[" + godot::String::num_int64(arr.size()) + "]";
        }
        godot::String out = "[";
        for (int i = 0; i < arr.size(); i++) {
            if (i > 0) out += ", ";
            out += "(" + num(arr[i].x) + "," + num(arr[i].y) + ")";
        }
        return out + "]";
    }
    case godot::Variant::PACKED_VECTOR3_ARRAY: {
        godot::PackedVector3Array arr = value;
        if (arr.size() > 16) {
            return "vec3[" + godot::String::num_int64(arr.size()) + "]";
        }
        godot::String out = "[";
        for (int i = 0; i < arr.size(); i++) {
            if (i > 0) out += ", ";
            out += "(" + num(arr[i].x) + "," + num(arr[i].y) + "," +
                   num(arr[i].z) + ")";
        }
        return out + "]";
    }
    case godot::Variant::PACKED_COLOR_ARRAY: {
        godot::PackedColorArray arr = value;
        if (arr.size() > 16) {
            return "color[" + godot::String::num_int64(arr.size()) + "]";
        }
        godot::String out = "[";
        for (int i = 0; i < arr.size(); i++) {
            if (i > 0) out += ", ";
            out += "rgba(" + num(arr[i].r) + "," + num(arr[i].g) + "," +
                   num(arr[i].b) + "," + num(arr[i].a) + ")";
        }
        return out + "]";
    }
    default:
        return godot::String(value);
    }
}

} // namespace

// -------------------------------------------------------------------------
// find_scene_state_index
// -------------------------------------------------------------------------

int find_scene_state_index(const godot::Ref<godot::SceneState> &state,
                           const godot::String &relative_path) {
    if (!state.is_valid()) return -1;
    int count = state->get_node_count();
    if (count == 0) return -1;

    if (relative_path == "." ||
        relative_path == godot::String(state->get_node_name(0))) {
        return 0;
    }

    for (int i = 1; i < count; i++) {
        godot::String state_path =
            godot::String(state->get_node_path(i, true));

        godot::String normalized = state_path;
        if (normalized.begins_with("./")) {
            normalized = normalized.substr(2);
        }

        if (normalized == relative_path) return i;
    }

    return -1;
}

// -------------------------------------------------------------------------
// format_connections
// -------------------------------------------------------------------------

godot::String format_connections(
    const godot::Ref<godot::SceneState> &state, int node_idx) {

    godot::String our_path;
    if (node_idx == 0) {
        our_path = ".";
    } else {
        our_path = godot::String(state->get_node_path(node_idx, true));
    }

    godot::String out;
    int conn_count = state->get_connection_count();

    for (int c = 0; c < conn_count; c++) {
        godot::String source =
            godot::String(state->get_connection_source(c));
        godot::String signal_name =
            godot::String(state->get_connection_signal(c));
        godot::String target =
            godot::String(state->get_connection_target(c));
        godot::String method =
            godot::String(state->get_connection_method(c));

        if (source == our_path) {
            out += "  >> " + signal_name + " -> " + target + "." +
                   method + "()\n";
        }

        if (target == our_path) {
            out += "  << " + source + "." + signal_name + " -> " +
                   method + "()\n";
        }
    }

    return out;
}

// -------------------------------------------------------------------------
// resolve_node
// -------------------------------------------------------------------------

godot::Node *resolve_node(godot::Node *root, const godot::String &node_path) {
    godot::String p = node_path.strip_edges();
    if (p.is_empty() || p == "." || p == "/") {
        return root;
    }

    if (p.begins_with("./")) {
        p = p.substr(2);
    }
    if (p.begins_with("/")) {
        p = p.substr(1);
    }

    godot::String root_name = godot::String(root->get_name());
    int slash = p.find("/");
    godot::String head = (slash == -1) ? p : p.left(slash);
    if (head == root_name) {
        p = (slash == -1) ? godot::String() : p.substr(slash + 1);
    }

    if (p.is_empty()) {
        return root;
    }

    return root->get_node_or_null(godot::NodePath(p));
}

// -------------------------------------------------------------------------
// collect_available_paths
// -------------------------------------------------------------------------

void collect_available_paths(godot::Node *node,
                             const godot::String &current_path,
                             godot::Array &paths) {
    if (paths.size() >= 50) return;

    godot::String display;
    if (current_path.is_empty()) {
        display = "\".\" (root: " +
                  godot::String(node->get_name()) + ", " +
                  node->get_class() + ")";
    } else {
        display = "\"" + current_path + "\" (" +
                  node->get_class() + ")";
    }
    paths.push_back(display);

    for (int i = 0; i < node->get_child_count(); i++) {
        if (paths.size() >= 50) return;
        godot::Node *child = node->get_child(i);
        godot::String child_path =
            current_path.is_empty()
                ? godot::String(child->get_name())
                : current_path + godot::String("/") +
                      godot::String(child->get_name());
        collect_available_paths(child, child_path, paths);
    }
}

} // namespace fennara::get_node_properties
