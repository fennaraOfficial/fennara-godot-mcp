#include "fennara/tools/get_class_info/docs.hpp"
#include "fennara/tools/get_class_info/docs_internal.hpp"

#include <godot_cpp/classes/xml_parser.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <algorithm>

namespace fennara::get_class_info {

namespace {
godot::String _collapse_whitespace(godot::String text) {
    text = text.replace("\r", " ").replace("\n", " ").replace("\t", " ");
    while (text.contains("  ")) {
        text = text.replace("  ", " ");
    }
    return text.strip_edges();
}

godot::String _read_simple_text(godot::Ref<godot::XMLParser> parser,
                                const godot::String &element_name) {
    if (parser->is_empty()) {
        return "";
    }

    godot::String text;
    int depth = 1;
    while (parser->read() == godot::OK) {
        godot::XMLParser::NodeType node_type = parser->get_node_type();
        if (node_type == godot::XMLParser::NODE_ELEMENT && !parser->is_empty()) {
            depth++;
            continue;
        }
        if (node_type == godot::XMLParser::NODE_ELEMENT_END) {
            depth--;
            if (depth == 0 && parser->get_node_name() == element_name) {
                break;
            }
            continue;
        }
        if (node_type == godot::XMLParser::NODE_TEXT ||
            node_type == godot::XMLParser::NODE_CDATA) {
            text += parser->get_node_data() + " ";
        }
    }

    return _collapse_whitespace(text);
}
DocProperty _parse_member(godot::Ref<godot::XMLParser> parser) {
    DocProperty property;
    property.name = parser->get_named_attribute_value_safe("name");
    property.type = parser->get_named_attribute_value_safe("type");
    property.default_value = parser->get_named_attribute_value_safe("default");
    property.setter = parser->get_named_attribute_value_safe("setter");
    property.getter = parser->get_named_attribute_value_safe("getter");
    property.enum_name = parser->get_named_attribute_value_safe("enum");
    property.overrides = parser->get_named_attribute_value_safe("overrides");
    const int attribute_count = parser->get_attribute_count();
    for (int i = 0; i < attribute_count; i++) {
        const godot::String attribute_name = parser->get_attribute_name(i);
        if (attribute_name == "name" || attribute_name == "type" ||
            attribute_name == "default" || attribute_name == "setter" ||
            attribute_name == "getter" || attribute_name == "enum" ||
            attribute_name == "overrides") {
            continue;
        }

        property.extra_attributes.push_back({
            attribute_name,
            parser->get_named_attribute_value_safe(attribute_name),
        });
    }
    property.description = _read_simple_text(parser, "member");
    return property;
}

DocArgument _parse_param(godot::Ref<godot::XMLParser> parser) {
    DocArgument arg;
    arg.index = parser->get_named_attribute_value_safe("index").to_int();
    arg.name = parser->get_named_attribute_value_safe("name");
    arg.type = parser->get_named_attribute_value_safe("type");
    arg.default_value = parser->get_named_attribute_value_safe("default");
    return arg;
}

DocMethod _parse_callable(godot::Ref<godot::XMLParser> parser,
                          const godot::String &end_name) {
    DocMethod method;
    method.name = parser->get_named_attribute_value_safe("name");
    method.qualifiers = parser->get_named_attribute_value_safe("qualifiers");
    method.return_type = parser->get_named_attribute_value_safe("type");

    if (parser->is_empty()) {
        return method;
    }

    while (parser->read() == godot::OK) {
        godot::XMLParser::NodeType node_type = parser->get_node_type();
        if (node_type != godot::XMLParser::NODE_ELEMENT &&
            node_type != godot::XMLParser::NODE_ELEMENT_END) {
            continue;
        }
        godot::String node_name = parser->get_node_name();

        if (node_type == godot::XMLParser::NODE_ELEMENT) {
            if (node_name == "return") {
                godot::String return_type =
                    parser->get_named_attribute_value_safe("type");
                if (!return_type.is_empty()) {
                    method.return_type = return_type;
                }
                if (!parser->is_empty()) {
                    parser->skip_section();
                }
            } else if (node_name == "param") {
                method.args.push_back(_parse_param(parser));
                if (!parser->is_empty()) {
                    parser->skip_section();
                }
            } else if (node_name == "description") {
                method.description = _read_simple_text(parser, "description");
            } else if (!parser->is_empty()) {
                parser->skip_section();
            }
            continue;
        }

        if (node_type == godot::XMLParser::NODE_ELEMENT_END &&
            node_name == end_name) {
            break;
        }
    }

    std::sort(method.args.begin(), method.args.end(),
              [](const DocArgument &a, const DocArgument &b) {
                  return a.index < b.index;
              });
    return method;
}

DocSignal _parse_signal(godot::Ref<godot::XMLParser> parser) {
    DocSignal signal;
    signal.name = parser->get_named_attribute_value_safe("name");
    if (parser->is_empty()) {
        return signal;
    }

    while (parser->read() == godot::OK) {
        godot::XMLParser::NodeType node_type = parser->get_node_type();
        if (node_type != godot::XMLParser::NODE_ELEMENT &&
            node_type != godot::XMLParser::NODE_ELEMENT_END) {
            continue;
        }
        godot::String node_name = parser->get_node_name();

        if (node_type == godot::XMLParser::NODE_ELEMENT) {
            if (node_name == "param") {
                signal.args.push_back(_parse_param(parser));
                if (!parser->is_empty()) {
                    parser->skip_section();
                }
            } else if (node_name == "description") {
                signal.description = _read_simple_text(parser, "description");
            } else if (!parser->is_empty()) {
                parser->skip_section();
            }
            continue;
        }

        if (node_type == godot::XMLParser::NODE_ELEMENT_END &&
            node_name == "signal") {
            break;
        }
    }

    std::sort(signal.args.begin(), signal.args.end(),
              [](const DocArgument &a, const DocArgument &b) {
                  return a.index < b.index;
              });
    return signal;
}

DocConstant _parse_constant(godot::Ref<godot::XMLParser> parser) {
    DocConstant constant;
    constant.name = parser->get_named_attribute_value_safe("name");
    constant.value = parser->get_named_attribute_value_safe("value");
    constant.enum_name = parser->get_named_attribute_value_safe("enum");
    constant.is_bitfield =
        parser->get_named_attribute_value_safe("is_bitfield") == "true";
    constant.description = _read_simple_text(parser, "constant");
    return constant;
}

DocThemeItem _parse_theme_item(godot::Ref<godot::XMLParser> parser) {
    DocThemeItem item;
    item.name = parser->get_named_attribute_value_safe("name");
    item.type = parser->get_named_attribute_value_safe("data_type");
    if (item.type.is_empty()) {
        item.type = parser->get_named_attribute_value_safe("type");
    }
    item.default_value = parser->get_named_attribute_value_safe("default");
    item.description = _read_simple_text(parser, "theme_item");
    return item;
}

void _parse_links_section(godot::Ref<godot::XMLParser> parser,
                          std::vector<DocLink> &links,
                          const godot::String &section_name) {
    while (parser->read() == godot::OK) {
        godot::XMLParser::NodeType node_type = parser->get_node_type();
        if (node_type != godot::XMLParser::NODE_ELEMENT &&
            node_type != godot::XMLParser::NODE_ELEMENT_END) {
            continue;
        }
        godot::String node_name = parser->get_node_name();

        if (node_type == godot::XMLParser::NODE_ELEMENT && node_name == "link") {
            DocLink link;
            link.title = parser->get_named_attribute_value_safe("title");
            link.url = _read_simple_text(parser, "link");
            links.push_back(link);
            continue;
        }

        if (node_type == godot::XMLParser::NODE_ELEMENT_END &&
            node_name == section_name) {
            break;
        }
    }
}

void _parse_properties_section(godot::Ref<godot::XMLParser> parser,
                               std::vector<DocProperty> &properties) {
    while (parser->read() == godot::OK) {
        godot::XMLParser::NodeType node_type = parser->get_node_type();
        if (node_type != godot::XMLParser::NODE_ELEMENT &&
            node_type != godot::XMLParser::NODE_ELEMENT_END) {
            continue;
        }
        godot::String node_name = parser->get_node_name();

        if (node_type == godot::XMLParser::NODE_ELEMENT && node_name == "member") {
            properties.push_back(_parse_member(parser));
            continue;
        }

        if (node_type == godot::XMLParser::NODE_ELEMENT_END &&
            node_name == "members") {
            break;
        }
    }
}

void _parse_methods_section(godot::Ref<godot::XMLParser> parser,
                            std::vector<DocMethod> &methods,
                            const godot::String &section_name,
                            const godot::String &entry_name) {
    while (parser->read() == godot::OK) {
        godot::XMLParser::NodeType node_type = parser->get_node_type();
        if (node_type != godot::XMLParser::NODE_ELEMENT &&
            node_type != godot::XMLParser::NODE_ELEMENT_END) {
            continue;
        }
        godot::String node_name = parser->get_node_name();

        if (node_type == godot::XMLParser::NODE_ELEMENT && node_name == entry_name) {
            methods.push_back(_parse_callable(parser, entry_name));
            continue;
        }

        if (node_type == godot::XMLParser::NODE_ELEMENT_END &&
            node_name == section_name) {
            break;
        }
    }
}

void _parse_signals_section(godot::Ref<godot::XMLParser> parser,
                            std::vector<DocSignal> &signals) {
    while (parser->read() == godot::OK) {
        godot::XMLParser::NodeType node_type = parser->get_node_type();
        if (node_type != godot::XMLParser::NODE_ELEMENT &&
            node_type != godot::XMLParser::NODE_ELEMENT_END) {
            continue;
        }
        godot::String node_name = parser->get_node_name();

        if (node_type == godot::XMLParser::NODE_ELEMENT && node_name == "signal") {
            signals.push_back(_parse_signal(parser));
            continue;
        }

        if (node_type == godot::XMLParser::NODE_ELEMENT_END &&
            node_name == "signals") {
            break;
        }
    }
}

void _parse_constants_section(godot::Ref<godot::XMLParser> parser,
                              std::vector<DocConstant> &constants) {
    while (parser->read() == godot::OK) {
        godot::XMLParser::NodeType node_type = parser->get_node_type();
        if (node_type != godot::XMLParser::NODE_ELEMENT &&
            node_type != godot::XMLParser::NODE_ELEMENT_END) {
            continue;
        }
        godot::String node_name = parser->get_node_name();

        if (node_type == godot::XMLParser::NODE_ELEMENT && node_name == "constant") {
            constants.push_back(_parse_constant(parser));
            continue;
        }

        if (node_type == godot::XMLParser::NODE_ELEMENT_END &&
            node_name == "constants") {
            break;
        }
    }
}

void _parse_theme_items_section(godot::Ref<godot::XMLParser> parser,
                                std::vector<DocThemeItem> &theme_items) {
    while (parser->read() == godot::OK) {
        godot::XMLParser::NodeType node_type = parser->get_node_type();
        if (node_type != godot::XMLParser::NODE_ELEMENT &&
            node_type != godot::XMLParser::NODE_ELEMENT_END) {
            continue;
        }
        godot::String node_name = parser->get_node_name();

        if (node_type == godot::XMLParser::NODE_ELEMENT &&
            node_name == "theme_item") {
            theme_items.push_back(_parse_theme_item(parser));
            continue;
        }

        if (node_type == godot::XMLParser::NODE_ELEMENT_END &&
            node_name == "theme_items") {
            break;
        }
    }
}

} // namespace
ClassDocumentation parse_class_documentation_xml(const godot::String &class_name,
                                                  const godot::String &branch,
                                                  const godot::String &xml_text) {
    ClassDocumentation docs;
    docs.found = true;
    docs.class_name = class_name;
    docs.branch = branch;

    godot::Ref<godot::XMLParser> parser;
    parser.instantiate();
    if (parser.is_null()) {
        docs.found = false;
        docs.fetch_message = "Failed to create XMLParser.";
        return docs;
    }

    godot::PackedByteArray xml_bytes = xml_text.to_utf8_buffer();
    if (parser->open_buffer(xml_bytes) != godot::OK) {
        docs.found = false;
        docs.fetch_message = "Failed to parse Godot XML docs.";
        return docs;
    }

    while (parser->read() == godot::OK) {
        if (parser->get_node_type() != godot::XMLParser::NODE_ELEMENT) {
            continue;
        }

        const godot::String node_name = parser->get_node_name();
        if (node_name == "class") {
            docs.inherits = parser->get_named_attribute_value_safe("inherits");
            continue;
        }
        if (node_name == "brief_description") {
            docs.brief_description =
                _read_simple_text(parser, "brief_description");
            continue;
        }
        if (node_name == "description") {
            docs.full_description = _read_simple_text(parser, "description");
            continue;
        }
        if (node_name == "tutorials") {
            _parse_links_section(parser, docs.tutorials, "tutorials");
            continue;
        }
        if (node_name == "members") {
            _parse_properties_section(parser, docs.properties);
            continue;
        }
        if (node_name == "methods") {
            _parse_methods_section(parser, docs.methods, "methods", "method");
            continue;
        }
        if (node_name == "signals") {
            _parse_signals_section(parser, docs.signals);
            continue;
        }
        if (node_name == "constants") {
            _parse_constants_section(parser, docs.constants);
            continue;
        }
        if (node_name == "theme_items") {
            _parse_theme_items_section(parser, docs.theme_items);
            continue;
        }
        if (node_name == "operators") {
            _parse_methods_section(parser, docs.operators, "operators", "operator");
            continue;
        }
        if (node_name == "constructors") {
            _parse_methods_section(parser, docs.constructors, "constructors",
                                   "constructor");
            continue;
        }
        if (!parser->is_empty()) {
            parser->skip_section();
        }
    }

    return docs;
}

} // namespace fennara::get_class_info
