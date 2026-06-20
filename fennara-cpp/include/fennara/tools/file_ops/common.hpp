#pragma once

#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara::file_ops {

// Returns true if an entry should be hidden from list/glob results.
// Hidden files are excluded unless include_hidden=true.
// Unimportant Godot metadata files are excluded unless include_unimportant=true.
bool should_filter_file(const godot::String &file_name,
                        bool include_hidden = false,
                        bool include_unimportant = false);

// Return the operation's positional args as strings.
godot::PackedStringArray collect_args(const godot::Dictionary &op);

// Delete a file or directory. If recursive, removes contents first.
bool delete_path(const godot::String &path, bool recursive);

// Recursively delete a directory and all its contents.
bool delete_directory_recursive(const godot::String &path);

// Recursively copy a directory. Appends to warnings/errors arrays.
bool copy_directory_recursive(const godot::String &source,
                              const godot::String &destination,
                              bool overwrite, godot::Array &warnings,
                              godot::Array &errors);

// Godot stores import/UID metadata beside many project files. File-level
// copy/move/delete operations should keep those sidecars in sync with the
// primary asset.
godot::PackedStringArray godot_sidecar_paths(const godot::String &path);
bool copy_godot_sidecars(const godot::String &source,
                         const godot::String &destination, bool overwrite,
                         godot::Array &warnings, godot::Array &errors);
bool move_godot_sidecars(const godot::String &source,
                         const godot::String &destination, bool overwrite,
                         godot::Array &warnings, godot::Array &errors);
bool delete_godot_sidecars(const godot::String &path, godot::Array &warnings,
                           godot::Array &errors);

// Match text against a glob pattern (supports * and ?).
bool match_glob(const godot::String &text, const godot::String &pattern);

// Match a file against a pattern that may contain **, path separators, or
// simple wildcards. Used by glob and rg convenience filtering.
bool match_file_pattern(const godot::String &file_name,
                        const godot::String &relative_path,
                        const godot::String &pattern);

// Tokenize a shell-like command string with basic quote handling.
bool tokenize_command(const godot::String &command,
                      godot::PackedStringArray &tokens_out,
                      godot::String &error_out);

// Normalize a path so it remains scoped to the current Godot project.
// Relative paths are resolved under res://, project-absolute filesystem paths
// are converted back to res://, and outside-project absolute paths are rejected.
// When allow_user_artifacts is true, any user:// path is also allowed for
// read-only inspection of the current project's Godot user data folder.
bool normalize_scoped_path(const godot::String &path_in,
                           godot::String &normalized_path_out,
                           godot::String &error_out,
                           const godot::String &op_name = "file_ops",
                           bool allow_user_artifacts = false);

// Validate a project path by walking exact directory entries. This prevents
// Windows' case-insensitive filesystem from hiding paths that Godot/Git/Linux
// will treat as different.
bool directory_exists_case_sensitive(const godot::String &path,
                                     godot::String &error_out,
                                     const godot::String &op_name = "glob");

} // namespace fennara::file_ops
