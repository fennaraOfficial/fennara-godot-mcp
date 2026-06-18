#include "fennara/tools/get_class_info/internal.hpp"

#include <vector>

namespace fennara::get_class_info {

namespace {

const char *kResourceBaseProperties[] = {
    "resource_local_to_scene",
    "resource_path",
    "resource_name",
};
godot::String format_doc_type(const godot::String &type_name) {
    return type_name.is_empty() ? "void" : type_name;
}

godot::String format_doc_args(const std::vector<DocArgument> &args) {
    godot::String out;
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) {
            out += ", ";
        }
        out += args[i].name + godot::String(": ") +
               format_doc_type(args[i].type);
        if (!args[i].default_value.is_empty()) {
            out += " = " + args[i].default_value;
        }
    }
    return out;
}

godot::String format_callable_signature(const DocMethod &method,
                                        const godot::String &fallback_name = "") {
    godot::String name = method.name.is_empty() ? fallback_name : method.name;
    godot::String line =
        format_doc_type(method.return_type) + " " + name + "(" +
        format_doc_args(method.args) + ")";
    if (!method.qualifiers.is_empty()) {
        line += "  [" + method.qualifiers + "]";
    }
    return line;
}

godot::String format_variant_like_python(const godot::Variant &value) {
    switch (value.get_type()) {
    case godot::Variant::NIL:
        return "None";
    case godot::Variant::BOOL:
        return (bool)value ? "True" : "False";
    case godot::Variant::INT:
    case godot::Variant::FLOAT:
        return godot::String(value);
    case godot::Variant::STRING:
        return godot::String(value);
    case godot::Variant::STRING_NAME:
        return godot::String(godot::StringName(value));
    case godot::Variant::NODE_PATH:
        return godot::String(godot::NodePath(value));
    case godot::Variant::ARRAY: {
        godot::Array array = value;
        godot::String out = "[";
        for (int i = 0; i < array.size(); i++) {
            if (i > 0) {
                out += ", ";
            }
            out += format_variant_like_python(array[i]);
        }
        out += "]";
        return out;
    }
    case godot::Variant::DICTIONARY: {
        godot::Dictionary dict = value;
        godot::Array keys = dict.keys();
        godot::String out = "{";
        for (int i = 0; i < keys.size(); i++) {
            if (i > 0) {
                out += ", ";
            }
            godot::String key = godot::String(keys[i]);
            out += "'" + key.replace("'", "\\'") + "': " +
                   format_variant_like_python(dict[keys[i]]);
        }
        out += "}";
        return out;
    }
    default:
        return godot::String(value);
    }
}

void append_section(godot::String &out,
                    const godot::String &title,
                    const std::vector<godot::String> &lines) {
    if (lines.empty()) {
        return;
    }
    out += "\n# " + title + " (" +
           godot::String::num_int64(static_cast<int64_t>(lines.size())) + ")\n";
    for (const godot::String &line : lines) {
        out += line + godot::String("\n");
    }
}

const DocProperty *find_doc_property(const ClassDocumentation &docs,
                                     const godot::String &name) {
    for (const DocProperty &prop : docs.properties) {
        if (prop.name == name) {
            return &prop;
        }
    }
    return nullptr;
}

bool is_allowed_resource_base_property(const godot::String &name) {
    for (const char *allowed : kResourceBaseProperties) {
        if (name == allowed) {
            return true;
        }
    }
    return false;
}

} // namespace

