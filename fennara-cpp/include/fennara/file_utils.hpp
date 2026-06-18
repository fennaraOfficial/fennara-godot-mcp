#pragma once

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara {
namespace file_utils {

// Recursively find all .gd files under res://, excluding addons/ and .godot/.
godot::Array find_all_gd_files();

// Recursively find script files indexed by SemanticSearch under res://.
godot::Array find_all_indexable_script_files();

// Recursively find files supported by script_diagnostics under res://.
godot::Array find_all_diagnostic_files();

// Recursive directory scanner (appends absolute paths to results).
void scan_dir_recursive(godot::String path, godot::Array &results);

// Resolve a file path to an absolute path (handles res://, relative, absolute).
godot::String resolve_path(godot::String file_path);

// Read entire file content as text. Returns empty string on failure.
godot::String read_file_content(godot::String abs_path);

// Convert an absolute filesystem path to a file:// URI.
godot::String path_to_uri(godot::String abs_path);

// Convert a file:// URI back to a res:// path.
godot::String uri_to_res_path(godot::String uri);

} // namespace file_utils
} // namespace fennara
