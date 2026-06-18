#include "fennara/tools/get_node_properties/tile_resources.hpp"

#include "fennara/tools/get_node_properties/common.hpp"

#include <godot_cpp/classes/tile_map.hpp>
#include <godot_cpp/classes/tile_map_layer.hpp>
#include <godot_cpp/variant/vector2i.hpp>

namespace fennara::get_node_properties {

namespace {

constexpr int kMaxCellSamples = 3;
constexpr int kMaxAtlasUsageLines = 16;

struct CellInfo {
    godot::Vector2i cell;
    int source_id = -1;
    godot::Vector2i atlas = godot::Vector2i(-1, -1);
    int alternative = -1;
    bool flip_h = false;
    bool flip_v = false;
    bool transpose = false;
};

struct UsageRow {
    godot::String key;
    int count = 0;
};

godot::String indent_str(int depth) {
    godot::String s;
    for (int i = 0; i < depth; i++) s += "  ";
    return s;
}

godot::String vec2i_key(const godot::Vector2i &v) {
    return "(" + godot::String::num_int64(v.x) + "," +
           godot::String::num_int64(v.y) + ")";
}

godot::String cell_key(const CellInfo &info) {
    return "source[" + godot::String::num_int64(info.source_id) +
           "] atlas:" + vec2i_key(info.atlas) + " alt:" +
           godot::String::num_int64(info.alternative);
}

godot::Vector<godot::Variant> sorted_keys(const godot::Dictionary &dict) {
    godot::Array keys = dict.keys();
    godot::Vector<godot::Variant> out;
    for (int i = 0; i < keys.size(); i++) out.push_back(keys[i]);

    for (int i = 0; i < (int)out.size(); i++) {
        for (int j = i + 1; j < (int)out.size(); j++) {
            if (godot::String(out[j]) < godot::String(out[i])) {
                godot::Variant tmp = out[i];
                out.write[i] = out[j];
                out.write[j] = tmp;
            }
        }
    }
    return out;
}

godot::String format_cells_summary(const godot::Vector<CellInfo> &cells,
                                   int indent_depth) {
    godot::String indent = indent_str(indent_depth);
    godot::String out;
    godot::Dictionary by_source;
    godot::Dictionary by_tile;

    for (int i = 0; i < (int)cells.size(); i++) {
        const CellInfo &info = cells[i];
        godot::String source_key = godot::String::num_int64(info.source_id);
        by_source[source_key] = int(by_source.get(source_key, 0)) + 1;
        godot::String key = cell_key(info);
        by_tile[key] = int(by_tile.get(key, 0)) + 1;
    }

    out += indent + "used_cells: " +
           godot::String::num_int64((int64_t)cells.size()) + "\n";

    out += indent + "cells_by_source:\n";
    godot::Vector<godot::Variant> source_keys = sorted_keys(by_source);
    for (int i = 0; i < (int)source_keys.size(); i++) {
        godot::String key = source_keys[i];
        out += indent + "  source[" + key + "]: " +
               godot::String::num_int64(int(by_source[key])) + "\n";
    }

    godot::Vector<UsageRow> usage;
    godot::Array tile_keys = by_tile.keys();
    for (int i = 0; i < tile_keys.size(); i++) {
        UsageRow row;
        row.key = tile_keys[i];
        row.count = by_tile[tile_keys[i]];
        usage.push_back(row);
    }
    for (int i = 0; i < (int)usage.size(); i++) {
        for (int j = i + 1; j < (int)usage.size(); j++) {
            if (usage[j].count > usage[i].count) {
                UsageRow tmp = usage[i];
                usage.write[i] = usage[j];
                usage.write[j] = tmp;
            }
        }
    }

    out += indent + "atlas_usage_top:\n";
    int shown = (int)usage.size() < kMaxAtlasUsageLines
                    ? (int)usage.size()
                    : kMaxAtlasUsageLines;
    for (int i = 0; i < shown; i++) {
        out += indent + "  " + usage[i].key + " cells:" +
               godot::String::num_int64(usage[i].count) + "\n";
    }
    if ((int)usage.size() > shown) {
        out += indent + "  ... " +
               godot::String::num_int64((int)usage.size() - shown) +
               " more atlas/alternative entries\n";
    }

    out += indent + "sample_cells:\n";
    int sample_count = (int)cells.size() < kMaxCellSamples
                           ? (int)cells.size()
                           : kMaxCellSamples;
    for (int i = 0; i < sample_count; i++) {
        const CellInfo &info = cells[i];
        out += indent + "  " + vec2i_key(info.cell) + " " + cell_key(info) +
               " flip_h:" + (info.flip_h ? "true" : "false") +
               " flip_v:" + (info.flip_v ? "true" : "false") +
               " transpose:" + (info.transpose ? "true" : "false") + "\n";
    }
    if ((int)cells.size() > sample_count) {
        out += indent + "  ... " +
               godot::String::num_int64((int)cells.size() - sample_count) +
               " more placed cells\n";
    }

    return out;
}

godot::Vector<CellInfo> collect_layer_cells(godot::TileMapLayer *layer) {
    godot::Vector<CellInfo> infos;
    godot::TypedArray<godot::Vector2i> cells = layer->get_used_cells();
    for (int i = 0; i < cells.size(); i++) {
        godot::Vector2i cell = cells[i];
        CellInfo info;
        info.cell = cell;
        info.source_id = layer->get_cell_source_id(cell);
        info.atlas = layer->get_cell_atlas_coords(cell);
        info.alternative = layer->get_cell_alternative_tile(cell);
        info.flip_h = layer->is_cell_flipped_h(cell);
        info.flip_v = layer->is_cell_flipped_v(cell);
        info.transpose = layer->is_cell_transposed(cell);
        infos.push_back(info);
    }
    return infos;
}

godot::Vector<CellInfo> collect_legacy_cells(godot::TileMap *tile_map,
                                             int layer_idx) {
    godot::Vector<CellInfo> infos;
    godot::TypedArray<godot::Vector2i> cells =
        tile_map->get_used_cells(layer_idx);
    for (int i = 0; i < cells.size(); i++) {
        godot::Vector2i cell = cells[i];
        CellInfo info;
        info.cell = cell;
        info.source_id = tile_map->get_cell_source_id(layer_idx, cell);
        info.atlas = tile_map->get_cell_atlas_coords(layer_idx, cell);
        info.alternative =
            tile_map->get_cell_alternative_tile(layer_idx, cell);
        info.flip_h = tile_map->is_cell_flipped_h(layer_idx, cell);
        info.flip_v = tile_map->is_cell_flipped_v(layer_idx, cell);
        info.transpose = tile_map->is_cell_transposed(layer_idx, cell);
        infos.push_back(info);
    }
    return infos;
}

int layer_index_from_property(const godot::String &property_name) {
    if (!property_name.begins_with("layer_")) return 0;
    godot::String rest = property_name.substr(6);
    int slash = rest.find("/");
    if (slash == -1) return 0;
    return rest.left(slash).to_int();
}

} // namespace

godot::String format_tile_map_layer_data(godot::Node *target,
                                         int indent_depth) {
    godot::String indent = indent_str(indent_depth);
    auto *layer = godot::Object::cast_to<godot::TileMapLayer>(target);
    if (layer == nullptr) return indent + "not a TileMapLayer\n";

    godot::PackedByteArray raw = layer->get_tile_map_data_as_array();
    godot::String out;
    out += indent +
           "<!-- read via TileMapLayer API; raw PackedByteArray omitted -->\n";
    out += indent + "bytes: " + godot::String::num_int64(raw.size()) + "\n";
    out += indent + "used_rect: " + format_variant(layer->get_used_rect()) + "\n";
    out += format_cells_summary(collect_layer_cells(layer), indent_depth);
    return out;
}

godot::String format_tile_map_legacy_data(godot::Node *target,
                                          const godot::String &property_name,
                                          int indent_depth) {
    godot::String indent = indent_str(indent_depth);
    auto *tile_map = godot::Object::cast_to<godot::TileMap>(target);
    if (tile_map == nullptr) return indent + "not a TileMap\n";

    int layer_idx = layer_index_from_property(property_name);
    godot::String out;
    out += indent +
           "<!-- read via deprecated TileMap API; raw PackedInt32Array omitted -->\n";
    out += indent + "layer: " + godot::String::num_int64(layer_idx) + "\n";
    out += indent + "layer_name: " + tile_map->get_layer_name(layer_idx) + "\n";
    out += indent + "layer_enabled: " +
           godot::String(tile_map->is_layer_enabled(layer_idx) ? "true" : "false") +
           "\n";
    out += indent + "layer_z_index: " +
           godot::String::num_int64(tile_map->get_layer_z_index(layer_idx)) +
           " layer_y_sort_origin: " +
           godot::String::num_int64(tile_map->get_layer_y_sort_origin(layer_idx)) +
           "\n";
    out += indent + "used_rect: " + format_variant(tile_map->get_used_rect()) + "\n";
    out += format_cells_summary(collect_legacy_cells(tile_map, layer_idx),
                                indent_depth);
    return out;
}

} // namespace fennara::get_node_properties
