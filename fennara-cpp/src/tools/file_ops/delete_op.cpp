#include "fennara/tools/file_ops/delete_op.hpp"

#include "fennara/addon_access.hpp"
#include "fennara/helpers.hpp"
#include "fennara/snapshot_manager.hpp"
#include "fennara/tools/file_ops/common.hpp"

#include <godot_cpp/classes/dir_access.hpp>

namespace fennara::file_ops {

namespace {

bool is_delete_token(const godot::String &token) {
    godot::String lower = token.to_lower();
    return lower == "delete" || lower == "rm" || lower == "del";
}

bool parse_delete_args(const godot::Dictionary &op, godot::String &path,
                       bool &recursive, godot::String &error) {
    godot::PackedStringArray args = collect_args(op);
    for (int i = 0; i < args.size(); i++) {
        godot::String token = args[i];
        if (i == 0 && is_delete_token(token)) continue;
        if (token == "--recursive" || token == "-r") {
            recursive = true;
            continue;
        }
        if (token.begins_with("-")) {
            error = godot::String("Unsupported delete argument: ") + token;
            return false;
        }
        if (path.is_empty()) path = token;
        else {
            error = godot::String("Unexpected delete argument: ") + token;
            return false;
        }
    }
    return true;
}

} // namespace

godot::Dictionary delete_op(const godot::Dictionary &op,
                            godot::Array &warnings,
                            godot::Array &errors) {
    godot::Dictionary result;
    result["op"] = "delete";

    godot::String path;
    if (op.has("path")) {
        path = op["path"];
    } else if (op.has("source")) {
        path = op["source"];
    }

    bool recursive = op.get("recursive", false);
    godot::String arg_error;
    if (!parse_delete_args(op, path, recursive, arg_error)) {
        errors.append(arg_error);
        result["success"] = false;
        result["error"] = arg_error;
        return result;
    }

    if (path.is_empty()) {
        godot::String msg = "Delete operation requires 'path' or 'source'";
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    godot::String path_error;
    if (!normalize_scoped_path(path, path, path_error, "delete")) {
        errors.append(path_error);
        result["success"] = false;
        result["error"] = path_error;
        return result;
    }
    result["path"] = path;

    godot::Dictionary addon_block;
    if (!fennara::addon_access::is_path_allowed(path, false, addon_block)) {
        errors.append(addon_block.get("error", "Path is blocked."));
        result.merge(addon_block);
        result["op"] = "delete";
        result["path"] = path;
        return result;
    }

    if (!fennara::path_exists(path)) {
        godot::String msg = godot::String("Path does not exist: ") + path;
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    if (fennara::is_protected_path(path)) {
        godot::String msg = fennara::protected_path_error(path);
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    bool is_dir = godot::DirAccess::dir_exists_absolute(path);

    if (is_dir) {
        godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(path);
        if (dir.is_valid()) {
            dir->list_dir_begin();
            godot::String file_name = dir->get_next();
            bool is_empty = file_name.is_empty();
            dir->list_dir_end();

            if (!is_empty && !recursive) {
                godot::String msg =
                    godot::String("Directory is not empty, use recursive=true: ") + path;
                errors.append(msg);
                result["success"] = false;
                result["error"] = msg;
                return result;
            }
        }
    }

    // Snapshot before delete
    auto *snap = FennaraSnapshotManager::get_active();
    if (snap) {
        if (!is_dir) {
            snap->snapshot_deleted(path);
        }
        // For recursive dir delete, individual files are snapshotted in delete_directory_recursive
    }

    if (file_ops::delete_path(path, recursive)) {
        if (!is_dir && !file_ops::delete_godot_sidecars(path, warnings,
                                                            errors)) {
            result["success"] = false;
            result["error"] = errors[errors.size() - 1];
            return result;
        }
        result["success"] = true;
        result["type"] = is_dir ? "directory" : "file";
        fennara::notify_editor_filesystem(path);
    } else {
        godot::String msg = godot::String("Failed to delete: ") + path;
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
    }

    return result;
}

} // namespace fennara::file_ops
