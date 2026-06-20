#include "fennara/tools/file_ops/create_dir.hpp"

#include "fennara/addon_access.hpp"
#include "fennara/helpers.hpp"
#include "fennara/snapshot_manager.hpp"
#include "fennara/tools/file_ops/common.hpp"

#include <godot_cpp/classes/dir_access.hpp>

namespace fennara::file_ops {

namespace {

bool is_create_dir_token(const godot::String &token) {
    godot::String lower = token.to_lower();
    return lower == "create_dir" || lower == "mkdir";
}

bool parse_create_dir_args(const godot::Dictionary &op, godot::String &path,
                           godot::String &error) {
    godot::PackedStringArray args = collect_args(op);
    for (int i = 0; i < args.size(); i++) {
        godot::String token = args[i];
        if (i == 0 && is_create_dir_token(token)) continue;
        if (token == "-p") continue;
        if (token.begins_with("-")) {
            error = godot::String("Unsupported create_dir argument: ") + token;
            return false;
        }
        if (path.is_empty()) path = token;
        else {
            error = godot::String("Unexpected create_dir argument: ") + token;
            return false;
        }
    }
    return true;
}

} // namespace

godot::Dictionary create_dir(const godot::Dictionary &op,
                             godot::Array &warnings,
                             godot::Array &errors) {
    godot::Dictionary result;
    result["op"] = "create_dir";

    godot::String path;
    if (op.has("destination")) {
        path = op["destination"];
    } else if (op.has("path")) {
        path = op["path"];
    }

    godot::String arg_error;
    if (!parse_create_dir_args(op, path, arg_error)) {
        errors.append(arg_error);
        result["success"] = false;
        result["error"] = arg_error;
        return result;
    }

    if (path.is_empty()) {
        godot::String msg = "create_dir operation requires 'destination' or 'path'";
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    godot::String path_error;
    if (!normalize_scoped_path(path, path, path_error, "create_dir")) {
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
        result["op"] = "create_dir";
        result["path"] = path;
        return result;
    }

    if (godot::DirAccess::dir_exists_absolute(path)) {
        warnings.append(godot::String("Directory already exists: ") + path);
        result["success"] = true;
        result["already_exists"] = true;
        return result;
    }

    // Snapshot before creating — on revert, this directory gets removed
    auto *snap = fennara::FennaraSnapshotManager::get_active();
    if (snap) snap->snapshot_created(path);

    godot::Error err = godot::DirAccess::make_dir_recursive_absolute(path);
    if (err != godot::OK) {
        godot::String msg = godot::String("Failed to create directory: ") +
                            path + " (error code: " +
                            godot::String::num_int64(err) + ")";
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    result["success"] = true;
    fennara::notify_editor_filesystem(path);
    return result;
}

} // namespace fennara::file_ops
