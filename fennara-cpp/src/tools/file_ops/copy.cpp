#include "fennara/tools/file_ops/copy.hpp"

#include "fennara/addon_access.hpp"
#include "fennara/helpers.hpp"
#include "fennara/snapshot_manager.hpp"
#include "fennara/tools/file_ops/common.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>

namespace fennara::file_ops {

namespace {

bool is_copy_token(const godot::String &token) {
    godot::String lower = token.to_lower();
    return lower == "copy" || lower == "cp";
}

bool parse_copy_args(const godot::Dictionary &op, godot::String &source,
                     godot::String &destination, bool &recursive,
                     bool &overwrite, godot::String &error) {
    godot::PackedStringArray args = collect_args(op);
    for (int i = 0; i < args.size(); i++) {
        godot::String token = args[i];
        if (i == 0 && is_copy_token(token)) continue;
        if (token == "--recursive" || token == "-r") {
            recursive = true;
            continue;
        }
        if (token == "--overwrite" || token == "-f") {
            overwrite = true;
            continue;
        }
        if (token.begins_with("-")) {
            error = godot::String("Unsupported copy argument: ") + token;
            return false;
        }
        if (source.is_empty()) source = token;
        else if (destination.is_empty()) destination = token;
        else {
            error = godot::String("Unexpected copy argument: ") + token;
            return false;
        }
    }
    return true;
}

} // namespace

godot::Dictionary copy(const godot::Dictionary &op, godot::Array &warnings,
                       godot::Array &errors) {
    godot::Dictionary result;
    result["op"] = "copy";

    godot::String source = op.get("source", "");
    godot::String destination = op.get("destination", "");
    bool recursive = op.get("recursive", false);
    bool overwrite = op.get("overwrite", false);

    godot::String arg_error;
    if (!parse_copy_args(op, source, destination, recursive, overwrite,
                         arg_error)) {
        errors.append(arg_error);
        result["success"] = false;
        result["error"] = arg_error;
        return result;
    }

    if (source.is_empty() || destination.is_empty()) {
        godot::String msg =
            "Copy operation requires args: [source, destination]";
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    godot::String path_error;
    if (!normalize_scoped_path(source, source, path_error, "copy source")) {
        errors.append(path_error);
        result["success"] = false;
        result["error"] = path_error;
        return result;
    }
    if (!normalize_scoped_path(destination, destination, path_error,
                               "copy destination")) {
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
        result["op"] = "copy";
        result["from"] = source;
        result["to"] = destination;
        return result;
    }
    if (!fennara::addon_access::is_path_allowed(destination, false, addon_block)) {
        errors.append(addon_block.get("error", "Destination path is blocked."));
        result.merge(addon_block);
        result["op"] = "copy";
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
            godot::String("Cannot copy to itself: ") + source;
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    if (fennara::path_exists(destination) && !overwrite) {
        godot::String msg =
            godot::String("Destination already exists (use overwrite=true): ") +
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

    bool is_dir = godot::DirAccess::dir_exists_absolute(source);

    if (!is_dir) {
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

    // Snapshot destination before copy
    auto *snap = FennaraSnapshotManager::get_active();
    if (snap) {
        if (is_dir) {
            // For directory copies, individual files will be handled by copy_directory_recursive
        } else {
            if (godot::FileAccess::file_exists(destination)) snap->snapshot_file(destination);
            else snap->snapshot_created(destination);
        }
    }

    if (is_dir) {
        if (!recursive) {
            godot::String msg =
                godot::String("Source is a directory, use recursive=true: ") +
                source;
            errors.append(msg);
            result["success"] = false;
            result["error"] = msg;
            return result;
        }

        if (copy_directory_recursive(source, destination, overwrite, warnings,
                                     errors)) {
            if (overwrite && fennara::path_exists(destination)) {
                warnings.append(
                    godot::String("Overwritten existing directory: ") +
                    destination);
            }
            result["success"] = true;
            result["type"] = "directory";
        } else {
            godot::String msg =
                godot::String("Failed to copy directory: ") + source;
            errors.append(msg);
            result["success"] = false;
            result["error"] = msg;
        }
    } else {
        godot::Error err =
            godot::DirAccess::copy_absolute(source, destination);
        if (err != godot::OK) {
            godot::String msg =
                godot::String("Failed to copy file: ") + source +
                " (error code: " + godot::String::num_int64(err) + ")";
            errors.append(msg);
            result["success"] = false;
            result["error"] = msg;
            return result;
        }

        if (overwrite && godot::FileAccess::file_exists(destination)) {
            warnings.append(
                godot::String("Overwritten existing file: ") + destination);
        }

        result["success"] = true;
        result["type"] = "file";

        if (!file_ops::copy_godot_sidecars(source, destination, overwrite,
                                               warnings, errors)) {
            result["success"] = false;
            result["error"] = errors[errors.size() - 1];
            return result;
        }
    }

    fennara::notify_editor_filesystem(destination);
    return result;
}

} // namespace fennara::file_ops
