#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <mutex>
#include <thread>
#include <vector>

namespace fennara {

class FennaraSnapshotManager;

class FennaraExecutor : public godot::Node {
    GDCLASS(FennaraExecutor, godot::Node)

protected:
    static void _bind_methods();

public:
    static bool has_tool(const godot::String &name);
    static godot::Dictionary execute_tool(const godot::String &name,
                                          const godot::Dictionary &args);
    static godot::Array execute_tool_calls(const godot::Array &tool_calls);

private:
    static bool _is_thread_safe(const godot::String &name);

    godot::Array _async_results;
    godot::Array _active_async_tools;  // prevent RefCounted tools from being freed mid-execution
    int _pending_async_tools = 0;
    bool _batch_cancelled = false;

    // --- Batch diagnostics for script writes ---
    struct PendingScriptWrite {
        int tool_index;
        godot::String file_path;       // resolved absolute path
        godot::Dictionary tool_args;
        godot::Dictionary write_result;
    };
    std::vector<PendingScriptWrite> _pending_script_writes;
    struct PendingRunSceneEditScript {
        int tool_index;
        godot::String script_path;     // res:// path
        godot::String resolved_script_path; // absolute path
        godot::Dictionary prepared_args;
    };
    std::vector<PendingRunSceneEditScript> _pending_run_scene_edit_scripts;
    std::thread _batch_diag_thread;
    std::mutex _batch_diag_mutex;
    godot::Dictionary _batch_diag_results;  // file_path -> per-file diagnostics dict

    void _run_batch_diagnostics();
    void _on_batch_diagnostics_complete();

    // --- Post-batch engine warning capture for modified scenes ---
    struct ModifiedScene {
        godot::String scene_path; // res:// path
        int tool_index;
    };
    std::vector<ModifiedScene> _modified_scenes;
    void _maybe_append_scene_validation(godot::Dictionary &res,
                                        const godot::String &scene_path);
    void _track_modified_scene(const godot::String &scene_path, int tool_index);
    void _capture_scene_warnings();
    godot::Dictionary _scene_to_indices; // kept between phases
    godot::Array _engine_warnings_per_scene; // parallel to scene_paths
    godot::Array _scene_paths_for_warnings;
    void _scrape_configuration_warnings();

    // --- Screenshot scene async (open scene → navigate → capture) ---
    struct PendingScreenshotScene {
        int tool_index;
        godot::Dictionary args;
    };
    std::vector<PendingScreenshotScene> _pending_screenshot_scenes;
    bool _screenshot_running = false;
    int _screenshot_tool_index = -1;
    godot::Dictionary _screenshot_args;
    godot::Dictionary _screenshot_nav_result;
    godot::Array _screenshot_views;
    godot::Array _screenshot_captures;
    int _screenshot_view_index = 0;
    void _start_next_screenshot_scene();
    void _on_screenshot_scene_opened();
    void _on_screenshot_capture();

    // --- Validate scene async (structural checks + daemon runtime batch) ---
    struct PendingValidateScene {
        int tool_index;
        godot::Dictionary args;
    };
    std::vector<PendingValidateScene> _pending_validate_scenes;
    bool _validate_scene_running = false;
    int _validate_scene_tool_index = -1;
    godot::Dictionary _validate_scene_args;
    std::thread _validate_scene_thread;
    std::mutex _validate_scene_mutex;
    bool _validate_scene_thread_done = false;
    godot::Dictionary _validate_scene_thread_result;
    void _start_next_validate_scene();
    void _on_validate_scene_complete();

    // --- Runtime script async (submit to live runtime helper -> wait for completion) ---
    struct PendingRuntimeScript {
        int tool_index;
        godot::Dictionary args;
    };
    std::vector<PendingRuntimeScript> _pending_runtime_scripts;
    bool _runtime_script_running = false;
    int _runtime_script_tool_index = -1;
    godot::String _runtime_script_run_id;
    godot::Dictionary _runtime_script_args;
    int64_t _runtime_script_wait_started_ms = 0;
    std::thread _runtime_script_thread;
    std::mutex _runtime_script_mutex;
    bool _runtime_script_thread_done = false;
    godot::Dictionary _runtime_script_thread_result;
    void _start_next_runtime_script();
    void _on_runtime_script_check_complete();

    // --- Runtime session async (preflight -> daemon-managed scene start/status/stop) ---
    struct PendingRuntimeSession {
        int tool_index;
        godot::Dictionary args;
    };
    std::vector<PendingRuntimeSession> _pending_runtime_sessions;
    bool _runtime_session_running = false;
    int _runtime_session_tool_index = -1;
    godot::Dictionary _runtime_session_args;
    std::thread _runtime_session_thread;
    std::mutex _runtime_session_mutex;
    bool _runtime_session_thread_done = false;
    godot::Dictionary _runtime_session_thread_result;
    void _start_next_runtime_session();
    void _on_runtime_session_complete();

public:
    FennaraExecutor();
    ~FennaraExecutor();

    void execute_tool_calls_async(const godot::Array &tool_calls);
    void cancel();
    void _on_async_tool_complete(const godot::Dictionary &result,
                                 int tool_index,
                                 const godot::String &tool_name,
                                 const godot::Dictionary &tool_args = godot::Dictionary());
    void _check_completion();
    void set_execution_context(const godot::String &request_id, int session_index);

    // Snapshot manager for revert system
    void set_snapshot_manager(FennaraSnapshotManager *mgr);

private:
    FennaraSnapshotManager *_snapshot_mgr = nullptr;
    godot::String _execution_request_id;
    godot::String _execution_batch_id;
    int _execution_session_index = -1;
    uint64_t _batch_counter = 0;

    godot::String _make_batch_id();
    godot::Dictionary _batch_log_context() const;
    godot::Dictionary _tool_log_context(const godot::String &tool_name, int tool_index = -1) const;
    godot::Dictionary _tool_result_metadata(const godot::String &tool_name, int tool_index) const;
    void _log_tool_event(const godot::String &message, godot::Dictionary details) const;
    void _clear_execution_context();
    bool _has_execution_context() const;
    void _print_fennara_activity(const godot::String &message) const;
    godot::String _friendly_tool_action(const godot::String &tool_name,
                                        const godot::Dictionary &args) const;
    void _track_edited_script(const godot::String &script_path);
    void _focus_best_edited_script(const godot::Dictionary &per_file_diagnostics);
    void _focus_script(const godot::String &script_path) const;

    std::vector<godot::String> _edited_script_paths;
};

} // namespace fennara
