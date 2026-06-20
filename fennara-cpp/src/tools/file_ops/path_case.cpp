#include "fennara/tools/file_ops/common.hpp"

#include <godot_cpp/classes/dir_access.hpp>

namespace fennara::file_ops {

bool directory_exists_case_sensitive(const godot::String &path,
                                     godot::String &error_out,
                                     const godot::String &op_name) {
    if (path == "res://" || path == "res://.") {
        return true;
    }
    if (!path.begins_with("res://")) {
        error_out = op_name + godot::String(" path must be under res://: ") +
                    path;
        return false;
    }

    godot::String relative = path.substr(6).replace("\\", "/");
    while (relative.ends_with("/")) {
        relative = relative.substr(0, relative.length() - 1);
    }
    if (relative.is_empty()) {
        return true;
    }

    godot::PackedStringArray parts = relative.split("/", false);
    godot::String current = "res://";
    for (int i = 0; i < parts.size(); i++) {
        godot::String wanted = parts[i];
        if (wanted == ".") {
            continue;
        }

        godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(current);
        if (!dir.is_valid()) {
            error_out = op_name + godot::String(" path does not exist: ") +
                        current;
            return false;
        }

        bool found_exact = false;
        bool found_case_mismatch = false;
        godot::String mismatched_name;
        dir->list_dir_begin();
        godot::String entry = dir->get_next();
        while (!entry.is_empty()) {
            if (entry == wanted) {
                found_exact = true;
                break;
            }
            if (entry.to_lower() == wanted.to_lower()) {
                found_case_mismatch = true;
                mismatched_name = entry;
            }
            entry = dir->get_next();
        }
        dir->list_dir_end();

        if (!found_exact) {
            if (found_case_mismatch) {
                error_out =
                    op_name +
                    godot::String(" path casing mismatch: requested '") +
                    wanted + "', actual '" + mismatched_name + "'";
            } else {
                error_out = op_name +
                            godot::String(" path does not exist with exact "
                                          "casing: ") +
                            path;
            }
            return false;
        }

        current = current.path_join(wanted);
    }

    if (!godot::DirAccess::dir_exists_absolute(current)) {
        error_out = op_name + godot::String(" path is not a directory: ") +
                    current;
        return false;
    }
    return true;
}

} // namespace fennara::file_ops
