#pragma once

#include <godot_cpp/variant/string.hpp>

#include <memory>

namespace fennara::linux_cef_runtime {

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
                  const RuntimeStatus &status);
    ~LoadedRuntime();

    LoadedRuntime(const LoadedRuntime &) = delete;
    LoadedRuntime &operator=(const LoadedRuntime &) = delete;

    LoadedRuntime(LoadedRuntime &&other) noexcept;
    LoadedRuntime &operator=(LoadedRuntime &&other) noexcept;

    const RuntimeStatus &status() const {
        return runtime_status;
    }

private:
    void close();

    void *library_handle = nullptr;
    RuntimeStatus runtime_status;
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
