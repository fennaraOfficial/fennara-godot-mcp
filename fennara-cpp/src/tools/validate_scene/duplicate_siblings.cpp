#include "fennara/tools/validate_scene.hpp"

#include <godot_cpp/classes/file_access.hpp>

namespace fennara {

void FennaraValidateSceneTool::_check_duplicate_siblings(
    const godot::String &scene_path, godot::Array &issues) {
    godot::Ref<godot::FileAccess> f =
        godot::FileAccess::open(scene_path, godot::FileAccess::READ);
    if (!f.is_valid()) return;

    godot::String content = f->get_as_text();
    f.unref();

    godot::PackedStringArray lines = content.split("\n");
    godot::Dictionary parent_to_children;

    for (int i = 0; i < lines.size(); i++) {
        godot::String line = lines[i].strip_edges();
        if (!line.begins_with("[node ")) continue;

        int name_start = line.find("name=\"");
        if (name_start < 0) continue;
        name_start += 6;
        int name_end = line.find("\"", name_start);
        if (name_end < 0) continue;
        godot::String name = line.substr(name_start, name_end - name_start);

        int parent_start = line.find("parent=\"");
        if (parent_start < 0) continue;
        parent_start += 8;
        int parent_end = line.find("\"", parent_start);
        if (parent_end < 0) continue;

        godot::String parent_key =
            line.substr(parent_start, parent_end - parent_start);
        if (parent_key.is_empty()) parent_key = ".";

        if (!parent_to_children.has(parent_key)) {
            parent_to_children[parent_key] = godot::Array();
        }

        godot::Array children = parent_to_children[parent_key];
        children.append(name);
        parent_to_children[parent_key] = children;
    }

    godot::Array parent_keys = parent_to_children.keys();
    for (int p = 0; p < parent_keys.size(); p++) {
        godot::String parent_key = parent_keys[p];
        godot::Array children = parent_to_children[parent_key];

        godot::Dictionary name_count;
        for (int c = 0; c < children.size(); c++) {
            godot::String name = children[c];
            if (name_count.has(name)) {
                name_count[name] = (int)name_count[name] + 1;
            } else {
                name_count[name] = 1;
            }
        }

        godot::String parent_path =
            parent_key == "." ? "." : "./" + parent_key;

        godot::Array names = name_count.keys();
        for (int n = 0; n < names.size(); n++) {
            godot::String name = names[n];
            int cnt = name_count[name];
            if (cnt <= 1) continue;

            _add_issue(
                issues, parent_path, "duplicate_sibling_names", "warning",
                godot::String("") +
                    godot::String::num_int64(cnt) +
                    " children named '" + name +
                    "' under '" + parent_path +
                    "' — Godot will silently rename, "
                    "breaking $Name references");
        }
    }
}

} // namespace fennara
