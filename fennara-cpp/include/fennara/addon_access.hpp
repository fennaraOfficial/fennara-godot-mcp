#pragma once

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara::addon_access {

godot::String normalize_res_path(const godot::String &path);
godot::String addon_root_for_path(const godot::String &path);
bool is_addons_root(const godot::String &path);
bool is_locked_addon_root(const godot::String &addon_root);

godot::Array allowed_addon_roots();
void set_allowed_addon_roots(const godot::Array &roots);
bool is_addon_root_allowed(const godot::String &addon_root);

godot::Dictionary status();
godot::Dictionary blocked_result(const godot::String &path);
bool is_path_allowed(const godot::String &path,
                     bool allow_addons_root_discovery,
                     godot::Dictionary &blocked_out);

} // namespace fennara::addon_access
