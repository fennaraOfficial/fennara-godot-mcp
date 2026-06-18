#include "fennara/tools/get_node_properties/theme_resources.hpp"

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace fennara::get_node_properties {

namespace {

constexpr int kSmallThemeMaxBytes = 8192;
constexpr int kSmallThemeMaxLines = 200;

godot::String indent_str(int depth) {
    godot::String s;
    for (int i = 0; i < depth; i++) s += "  ";
    return s;
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

godot::HashMap<godot::String, godot::String> property_map(
    const godot::Vector<godot::String> &lines) {
    godot::HashMap<godot::String, godot::String> props;
    for (int i = 0; i < (int)lines.size(); i++) {
        int eq = lines[i].find(" = ");
        if (eq == -1) continue;
        props[lines[i].left(eq)] = lines[i].substr(eq + 3);
    }
    return props;
}

int file_size_bytes(const godot::String &path) {
    godot::PackedByteArray bytes = godot::FileAccess::get_file_as_bytes(path);
    return bytes.size();
}

int line_count(const godot::String &path) {
    godot::String text = godot::FileAccess::get_file_as_string(path);
    if (text.is_empty()) return 0;
    return text.split("\n").size();
}

void add_count(godot::Dictionary &dict, const godot::String &key,
               int amount = 1) {
    dict[key] = int(dict.get(key, 0)) + amount;
}

void add_nested_count(godot::Dictionary &dict, const godot::String &group,
                      const godot::String &key) {
    godot::Dictionary counts = dict.get(group, godot::Dictionary());
    add_count(counts, key);
    dict[group] = counts;
}

void add_nested_item(godot::Dictionary &dict, const godot::String &group,
                     const godot::String &key, const godot::String &item) {
    godot::Dictionary groups = dict.get(group, godot::Dictionary());
    godot::String items = groups.get(key, godot::String());
    if (items.is_empty()) {
        items = item;
    } else {
        items += ", " + item;
    }
    groups[key] = items;
    dict[group] = groups;
}

godot::String counts_inline(const godot::Dictionary &dict) {
    godot::Array keys = dict.keys();
    keys.sort();
    godot::String out;
    for (int i = 0; i < keys.size(); i++) {
        if (i > 0) out += ", ";
        godot::String key = keys[i];
        out += key + ":" + godot::String::num_int64(int(dict[key]));
    }
    return out.is_empty() ? "none" : out;
}

godot::String category_from_theme_key(const godot::String &key) {
    int first = key.find("/");
    if (first == -1) return "properties";
    godot::String rest = key.substr(first + 1);
    int second = rest.find("/");
    return second == -1 ? rest : rest.left(second);
}

godot::String type_from_theme_key(const godot::String &key) {
    int slash = key.find("/");
    return slash == -1 ? key : key.left(slash);
}

godot::String item_from_theme_key(const godot::String &key) {
    int first = key.find("/");
    if (first == -1) return key;
    godot::String rest = key.substr(first + 1);
    int second = rest.find("/");
    return second == -1 ? rest : rest.substr(second + 1);
}

godot::String ref_type_label(const godot::String &value,
                             const TextResourceData &resource_data) {
    godot::String ext_id = extract_ref_id(value, "ExtResource");
    if (!ext_id.is_empty() && resource_data.ext_resources.has(ext_id)) {
        return resource_data.ext_resources[ext_id].type;
    }

    godot::String sub_id = extract_ref_id(value, "SubResource");
    if (!sub_id.is_empty() && resource_data.sub_resources.has(sub_id)) {
        return resource_data.sub_resources[sub_id].type;
    }

    if (value.strip_edges() == "null") return "null";
    return godot::String();
}

struct ThemeSummary {
    int theme_items = 0;
    godot::Dictionary totals;
    godot::Dictionary by_type;
    godot::Dictionary available_items;
    godot::Dictionary resource_refs;
    godot::Dictionary relevant_values;
};

ThemeSummary summarize_theme(const TextResourceData &resource_data,
                             const godot::String &relevant_type) {
    ThemeSummary summary;

    for (int i = 0; i < (int)resource_data.root_lines.size(); i++) {
        const godot::String &line = resource_data.root_lines[i];
        int eq = line.find(" = ");
        if (eq == -1) continue;

        godot::String key = line.left(eq);
        godot::String value = line.substr(eq + 3);
        godot::String type = type_from_theme_key(key);
        godot::String category = category_from_theme_key(key);
        godot::String item = item_from_theme_key(key);

        summary.theme_items++;
        add_count(summary.totals, category);
        add_nested_count(summary.by_type, type, category);
        add_nested_item(summary.available_items, type, category, item);

        godot::String ref_type = ref_type_label(value, resource_data);
        if (!ref_type.is_empty()) add_count(summary.resource_refs, ref_type);

        if (!relevant_type.is_empty() && type == relevant_type) {
            summary.relevant_values[key] = value;
        }
    }

    return summary;
}

godot::String format_type_counts(const godot::Dictionary &by_type,
                                 const godot::String &indent) {
    godot::Array types = by_type.keys();
    types.sort();
    godot::String out;
    for (int i = 0; i < types.size(); i++) {
        godot::String type = types[i];
        godot::Dictionary counts = by_type[type];
        out += indent + type + ": " + counts_inline(counts) + "\n";
    }
    return out;
}

godot::String format_available_items(const godot::Dictionary &available,
                                     const godot::String &indent) {
    godot::Array types = available.keys();
    types.sort();
    godot::String out;
    for (int i = 0; i < types.size(); i++) {
        godot::String type = types[i];
        godot::Dictionary categories = available[type];
        godot::Array category_keys = categories.keys();
        category_keys.sort();
        for (int j = 0; j < category_keys.size(); j++) {
            godot::String category = category_keys[j];
            out += indent + type + "/" + category + ": " +
                   godot::String(categories[category]) + "\n";
        }
    }
    return out;
}

godot::String readable_theme_value(const godot::String &value,
                                   const TextResourceData &resource_data) {
    godot::String ext_id = extract_ref_id(value, "ExtResource");
    if (!ext_id.is_empty() && resource_data.ext_resources.has(ext_id)) {
        const ExtResourceEntry &entry = resource_data.ext_resources[ext_id];
        return entry.path + godot::String(" [") + entry.type + "]";
    }

    godot::String sub_id = extract_ref_id(value, "SubResource");
    if (!sub_id.is_empty() && resource_data.sub_resources.has(sub_id)) {
        return godot::String("<") +
               resource_data.sub_resources[sub_id].type + ">";
    }

    return value;
}

godot::String format_relevant_values(const godot::Dictionary &values,
                                     const TextResourceData &resource_data,
                                     const godot::String &indent) {
    godot::Array keys = values.keys();
    keys.sort();
    if (keys.is_empty()) return indent + godot::String("none\n");

    godot::String out;
    for (int i = 0; i < keys.size(); i++) {
        godot::String key = keys[i];
        out += indent + key + " = " +
               readable_theme_value(values[key], resource_data) + "\n";
    }
    return out;
}

godot::String node_relative_path(godot::Node *scene_root, godot::Node *node) {
    if (node == scene_root) return ".";
    return godot::String(scene_root->get_path_to(node));
}

godot::String node_parent_path(godot::Node *scene_root, godot::Node *node) {
    if (node == scene_root) return godot::String();
    godot::String rel = node_relative_path(scene_root, node);
    int last_slash = rel.rfind("/");
    return last_slash == -1 ? "." : rel.left(last_slash);
}

} // namespace

bool should_summarize_theme_resource(const ExtResourceEntry &entry) {
    return file_size_bytes(entry.path) > kSmallThemeMaxBytes ||
           line_count(entry.path) > kSmallThemeMaxLines;
}

godot::String format_theme_resource(const ExtResourceEntry &entry,
                                    const TextResourceData &resource_data,
                                    godot::Node *scene_root,
                                    godot::Node *target,
                                    int indent_depth) {
    godot::String indent = indent_str(indent_depth);
    godot::String out;

    int size = file_size_bytes(entry.path);
    int lines = line_count(entry.path);
    bool large = size > kSmallThemeMaxBytes || lines > kSmallThemeMaxLines;
    bool root_theme = target == scene_root;
    godot::String relevant_type =
        root_theme || target == nullptr ? godot::String()
                                        : godot::String(target->get_class());

    out += indent + "path = " + entry.path + "\n";
    out += indent + "file_size: " + godot::String::num_int64(size) +
           " bytes\n";
    out += indent + "lines: " + godot::String::num_int64(lines) + "\n";

    ThemeSummary summary = summarize_theme(resource_data, relevant_type);

    if (!root_theme) {
        out += indent + "<!-- explicit Theme assigned; ";
        out += large ? "large Theme omitted" : "Theme summarized";
        out += " -->\n";
        out += indent + "relevant_theme_items:\n";
        out += format_relevant_values(summary.relevant_values, resource_data,
                                      indent + "  ");
        out += indent +
               "note: local theme_override_* properties override matching Theme items below\n";
        return out;
    }

    out += indent + "<!-- explicit Theme assigned; ";
    out += large ? "large Theme summarized" : "Theme summarized";
    out += " -->\n";
    out += indent + "totals:\n";
    out += indent + "  ext_resources: " +
           godot::String::num_int64(resource_data.ext_resources.size()) + "\n";
    out += indent + "  sub_resources: " +
           godot::String::num_int64(resource_data.sub_resources.size()) + "\n";
    out += indent + "  theme_items: " +
           godot::String::num_int64(summary.theme_items) + "\n";
    out += indent + "  categories: " + counts_inline(summary.totals) + "\n";
    out += indent + "  resource_refs: " +
           counts_inline(summary.resource_refs) + "\n";
    out += indent + "control_types:\n";
    out += format_type_counts(summary.by_type, indent + "  ");
    out += indent + "available_items_index: omitted; control_types lists every type/category count\n";
    out += indent +
           "note: no Theme items are omitted from counts; use run_scene_edit_script to load the Theme resource for exact item names and values\n";
    out += indent +
           "note: this Theme is inherited by descendant Control nodes unless they set their own theme\n";
    return out;
}

godot::String format_inherited_theme_note(const TscnData &data,
                                          const godot::String &file_text,
                                          godot::Node *scene_root,
                                          godot::Node *target,
                                          const godot::String &relative_path,
                                          bool target_has_explicit_theme) {
    (void)relative_path;
    if (target_has_explicit_theme) return godot::String();
    if (godot::Object::cast_to<godot::Control>(target) == nullptr) {
        return godot::String();
    }

    godot::Node *node = target->get_parent();
    while (node != nullptr) {
        if (godot::Object::cast_to<godot::Control>(node) == nullptr) {
            node = node->get_parent();
            continue;
        }

        godot::Vector<godot::String> lines = find_node_block(
            file_text, godot::String(node->get_name()),
            node_parent_path(scene_root, node));
        godot::HashMap<godot::String, godot::String> props =
            property_map(lines);

        if (props.has("theme")) {
            godot::String ext_id = extract_ref_id(props["theme"], "ExtResource");
            if (!ext_id.is_empty() && data.ext_resources.has(ext_id)) {
                const ExtResourceEntry &entry = data.ext_resources[ext_id];
                if (entry.type == "Theme") {
                    godot::String from_path = node_relative_path(scene_root, node);
                    godot::String out = "theme_inherited:\n";
                    out += "  from_node: " + godot::String(node->get_name()) + "\n";
                    out += "  from_path: " + from_path + "\n";
                    out += "  resource: " + entry.path + " [Theme]\n";
                    out += "  file_size: " +
                           godot::String::num_int64(file_size_bytes(entry.path)) +
                           " bytes\n";
                    out += "  lines: " +
                           godot::String::num_int64(line_count(entry.path)) + "\n";
                    out += "  note: inherited Control theme; use run_scene_edit_script to load the Theme resource and inspect " +
                           godot::String(target->get_class()) +
                           "/ entries if visual styling matters\n";
                    return out;
                }
            }
        }

        if (node == scene_root) break;
        node = node->get_parent();
    }

    return godot::String();
}

} // namespace fennara::get_node_properties
