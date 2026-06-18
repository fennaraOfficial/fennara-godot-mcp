#include "fennara/local_bridge.hpp"

#include "fennara/app_paths.hpp"
#include "fennara/logger.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>

namespace fennara {

void FennaraLocalBridge::_start_daemon_if_available() {
    if (_daemon_spawn_attempted) {
        return;
    }

    godot::String daemon_path = _daemon_binary_path();
    if (daemon_path.is_empty() || !godot::FileAccess::file_exists(daemon_path)) {
        FLOG_NET("Local bridge daemon binary not installed");
        return;
    }

    godot::PackedStringArray args;
    int32_t pid = godot::OS::get_singleton()->create_process(daemon_path, args, false);
    if (pid > 0) {
        _daemon_spawn_attempted = true;
        FLOG_NET("Local bridge started daemon");
    } else {
        FLOG_ERR("Local bridge failed to start daemon");
    }
}

godot::String FennaraLocalBridge::_daemon_binary_path() const {
    return app_paths::daemon_binary_path();
}

} // namespace fennara
