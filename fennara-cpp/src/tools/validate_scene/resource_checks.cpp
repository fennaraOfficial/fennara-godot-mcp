#include "fennara/tools/validate_scene.hpp"

#include <godot_cpp/classes/file_access.hpp>

namespace fennara {

void FennaraValidateSceneTool::_check_missing_ext_resources(
    const godot::String &scene_path, godot::Array &issues) {
    godot::Ref<godot::FileAccess> f =
        godot::FileAccess::open(scene_path, godot::FileAccess::READ);
    if (!f.is_valid()) return;

    godot::String content = f->get_as_text();
    f.unref();

    godot::PackedStringArray lines = content.split("\n");
    for (int i = 0; i < lines.size(); i++) {
        godot::String line = lines[i].strip_edges();
        if (!line.begins_with("[ext_resource")) continue;

        int path_start = line.find("path=\"");
        if (path_start < 0) continue;
        path_start += 6;
        int path_end = line.find("\"", path_start);
        if (path_end < 0) continue;
        godot::String res_path =
            line.substr(path_start, path_end - path_start);

        if (res_path.is_empty() || res_path.contains("::")) continue;
        if (godot::FileAccess::file_exists(res_path)) continue;

        godot::String check_name = "missing_resource";
        int type_start = line.find("type=\"");
        if (type_start >= 0) {
            type_start += 6;
            int type_end = line.find("\"", type_start);
            if (type_end >= 0) {
                godot::String res_type =
                    line.substr(type_start, type_end - type_start);
                if (res_type == "Script" || res_type == "GDScript") {
                    check_name = "missing_script";
                } else if (res_type == "PackedScene") {
                    check_name = "missing_subscene";
                }
            }
        }

        _add_issue(issues, scene_path, check_name, "error",
                   "Referenced resource file not found: " + res_path);
    }
}

} // namespace fennara
