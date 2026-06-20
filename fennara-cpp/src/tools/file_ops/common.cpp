#include "fennara/tools/file_ops/common.hpp"

#include "fennara/helpers.hpp"
#include "fennara/snapshot_manager.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/reg_ex.hpp>
#include <godot_cpp/classes/reg_ex_match.hpp>

namespace fennara::file_ops {

bool should_filter_file(const godot::String &file_name, bool include_hidden,
                        bool include_unimportant) {
    if (!include_hidden && file_name.begins_with(".")) {
        return true;
    }

    if (!include_unimportant &&
        (file_name.ends_with(".uid") || file_name.ends_with(".import") ||
         file_name.ends_with(".remap"))) {
        return true;
    }

    return false;
}

godot::PackedStringArray collect_args(const godot::Dictionary &op) {
    godot::PackedStringArray tokens;
    godot::Array raw_args = fennara::safe_get_array(op, "args");
    for (int i = 0; i < raw_args.size(); i++) {
        tokens.append(godot::String(raw_args[i]));
    }
    return tokens;
}

bool delete_directory_recursive(const godot::String &path) {
    godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(path);
    if (!dir.is_valid()) {
        return false;
    }

    dir->list_dir_begin();
    godot::String file_name = dir->get_next();
    while (!file_name.is_empty()) {
        if (file_name != "." && file_name != "..") {
            godot::String full_path = path.path_join(file_name);
            if (dir->current_is_dir()) {
                if (!delete_directory_recursive(full_path)) {
                    return false;
                }
            } else {
                // Snapshot file before deletion for revert support
                auto *snap = FennaraSnapshotManager::get_active();
                if (snap) {
                    snap->snapshot_deleted(full_path);
                }
                if (godot::DirAccess::remove_absolute(full_path) != godot::OK) {
                    return false;
                }
            }
        }
        file_name = dir->get_next();
    }
    dir->list_dir_end();

    return godot::DirAccess::remove_absolute(path) == godot::OK;
}

bool delete_path(const godot::String &path, bool recursive) {
    if (godot::DirAccess::dir_exists_absolute(path)) {
        if (recursive) {
            return delete_directory_recursive(path);
        }
        return godot::DirAccess::remove_absolute(path) == godot::OK;
    }
    return godot::DirAccess::remove_absolute(path) == godot::OK;
}

bool copy_directory_recursive(const godot::String &source,
                              const godot::String &destination,
                              bool overwrite, godot::Array &warnings,
                              godot::Array &errors) {
    if (!godot::DirAccess::dir_exists_absolute(destination)) {
        if (godot::DirAccess::make_dir_recursive_absolute(destination) !=
            godot::OK) {
            return false;
        }
    }

    godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(source);
    if (!dir.is_valid()) {
        return false;
    }

    dir->list_dir_begin();
    godot::String file_name = dir->get_next();
    while (!file_name.is_empty()) {
        if (file_name != "." && file_name != "..") {
            godot::String source_path = source.path_join(file_name);
            godot::String dest_path = destination.path_join(file_name);

            if (dir->current_is_dir()) {
                if (!copy_directory_recursive(source_path, dest_path, overwrite,
                                             warnings, errors)) {
                    return false;
                }
            } else {
                if (godot::FileAccess::file_exists(dest_path) && !overwrite) {
                    errors.append(godot::String("File exists (skipped): ") +
                                  dest_path);
                    file_name = dir->get_next();
                    continue;
                }
                if (godot::DirAccess::copy_absolute(source_path, dest_path) !=
                    godot::OK) {
                    errors.append(godot::String("Failed to copy: ") +
                                  source_path);
                    return false;
                }
            }
        }
        file_name = dir->get_next();
    }
    dir->list_dir_end();

    return true;
}

bool match_glob(const godot::String &text, const godot::String &pattern) {
    godot::String regex_pattern = pattern;
    regex_pattern = regex_pattern.replace(".", "\\.");
    regex_pattern = regex_pattern.replace("*", ".*");
    regex_pattern = regex_pattern.replace("?", ".");
    regex_pattern = godot::String("^") + regex_pattern + "$";

    godot::Ref<godot::RegEx> regex;
    regex.instantiate();
    regex->compile(regex_pattern);
    return regex->search(text).is_valid();
}

bool match_file_pattern(const godot::String &file_name,
                        const godot::String &relative_path,
                        const godot::String &pattern) {
    godot::String normalized = pattern;
    if (normalized.begins_with("res://")) {
        normalized = normalized.substr(6);
    }

    // Handle ** recursive patterns
    if (normalized.find("**") != -1) {
        int star_idx = normalized.find("**");
        godot::String prefix = normalized.substr(0, star_idx);
        godot::String suffix = normalized.substr(star_idx + 2);

        if (prefix.ends_with("/")) {
            prefix = prefix.substr(0, prefix.length() - 1);
        }
        if (suffix.begins_with("/")) {
            suffix = suffix.substr(1);
        }

        if (!prefix.is_empty() && !relative_path.begins_with(prefix)) {
            return false;
        }

        if (suffix.is_empty()) {
            return true;
        }
        if (suffix.find("/") == -1) {
            return match_glob(file_name, suffix);
        }

        godot::String path_to_match = relative_path;
        if (!prefix.is_empty()) {
            path_to_match = relative_path.substr(prefix.length());
            if (path_to_match.begins_with("/")) {
                path_to_match = path_to_match.substr(1);
            }
        }
        return match_glob(path_to_match, suffix);
    }

    // Handle patterns with path separators (but no **)
    if (normalized.find("/") != -1) {
        return match_glob(relative_path, normalized);
    }

    // Simple pattern — match against filename only
    return match_glob(file_name, pattern);
}

bool tokenize_command(const godot::String &command,
                      godot::PackedStringArray &tokens_out,
                      godot::String &error_out) {
    godot::String current;
    bool in_single = false;
    bool in_double = false;
    bool escaping = false;

    for (int i = 0; i < command.length(); i++) {
        char32_t ch = command.unicode_at(i);

        if (escaping) {
            current += godot::String::chr(ch);
            escaping = false;
            continue;
        }

        if (ch == '\\' && !in_single) {
            escaping = true;
            continue;
        }

        if (ch == '"' && !in_single) {
            in_double = !in_double;
            continue;
        }

        if (ch == '\'' && !in_double) {
            in_single = !in_single;
            continue;
        }

        if (!in_single && !in_double &&
            (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')) {
            if (!current.is_empty()) {
                tokens_out.append(current);
                current = "";
            }
            continue;
        }

        current += godot::String::chr(ch);
    }

    if (escaping || in_single || in_double) {
        error_out =
            "Invalid command string: unterminated quote or escape sequence";
        return false;
    }

    if (!current.is_empty()) {
        tokens_out.append(current);
    }

    return true;
}

bool normalize_scoped_path(const godot::String &path_in,
                           godot::String &normalized_path_out,
                           godot::String &error_out,
                           const godot::String &op_name,
                           bool allow_user_artifacts) {
    godot::String path = path_in.replace("\\", "/");
    if (path.is_empty()) {
        error_out = op_name + godot::String(" path cannot be empty");
        return false;
    }

    if (path.begins_with("user://")) {
        if (allow_user_artifacts) {
            normalized_path_out = path;
            return true;
        }
        error_out = op_name +
                    godot::String(" path is outside project scope: ") +
                    path_in;
        return false;
    }

    godot::String project_root =
        godot::ProjectSettings::get_singleton()->globalize_path("res://");
    godot::String normalized_project_root = project_root.replace("\\", "/");
    if (path.begins_with(normalized_project_root)) {
        normalized_path_out =
            godot::String("res://") + path.substr(normalized_project_root.length());
        return true;
    }

    if (path.begins_with("res://")) {
        normalized_path_out = path;
        return true;
    }

    if (path.contains(":") || path.begins_with("/")) {
        error_out = op_name +
                    godot::String(" path is outside project scope: ") +
                    path_in;
        return false;
    }

    normalized_path_out = godot::String("res://") + path;
    return true;
}

} // namespace fennara::file_ops
