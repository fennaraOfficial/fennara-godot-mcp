#pragma once

#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <utility>
#include <vector>

namespace fennara::get_class_info {

struct DocArgument {
    int index = 0;
    godot::String name;
    godot::String type;
    godot::String default_value;
};

struct DocLink {
    godot::String title;
    godot::String url;
};

struct DocProperty {
    godot::String name;
    godot::String type;
    godot::String default_value;
    godot::String setter;
    godot::String getter;
    godot::String enum_name;
    godot::String overrides;
    godot::String declared_in;
    godot::String description;
    std::vector<std::pair<godot::String, godot::String>> extra_attributes;
};

struct DocMethod {
    godot::String name;
    godot::String qualifiers;
    godot::String return_type;
    std::vector<DocArgument> args;
    godot::String description;
};

struct DocSignal {
    godot::String name;
    std::vector<DocArgument> args;
    godot::String description;
};

struct DocConstant {
    godot::String name;
    godot::String value;
    godot::String description;
    godot::String enum_name;
    bool is_bitfield = false;
};

struct DocThemeItem {
    godot::String name;
    godot::String type;
    godot::String default_value;
    godot::String description;
};

struct ClassDocumentation {
    bool found = false;
    godot::String class_name;
    godot::String branch;
    godot::String module_notice;
    godot::String fetch_message;
    godot::String inherits;
    godot::String brief_description;
    godot::String full_description;
    std::vector<DocLink> tutorials;
    std::vector<DocProperty> properties;
    std::vector<DocMethod> methods;
    std::vector<DocSignal> signals;
    std::vector<DocConstant> constants;
    std::vector<DocThemeItem> theme_items;
    std::vector<DocMethod> operators;
    std::vector<DocMethod> constructors;
};

ClassDocumentation fetch_and_parse_class_documentation(
    const godot::String &class_name,
    const godot::String &branch);
void warm_class_documentation_cache(const godot::PackedStringArray &class_names,
                                    const godot::String &branch);

} // namespace fennara::get_class_info
