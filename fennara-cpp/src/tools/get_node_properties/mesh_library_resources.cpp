#include "fennara/tools/get_node_properties/mesh_library_resources.hpp"
#include "fennara/tools/get_node_properties/common.hpp"

#include <godot_cpp/classes/grid_map.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_library.hpp>
#include <godot_cpp/classes/navigation_mesh.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector3i.hpp>

namespace fennara::get_node_properties {

namespace {

constexpr int kMaxMeshLibraryItems = 30;
constexpr int kMaxGridMapSamplesPerItem = 4;

godot::String indent_str(int depth) {
    godot::String s;
    for (int i = 0; i < depth; i++) s += "  ";
    return s;
}

godot::String vector3i_str(const godot::Vector3i &v) {
    return "(" + godot::String::num_int64(v.x) + ", " +
           godot::String::num_int64(v.y) + ", " +
           godot::String::num_int64(v.z) + ")";
}

godot::String aabb_str(const godot::AABB &aabb) {
    return "pos:" + godot::String(aabb.position) +
           " size:" + godot::String(aabb.size);
}

godot::String resource_label(const godot::Ref<godot::Resource> &res) {
    if (!res.is_valid()) return "null";
    return format_resource(res.ptr());
}

godot::Ref<godot::MeshLibrary> target_mesh_library(
    godot::Node *target, const godot::String &property_name) {
    if (target == nullptr) return godot::Ref<godot::MeshLibrary>();

    auto *grid_map = godot::Object::cast_to<godot::GridMap>(target);
    if (grid_map != nullptr) {
        godot::Ref<godot::MeshLibrary> library = grid_map->get_mesh_library();
        if (library.is_valid()) return library;
    }

    if (!property_name.is_empty() && !property_name.contains("/")) {
        godot::Variant value = target->get(property_name);
        if (value.get_type() == godot::Variant::OBJECT) {
            godot::Object *obj = value;
            auto *library = godot::Object::cast_to<godot::MeshLibrary>(obj);
            if (library != nullptr) {
                return godot::Ref<godot::MeshLibrary>(library);
            }
        }
    }

    return godot::Ref<godot::MeshLibrary>();
}

int find_item_index(const godot::Vector<int> &ids, int item) {
    for (int i = 0; i < (int)ids.size(); i++) {
        if (ids[i] == item) return i;
    }
    return -1;
}

godot::String item_name(godot::Ref<godot::MeshLibrary> library, int item) {
    if (!library.is_valid() || item < 0) return godot::String();
    return library->get_item_name(item);
}

} // namespace

godot::String format_mesh_library_resource(const godot::String &property_name,
                                           godot::Node *target,
                                           int indent_depth) {
    godot::String indent = indent_str(indent_depth);
    godot::Ref<godot::MeshLibrary> library =
        target_mesh_library(target, property_name);

    if (!library.is_valid()) {
        return indent +
               "<!-- MeshLibrary API unavailable: target has no MeshLibrary value for this property -->\n";
    }

    godot::String out = indent + "<!-- read via MeshLibrary API -->\n";
    godot::String path = library->get_path();
    if (!path.is_empty() && !path.contains("::")) {
        out += indent + "path = " + path + "\n";
    }

    godot::PackedInt32Array ids = library->get_item_list();
    out += indent + "items: " + godot::String::num_int64(ids.size()) + "\n";
    out += indent + "last_unused_item_id: " +
           godot::String::num_int64(library->get_last_unused_item_id()) + "\n";

    int shown = ids.size() < kMaxMeshLibraryItems ? ids.size()
                                                  : kMaxMeshLibraryItems;
    for (int i = 0; i < shown; i++) {
        int id = ids[i];
        godot::String name = library->get_item_name(id);
        out += indent + "  item[" + godot::String::num_int64(id) + "]";
        if (!name.is_empty()) out += " \"" + name + "\"";
        out += "\n";

        godot::Ref<godot::Mesh> mesh = library->get_item_mesh(id);
        out += indent + "    mesh: " + resource_label(mesh) + "\n";
        if (mesh.is_valid()) {
            out += indent + "    mesh_class: " + mesh->get_class() + "\n";
            out += indent + "    surfaces: " +
                   godot::String::num_int64(mesh->get_surface_count()) + "\n";
            out += indent + "    aabb: " + aabb_str(mesh->get_aabb()) + "\n";
        }

        godot::Array shapes = library->get_item_shapes(id);
        out += indent + "    collision_shapes: " +
               godot::String::num_int64(shapes.size() / 2) + "\n";

        godot::Ref<godot::NavigationMesh> nav_mesh =
            library->get_item_navigation_mesh(id);
        if (nav_mesh.is_valid()) {
            out += indent + "    navigation_mesh: " +
                   resource_label(nav_mesh) + "\n";
            out += indent + "    navigation_layers: " +
                   godot::String::num_int64(
                       library->get_item_navigation_layers(id)) +
                   "\n";
        }

        godot::Ref<godot::Texture2D> preview = library->get_item_preview(id);
        if (preview.is_valid()) {
            out += indent + "    preview: " + resource_label(preview) + "\n";
        }
    }

    if (ids.size() > shown) {
        out += indent + "  ... " +
               godot::String::num_int64(ids.size() - shown) +
               " more items\n";
    }

    out += indent +
           "detail_hint: Use run_scene_edit_script for exact MeshLibrary item transforms, shape resources, or per-surface material details.\n";
    return out;
}

godot::String format_grid_map_data(godot::Node *target, int indent_depth) {
    godot::String indent = indent_str(indent_depth);
    auto *grid_map = godot::Object::cast_to<godot::GridMap>(target);
    if (grid_map == nullptr) {
        return indent +
               "<!-- GridMap data API unavailable: target is not a GridMap -->\n";
    }

    godot::TypedArray<godot::Vector3i> cells = grid_map->get_used_cells();
    godot::String out = indent + "<!-- read via GridMap API -->\n";
    out += indent + "used_cells: " + godot::String::num_int64(cells.size()) +
           "\n";

    godot::Ref<godot::MeshLibrary> library = grid_map->get_mesh_library();
    if (library.is_valid()) {
        out += indent + "mesh_library_items: " +
               godot::String::num_int64(library->get_item_list().size()) +
               "\n";
    }

    if (cells.is_empty()) return out;

    godot::Vector3i min_cell = cells[0];
    godot::Vector3i max_cell = cells[0];
    godot::Vector<int> item_ids;
    godot::Vector<int> item_counts;

    for (int i = 0; i < cells.size(); i++) {
        godot::Vector3i cell = cells[i];
        if (cell.x < min_cell.x) min_cell.x = cell.x;
        if (cell.y < min_cell.y) min_cell.y = cell.y;
        if (cell.z < min_cell.z) min_cell.z = cell.z;
        if (cell.x > max_cell.x) max_cell.x = cell.x;
        if (cell.y > max_cell.y) max_cell.y = cell.y;
        if (cell.z > max_cell.z) max_cell.z = cell.z;

        int item = grid_map->get_cell_item(cell);
        int idx = find_item_index(item_ids, item);
        if (idx == -1) {
            item_ids.push_back(item);
            item_counts.push_back(1);
        } else {
            item_counts.write[idx] = item_counts[idx] + 1;
        }
    }

    out += indent + "bounds: " + vector3i_str(min_cell) + " -> " +
           vector3i_str(max_cell) + "\n";
    out += indent + "item_usage:\n";
    for (int i = 0; i < (int)item_ids.size(); i++) {
        godot::String name = item_name(library, item_ids[i]);
        out += indent + "  item[" + godot::String::num_int64(item_ids[i]) +
               "]";
        if (!name.is_empty()) out += " \"" + name + "\"";
        out += ": " + godot::String::num_int64(item_counts[i]) + " cells\n";
    }

    out += indent + "sample_cells_by_item:\n";
    for (int item_idx = 0; item_idx < (int)item_ids.size(); item_idx++) {
        int item = item_ids[item_idx];
        godot::String name = item_name(library, item);
        out += indent + "  item[" + godot::String::num_int64(item) + "]";
        if (!name.is_empty()) out += " \"" + name + "\"";
        out += ":\n";

        int shown = 0;
        for (int cell_idx = 0; cell_idx < cells.size(); cell_idx++) {
            godot::Vector3i cell = cells[cell_idx];
            if (grid_map->get_cell_item(cell) != item) continue;
            out += indent + "    " + vector3i_str(cell) + " orientation:" +
                   godot::String::num_int64(
                       grid_map->get_cell_item_orientation(cell)) +
                   "\n";
            shown++;
            if (shown >= kMaxGridMapSamplesPerItem) break;
        }

        int remaining = item_counts[item_idx] - shown;
        if (remaining > 0) {
            out += indent + "    ... " +
                   godot::String::num_int64(remaining) + " more cells\n";
        }
    }
    out += indent +
           "detail_hint: Use run_scene_edit_script with GridMap.get_used_cells(), get_cell_item(), and get_cell_item_orientation() for exact placement edits.\n";
    return out;
}

} // namespace fennara::get_node_properties
