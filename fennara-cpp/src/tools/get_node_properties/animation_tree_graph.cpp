#include "fennara/tools/get_node_properties/animation_tree_graph.hpp"

#include <godot_cpp/variant/dictionary.hpp>

namespace fennara::get_node_properties {

namespace {

constexpr int kMaxRecursionDepth = 5;
constexpr int kMaxListItems = 24;
constexpr int kMaxBlendPointItems = 3;

struct NamedRef {
    godot::String name;
    godot::String id;
};

godot::String indent_str(int depth) {
    godot::String s;
    for (int i = 0; i < depth; i++) s += "  ";
    return s;
}

bool seen_id(const godot::Vector<godot::String> &seen,
             const godot::String &id) {
    for (int i = 0; i < (int)seen.size(); i++) {
        if (seen[i] == id) return true;
    }
    return false;
}

bool has_name(const godot::Vector<godot::String> &names,
              const godot::String &name) {
    for (int i = 0; i < (int)names.size(); i++) {
        if (names[i] == name) return true;
    }
    return false;
}

bool split_property(const godot::String &line, godot::String &key,
                    godot::String &value) {
    int eq = line.find(" = ");
    if (eq == -1) return false;
    key = line.left(eq);
    value = line.substr(eq + 3);
    return true;
}

godot::Dictionary property_map(const godot::Vector<godot::String> &lines) {
    godot::Dictionary props;
    for (int i = 0; i < (int)lines.size(); i++) {
        godot::String key;
        godot::String value;
        if (split_property(lines[i], key, value)) props[key] = value;
    }
    return props;
}

godot::String prop_value(const godot::Dictionary &props,
                         const godot::String &key) {
    if (!props.has(key)) return godot::String();
    return godot::String(props[key]);
}

godot::String extract_ref_id(const godot::String &value,
                             const godot::String &prefix) {
    godot::String trimmed = value.strip_edges();
    if (!trimmed.begins_with(prefix + godot::String("(\""))) {
        return godot::String();
    }
    if (!trimmed.ends_with("\")")) return godot::String();
    int start = prefix.length() + 2;
    int end = trimmed.length() - 2;
    if (end <= start) return godot::String();
    return trimmed.substr(start, end - start);
}

godot::String clean_symbol(const godot::String &value) {
    godot::String trimmed = value.strip_edges();
    if (trimmed.begins_with("&\"") && trimmed.ends_with("\"")) {
        return trimmed.substr(2, trimmed.length() - 3);
    }
    if (trimmed.begins_with("\"") && trimmed.ends_with("\"")) {
        return trimmed.substr(1, trimmed.length() - 2);
    }
    return trimmed;
}

godot::String animation_label(
    const godot::Dictionary &props) {
    godot::String animation = prop_value(props, "animation");
    if (animation.is_empty()) return godot::String();
    return godot::String(" animation:\"") + clean_symbol(animation) + "\"";
}

godot::String animation_name(const TscnData &data, const godot::String &id) {
    if (!data.sub_resources.has(id)) return godot::String();
    const SubResourceBlock &block = data.sub_resources[id];
    if (block.type != "AnimationNodeAnimation") return godot::String();
    return clean_symbol(prop_value(property_map(block.lines), "animation"));
}

godot::String resource_label(const TscnData &data, const godot::String &id) {
    if (!data.sub_resources.has(id)) return godot::String("<missing>");
    const SubResourceBlock &block = data.sub_resources[id];
    godot::String label = block.type;
    if (block.type == "AnimationNodeAnimation") {
        label += animation_label(property_map(block.lines));
    }
    return label;
}

void add_count(godot::Dictionary &dict, const godot::String &key) {
    dict[key] = int(dict.get(key, 0)) + 1;
}

godot::String counts_inline(const godot::Dictionary &dict, int max_items = 8) {
    godot::Array keys = dict.keys();
    keys.sort();
    godot::String out;
    int shown = keys.size() < max_items ? keys.size() : max_items;
    for (int i = 0; i < shown; i++) {
        if (i > 0) out += ", ";
        godot::String key = keys[i];
        out += key + godot::String(":") +
               godot::String::num_int64(int(dict[key]));
    }
    if (keys.size() > shown) {
        out += ", ... " + godot::String::num_int64(keys.size() - shown) +
               " more";
    }
    return out.is_empty() ? "none" : out;
}

godot::Vector<godot::String> parse_array_tokens(const godot::String &value) {
    godot::Vector<godot::String> tokens;
    int i = 0;
    while (i < value.length()) {
        if (value.substr(i, 13) == "SubResource(\"") {
            int start = i + 13;
            int end = value.find("\")", start);
            if (end == -1) break;
            tokens.push_back("R:" + value.substr(start, end - start));
            i = end + 2;
            continue;
        }

        if (value[i] == '&' && i + 1 < value.length() && value[i + 1] == '"') {
            int start = i + 2;
            int end = value.find("\"", start);
            if (end == -1) break;
            tokens.push_back("S:" + value.substr(start, end - start));
            i = end + 1;
            continue;
        }

        if (value[i] == '"') {
            int start = i + 1;
            int end = value.find("\"", start);
            if (end == -1) break;
            tokens.push_back("S:" + value.substr(start, end - start));
            i = end + 1;
            continue;
        }

        bool number_start = (value[i] >= '0' && value[i] <= '9') ||
                            value[i] == '-';
        if (number_start) {
            int start = i;
            while (i < value.length() && value[i] != ',' &&
                   value[i] != ']' && value[i] != ' ') {
                i++;
            }
            tokens.push_back("N:" + value.substr(start, i - start));
            continue;
        }

        i++;
    }
    return tokens;
}

godot::String transition_detail(const TscnData &data, const godot::String &id) {
    if (!data.sub_resources.has(id)) return godot::String();
    const SubResourceBlock &block = data.sub_resources[id];
    godot::Dictionary props = property_map(block.lines);
    godot::Vector<godot::String> keys;
    keys.push_back("advance_mode");
    keys.push_back("advance_condition");
    keys.push_back("xfade_time");
    keys.push_back("reset");
    keys.push_back("switch_mode");

    godot::String out;
    for (int i = 0; i < (int)keys.size(); i++) {
        godot::String value = prop_value(props, keys[i]);
        if (value.is_empty()) continue;
        if (!out.is_empty()) out += " ";
        out += keys[i] + godot::String(":") + clean_symbol(value);
    }
    return out;
}

void add_named_ref(godot::Vector<NamedRef> &items, const godot::String &name,
                   const godot::String &id) {
    for (int i = 0; i < (int)items.size(); i++) {
        if (items[i].name == name) return;
    }
    NamedRef ref;
    ref.name = name;
    ref.id = id;
    items.push_back(ref);
}

godot::Vector<NamedRef> collect_blend_tree_nodes(
    const godot::Dictionary &props) {
    godot::Vector<NamedRef> nodes;
    godot::Array keys = props.keys();
    keys.sort();

    for (int i = 0; i < keys.size(); i++) {
        godot::String key = keys[i];
        if (!key.begins_with("nodes/") || !key.ends_with("/node")) continue;
        godot::String rest = key.substr(6);
        int slash = rest.find("/");
        if (slash == -1) continue;
        godot::String name = rest.left(slash);
        godot::String id = extract_ref_id(props[key], "SubResource");
        add_named_ref(nodes, name, id);
    }
    return nodes;
}

godot::Vector<NamedRef> collect_state_nodes(
    const godot::Dictionary &props,
    godot::Vector<godot::String> &state_names) {
    godot::Vector<NamedRef> states;
    godot::Array keys = props.keys();
    keys.sort();

    for (int i = 0; i < keys.size(); i++) {
        godot::String key = keys[i];
        if (!key.begins_with("states/")) continue;
        godot::String rest = key.substr(7);
        int slash = rest.find("/");
        if (slash == -1) continue;
        godot::String name = rest.left(slash);
        if (!has_name(state_names, name)) state_names.push_back(name);
        if (!key.ends_with("/node")) continue;
        godot::String id = extract_ref_id(props[key], "SubResource");
        add_named_ref(states, name, id);
    }
    return states;
}

godot::String format_connections(
    const godot::Dictionary &props, int indent_depth) {
    godot::String value = prop_value(props, "node_connections");
    if (value.is_empty()) return godot::String();

    godot::Vector<godot::String> tokens = parse_array_tokens(value);
    int triples = tokens.size() / 3;
    godot::String indent = indent_str(indent_depth);
    godot::String out = indent + "connections: " +
                        godot::String::num_int64(triples) + "\n";

    int shown = triples < kMaxListItems ? triples : kMaxListItems;
    for (int i = 0; i < shown; i++) {
        godot::String to = tokens[i * 3].substr(2);
        godot::String input = tokens[i * 3 + 1].substr(2);
        godot::String from = tokens[i * 3 + 2].substr(2);
        out += indent + "  " + to + "[" + input + "] <- " + from + "\n";
    }
    if (triples > shown) {
        out += indent + "  ... " + godot::String::num_int64(triples - shown) +
               " more\n";
    }
    return out;
}

godot::String format_transitions(
    const TscnData &data,
    const godot::Dictionary &props,
    int indent_depth) {
    godot::String value = prop_value(props, "transitions");
    if (value.is_empty()) return godot::String();

    godot::Vector<godot::String> tokens = parse_array_tokens(value);
    int triples = tokens.size() / 3;
    godot::String indent = indent_str(indent_depth);
    godot::String out = indent + "transitions: " +
                        godot::String::num_int64(triples) + "\n";

    int shown = triples < kMaxListItems ? triples : kMaxListItems;
    for (int i = 0; i < shown; i++) {
        godot::String from = tokens[i * 3].substr(2);
        godot::String to = tokens[i * 3 + 1].substr(2);
        godot::String trans_id = tokens[i * 3 + 2].substr(2);
        godot::String detail = transition_detail(data, trans_id);
        out += indent + "  " + from + " -> " + to;
        if (!detail.is_empty()) out += " (" + detail + ")";
        out += "\n";
    }
    if (triples > shown) {
        out += indent + "  ... " + godot::String::num_int64(triples - shown) +
               " more\n";
        out += indent +
               "  detail_hint: Full transition list omitted for compactness; "
               "use run_scene_edit_script to load AnimationTree.tree_root and print "
               "that state machine's transitions.\n";
    }
    return out;
}

godot::String format_blend_space(const TscnData &data, const SubResourceBlock &block,
                                 int indent_depth) {
    godot::Dictionary props = property_map(block.lines);
    godot::Vector<NamedRef> points;
    godot::Array keys = props.keys();
    keys.sort();

    for (int i = 0; i < keys.size(); i++) {
        godot::String key = keys[i];
        if (!key.begins_with("blend_point_") || !key.ends_with("/node")) continue;
        godot::String point = key.left(key.find("/"));
        godot::String id = extract_ref_id(props[key], "SubResource");
        add_named_ref(points, point, id);
    }

    godot::String indent = indent_str(indent_depth);
    godot::String out = indent + "blend_points: " +
                        godot::String::num_int64(points.size()) + "\n";
    godot::Dictionary animation_counts;
    for (int i = 0; i < (int)points.size(); i++) {
        godot::String name = animation_name(data, points[i].id);
        if (!name.is_empty()) add_count(animation_counts, name);
    }
    if (!animation_counts.is_empty()) {
        out += indent + "animations: " + counts_inline(animation_counts) + "\n";
    }

    out += indent + "sample_points:\n";
    int shown = points.size() < kMaxBlendPointItems ? points.size() : kMaxBlendPointItems;
    for (int i = 0; i < shown; i++) {
        godot::String pos =
            prop_value(props, points[i].name + godot::String("/pos"));
        godot::String label = resource_label(data, points[i].id);
        out += indent + "  " + points[i].name;
        if (!pos.is_empty()) out += " " + pos;
        out += " -> " + label + "\n";
    }
    if (points.size() > shown) {
        out += indent + "  ... " +
               godot::String::num_int64(points.size() - shown) + " more\n";
    }
    return out;
}

godot::String format_simple_animation_node(const SubResourceBlock &block,
                                           int indent_depth) {
    godot::Dictionary props = property_map(block.lines);
    godot::Vector<godot::String> keys;
    keys.push_back("animation");
    keys.push_back("fadein_time");
    keys.push_back("fadeout_time");
    keys.push_back("autorestart");
    keys.push_back("mix_mode");
    keys.push_back("sync");
    keys.push_back("filter_enabled");

    godot::String indent = indent_str(indent_depth);
    godot::String out;
    for (int i = 0; i < (int)keys.size(); i++) {
        godot::String value = prop_value(props, keys[i]);
        if (value.is_empty()) continue;
        out += indent + keys[i] + " = " + clean_symbol(value) + "\n";
    }
    return out;
}

godot::String format_resource_summary(const TscnData &data,
                                      const godot::String &id,
                                      int indent_depth,
                                      int recursion_depth,
                                      godot::Vector<godot::String> seen);

godot::String format_state_machine(
    const TscnData &data, const SubResourceBlock &block, int indent_depth,
    int recursion_depth, godot::Vector<godot::String> seen) {
    godot::Dictionary props = property_map(block.lines);
    godot::Vector<godot::String> state_names;
    godot::Vector<NamedRef> state_nodes = collect_state_nodes(props, state_names);

    godot::String indent = indent_str(indent_depth);
    godot::String out = indent + "states: " +
                        godot::String::num_int64(state_names.size()) + "\n";

    int shown = state_names.size() < kMaxListItems ? state_names.size() : kMaxListItems;
    for (int i = 0; i < shown; i++) {
        godot::String label = "Start/End";
        for (int j = 0; j < (int)state_nodes.size(); j++) {
            if (state_nodes[j].name == state_names[i]) {
                label = resource_label(data, state_nodes[j].id);
                break;
            }
        }
        out += indent + "  " + state_names[i] + ": " + label + "\n";
    }
    if (state_names.size() > shown) {
        out += indent + "  ... " +
               godot::String::num_int64(state_names.size() - shown) + " more\n";
    }

    out += format_transitions(data, props, indent_depth);

    if (recursion_depth >= kMaxRecursionDepth) return out;

    int nested = 0;
    for (int i = 0; i < (int)state_nodes.size(); i++) {
        if (!data.sub_resources.has(state_nodes[i].id)) continue;
        godot::String type = data.sub_resources[state_nodes[i].id].type;
        bool useful_nested = type == "AnimationNodeStateMachine" ||
                             type == "AnimationNodeBlendTree" ||
                             type == "AnimationNodeBlendSpace1D" ||
                             type == "AnimationNodeBlendSpace2D";
        if (!useful_nested) continue;
        if (nested == 0) out += indent + "nested:\n";
        nested++;
        out += indent + "  " + state_nodes[i].name + " = <" + type + ">\n";
        out += format_resource_summary(data, state_nodes[i].id, indent_depth + 2,
                                       recursion_depth + 1, seen);
        out += indent + "  </" + type + ">\n";
        if (nested >= 6 && state_nodes.size() > nested) {
            out += indent + "  ... more nested states omitted\n";
            break;
        }
    }

    return out;
}

godot::String format_blend_tree(
    const TscnData &data, const SubResourceBlock &block, int indent_depth,
    int recursion_depth, godot::Vector<godot::String> seen) {
    godot::Dictionary props = property_map(block.lines);
    godot::Vector<NamedRef> nodes = collect_blend_tree_nodes(props);

    godot::String indent = indent_str(indent_depth);
    godot::String out;
    godot::String graph_offset = prop_value(props, "graph_offset");
    if (!graph_offset.is_empty()) out += indent + "graph_offset = " + graph_offset + "\n";

    out += indent + "nodes: " + godot::String::num_int64(nodes.size());
    if (props.has("nodes/output/position")) out += " plus output";
    out += "\n";
    if (props.has("nodes/output/position")) {
        out += indent + "  output: AnimationNodeOutput\n";
    }

    int shown = nodes.size() < kMaxListItems ? nodes.size() : kMaxListItems;
    for (int i = 0; i < shown; i++) {
        out += indent + "  " + nodes[i].name + ": " +
               resource_label(data, nodes[i].id) + "\n";
    }
    if (nodes.size() > shown) {
        out += indent + "  ... " +
               godot::String::num_int64(nodes.size() - shown) + " more\n";
    }

    out += format_connections(props, indent_depth);

    if (recursion_depth >= kMaxRecursionDepth) return out;

    int nested = 0;
    for (int i = 0; i < (int)nodes.size(); i++) {
        if (!data.sub_resources.has(nodes[i].id)) continue;
        godot::String type = data.sub_resources[nodes[i].id].type;
        bool useful_nested = type == "AnimationNodeStateMachine" ||
                             type == "AnimationNodeBlendTree" ||
                             type == "AnimationNodeBlendSpace1D" ||
                             type == "AnimationNodeBlendSpace2D";
        if (!useful_nested) continue;
        if (nested == 0) out += indent + "nested:\n";
        nested++;
        out += indent + "  " + nodes[i].name + " = <" + type + ">\n";
        out += format_resource_summary(data, nodes[i].id, indent_depth + 2,
                                       recursion_depth + 1, seen);
        out += indent + "  </" + type + ">\n";
        if (nested >= 8 && nodes.size() > nested) {
            out += indent + "  ... more nested graph nodes omitted\n";
            break;
        }
    }

    return out;
}

godot::String format_resource_summary(const TscnData &data,
                                      const godot::String &id,
                                      int indent_depth,
                                      int recursion_depth,
                                      godot::Vector<godot::String> seen) {
    godot::String indent = indent_str(indent_depth);
    if (!data.sub_resources.has(id)) {
        return indent + "missing_subresource = " + id + "\n";
    }
    if (seen_id(seen, id)) {
        return indent + "cycle_ref = " + id + "\n";
    }

    seen.push_back(id);
    const SubResourceBlock &block = data.sub_resources[id];

    if (block.type == "AnimationNodeBlendTree") {
        return format_blend_tree(data, block, indent_depth, recursion_depth, seen);
    }
    if (block.type == "AnimationNodeStateMachine") {
        return format_state_machine(data, block, indent_depth, recursion_depth, seen);
    }
    if (block.type == "AnimationNodeBlendSpace1D" ||
        block.type == "AnimationNodeBlendSpace2D") {
        return format_blend_space(data, block, indent_depth);
    }

    godot::String simple = format_simple_animation_node(block, indent_depth);
    if (!simple.is_empty()) return simple;
    return indent + "type = " + block.type + "\n";
}

} // namespace

godot::String format_animation_tree_graph(const TscnData &data,
                                          const godot::String &root_id,
                                          int indent_depth) {
    godot::String indent = indent_str(indent_depth);
    godot::String out;

    if (!data.sub_resources.has(root_id)) {
        return indent + "missing_root = " + root_id + "\n";
    }

    const SubResourceBlock &root = data.sub_resources[root_id];
    out += indent + "<!-- read from AnimationTree.tree_root AnimationNode* subresources -->\n";
    out += indent + "root: " + root.type + "\n";

    godot::Vector<godot::String> seen;
    out += format_resource_summary(data, root_id, indent_depth, 0, seen);
    out += indent +
           "detail_hint: Exact AnimationTree graph fields are serialized under AnimationNode* subresources; use run_scene_edit_script to load AnimationTree.tree_root and inspect listed graph node/state names when exact values are needed.\n";

    return out;
}

godot::String format_animation_tree_parameters(
    const godot::Vector<godot::String> &node_lines, int indent_depth) {
    godot::Dictionary groups;

    for (int i = 0; i < (int)node_lines.size(); i++) {
        godot::String key;
        godot::String value;
        if (!split_property(node_lines[i], key, value)) continue;
        if (!key.begins_with("parameters/")) continue;

        godot::String rest = key.substr(11);
        int slash = rest.find("/");
        godot::String group = slash == -1 ? "root" : rest.left(slash);
        godot::String item = slash == -1 ? rest : rest.substr(slash + 1);

        godot::Array entries = groups.get(group, godot::Array());
        entries.push_back(item + godot::String(" = ") + value);
        groups[group] = entries;
    }

    if (groups.is_empty()) return godot::String();

    godot::String indent = indent_str(indent_depth);
    godot::String out = indent + "parameters:\n";
    godot::Array group_names = groups.keys();
    group_names.sort();

    for (int i = 0; i < group_names.size(); i++) {
        godot::String group = group_names[i];
        godot::Array entries = groups[group];
        out += indent + "  " + group + ":\n";

        int shown = entries.size() < kMaxListItems ? entries.size() : kMaxListItems;
        for (int j = 0; j < shown; j++) {
            out += indent + "    " + godot::String(entries[j]) + "\n";
        }
        if (entries.size() > shown) {
            out += indent + "    ... " +
                   godot::String::num_int64(entries.size() - shown) + " more\n";
        }
    }

    return out;
}

} // namespace fennara::get_node_properties
