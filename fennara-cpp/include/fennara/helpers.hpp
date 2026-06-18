#pragma once

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace fennara {

// Extract an Array from args[key]. Handles LLMs sending JSON strings instead
// of actual arrays: if the value is a String, tries JSON::parse_string().
// Returns empty Array on type mismatch or parse failure.
godot::Array safe_get_array(const godot::Dictionary &args, const godot::String &key);

// Converts LLM JSON types to Godot types:
//   Dict{x,y}       → Vector2
//   Dict{x,y,z}     → Vector3
//   Dict{r,g,b[,a]} → Color
//   Dict{position,size} → Rect2
//   String numbers   → int/float
//   Arrays           → recursively parsed
godot::Variant parse_value(const godot::Variant &value, godot::Variant::Type expected_type = godot::Variant::NIL);

// Converts Godot types to JSON-friendly Dicts:
//   Vector2   → Dict{x,y}
//   Vector3   → Dict{x,y,z}
//   Color     → Dict{r,g,b,a}
//   Texture2D → resource_path string
godot::Variant serialize_value(const godot::Variant &value);

// Ensures path starts with "res://".
godot::String normalize_path(const godot::String &path);

// Returns true if path is protected (project.godot, addons/fennara, .godot, .git).
bool is_protected_path(const godot::String &path);

// Returns a descriptive error message for blocked protected paths.
godot::String protected_path_error(const godot::String &path);

// Returns true if file or directory exists at path.
bool path_exists(const godot::String &path);

// Parse Color from hex string or Dict{r,g,b[,a]}. Returns WHITE on failure.
godot::Color parse_color(const godot::Variant &value);

// Parse Vector3 from Array[3] or Dict{x,y,z}. Returns ZERO on failure.
godot::Vector3 parse_vector3(const godot::Variant &value);

// Notifies the editor filesystem about a changed file.
// Calls update_file() + scan(), and if the file is a scene (.tscn/.scn)
// currently open in the editor, reloads it to prevent the
// "Files Modified on Disk" dialog.
void notify_editor_filesystem(const godot::String &path);

} // namespace fennara
