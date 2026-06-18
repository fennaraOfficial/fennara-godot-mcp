#include "fennara/tools/write_or_update_file.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace fennara {

void FennaraWriteOrUpdateFileTool::_bind_methods() {
    godot::ClassDB::bind_static_method(
        "FennaraWriteOrUpdateFileTool", godot::D_METHOD("execute", "args"),
        &FennaraWriteOrUpdateFileTool::execute);
}

godot::Dictionary FennaraWriteOrUpdateFileTool::execute(
    const godot::Dictionary &args) {
    godot::String mode = args.get("mode", "");
    godot::String file_path = args.get("file_path", "");

    if (mode.is_empty()) {
        godot::Dictionary r;
        r["success"] = false;
        r["error"] = "mode required ('write' or 'update')";
        return _stamp_result(r, args);
    }
    if (file_path.is_empty()) {
        godot::Dictionary r;
        r["success"] = false;
        r["error"] = "file_path required";
        return _stamp_result(r, args);
    }

    if (mode == "write") {
        return _stamp_result(_execute_write(args), args);
    }
    if (mode == "update") {
        return _stamp_result(_execute_update(args), args);
    }

    godot::Dictionary r;
    r["success"] = false;
    r["error"] = "Invalid mode. Use 'write' or 'update'";
    return _stamp_result(r, args);
}

} // namespace fennara
