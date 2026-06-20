#include "fennara/tools/file_ops/glob.hpp"

#include "fennara/addon_access.hpp"
#include "fennara/helpers.hpp"
#include "fennara/tools/file_ops/common.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>

namespace fennara::file_ops {

static bool is_glob_executable_token(const godot::String &token) {
    godot::String lower = token.to_lower();
    return lower == "glob" || lower == "find";
}

// Forward declarations for recursive helpers
static void glob_doublestar(const godot::String &dir_path,
                            const godot::String &suffix, bool include_dirs,
                            bool include_hidden, bool include_unimportant,
                            godot::Array &matches, int max_results);

static void glob_with_path(const godot::String &dir_path,
                           const godot::String &pattern, bool include_dirs,
                           bool include_hidden, bool include_unimportant,
                           godot::Array &matches, int max_results,
                           const godot::String &base_path);

static void glob_simple(const godot::String &dir_path,
                        const godot::String &pattern, bool include_dirs,
                        bool include_hidden, bool include_unimportant,
                        godot::Array &matches, int max_results);

// --- Recursive doublestar search ---

static void glob_doublestar(const godot::String &dir_path,
                            const godot::String &suffix, bool include_dirs,
                            bool include_hidden, bool include_unimportant,
                            godot::Array &matches, int max_results) {
    if (matches.size() >= max_results) {
        return;
    }

    godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(dir_path);
    if (!dir.is_valid()) {
        return;
    }

    dir->list_dir_begin();
    godot::String file_name = dir->get_next();
    while (!file_name.is_empty()) {
        if (file_name != "." && file_name != ".." &&
            !should_filter_file(file_name, include_hidden,
                                include_unimportant)) {

            godot::String full_path = dir_path.path_join(file_name);

            if (dir->current_is_dir()) {
                if (include_dirs && !suffix.is_empty() &&
                    match_glob(file_name, suffix)) {
                    matches.append(full_path);
                }
                glob_doublestar(full_path, suffix, include_dirs, include_hidden,
                                include_unimportant, matches, max_results);
            } else {
                if (suffix.is_empty()) {
                    matches.append(full_path);
                } else if (match_glob(file_name, suffix)) {
                    matches.append(full_path);
                }
            }
        }

        if (matches.size() >= max_results) {
            break;
        }
        file_name = dir->get_next();
    }
    dir->list_dir_end();
}

// --- Path-based glob search ---

static void glob_with_path(const godot::String &dir_path,
                           const godot::String &pattern, bool include_dirs,
                           bool include_hidden, bool include_unimportant,
                           godot::Array &matches, int max_results,
                           const godot::String &base_path) {
    if (matches.size() >= max_results) {
        return;
    }

    godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(dir_path);
    if (!dir.is_valid()) {
        return;
    }

    dir->list_dir_begin();
    godot::String file_name = dir->get_next();
    while (!file_name.is_empty()) {
        if (file_name != "." && file_name != ".." &&
            !should_filter_file(file_name, include_hidden,
                                include_unimportant)) {

            godot::String full_path = dir_path.path_join(file_name);
            godot::String relative_path = full_path.substr(base_path.length());
            if (relative_path.begins_with("/")) {
                relative_path = relative_path.substr(1);
            }

            if (dir->current_is_dir()) {
                if (include_dirs && match_glob(relative_path, pattern)) {
                    matches.append(full_path);
                }
                glob_with_path(full_path, pattern, include_dirs, include_hidden,
                               include_unimportant, matches, max_results,
                               base_path);
            } else {
                if (match_glob(relative_path, pattern)) {
                    matches.append(full_path);
                }
            }
        }

        if (matches.size() >= max_results) {
            break;
        }
        file_name = dir->get_next();
    }
    dir->list_dir_end();
}

// --- Simple glob (current directory only) ---

static void glob_simple(const godot::String &dir_path,
                        const godot::String &pattern, bool include_dirs,
                        bool include_hidden, bool include_unimportant,
                        godot::Array &matches, int max_results) {
    if (matches.size() >= max_results) {
        return;
    }

    godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(dir_path);
    if (!dir.is_valid()) {
        return;
    }

    dir->list_dir_begin();
    godot::String file_name = dir->get_next();
    while (!file_name.is_empty()) {
        if (file_name != "." && file_name != ".." &&
            !should_filter_file(file_name, include_hidden,
                                include_unimportant)) {

            godot::String full_path = dir_path.path_join(file_name);

            if (dir->current_is_dir()) {
                if (include_dirs && match_glob(file_name, pattern)) {
                    matches.append(full_path);
                }
            } else {
                if (match_glob(file_name, pattern)) {
                    matches.append(full_path);
                }
            }
        }

        if (matches.size() >= max_results) {
            break;
        }
        file_name = dir->get_next();
    }
    dir->list_dir_end();
}

// --- Public entry point ---

godot::Dictionary glob(const godot::Dictionary &op, godot::Array &warnings,
                       godot::Array &errors) {
    godot::Dictionary result;
    result["op"] = "glob";

    godot::String pattern = op.get("pattern", "");
    godot::String base_path =
        op.get("path", godot::String("res://"));
    bool include_dirs = op.get("include_dirs", false);
    bool include_hidden = op.get("include_hidden", false);
    bool include_unimportant = op.get("include_unimportant", false);
    int max_results = op.get("max_results", 100);
    bool args_only = op.has("args") && !op.has("command");
    int positional_count = 0;

    godot::PackedStringArray tokens;
    if (op.has("args")) {
        godot::Array raw_args = fennara::safe_get_array(op, "args");
        for (int i = 0; i < raw_args.size(); i++) {
            tokens.append(godot::String(raw_args[i]));
        }
    }

    if (op.has("command")) {
        godot::PackedStringArray command_tokens;
        godot::String parse_error;
        if (!tokenize_command(godot::String(op["command"]), command_tokens,
                              parse_error)) {
            errors.append(parse_error);
            result["success"] = false;
            result["error"] = parse_error;
            return result;
        }
        for (int i = 0; i < command_tokens.size(); i++) {
            tokens.append(command_tokens[i]);
        }
    }

    for (int i = 0; i < tokens.size(); i++) {
        godot::String token = tokens[i];
        if (token.is_empty()) {
            continue;
        }

        if (i == 0 && is_glob_executable_token(token)) {
            continue;
        }

        if (token == "--dirs" || token == "--include-dirs") {
            include_dirs = true;
            continue;
        }
        if (token == "--hidden") {
            include_hidden = true;
            continue;
        }
        if (token == "--unimportant") {
            include_unimportant = true;
            continue;
        }
        if (token == "-a" || token == "--all") {
            include_hidden = true;
            include_unimportant = true;
            continue;
        }
        if (token == "-p" || token == "--path") {
            if (i + 1 >= tokens.size()) {
                godot::String msg =
                    godot::String("glob argument requires a value: ") + token;
                errors.append(msg);
                result["success"] = false;
                result["error"] = msg;
                return result;
            }
            base_path = tokens[++i];
            continue;
        }
        if (token == "-m" || token == "--max-results") {
            if (i + 1 >= tokens.size()) {
                godot::String msg =
                    godot::String("glob argument requires a value: ") + token;
                errors.append(msg);
                result["success"] = false;
                result["error"] = msg;
                return result;
            }
            godot::String value = tokens[++i];
            if (!value.is_valid_int()) {
                godot::String msg =
                    godot::String("glob max-results must be an integer: ") +
                    value;
                errors.append(msg);
                result["success"] = false;
                result["error"] = msg;
                return result;
            }
            max_results = value.to_int();
            continue;
        }

        if (token.begins_with("-")) {
            godot::String msg =
                godot::String("Unsupported glob argument: ") + token;
            errors.append(msg);
            result["success"] = false;
            result["error"] = msg;
            return result;
        }

        if (args_only) {
            positional_count++;
            bool looks_like_pattern =
                token.find("*") != -1 || token.find("?") != -1;
            if (positional_count == 1 && looks_like_pattern) {
                pattern = token;
            } else if (positional_count == 1) {
                base_path = token;
            } else if (positional_count == 2 && pattern.is_empty()) {
                pattern = token;
            } else if (positional_count == 2) {
                base_path = token;
            } else {
                godot::String msg =
                    godot::String("Unexpected glob argument: ") + token;
                errors.append(msg);
                result["success"] = false;
                result["error"] = msg;
                return result;
            }
        } else if (pattern.is_empty()) {
            pattern = token;
        } else {
            base_path = token;
        }
    }

    if (pattern.is_empty()) {
        godot::String msg = "glob operation requires 'pattern'";
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    godot::String normalized_base_path;
    godot::String path_error;
    if (!normalize_scoped_path(base_path, normalized_base_path, path_error,
                               "glob", true)) {
        errors.append(path_error);
        result["success"] = false;
        result["error"] = path_error;
        return result;
    }
    base_path = normalized_base_path;

    godot::Dictionary addon_block;
    if (!fennara::addon_access::is_path_allowed(base_path, false, addon_block)) {
        errors.append(addon_block.get("error", "Base path is blocked."));
        result.merge(addon_block);
        result["op"] = "glob";
        result["base_path"] = base_path;
        result["pattern"] = pattern;
        return result;
    }

    result["pattern"] = pattern;
    result["base_path"] = base_path;
    result["include_dirs"] = include_dirs;
    result["include_hidden"] = include_hidden;
    result["include_unimportant"] = include_unimportant;
    result["max_results"] = max_results;

    if (!godot::DirAccess::dir_exists_absolute(base_path)) {
        godot::String msg =
            godot::String("Base path does not exist: ") + base_path;
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    godot::String casing_error;
    if (!directory_exists_case_sensitive(base_path, casing_error, "glob")) {
        errors.append(casing_error);
        result["success"] = false;
        result["error"] = casing_error;
        return result;
    }

    godot::Array matches;

    if (pattern.find("**") != -1) {
        // Handle ** recursive patterns
        int star_idx = pattern.find("**");
        godot::String prefix = pattern.substr(0, star_idx);
        godot::String suffix = pattern.substr(star_idx + 2);
        godot::String search_root = base_path;

        if (prefix.ends_with("/")) {
            prefix = prefix.substr(0, prefix.length() - 1);
        }
        if (suffix.begins_with("/")) {
            suffix = suffix.substr(1);
        }

        if (!prefix.is_empty()) {
            search_root = base_path.path_join(prefix);
        }

        if (!godot::DirAccess::dir_exists_absolute(search_root)) {
            result["success"] = true;
            result["matches"] = matches;
            result["count"] = 0;
            return result;
        }

        if (!directory_exists_case_sensitive(search_root, casing_error,
                                             "glob")) {
            errors.append(casing_error);
            result["success"] = false;
            result["error"] = casing_error;
            return result;
        }

        glob_doublestar(search_root, suffix, include_dirs, include_hidden,
                        include_unimportant, matches,
                        max_results);
    } else if (pattern.find("/") != -1) {
        glob_with_path(base_path, pattern, include_dirs, include_hidden,
                       include_unimportant, matches, max_results, base_path);
    } else {
        glob_simple(base_path, pattern, include_dirs, include_hidden,
                    include_unimportant, matches, max_results);
    }

    // Sort by modification time (newest first)
    // Note: Godot Array sort_custom requires a Callable, so we do a simple
    // insertion sort here since max_results is bounded (typically <=100)
    for (int i = 1; i < matches.size(); i++) {
        godot::String path_i = matches[i];
        int64_t time_i = godot::FileAccess::get_modified_time(path_i);
        int j = i - 1;
        while (j >= 0) {
            godot::String path_j = matches[j];
            int64_t time_j = godot::FileAccess::get_modified_time(path_j);
            if (time_j >= time_i) {
                break;
            }
            matches[j + 1] = matches[j];
            j--;
        }
        matches[j + 1] = path_i;
    }

    result["success"] = true;
    result["matches"] = matches;
    result["count"] = matches.size();
    return result;
}

} // namespace fennara::file_ops
