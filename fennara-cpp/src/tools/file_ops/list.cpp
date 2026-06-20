#include "fennara/tools/file_ops/list.hpp"

#include "fennara/addon_access.hpp"
#include "fennara/helpers.hpp"
#include "fennara/tools/file_ops/common.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>

namespace fennara::file_ops {

static bool is_list_executable_token(const godot::String &token) {
    godot::String lower = token.to_lower();
    return lower == "list" || lower == "ls" || lower == "dir";
}

godot::Dictionary list(const godot::Dictionary &op, godot::Array &warnings,
                       godot::Array &errors) {
    godot::Dictionary result;
    result["op"] = "list";

    godot::String path = op.get("path", op.get("destination", "res://"));
    bool include_hidden = op.get("include_hidden", false);
    bool include_unimportant = op.get("include_unimportant", false);
    bool files_only = op.get("files_only", false);
    bool dirs_only = op.get("dirs_only", false);

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

        if (i == 0 && is_list_executable_token(token)) {
            continue;
        }

        if (token == "-a" || token == "--all") {
            include_hidden = true;
            include_unimportant = true;
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
        if (token == "--files-only") {
            files_only = true;
            continue;
        }
        if (token == "--dirs-only") {
            dirs_only = true;
            continue;
        }
        if (token == "-p" || token == "--path") {
            if (i + 1 >= tokens.size()) {
                godot::String msg =
                    godot::String("list argument requires a value: ") + token;
                errors.append(msg);
                result["success"] = false;
                result["error"] = msg;
                return result;
            }
            path = tokens[++i];
            continue;
        }

        if (token.begins_with("-")) {
            godot::String msg =
                godot::String("Unsupported list argument: ") + token;
            errors.append(msg);
            result["success"] = false;
            result["error"] = msg;
            return result;
        }

        path = token;
    }

    if (files_only && dirs_only) {
        godot::String msg =
            "list cannot use both files_only and dirs_only at the same time";
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    godot::String normalized_path;
    godot::String path_error;
    if (!normalize_scoped_path(path, normalized_path, path_error, "list", true)) {
        errors.append(path_error);
        result["success"] = false;
        result["error"] = path_error;
        return result;
    }

    path = normalized_path;
    result["path"] = path;
    result["include_hidden"] = include_hidden;
    result["include_unimportant"] = include_unimportant;
    result["files_only"] = files_only;
    result["dirs_only"] = dirs_only;

    godot::Dictionary addon_block;
    if (!fennara::addon_access::is_path_allowed(path, true, addon_block)) {
        errors.append(addon_block.get("error", "Path is blocked."));
        result.merge(addon_block);
        result["op"] = "list";
        result["path"] = path;
        return result;
    }

    if (!godot::DirAccess::dir_exists_absolute(path)) {
        godot::String msg;
        if (godot::FileAccess::file_exists(path)) {
            msg = godot::String("'") + path +
                  "' is a file, not a directory. The file EXISTS. Use "
                  "read_file to read it, or list its parent directory instead.";
        } else {
            msg = godot::String("Directory does not exist: ") + path;
        }
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(path);
    if (!dir.is_valid()) {
        godot::String msg =
            godot::String("Failed to open directory: ") + path;
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    godot::Array files;
    godot::Array directories;

    dir->list_dir_begin();
    godot::String file_name = dir->get_next();
    while (!file_name.is_empty()) {
        if (file_name != "." && file_name != ".." &&
            !should_filter_file(file_name, include_hidden,
                                include_unimportant)) {
            godot::String child_path = path.path_join(file_name);
            if (dir->current_is_dir()) {
                if (!files_only) {
                    directories.append(child_path);
                }
            } else {
                if (!dirs_only) {
                    files.append(child_path);
                }
            }
        }
        file_name = dir->get_next();
    }
    dir->list_dir_end();

    result["success"] = true;
    result["files"] = files;
    result["directories"] = directories;
    result["total_items"] = files.size() + directories.size();
    return result;
}

} // namespace fennara::file_ops
