#include "fennara/tools/file_ops/common.hpp"

#include "fennara/helpers.hpp"
#include "fennara/snapshot_manager.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>

namespace fennara::file_ops {

namespace {

void snapshot_destination(const godot::String &path) {
    auto *snap = FennaraSnapshotManager::get_active();
    if (!snap) {
        return;
    }
    if (godot::FileAccess::file_exists(path)) {
        snap->snapshot_file(path);
    } else {
        snap->snapshot_created(path);
    }
}

bool delete_stale_sidecar(const godot::String &path, godot::Array &warnings,
                          godot::Array &errors) {
    auto *snap = FennaraSnapshotManager::get_active();
    if (snap) {
        snap->snapshot_deleted(path);
    }

    if (godot::DirAccess::remove_absolute(path) != godot::OK) {
        errors.append(godot::String("Failed to delete stale sidecar: ") +
                      path);
        return false;
    }

    warnings.append(godot::String("Deleted stale Godot sidecar: ") + path);
    fennara::notify_editor_filesystem(path);
    return true;
}

} // namespace

godot::PackedStringArray godot_sidecar_paths(const godot::String &path) {
    godot::PackedStringArray paths;
    if (path.ends_with(".import") || path.ends_with(".uid") ||
        path.ends_with(".remap")) {
        return paths;
    }
    paths.append(path + godot::String(".import"));
    paths.append(path + godot::String(".uid"));
    paths.append(path + godot::String(".remap"));
    return paths;
}

bool copy_godot_sidecars(const godot::String &source,
                         const godot::String &destination, bool overwrite,
                         godot::Array &warnings, godot::Array &errors) {
    godot::PackedStringArray source_sidecars = godot_sidecar_paths(source);
    godot::PackedStringArray destination_sidecars =
        godot_sidecar_paths(destination);

    for (int i = 0; i < source_sidecars.size(); i++) {
        godot::String sidecar_source = source_sidecars[i];
        godot::String sidecar_destination = destination_sidecars[i];
        if (!godot::FileAccess::file_exists(sidecar_source)) {
            if (overwrite &&
                godot::FileAccess::file_exists(sidecar_destination) &&
                !delete_stale_sidecar(sidecar_destination, warnings, errors)) {
                return false;
            }
            continue;
        }

        if (godot::FileAccess::file_exists(sidecar_destination) && !overwrite) {
            errors.append(godot::String("Sidecar exists (skipped): ") +
                          sidecar_destination);
            return false;
        }

        snapshot_destination(sidecar_destination);
        if (godot::DirAccess::copy_absolute(sidecar_source,
                                            sidecar_destination) != godot::OK) {
            errors.append(godot::String("Failed to copy sidecar: ") +
                          sidecar_source);
            return false;
        }
        warnings.append(godot::String("Copied Godot sidecar: ") +
                        sidecar_destination);
        fennara::notify_editor_filesystem(sidecar_destination);
    }

    return true;
}

bool move_godot_sidecars(const godot::String &source,
                         const godot::String &destination, bool overwrite,
                         godot::Array &warnings, godot::Array &errors) {
    godot::PackedStringArray source_sidecars = godot_sidecar_paths(source);
    godot::PackedStringArray destination_sidecars =
        godot_sidecar_paths(destination);

    for (int i = 0; i < source_sidecars.size(); i++) {
        godot::String sidecar_source = source_sidecars[i];
        godot::String sidecar_destination = destination_sidecars[i];
        if (!godot::FileAccess::file_exists(sidecar_source)) {
            if (overwrite &&
                godot::FileAccess::file_exists(sidecar_destination) &&
                !delete_stale_sidecar(sidecar_destination, warnings, errors)) {
                return false;
            }
            continue;
        }

        if (godot::FileAccess::file_exists(sidecar_destination) && !overwrite) {
            errors.append(godot::String("Sidecar destination already exists: ") +
                          sidecar_destination);
            return false;
        }

        auto *snap = FennaraSnapshotManager::get_active();
        if (snap) {
            snap->snapshot_deleted(sidecar_source);
            snapshot_destination(sidecar_destination);
        }

        if (godot::FileAccess::file_exists(sidecar_destination) &&
            godot::DirAccess::remove_absolute(sidecar_destination) !=
                godot::OK) {
            errors.append(godot::String("Failed to overwrite sidecar: ") +
                          sidecar_destination);
            return false;
        }

        if (godot::DirAccess::rename_absolute(sidecar_source,
                                              sidecar_destination) !=
            godot::OK) {
            errors.append(godot::String("Failed to move sidecar: ") +
                          sidecar_source);
            return false;
        }
        warnings.append(godot::String("Moved Godot sidecar: ") +
                        sidecar_destination);
        fennara::notify_editor_filesystem(sidecar_source);
        fennara::notify_editor_filesystem(sidecar_destination);
    }

    return true;
}

bool delete_godot_sidecars(const godot::String &path, godot::Array &warnings,
                           godot::Array &errors) {
    godot::PackedStringArray sidecars = godot_sidecar_paths(path);
    for (int i = 0; i < sidecars.size(); i++) {
        godot::String sidecar = sidecars[i];
        if (!godot::FileAccess::file_exists(sidecar)) {
            continue;
        }

        auto *snap = FennaraSnapshotManager::get_active();
        if (snap) {
            snap->snapshot_deleted(sidecar);
        }

        if (godot::DirAccess::remove_absolute(sidecar) != godot::OK) {
            errors.append(godot::String("Failed to delete sidecar: ") +
                          sidecar);
            return false;
        }
        warnings.append(godot::String("Deleted Godot sidecar: ") + sidecar);
        fennara::notify_editor_filesystem(sidecar);
    }

    return true;
}

} // namespace fennara::file_ops
