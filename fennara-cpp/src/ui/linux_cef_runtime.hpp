#pragma once

#include <godot_cpp/variant/string.hpp>

#include <memory>

namespace fennara::linux_cef_runtime {

namespace capi {
struct cef_api_t;
}

enum class StatusCode {
    Ready,
    Missing,
    Corrupt,
    WrongVersion,
    LoadFailed,
};

struct RuntimeStatus {
    StatusCode code = StatusCode::Missing;
    godot::String message;
    godot::String runtime_dir;
    godot::String marker_path;
    godot::String libcef_path;
    godot::String version;

    bool ok() const {
        return code == StatusCode::Ready;
    }
};

class LoadedRuntime {
public:
    LoadedRuntime(void *handle,
                  const RuntimeStatus &status,
                  std::unique_ptr<capi::cef_api_t> api);
    ~LoadedRuntime();

    LoadedRuntime(const LoadedRuntime &) = delete;
    LoadedRuntime &operator=(const LoadedRuntime &) = delete;

    LoadedRuntime(LoadedRuntime &&other) noexcept;
    LoadedRuntime &operator=(LoadedRuntime &&other) noexcept;

    const RuntimeStatus &status() const {
        return runtime_status;
    }

    const capi::cef_api_t &api() const;

private:
    void close();

    void *library_handle = nullptr;
    RuntimeStatus runtime_status;
    std::unique_ptr<capi::cef_api_t> cef_api;
};

struct LoadResult {
    RuntimeStatus status;
    std::unique_ptr<LoadedRuntime> runtime;

    bool ok() const {
        return runtime != nullptr && status.ok();
    }
};

RuntimeStatus discover();
LoadResult load();

} // namespace fennara::linux_cef_runtime
