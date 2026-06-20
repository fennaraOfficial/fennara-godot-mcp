#include "fennara/tools/file_ops/move.hpp"

#include "fennara/addon_access.hpp"
#include "fennara/helpers.hpp"
#include "fennara/snapshot_manager.hpp"
#include "fennara/tools/file_ops/common.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>

namespace fennara::file_ops {

namespace {

bool is_move_token(const godot::String &token) {
    godot::String lower = token.to_lower();
    return lower == "move" || lower == "mv" || lower == "rename";
}

bool parse_move_args(const godot::Dictionary &op, godot::String &source,
                     godot::String &destination, bool &overwrite,
                     godot::String &error) {
    godot::PackedStringArray args = collect_args(op);
    for (int i = 0; i < args.size(); i++) {
        godot::String token = args[i];
        if (i == 0 && is_move_token(token)) continue;
        if (token == "--overwrite" || token == "-f") {
            overwrite = true;
            continue;
        }
        if (token.begins_with("-")) {
            error = godot::String("Unsupported move argument: ") + token;
            return false;
        }
        if (source.is_empty()) source = token;
        else if (destination.is_empty()) destination = token;
        else {
            error = godot::String("Unexpected move argument: ") + token;
            return false;
        }
    }
    return true;
}

} // namespace

godot::Dictionary move(const godot::Dictionary &op, godot::Array &warnings,
                       godot::Array &errors) {
    godot::Dictionary result;
    result["op"] = "move";

    godot::String source = op.get("source", "");
    godot::String destination = op.get("destination", "");
    bool overwrite = op.get("overwrite", false);

    godot::String arg_error;
    if (!parse_move_args(op, source, destination, overwrite, arg_error)) {
        errors.append(arg_error);
        result["success"] = false;
        result["error"] = arg_error;
        return result;
    }

    if (source.is_empty() || destination.is_empty()) {
        godot::String msg =
            "Move operation requires args: [source, destination]";
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    godot::String path_error;
    if (!normalize_scoped_path(source, source, path_error, "move source")) {
        errors.append(path_error);
        result["success"] = false;
        result["error"] = path_error;
        return result;
    }
    if (!normalize_scoped_path(destination, destination, path_error,
                               "move destination")) {
        errors.append(path_error);
        result["success"] = false;
        result["error"] = path_error;
        return result;
    }

    result["from"] = source;
    result["to"] = destination;

    godot::Dictionary addon_block;
    if (!fennara::addon_access::is_path_allowed(source, false, addon_block)) {
        errors.append(addon_block.get("error", "Source path is blocked."));
        result.merge(addon_block);
        result["op"] = "move";
        result["from"] = source;
        result["to"] = destination;
        return result;
    }
    if (!fennara::addon_access::is_path_allowed(destination, false, addon_block)) {
        errors.append(addon_block.get("error", "Destination path is blocked."));
        result.merge(addon_block);
        result["op"] = "move";
        result["from"] = source;
        result["to"] = destination;
        return result;
    }

    if (!fennara::path_exists(source)) {
        godot::String msg =
            godot::String("Source does not exist: ") + source;
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    if (source == destination) {
        godot::String msg =
            godot::String("Cannot move to itself: ") + source;
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    if (fennara::is_protected_path(source)) {
        godot::String msg = fennara::protected_path_error(source);
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    bool destination_exists = fennara::path_exists(destination);
    if (destination_exists) {
        if (!overwrite) {
            godot::String msg =
                godot::String(
                    "Destination already exists (use overwrite=true): ") +
                destination;
            errors.append(msg);
            result["success"] = false;
            result["error"] = msg;
            return result;
        }

        if (fennara::is_protected_path(destination)) {
            godot::String msg = fennara::protected_path_error(destination);
            errors.append(msg);
            result["success"] = false;
            result["error"] = msg;
            return result;
        }
    }

    // Ensure parent directory exists
    godot::String parent_dir = destination.get_base_dir();
    if (!godot::DirAccess::dir_exists_absolute(parent_dir)) {
        if (godot::DirAccess::make_dir_recursive_absolute(parent_dir) !=
            godot::OK) {
            godot::String msg =
                godot::String("Failed to create parent directory: ") +
                parent_dir;
            errors.append(msg);
            result["success"] = false;
            result["error"] = msg;
            return result;
        }
    }

    bool source_is_dir = godot::DirAccess::dir_exists_absolute(source);
    if (!source_is_dir) {
        godot::PackedStringArray source_sidecars =
            file_ops::godot_sidecar_paths(source);
        godot::PackedStringArray destination_sidecars =
            file_ops::godot_sidecar_paths(destination);
        for (int i = 0; i < source_sidecars.size(); i++) {
            if (godot::FileAccess::file_exists(source_sidecars[i]) &&
                godot::FileAccess::file_exists(destination_sidecars[i]) &&
                !overwrite) {
                godot::String msg =
                    godot::String("Sidecar destination already exists "
                                  "(use overwrite=true): ") +
                    godot::String(destination_sidecars[i]);
                errors.append(msg);
                result["success"] = false;
                result["error"] = msg;
                return result;
            }
        }
    }

    // Snapshot before move
    auto *snap = FennaraSnapshotManager::get_active();
    if (snap) {
        snap->snapshot_deleted(source);
        if (godot::FileAccess::file_exists(destination)) snap->snapshot_file(destination);
        else snap->snapshot_created(destination);
    }

    if (destination_exists) {
        if (!file_ops::delete_path(destination, true)) {
            godot::String msg =
                godot::String("Failed to overwrite destination: ") +
                destination;
            errors.append(msg);
            result["success"] = false;
            result["error"] = msg;
            return result;
        }
        warnings.append(
            godot::String("Overwritten existing path: ") + destination);
    }

    godot::Error err =
        godot::DirAccess::rename_absolute(source, destination);
    if (err != godot::OK) {
        godot::String msg = godot::String("Failed to move: ") + source +
                            " (error code: " +
                            godot::String::num_int64(err) + ")";
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    bool is_dir = godot::DirAccess::dir_exists_absolute(destination);
    if (!is_dir &&
        !file_ops::move_godot_sidecars(source, destination, overwrite,
                                           warnings, errors)) {
        result["success"] = false;
        result["error"] = errors[errors.size() - 1];
        return result;
    }

    result["success"] = true;
    result["type"] = is_dir ? "directory" : "file";
    fennara::notify_editor_filesystem(source);
    fennara::notify_editor_filesystem(destination);
    return result;
}

} // namespace fennara::file_ops
