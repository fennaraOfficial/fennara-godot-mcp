#include "fennara/app_paths.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>

namespace fennara::app_paths {
namespace {

constexpr const char *kCSharpLsVersion = "0.20.0";

void append_if_present(godot::PackedStringArray &paths, const godot::String &path) {
    if (!path.is_empty() && !paths.has(path)) {
        paths.append(path);
    }
}

godot::String home_dir() {
    godot::OS *os = godot::OS::get_singleton();
    if (os == nullptr) {
        return "";
    }

    godot::String home = os->get_environment("HOME");
    if (!home.is_empty()) {
        return home;
    }
    return os->get_environment("USERPROFILE");
}

godot::String binary_name(const char *base_name) {
    godot::OS *os = godot::OS::get_singleton();
    if (os != nullptr && os->get_name() == "Windows") {
        return godot::String(base_name) + ".exe";
    }
    return base_name;
}

} // namespace

godot::PackedStringArray app_dir_candidates() {
    godot::PackedStringArray paths;
    godot::OS *os = godot::OS::get_singleton();
    if (os == nullptr) {
        return paths;
    }

    const godot::String os_name = os->get_name();
    if (os_name == "Windows") {
        const godot::String local_app_data = os->get_environment("LOCALAPPDATA");
        if (!local_app_data.is_empty()) {
            append_if_present(paths, local_app_data.path_join("Fennara"));
        }

        const godot::String user_profile = os->get_environment("USERPROFILE");
        if (!user_profile.is_empty()) {
            append_if_present(paths, user_profile.path_join("AppData").path_join("Local").path_join("Fennara"));
        }
        return paths;
    }

    const godot::String home = home_dir();
    if (os_name == "macOS") {
        if (!home.is_empty()) {
            append_if_present(paths, home.path_join("Library").path_join("Application Support").path_join("Fennara"));
        }
        return paths;
    }

    const godot::String xdg_data_home = os->get_environment("XDG_DATA_HOME");
    if (!xdg_data_home.is_empty()) {
        append_if_present(paths, xdg_data_home.path_join("fennara"));
    }
    if (!home.is_empty()) {
        append_if_present(paths, home.path_join(".local").path_join("share").path_join("fennara"));
    }
    if (!xdg_data_home.is_empty()) {
        append_if_present(paths, xdg_data_home.path_join("Fennara"));
    }
    if (!home.is_empty()) {
        append_if_present(paths, home.path_join(".local").path_join("share").path_join("Fennara"));
    }
    return paths;
}

godot::String app_dir() {
    const godot::PackedStringArray paths = app_dir_candidates();
    return paths.is_empty() ? godot::String() : paths[0];
}

godot::PackedStringArray webview_dir_candidates() {
    godot::PackedStringArray paths;
    const godot::PackedStringArray dirs = app_dir_candidates();
    for (int i = 0; i < dirs.size(); i++) {
        append_if_present(paths, dirs[i].path_join("webview"));
    }
    return paths;
}

godot::String webview_dir() {
    const godot::PackedStringArray paths = webview_dir_candidates();
    return paths.is_empty() ? godot::String() : paths[0];
}

godot::String webview_profile_dir() {
    const godot::String dir = app_dir();
    return dir.is_empty()
               ? godot::String()
               : dir.path_join("cache").path_join("webview").path_join("profiles");
}

godot::String webview_log_dir() {
    const godot::String dir = app_dir();
    return dir.is_empty()
               ? godot::String()
               : dir.path_join("logs").path_join("webview");
}

godot::String csharp_ls_binary_path() {
    const godot::PackedStringArray paths = app_dir_candidates();
    const godot::String name = binary_name("csharp-ls");
    for (int i = 0; i < paths.size(); i++) {
        const godot::String candidate = paths[i]
                                            .path_join("tools")
                                            .path_join("csharp-ls")
                                            .path_join(kCSharpLsVersion)
                                            .path_join(name);
        if (godot::FileAccess::file_exists(candidate)) {
            return candidate;
        }
    }
    return paths.is_empty()
               ? godot::String()
               : paths[0]
                     .path_join("tools")
                     .path_join("csharp-ls")
                     .path_join(kCSharpLsVersion)
                     .path_join(name);
}

godot::String daemon_binary_path() {
    const godot::PackedStringArray paths = app_dir_candidates();
    const godot::String name = binary_name("fennara-daemon");
    for (int i = 0; i < paths.size(); i++) {
        const godot::String candidate = paths[i].path_join("bin").path_join(name);
        if (godot::FileAccess::file_exists(candidate)) {
            return candidate;
        }
    }
    return paths.is_empty() ? godot::String() : paths[0].path_join("bin").path_join(name);
}

godot::String docs_cache_dir() {
    const godot::String dir = app_dir();
    return dir.is_empty() ? godot::String() : dir.path_join(".cache").path_join("godot_docs");
}

godot::String runtime_state_path() {
    const godot::String dir = app_dir();
    return dir.is_empty() ? godot::String() : dir.path_join("runtime_state.json");
}

godot::PackedStringArray runtime_state_read_paths() {
    godot::PackedStringArray paths;
    const godot::PackedStringArray dirs = app_dir_candidates();
    for (int i = 0; i < dirs.size(); i++) {
        append_if_present(paths, dirs[i].path_join("runtime_state.json"));
    }
    for (int i = 0; i < dirs.size(); i++) {
        append_if_present(paths, dirs[i].path_join("device.json"));
    }
    return paths;
}

godot::Dictionary read_json_first_existing(const godot::PackedStringArray &paths,
                                           godot::String *used_path) {
    for (int i = 0; i < paths.size(); i++) {
        const godot::String path = paths[i];
        godot::Ref<godot::FileAccess> file = godot::FileAccess::open(path, godot::FileAccess::READ);
        if (file.is_null()) {
            continue;
        }

        godot::Variant parsed = godot::JSON::parse_string(file->get_as_text());
        if (parsed.get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        if (used_path != nullptr) {
            *used_path = path;
        }
        return parsed;
    }
    return godot::Dictionary();
}

bool write_json(const godot::String &path, const godot::Dictionary &data) {
    if (path.is_empty()) {
        return false;
    }

    const godot::String base_dir = path.get_base_dir();
    if (!base_dir.is_empty()) {
        godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(base_dir);
        if (dir.is_null()) {
            godot::DirAccess::make_dir_recursive_absolute(base_dir);
        }
    }

    godot::Ref<godot::FileAccess> file = godot::FileAccess::open(path, godot::FileAccess::WRITE);
    if (file.is_null()) {
        return false;
    }

    file->store_string(godot::JSON::stringify(data, "\t"));
    file->store_string("\n");
    return true;
}

} // namespace fennara::app_paths
