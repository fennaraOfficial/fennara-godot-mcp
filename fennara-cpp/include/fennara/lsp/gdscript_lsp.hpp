#pragma once

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara {
namespace gdscript_lsp {

// Run a shared Godot LSP diagnostics pass for the provided .gd files.
// Input paths should be absolute paths or res:// paths to existing files.
//
// Returns:
// {
//   "success": bool,
//   "error": String,                 // only on failure
//   "file_count": int,
//   "per_file": {
//      "<absolute_path>": {
//          "diagnostics": Array[Dictionary],
//          "total_errors": int,
//          "total_warnings": int
//      }
//   }
// }
godot::Dictionary diagnose_files(const godot::Array &file_paths,
                                 const godot::String &client_name);

} // namespace gdscript_lsp
} // namespace fennara
