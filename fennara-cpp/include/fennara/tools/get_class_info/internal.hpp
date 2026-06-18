#pragma once

#include "fennara/tools/get_class_info/docs.hpp"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara::get_class_info {

godot::Array collect_runtime_properties(const godot::String &class_name);
godot::PackedStringArray collect_inherits_chain(const godot::String &class_name);
godot::PackedStringArray collect_inherited_by(const godot::String &class_name);
ClassDocumentation collect_docs_for_class_info(const godot::String &class_name,
                                               const godot::String &branch,
                                               const godot::PackedStringArray &inherits_chain);
godot::String render_docs_text(const ClassDocumentation &docs);
godot::String render_runtime_hierarchy_text(const godot::PackedStringArray &inherits_chain,
                                            const godot::PackedStringArray &inherited_by);
godot::String render_runtime_properties_text(const godot::Array &properties,
                                             const ClassDocumentation &docs);

} // namespace fennara::get_class_info
