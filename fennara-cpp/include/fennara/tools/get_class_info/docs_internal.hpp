#pragma once

#include "fennara/tools/get_class_info/docs.hpp"

namespace fennara::get_class_info {

struct HttpFetchResult {
    bool ok = false;
    bool not_found = false;
    int response_code = 0;
    godot::String body;
    godot::String error;
};

struct CachedXmlLookup {
    bool found = false;
    bool fresh = false;
    godot::String xml_text;
};

ClassDocumentation parse_class_documentation_xml(const godot::String &class_name,
                                                 const godot::String &branch,
                                                 const godot::String &xml_text);

} // namespace fennara::get_class_info