ClassDocumentation collect_docs_for_class_info(const godot::String &class_name,
                                               const godot::String &branch,
                                               const godot::PackedStringArray &inherits_chain) {
    ClassDocumentation base_docs =
        get_class_info::fetch_and_parse_class_documentation(class_name, branch);
    if (!base_docs.found) {
        return base_docs;
    }

    int resource_idx = -1;
    for (int i = 0; i < inherits_chain.size(); i++) {
        if (inherits_chain[i] == "Resource") {
            resource_idx = i;
            break;
        }
    }
    if (resource_idx == -1) {
        return base_docs;
    }

    std::vector<DocProperty> merged_props;
    for (int i = resource_idx; i >= 0; i--) {
        godot::String owner = inherits_chain[i];
        ClassDocumentation owner_docs =
            (owner == class_name) ? base_docs
                                  : get_class_info::fetch_and_parse_class_documentation(owner, branch);
        if (!owner_docs.found) {
            continue;
        }

        for (const DocProperty &source_prop : owner_docs.properties) {
            if (owner == "Resource" &&
                !is_allowed_resource_base_property(source_prop.name)) {
                continue;
            }

            DocProperty prop = source_prop;
            if (prop.declared_in.is_empty()) {
                prop.declared_in = owner;
            }

            bool replaced = false;
            for (DocProperty &existing : merged_props) {
                if (existing.name == prop.name) {
                    existing = prop;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                merged_props.push_back(prop);
            }
        }
    }

    base_docs.properties = merged_props;
    return base_docs;
}
godot::String render_docs_text(const ClassDocumentation &docs) {
    godot::String out;
    if (!docs.found) {
        if (!docs.fetch_message.is_empty()) {
            out += docs.fetch_message + godot::String("\n");
        }
        return out;
    }

    out += "\n# GITHUB DOCS: " + docs.class_name;
    if (!docs.inherits.is_empty()) {
        out += " (inherits: " + docs.inherits + ")";
    }
    out += "\n";

    if (!docs.brief_description.is_empty()) {
        out += "\n" + docs.brief_description + "\n";
    }
    if (!docs.full_description.is_empty() &&
        docs.full_description != docs.brief_description) {
        out += "\n" + docs.full_description + "\n";
    }

    std::vector<godot::String> lines;

    lines.clear();
    for (const auto &tutorial : docs.tutorials) {
        if (!tutorial.title.is_empty() && !tutorial.url.is_empty()) {
            lines.push_back("  " + tutorial.title + ": " + tutorial.url);
        } else if (!tutorial.url.is_empty()) {
            lines.push_back("  " + tutorial.url);
        }
    }
    append_section(out, "TUTORIALS", lines);

    lines.clear();
    for (const DocMethod &ctor : docs.constructors) {
        godot::String line = "  " + format_callable_signature(ctor, docs.class_name);
        if (!ctor.description.is_empty()) {
            line += "\n      " + ctor.description;
        }
        lines.push_back(line);
    }
    append_section(out, "CONSTRUCTORS", lines);

    lines.clear();
    for (const DocMethod &method : docs.methods) {
        godot::String line = "  " + format_callable_signature(method);
        if (!method.description.is_empty()) {
            line += "\n      " + method.description;
        }
        lines.push_back(line);
    }
    append_section(out, "METHODS", lines);

    lines.clear();
    for (const DocSignal &signal : docs.signals) {
        godot::String line =
            "  " + signal.name + "(" + format_doc_args(signal.args) + ")";
        if (!signal.description.is_empty()) {
            line += "\n      " + signal.description;
        }
        lines.push_back(line);
    }
    append_section(out, "SIGNALS", lines);

    std::vector<godot::String> standalone_constants;
    std::vector<godot::String> enum_order;
    std::vector<std::vector<godot::String>> enum_lines;
    for (const DocConstant &constant : docs.constants) {
        godot::String line = "  " + constant.name + " = " + constant.value;
        if (!constant.description.is_empty()) {
            line += "  " + constant.description;
        }
        if (constant.enum_name.is_empty()) {
            standalone_constants.push_back(line);
            continue;
        }

        int enum_index = -1;
        for (size_t i = 0; i < enum_order.size(); i++) {
            if (enum_order[i] == constant.enum_name) {
                enum_index = static_cast<int>(i);
                break;
            }
        }
        if (enum_index == -1) {
            enum_order.push_back(constant.enum_name);
            enum_lines.push_back({});
            enum_index = static_cast<int>(enum_order.size()) - 1;
        }
        enum_lines[enum_index].push_back(line);
    }
    append_section(out, "CONSTANTS", standalone_constants);
    for (size_t i = 0; i < enum_order.size(); i++) {
        godot::String title = "ENUM: " + enum_order[i];
        append_section(out, title, enum_lines[i]);
    }

    return out;
}

godot::String render_runtime_hierarchy_text(const godot::PackedStringArray &inherits_chain,
                                            const godot::PackedStringArray &inherited_by) {
    godot::String out;

    if (!inherits_chain.is_empty()) {
        out += "\n# RUNTIME INHERITS CHAIN (" +
               godot::String::num_int64(inherits_chain.size()) + ")\n  ";
        for (int i = 0; i < inherits_chain.size(); i++) {
            if (i > 0) {
                out += " -> ";
            }
            out += inherits_chain[i];
        }
        out += "\n";
    }

    out += "\n# RUNTIME INHERITED BY (" +
           godot::String::num_int64(inherited_by.size()) + ")\n";
    for (int i = 0; i < inherited_by.size(); i++) {
        out += "  - " + inherited_by[i] + "\n";
    }

    return out;
}

godot::String render_runtime_properties_text(const godot::Array &properties,
                                             const ClassDocumentation &docs) {
    int shared_count = 0;
    for (int i = 0; i < properties.size(); i++) {
        godot::Dictionary prop = properties[i];
        if (find_doc_property(docs, prop.get("name", "")) != nullptr) {
            shared_count++;
        }
    }

    godot::String out = "\n# INTERSECTION PROPERTIES: " + docs.class_name +
                        " (" + godot::String::num_int64(shared_count) + ")\n";
    for (int i = 0; i < properties.size(); i++) {
        godot::Dictionary prop = properties[i];
        godot::String name = prop.get("name", "");
        const DocProperty *doc_prop = find_doc_property(docs, name);
        if (doc_prop == nullptr) {
            continue;
        }

        godot::String line = "- " + name + ": " + godot::String(prop.get("type", ""));

        std::vector<godot::String> hints;
        if (prop.has("hint_string")) {
            hints.push_back("hint_string: " + godot::String(prop["hint_string"]));
        }
        if (!doc_prop->setter.is_empty()) {
            hints.push_back("set: " + doc_prop->setter);
        }
        if (!doc_prop->getter.is_empty()) {
            hints.push_back("get: " + doc_prop->getter);
        }
        if (!doc_prop->default_value.is_empty()) {
            hints.push_back("default: " + doc_prop->default_value);
        }
        if (!doc_prop->enum_name.is_empty()) {
            hints.push_back("enum: " + doc_prop->enum_name);
        }
        if (!doc_prop->overrides.is_empty()) {
            hints.push_back("overrides: " + doc_prop->overrides);
        }
        if (!doc_prop->declared_in.is_empty()) {
            hints.push_back("declared_in: " + doc_prop->declared_in);
        }
        for (const auto &entry : doc_prop->extra_attributes) {
            if (!entry.first.is_empty() && !entry.second.is_empty()) {
                hints.push_back(entry.first + godot::String(": ") + entry.second);
            }
        }
        if (!hints.empty()) {
            line += "\n  [";
            for (size_t j = 0; j < hints.size(); j++) {
                if (j > 0) {
                    line += ", ";
                }
                line += hints[j];
            }
            line += "]";
        }
        if (!doc_prop->description.is_empty()) {
            line += "\n  " + doc_prop->description;
        }

        out += line + "\n";
    }

    return out;
}


} // namespace fennara::get_class_info
