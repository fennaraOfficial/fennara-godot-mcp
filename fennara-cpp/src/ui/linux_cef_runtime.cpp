#ifdef __linux__

#include "linux_cef_runtime.hpp"

#include "linux_cef_capi.hpp"

#include "fennara/app_paths.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <dlfcn.h>

#include <string>
#include <utility>

namespace fennara::linux_cef_runtime {
namespace {

constexpr const char *kMarkerFile = "fennara-cef-runtime.json";
constexpr const char *kCurrentFile = "current.json";
constexpr const char *kLibcefFile = "libcef.so";

godot::String current_platform_arch() {
#if defined(__x86_64__) || defined(__amd64__)
    return "linux-x64";
#elif defined(__aarch64__)
    return "linux-arm64";
#else
    return "linux-unknown";
#endif
}

std::string utf8(const godot::String &value) {
    return value.utf8().get_data();
}

bool starts_with_todo(const godot::String &value) {
    return utf8(value).rfind("TODO", 0) == 0;
}

bool relative_path_is_safe(const godot::String &path) {
    const std::string value = utf8(path);
    return !value.empty() &&
           value[0] != '/' &&
           value.find('\\') == std::string::npos &&
           value.find("..") == std::string::npos;
}

RuntimeStatus make_status(StatusCode code,
                          const godot::String &message,
                          const godot::String &runtime_dir = godot::String(),
                          const godot::String &marker_path = godot::String(),
                          const godot::String &libcef_path = godot::String(),
                          const godot::String &version = godot::String()) {
    RuntimeStatus status;
    status.code = code;
    status.message = message;
    status.runtime_dir = runtime_dir;
    status.marker_path = marker_path;
    status.libcef_path = libcef_path;
    status.version = version;
    return status;
}

template <typename T>
bool resolve_symbol(void *handle, const char *name, T &out, godot::String &missing_name) {
    dlerror();
    void *symbol = dlsym(handle, name);
    const char *symbol_error = dlerror();
    if (symbol_error != nullptr || symbol == nullptr) {
        missing_name = name;
        return false;
    }
    out = reinterpret_cast<T>(symbol);
    return true;
}

std::unique_ptr<capi::cef_api_t> resolve_api(void *handle, godot::String &missing_name) {
    auto api = std::make_unique<capi::cef_api_t>();
    if (!resolve_symbol(handle, "cef_execute_process", api->cef_execute_process, missing_name) ||
        !resolve_symbol(handle, "cef_initialize", api->cef_initialize, missing_name) ||
        !resolve_symbol(handle, "cef_shutdown", api->cef_shutdown, missing_name) ||
        !resolve_symbol(handle, "cef_do_message_loop_work", api->cef_do_message_loop_work, missing_name) ||
        !resolve_symbol(handle,
                        "cef_browser_host_create_browser_sync",
                        api->cef_browser_host_create_browser_sync,
                        missing_name)) {
        return nullptr;
    }
    return api;
}

godot::Dictionary read_marker(const godot::String &marker_path) {
    godot::Ref<godot::FileAccess> file =
        godot::FileAccess::open(marker_path, godot::FileAccess::READ);
    if (file.is_null()) {
        return godot::Dictionary();
    }

    godot::Variant parsed = godot::JSON::parse_string(file->get_as_text());
    if (parsed.get_type() != godot::Variant::DICTIONARY) {
        return godot::Dictionary();
    }
    return parsed;
}

RuntimeStatus validate_runtime_dir(const godot::String &runtime_dir) {
    const godot::String marker_path = runtime_dir.path_join(kMarkerFile);
    if (!godot::FileAccess::file_exists(marker_path)) {
        return make_status(
            StatusCode::Missing,
            "Linux CEF runtime marker is missing: " + marker_path,
            runtime_dir,
            marker_path);
    }

    godot::Dictionary marker = read_marker(marker_path);
    if (marker.is_empty()) {
        return make_status(
            StatusCode::Corrupt,
            "Linux CEF runtime marker is not valid JSON: " + marker_path,
            runtime_dir,
            marker_path);
    }

    const godot::String runtime = marker.get("runtime", "");
    const godot::String platform = marker.get("platform", "");
    const godot::String platform_arch = marker.get("platform_arch", "");
    const godot::String version = marker.get("version", "");
    const godot::String expected_platform_arch = current_platform_arch();
    const godot::String version_dir_name = runtime_dir.get_file();

    if (runtime != "cef" || platform != "linux" || platform_arch != expected_platform_arch) {
        return make_status(
            StatusCode::Corrupt,
            "Linux CEF runtime marker does not match this platform: " + marker_path,
            runtime_dir,
            marker_path,
            godot::String(),
            version);
    }

    if (version.is_empty() || starts_with_todo(version) || version != version_dir_name) {
        return make_status(
            StatusCode::WrongVersion,
            "Linux CEF runtime marker version does not match its directory: " + marker_path,
            runtime_dir,
            marker_path,
            godot::String(),
            version);
    }

    godot::Variant required_variant = marker.get("required_files", godot::Array());
    if (required_variant.get_type() != godot::Variant::ARRAY) {
        return make_status(
            StatusCode::Corrupt,
            "Linux CEF runtime marker is missing required_files: " + marker_path,
            runtime_dir,
            marker_path,
            godot::String(),
            version);
    }

    godot::Array required_files = required_variant;
    for (int i = 0; i < required_files.size(); i++) {
        if (required_files[i].get_type() != godot::Variant::STRING) {
            return make_status(
                StatusCode::Corrupt,
                "Linux CEF runtime marker has an invalid required_files entry: " + marker_path,
                runtime_dir,
                marker_path,
                godot::String(),
                version);
        }

        const godot::String relative = required_files[i];
        if (!relative_path_is_safe(relative)) {
            return make_status(
                StatusCode::Corrupt,
                "Linux CEF runtime marker has an unsafe required file path: " + relative,
                runtime_dir,
                marker_path,
                godot::String(),
                version);
        }

        const godot::String expected_path = runtime_dir.path_join(relative);
        if (!godot::FileAccess::file_exists(expected_path)) {
            return make_status(
                StatusCode::Corrupt,
                "Linux CEF runtime is missing required file: " + expected_path,
                runtime_dir,
                marker_path,
                godot::String(),
                version);
        }
    }

    const godot::String libcef_path = runtime_dir.path_join(kLibcefFile);
    if (!godot::FileAccess::file_exists(libcef_path)) {
        return make_status(
            StatusCode::Corrupt,
            "Linux CEF runtime is missing libcef.so: " + libcef_path,
            runtime_dir,
            marker_path,
            libcef_path,
            version);
    }

    return make_status(
        StatusCode::Ready,
        "Linux CEF runtime is present: " + runtime_dir,
        runtime_dir,
        marker_path,
        libcef_path,
        version);
}

RuntimeStatus discover_in_root(const godot::String &root) {
    godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(root);
    if (dir.is_null()) {
        return make_status(
            StatusCode::Missing,
            "Linux CEF runtime root is missing: " + root);
    }

    const godot::String current_path = root.path_join(kCurrentFile);
    if (godot::FileAccess::file_exists(current_path)) {
        godot::Dictionary current = read_marker(current_path);
        const godot::String runtime = current.get("runtime", "");
        const godot::String platform = current.get("platform", "");
        const godot::String platform_arch = current.get("platform_arch", "");
        const godot::String dir_name = current.get("dir", "");
        if (current.is_empty() ||
            runtime != "cef" ||
            platform != "linux" ||
            platform_arch != current_platform_arch() ||
            !relative_path_is_safe(dir_name)) {
            return make_status(
                StatusCode::Corrupt,
                "Linux CEF current runtime marker is invalid: " + current_path,
                root,
                current_path);
        }
        return validate_runtime_dir(root.path_join(dir_name));
    }

    RuntimeStatus first_problem;
    bool saw_runtime_dir = false;
    dir->list_dir_begin();
    while (true) {
        godot::String name = dir->get_next();
        if (name.is_empty()) {
            break;
        }
        if (name == "." || name == ".." || !dir->current_is_dir()) {
            continue;
        }

        saw_runtime_dir = true;
        RuntimeStatus status = validate_runtime_dir(root.path_join(name));
        if (status.ok()) {
            dir->list_dir_end();
            return status;
        }
        if (first_problem.message.is_empty()) {
            first_problem = status;
        }
    }
    dir->list_dir_end();

    if (!saw_runtime_dir) {
        return make_status(
            StatusCode::Missing,
            "Linux CEF runtime root has no version directories: " + root);
    }
    return first_problem;
}

} // namespace

LoadedRuntime::LoadedRuntime(void *handle,
                             const RuntimeStatus &status,
                             std::unique_ptr<capi::cef_api_t> api) :
        library_handle(handle),
        runtime_status(status),
        cef_api(std::move(api)) {
}

LoadedRuntime::~LoadedRuntime() {
    close();
}

LoadedRuntime::LoadedRuntime(LoadedRuntime &&other) noexcept :
        library_handle(other.library_handle),
        runtime_status(other.runtime_status),
        cef_api(std::move(other.cef_api)) {
    other.library_handle = nullptr;
}

LoadedRuntime &LoadedRuntime::operator=(LoadedRuntime &&other) noexcept {
    if (this != &other) {
        close();
        library_handle = other.library_handle;
        runtime_status = other.runtime_status;
        cef_api = std::move(other.cef_api);
        other.library_handle = nullptr;
    }
    return *this;
}

const capi::cef_api_t &LoadedRuntime::api() const {
    return *cef_api;
}

void LoadedRuntime::close() {
    if (library_handle != nullptr) {
        dlclose(library_handle);
        library_handle = nullptr;
    }
}

RuntimeStatus discover() {
    const godot::String platform_arch = current_platform_arch();
    if (platform_arch == "linux-unknown") {
        return make_status(
            StatusCode::Missing,
            "Linux CEF runtime is not configured for this CPU architecture");
    }

    const godot::PackedStringArray webview_dirs = fennara::app_paths::webview_dir_candidates();
    if (webview_dirs.is_empty()) {
        return make_status(
            StatusCode::Missing,
            "Linux CEF runtime cannot be located because Fennara app data paths are unavailable");
    }

    RuntimeStatus first_problem;
    for (int i = 0; i < webview_dirs.size(); i++) {
        const godot::String root =
            webview_dirs[i].path_join("cef").path_join(platform_arch);
        RuntimeStatus status = discover_in_root(root);
        if (status.ok()) {
            return status;
        }
        if (first_problem.message.is_empty()) {
            first_problem = status;
        }
    }

    if (!first_problem.message.is_empty()) {
        return first_problem;
    }
    return make_status(
        StatusCode::Missing,
        "Linux CEF runtime was not found under Fennara app data");
}

LoadResult load() {
    LoadResult result;
    result.status = discover();
    if (!result.status.ok()) {
        return result;
    }

    const std::string libcef_path = utf8(result.status.libcef_path);
    dlerror();
    void *handle = dlopen(libcef_path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (handle == nullptr) {
        const char *error = dlerror();
        result.status = make_status(
            StatusCode::LoadFailed,
            godot::String("Linux CEF runtime libcef.so could not be opened: ") +
                result.status.libcef_path +
                (error != nullptr ? godot::String(" (") + error + ")" : godot::String()),
            result.status.runtime_dir,
            result.status.marker_path,
            result.status.libcef_path,
            result.status.version);
        return result;
    }

    godot::String missing_symbol;
    std::unique_ptr<capi::cef_api_t> api = resolve_api(handle, missing_symbol);
    if (api == nullptr) {
        dlclose(handle);
        result.status = make_status(
            StatusCode::LoadFailed,
            "Linux CEF runtime libcef.so is missing required symbol " +
                missing_symbol + ": " + result.status.libcef_path,
            result.status.runtime_dir,
            result.status.marker_path,
            result.status.libcef_path,
            result.status.version);
        return result;
    }

    result.runtime = std::make_unique<LoadedRuntime>(handle, result.status, std::move(api));
    return result;
}

} // namespace fennara::linux_cef_runtime

#endif
