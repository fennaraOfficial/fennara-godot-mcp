#include "fennara/tools/validate_scene.hpp"

#include <godot_cpp/classes/file_access.hpp>

namespace fennara {

namespace {

godot::String s_extract_quoted_attr(const godot::String &line,
                                    const godot::String &name) {
    godot::String needle = name + godot::String("=\"");
    int start = line.find(needle);
    if (start < 0) return "";
    start += name.length() + 2;
    int end = line.find("\"", start);
    if (end < 0) return "";
    return line.substr(start, end - start);
}

godot::String s_extract_instance_id(const godot::String &line) {
    int start = line.find("instance=ExtResource(\"");
    if (start < 0) return "";
    start += 22;
    int end = line.find("\"", start);
    if (end < 0) return "";
    return line.substr(start, end - start);
}

godot::Array s_get_instanced_scene_paths(const godot::String &scene_path) {
    godot::Array deps;
    godot::Ref<godot::FileAccess> f =
        godot::FileAccess::open(scene_path, godot::FileAccess::READ);
    if (!f.is_valid()) return deps;

    godot::String content = f->get_as_text();
    f.unref();

    godot::Dictionary packed_scene_resources;
    godot::PackedStringArray lines = content.split("\n");
    for (int i = 0; i < lines.size(); i++) {
        godot::String line = lines[i].strip_edges();
        if (!line.begins_with("[ext_resource")) continue;

        godot::String type = s_extract_quoted_attr(line, "type");
        if (type != "PackedScene") continue;

        godot::String id = s_extract_quoted_attr(line, "id");
        godot::String path = s_extract_quoted_attr(line, "path");
        if (!id.is_empty() && !path.is_empty() && !path.contains("::")) {
            packed_scene_resources[id] = path;
        }
    }

    for (int i = 0; i < lines.size(); i++) {
        godot::String line = lines[i].strip_edges();
        if (!line.begins_with("[node ")) continue;

        godot::String id = s_extract_instance_id(line);
        if (id.is_empty() || !packed_scene_resources.has(id)) continue;

        godot::String dep_path = packed_scene_resources[id];
        if (!deps.has(dep_path)) deps.append(dep_path);
    }

    return deps;
}

} // namespace

void FennaraValidateSceneTool::_check_cyclic_dependencies(
    const godot::String &scene_path, godot::Array &issues) {
    godot::Array chain;
    godot::Dictionary visited;
    _detect_cycle(scene_path, chain, visited, issues);
}

bool FennaraValidateSceneTool::_detect_cycle(
    const godot::String &scene_path,
    godot::Array &chain,
    godot::Dictionary &visited,
    godot::Array &issues) {

    for (int i = 0; i < chain.size(); i++) {
        if (godot::String(chain[i]) == scene_path) {
            godot::String cycle_str;
            for (int j = i; j < chain.size(); j++) {
                if (!cycle_str.is_empty()) cycle_str += " -> ";
                cycle_str += godot::String(chain[j]);
            }
            cycle_str += " -> " + scene_path;

            _add_issue(issues, scene_path, "cyclic_dependency", "error",
                       "Cyclic scene dependency detected: " + cycle_str);
            return true;
        }
    }

    if (!godot::FileAccess::file_exists(scene_path)) return false;
    if (visited.has(scene_path)) return false;

    chain.append(scene_path);

    godot::Array deps = s_get_instanced_scene_paths(scene_path);
    for (int i = 0; i < deps.size(); i++) {
        godot::String dep_path = deps[i];
        if (_detect_cycle(dep_path, chain, visited, issues)) {
            chain.resize(chain.size() - 1);
            return true;
        }
    }

    chain.resize(chain.size() - 1);
    visited[scene_path] = true;
    return false;
}

} // namespace fennara
