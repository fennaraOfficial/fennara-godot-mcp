#include "fennara/tools/get_node_properties/tile_resources.hpp"

#include "fennara/tools/get_node_properties/common.hpp"

#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/navigation_polygon.hpp>
#include <godot_cpp/classes/occluder_polygon2d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/physics_material.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/tile_data.hpp>
#include <godot_cpp/classes/tile_map.hpp>
#include <godot_cpp/classes/tile_map_layer.hpp>
#include <godot_cpp/classes/tile_map_pattern.hpp>
#include <godot_cpp/classes/tile_set.hpp>
#include <godot_cpp/classes/tile_set_atlas_source.hpp>
#include <godot_cpp/classes/tile_set_scenes_collection_source.hpp>
#include <godot_cpp/classes/tile_set_source.hpp>

namespace fennara::get_node_properties {

namespace {

constexpr int kMaxSummaryExamples = 16;
constexpr int kMaxNotableExamples = 3;

godot::String indent_str(int depth) {
    godot::String s;
    for (int i = 0; i < depth; i++) s += "  ";
    return s;
}

godot::String resource_label(const godot::Variant &value) {
    if (value.get_type() == godot::Variant::NIL) return "null";
    godot::Object *obj = value;
    if (obj == nullptr) return "null";

    auto *res = godot::Object::cast_to<godot::Resource>(obj);
    if (res != nullptr) {
        godot::String path = res->get_path();
        if (!path.is_empty() && !path.contains("::")) return path;
        godot::String name = res->get_name();
        if (!name.is_empty()) {
            return "<" + res->get_class() + " name=\"" + name + "\">";
        }
        return "<" + res->get_class() + ">";
    }

    return "<" + obj->get_class() + ">";
}

godot::String vec2i_key(const godot::Vector2i &v) {
    return "(" + godot::String::num_int64(v.x) + "," +
           godot::String::num_int64(v.y) + ")";
}

godot::String type_label(int variant_type) {
    return godot::Variant::get_type_name((godot::Variant::Type)variant_type);
}

void add_count(godot::Dictionary &dict, const godot::String &key,
               int amount = 1) {
    dict[key] = int(dict.get(key, 0)) + amount;
}

godot::String counts_inline(const godot::Dictionary &dict, int max_items = 10) {
    godot::Array keys = dict.keys();
    keys.sort();
    godot::String out;
    int shown = keys.size() < max_items ? keys.size() : max_items;
    for (int i = 0; i < shown; i++) {
        if (i > 0) out += ", ";
        godot::String key = keys[i];
        out += key + ":" + godot::String::num_int64(int(dict[key]));
    }
    if (keys.size() > shown) {
        out += ", ... " + godot::String::num_int64(keys.size() - shown) +
               " more";
    }
    return out.is_empty() ? "none" : out;
}

void add_nested_count(godot::Dictionary &dict, const godot::String &group,
                      const godot::String &key) {
    godot::Dictionary counts = dict.get(group, godot::Dictionary());
    counts[key] = int(counts.get(key, 0)) + 1;
    dict[group] = counts;
}

godot::String nested_counts_block(const godot::Dictionary &dict,
                                  const godot::String &indent,
                                  int max_items = 4) {
    godot::Array keys = dict.keys();
    keys.sort();
    if (keys.is_empty()) return indent + godot::String("none\n");

    godot::String out;
    for (int i = 0; i < keys.size(); i++) {
        godot::String key = keys[i];
        godot::Dictionary counts = dict.get(key, godot::Dictionary());
        godot::Array value_keys = counts.keys();
        if (value_keys.size() > max_items) {
            out += indent + key + ": " +
                   godot::String::num_int64(value_keys.size()) +
                   " unique values\n";
        } else {
            out += indent + key + ": " + counts_inline(counts, max_items) +
                   "\n";
        }
    }
    return out;
}

godot::String examples_inline(const godot::Vector<godot::String> &examples,
                              int total_count) {
    if (total_count == 0) return "none";

    godot::String out;
    int shown = (int)examples.size() < kMaxNotableExamples
                    ? (int)examples.size()
                    : kMaxNotableExamples;
    for (int i = 0; i < shown; i++) {
        if (i > 0) out += ", ";
        out += examples[i];
    }
    if (total_count > shown) {
        out += ", ... " + godot::String::num_int64(total_count - shown) +
               " more";
    }
    return out;
}

void append_example(godot::Vector<godot::String> &examples,
                    const godot::String &line) {
    if ((int)examples.size() >= kMaxSummaryExamples) return;
    examples.push_back(line);
}

godot::String examples_block(const godot::Vector<godot::String> &examples,
                             const godot::String &indent,
                             int total_count) {
    godot::String out;
    for (int i = 0; i < (int)examples.size(); i++) {
        out += indent + godot::String("  ") + examples[i] + "\n";
    }
    if (total_count > (int)examples.size()) {
        out += indent + godot::String("  ... ") +
               godot::String::num_int64(total_count - examples.size()) +
               " more\n";
    }
    if (examples.is_empty() && total_count == 0) {
        out += indent + godot::String("  none\n");
    }
    return out;
}

godot::String terrain_name(godot::Ref<godot::TileSet> tile_set,
                           int terrain_set, int terrain) {
    if (!tile_set.is_valid() || terrain_set < 0 || terrain < 0) {
        return godot::String::num_int64(terrain);
    }
    if (terrain_set >= tile_set->get_terrain_sets_count() ||
        terrain >= tile_set->get_terrains_count(terrain_set)) {
        return godot::String::num_int64(terrain);
    }
    godot::String name = tile_set->get_terrain_name(terrain_set, terrain);
    if (name.is_empty()) return godot::String::num_int64(terrain);
    return name + "(" + godot::String::num_int64(terrain) + ")";
}

godot::String format_pattern(godot::Ref<godot::TileMapPattern> pattern,
                             int pattern_index, int indent_depth) {
    godot::String indent = indent_str(indent_depth);
    if (!pattern.is_valid()) {
        return indent + "pattern[" + godot::String::num_int64(pattern_index) +
               "] = null\n";
    }

    godot::TypedArray<godot::Vector2i> cells = pattern->get_used_cells();
    godot::String out = indent + "pattern[" +
                        godot::String::num_int64(pattern_index) + "] size:" +
                        format_variant(pattern->get_size()) + " used_cells:" +
                        godot::String::num_int64(cells.size()) + "\n";
    int shown = cells.size() < kMaxSummaryExamples ? cells.size()
                                                   : kMaxSummaryExamples;
    for (int i = 0; i < shown; i++) {
        godot::Vector2i cell = cells[i];
        out += indent + "  " + vec2i_key(cell) + " source[" +
               godot::String::num_int64(pattern->get_cell_source_id(cell)) +
               "] atlas:" + vec2i_key(pattern->get_cell_atlas_coords(cell)) +
               " alt:" +
               godot::String::num_int64(pattern->get_cell_alternative_tile(cell)) +
               "\n";
    }
    if (cells.size() > shown) {
        out += indent + "  ... " +
               godot::String::num_int64(cells.size() - shown) +
               " more pattern cells\n";
    }
    return out;
}

void summarize_tile_data(godot::TileData *data,
                         godot::Ref<godot::TileSet> tile_set,
                         const godot::String &tile_label,
                         godot::Dictionary &terrain_counts,
                         godot::Dictionary &probability_counts,
                         godot::Dictionary &custom_value_counts,
                         godot::Vector<godot::String> &collision_examples,
                         godot::Vector<godot::String> &unusual_examples,
                         int &collision_tiles, int &collision_polygons,
                         int &occlusion_tiles, int &occlusion_polygons,
                         int &navigation_tiles, int &peering_tiles,
                         int &peering_bits, int &transformed_tiles,
                         int &texture_origin_tiles, int &z_sort_tiles,
                         int &material_tiles) {
    if (data == nullptr) return;

    int terrain_set = data->get_terrain_set();
    int terrain = data->get_terrain();
    add_count(terrain_counts,
              godot::String("set:") + godot::String::num_int64(terrain_set) +
                  " terrain:" + terrain_name(tile_set, terrain_set, terrain));
    add_count(probability_counts, format_variant(data->get_probability()));

    if (data->get_flip_h() || data->get_flip_v() || data->get_transpose()) {
        transformed_tiles++;
        append_example(unusual_examples,
                       tile_label + godot::String(" transform flip_h:") +
                           (data->get_flip_h() ? "true" : "false") +
                           " flip_v:" +
                           (data->get_flip_v() ? "true" : "false") +
                           " transpose:" +
                           (data->get_transpose() ? "true" : "false"));
    }
    if (data->get_texture_origin() != godot::Vector2i(0, 0)) {
        texture_origin_tiles++;
        append_example(unusual_examples,
                       tile_label + godot::String(" texture_origin:") +
                           format_variant(data->get_texture_origin()));
    }
    if (data->get_z_index() != 0 || data->get_y_sort_origin() != 0) {
        z_sort_tiles++;
        append_example(unusual_examples,
                       tile_label + godot::String(" z_index:") +
                           godot::String::num_int64(data->get_z_index()) +
                           " y_sort_origin:" +
                           godot::String::num_int64(data->get_y_sort_origin()));
    }
    if (data->get_material().is_valid()) {
        material_tiles++;
        append_example(unusual_examples,
                       tile_label + godot::String(" material:") +
                           resource_label(data->get_material()));
    }

    if (!tile_set.is_valid()) return;

    for (int layer = 0; layer < tile_set->get_custom_data_layers_count(); layer++) {
        godot::String name = tile_set->get_custom_data_layer_name(layer);
        godot::String value =
            format_variant(data->get_custom_data_by_layer_id(layer));
        add_nested_count(custom_value_counts, name, value);
    }

    int tile_collision_polygons = 0;
    for (int layer = 0; layer < tile_set->get_physics_layers_count(); layer++) {
        tile_collision_polygons += data->get_collision_polygons_count(layer);
    }
    if (tile_collision_polygons > 0) {
        collision_tiles++;
        collision_polygons += tile_collision_polygons;
        append_example(collision_examples,
                       tile_label + godot::String(" collision_polygons:") +
                           godot::String::num_int64(tile_collision_polygons));
    }

    int tile_occlusion_polygons = 0;
    for (int layer = 0; layer < tile_set->get_occlusion_layers_count(); layer++) {
        tile_occlusion_polygons += data->get_occluder_polygons_count(layer);
    }
    if (tile_occlusion_polygons > 0) {
        occlusion_tiles++;
        occlusion_polygons += tile_occlusion_polygons;
        append_example(unusual_examples,
                       tile_label + godot::String(" occlusion_polygons:") +
                           godot::String::num_int64(tile_occlusion_polygons));
    }

    for (int layer = 0; layer < tile_set->get_navigation_layers_count(); layer++) {
        if (data->get_navigation_polygon(layer).is_valid()) {
            navigation_tiles++;
            append_example(unusual_examples,
                           tile_label + godot::String(" navigation_layer:") +
                               godot::String::num_int64(layer));
        }
    }

    int tile_peering_bits = 0;
    for (int bit = 0; bit <= 15; bit++) {
        auto neighbor = (godot::TileSet::CellNeighbor)bit;
        if (!data->is_valid_terrain_peering_bit(neighbor)) continue;
        int value = data->get_terrain_peering_bit(neighbor);
        if (value < 0) continue;
        tile_peering_bits++;
    }
    if (tile_peering_bits > 0) {
        peering_tiles++;
        peering_bits += tile_peering_bits;
    }
}

godot::String format_atlas_source(godot::TileSetAtlasSource *source,
                                  godot::Ref<godot::TileSet> tile_set,
                                  int source_id, int indent_depth) {
    godot::String indent = indent_str(indent_depth);
    godot::String child = indent_str(indent_depth + 1);
    if (source == nullptr) return indent + "source = null\n";

    godot::String out = indent + "source[" +
                        godot::String::num_int64(source_id) +
                        "] = <TileSetAtlasSource>\n";
    out += child + "texture = " + resource_label(source->get_texture()) + "\n";
    out += child + "margins = " + format_variant(source->get_margins()) +
           " separation = " + format_variant(source->get_separation()) + "\n";
    out += child + "texture_region_size = " +
           format_variant(source->get_texture_region_size()) +
           " atlas_grid_size = " +
           format_variant(source->get_atlas_grid_size()) + "\n";
    out += child + "use_texture_padding = " +
           godot::String(source->get_use_texture_padding() ? "true" : "false") +
           "\n";
    out += child + "tiles = " +
           godot::String::num_int64(source->get_tiles_count()) + "\n";

    int alternatives = 0;
    int animated_tiles = 0;
    int collision_tiles = 0;
    int collision_polygons = 0;
    int occlusion_tiles = 0;
    int occlusion_polygons = 0;
    int navigation_tiles = 0;
    int peering_tiles = 0;
    int peering_bits = 0;
    int transformed_tiles = 0;
    int texture_origin_tiles = 0;
    int z_sort_tiles = 0;
    int material_tiles = 0;
    godot::Dictionary terrain_counts;
    godot::Dictionary probability_counts;
    godot::Dictionary custom_value_counts;
    godot::Dictionary tile_size_counts;
    godot::Dictionary alternatives_per_tile_counts;
    godot::Vector<godot::String> animated_examples;
    godot::Vector<godot::String> collision_examples;
    godot::Vector<godot::String> unusual_examples;

    for (int i = 0; i < source->get_tiles_count(); i++) {
        godot::Vector2i coords = source->get_tile_id(i);
        int alt_count = source->get_alternative_tiles_count(coords);
        alternatives += alt_count;
        add_count(tile_size_counts,
                  format_variant(source->get_tile_size_in_atlas(coords)));
        add_count(alternatives_per_tile_counts,
                  godot::String::num_int64(alt_count));

        int frames = source->get_tile_animation_frames_count(coords);
        if (frames > 1 || source->get_tile_animation_speed(coords) != 1.0) {
            animated_tiles++;
            append_example(animated_examples,
                           "tile[" + vec2i_key(coords) + "] frames:" +
                               godot::String::num_int64(frames) + " columns:" +
                               godot::String::num_int64(
                                   source->get_tile_animation_columns(coords)) +
                               " speed:" +
                               format_variant(source->get_tile_animation_speed(coords)) +
                               " total_duration:" +
                               format_variant(
                                   source->get_tile_animation_total_duration(coords)));
        }

        for (int a = 0; a < alt_count; a++) {
            int alt_id = source->get_alternative_tile_id(coords, a);
            godot::String tile_label = "tile[" + vec2i_key(coords) + "] alt[" +
                                       godot::String::num_int64(alt_id) + "]";
            summarize_tile_data(
                source->get_tile_data(coords, alt_id), tile_set, tile_label,
                terrain_counts, probability_counts, custom_value_counts,
                collision_examples, unusual_examples, collision_tiles, collision_polygons,
                occlusion_tiles, occlusion_polygons, navigation_tiles,
                peering_tiles, peering_bits, transformed_tiles,
                texture_origin_tiles, z_sort_tiles, material_tiles);
        }
    }

    out += child + "atlas_tiles_summary:\n";
    out += child + "  sizes: " + counts_inline(tile_size_counts) + "\n";
    out += child + "  alternatives: " +
           counts_inline(alternatives_per_tile_counts) + "\n";
    out += child + "tile_data_summary:\n";
    out += child + "  alternatives: " + godot::String::num_int64(alternatives) + "\n";
    out += child + "  terrain_counts: " + counts_inline(terrain_counts) + "\n";
    out += child + "  probability_counts: " + counts_inline(probability_counts) + "\n";
    out += child + "  custom_data_counts:\n" +
           nested_counts_block(custom_value_counts, child + "    ");
    out += child + "  collision_tiles: " +
           godot::String::num_int64(collision_tiles) + " polygons:" +
           godot::String::num_int64(collision_polygons) + "\n";
    out += child + "  occlusion_tiles: " +
           godot::String::num_int64(occlusion_tiles) + " polygons:" +
           godot::String::num_int64(occlusion_polygons) + "\n";
    out += child + "  navigation_tiles: " +
           godot::String::num_int64(navigation_tiles) + "\n";
    out += child + "  peering_tiles: " + godot::String::num_int64(peering_tiles) +
           " peering_bits:" + godot::String::num_int64(peering_bits) + "\n";
    out += child + "  animated_tiles: " + godot::String::num_int64(animated_tiles) + "\n";
    out += child + "  transformed_tiles: " +
           godot::String::num_int64(transformed_tiles) +
           " texture_origin_tiles:" +
           godot::String::num_int64(texture_origin_tiles) +
           " z_or_y_sort_tiles:" + godot::String::num_int64(z_sort_tiles) +
           " material_tiles:" + godot::String::num_int64(material_tiles) + "\n";

    out += child + "notable_tiles:\n";
    out += child + "  collision: " +
           examples_inline(collision_examples, collision_tiles) + "\n";
    out += child + "  animated: " +
           examples_inline(animated_examples, animated_tiles) + "\n";
    out += child + "  unusual: " +
           examples_inline(unusual_examples,
                           transformed_tiles + texture_origin_tiles +
                               z_sort_tiles + material_tiles + occlusion_tiles +
                               navigation_tiles) +
           "\n";
    out += child +
           "detail_hint: For exact TileData for one atlas tile, use run_scene_edit_script and log TileSetAtlasSource.get_tile_data(coords, alternative_id) plus the TileData APIs.\n";
    out += indent + "</TileSetAtlasSource>\n";
    return out;
}

godot::String format_scene_source(godot::TileSetScenesCollectionSource *source,
                                  int source_id, int indent_depth) {
    godot::String indent = indent_str(indent_depth);
    godot::String child = indent_str(indent_depth + 1);
    if (source == nullptr) return indent + "source = null\n";

    godot::String out = indent + "source[" +
                        godot::String::num_int64(source_id) +
                        "] = <TileSetScenesCollectionSource>\n";
    out += child + "scene_tiles = " +
           godot::String::num_int64(source->get_scene_tiles_count()) + "\n";
    for (int i = 0; i < source->get_scene_tiles_count(); i++) {
        int id = source->get_scene_tile_id(i);
        out += child + "scene_tile[" + godot::String::num_int64(id) +
               "] scene:" + resource_label(source->get_scene_tile_scene(id)) +
               " display_placeholder:" +
               (source->get_scene_tile_display_placeholder(id) ? "true" : "false") +
               "\n";
    }
    out += indent + "</TileSetScenesCollectionSource>\n";
    return out;
}

godot::String format_tileset(godot::Ref<godot::TileSet> tile_set,
                             int indent_depth) {
    godot::String indent = indent_str(indent_depth);
    if (!tile_set.is_valid()) return indent + "null TileSet\n";

    godot::String out = indent + "tile_size = " +
                        format_variant(tile_set->get_tile_size()) + "\n";
    out += indent + "tile_shape = " +
           godot::String::num_int64(tile_set->get_tile_shape()) +
           " tile_layout = " +
           godot::String::num_int64(tile_set->get_tile_layout()) +
           " tile_offset_axis = " +
           godot::String::num_int64(tile_set->get_tile_offset_axis()) + "\n";
    out += indent + "uv_clipping = " +
           godot::String(tile_set->is_uv_clipping() ? "true" : "false") + "\n";

    out += indent + "physics_layers: " +
           godot::String::num_int64(tile_set->get_physics_layers_count()) + "\n";
    for (int i = 0; i < tile_set->get_physics_layers_count(); i++) {
        out += indent + "  [" + godot::String::num_int64(i) +
               "] collision_layer:" +
               godot::String::num_int64(tile_set->get_physics_layer_collision_layer(i)) +
               " collision_mask:" +
               godot::String::num_int64(tile_set->get_physics_layer_collision_mask(i)) +
               " priority:" +
               format_variant(tile_set->get_physics_layer_collision_priority(i)) +
               " material:" +
               resource_label(tile_set->get_physics_layer_physics_material(i)) + "\n";
    }

    out += indent + "navigation_layers: " +
           godot::String::num_int64(tile_set->get_navigation_layers_count()) + "\n";
    for (int i = 0; i < tile_set->get_navigation_layers_count(); i++) {
        out += indent + "  [" + godot::String::num_int64(i) + "] layers:" +
               godot::String::num_int64(tile_set->get_navigation_layer_layers(i)) + "\n";
    }

    out += indent + "occlusion_layers: " +
           godot::String::num_int64(tile_set->get_occlusion_layers_count()) + "\n";
    for (int i = 0; i < tile_set->get_occlusion_layers_count(); i++) {
        out += indent + "  [" + godot::String::num_int64(i) +
               "] light_mask:" +
               godot::String::num_int64(tile_set->get_occlusion_layer_light_mask(i)) +
               " sdf_collision:" +
               (tile_set->get_occlusion_layer_sdf_collision(i) ? "true" : "false") +
               "\n";
    }

    out += indent + "custom_data_layers: " +
           godot::String::num_int64(tile_set->get_custom_data_layers_count()) + "\n";
    for (int i = 0; i < tile_set->get_custom_data_layers_count(); i++) {
        out += indent + "  [" + godot::String::num_int64(i) + "] name:" +
               tile_set->get_custom_data_layer_name(i) + " type:" +
               type_label(tile_set->get_custom_data_layer_type(i)) + "\n";
    }

    out += indent + "terrain_sets: " +
           godot::String::num_int64(tile_set->get_terrain_sets_count()) + "\n";
    for (int set = 0; set < tile_set->get_terrain_sets_count(); set++) {
        out += indent + "  set[" + godot::String::num_int64(set) +
               "] mode:" +
               godot::String::num_int64(tile_set->get_terrain_set_mode(set)) +
               " terrains:" +
               godot::String::num_int64(tile_set->get_terrains_count(set)) + "\n";
        for (int terrain = 0; terrain < tile_set->get_terrains_count(set);
             terrain++) {
            out += indent + "    terrain[" +
                   godot::String::num_int64(terrain) + "] name:" +
                   tile_set->get_terrain_name(set, terrain) + " color:" +
                   format_variant(tile_set->get_terrain_color(set, terrain)) + "\n";
        }
    }

    out += indent + "patterns: " +
           godot::String::num_int64(tile_set->get_patterns_count()) + "\n";
    for (int i = 0; i < tile_set->get_patterns_count(); i++) {
        out += format_pattern(tile_set->get_pattern(i), i, indent_depth + 1);
    }

    out += indent + "sources: " +
           godot::String::num_int64(tile_set->get_source_count()) + "\n";
    for (int i = 0; i < tile_set->get_source_count(); i++) {
        int source_id = tile_set->get_source_id(i);
        godot::Ref<godot::TileSetSource> source = tile_set->get_source(source_id);
        if (!source.is_valid()) {
            out += indent + "  source[" + godot::String::num_int64(source_id) +
                   "] = null\n";
            continue;
        }

        auto *atlas = godot::Object::cast_to<godot::TileSetAtlasSource>(source.ptr());
        auto *scenes =
            godot::Object::cast_to<godot::TileSetScenesCollectionSource>(source.ptr());
        if (atlas != nullptr) {
            out += format_atlas_source(atlas, tile_set, source_id,
                                       indent_depth + 1);
        } else if (scenes != nullptr) {
            out += format_scene_source(scenes, source_id, indent_depth + 1);
        } else {
            out += indent + "  source[" +
                   godot::String::num_int64(source_id) + "] = <" +
                   source->get_class() + "> tiles:" +
                   godot::String::num_int64(source->get_tiles_count()) + "\n";
        }
    }

    return out;
}

godot::Ref<godot::TileSet> target_tileset(godot::Node *target) {
    if (target == nullptr) return godot::Ref<godot::TileSet>();

    auto *layer = godot::Object::cast_to<godot::TileMapLayer>(target);
    if (layer != nullptr) return layer->get_tile_set();

    auto *legacy = godot::Object::cast_to<godot::TileMap>(target);
    if (legacy != nullptr) return legacy->get_tileset();

    return godot::Ref<godot::TileSet>();
}

} // namespace

godot::String format_tile_resource(const godot::String &resource_type,
                                   const godot::String &property_name,
                                   godot::Node *target, int indent_depth) {
    (void)property_name;
    godot::String indent = indent_str(indent_depth);
    godot::Ref<godot::TileSet> tile_set = target_tileset(target);

    if (resource_type == "TileSet") {
        return indent + "<!-- read via TileSet API -->\n" +
               format_tileset(tile_set, indent_depth);
    }

    if (!tile_set.is_valid()) {
        return indent +
               "<!-- tile resource API unavailable: target has no TileSet -->\n";
    }

    return indent + "<!-- read via owning TileSet API -->\n" +
           format_tileset(tile_set, indent_depth);
}

} // namespace fennara::get_node_properties
