#include "fennara/executor.hpp"
#include "fennara/snapshot_manager.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace fennara {

void FennaraExecutor::_bind_methods() {
    godot::ClassDB::bind_static_method(
        "FennaraExecutor", godot::D_METHOD("has_tool", "name"),
        &FennaraExecutor::has_tool);
    godot::ClassDB::bind_static_method(
        "FennaraExecutor", godot::D_METHOD("execute_tool", "name", "args"),
        &FennaraExecutor::execute_tool);
    godot::ClassDB::bind_static_method(
        "FennaraExecutor", godot::D_METHOD("execute_tool_calls", "tool_calls"),
        &FennaraExecutor::execute_tool_calls);

    godot::ClassDB::bind_method(
        godot::D_METHOD("execute_tool_calls_async", "tool_calls"),
        &FennaraExecutor::execute_tool_calls_async);
    godot::ClassDB::bind_method(godot::D_METHOD("cancel"),
                                &FennaraExecutor::cancel);
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_snapshot_manager", "manager"),
        &FennaraExecutor::set_snapshot_manager);
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_execution_context", "request_id", "session_index"),
        &FennaraExecutor::set_execution_context);
    godot::ClassDB::bind_method(
        godot::D_METHOD("_on_async_tool_complete", "result", "tool_index", "tool_name", "tool_args", "batch_generation"),
        &FennaraExecutor::_on_async_tool_complete);
    godot::ClassDB::bind_method(
        godot::D_METHOD("_on_batch_diagnostics_complete", "batch_generation"),
        &FennaraExecutor::_on_batch_diagnostics_complete);
    godot::ClassDB::bind_method(
        godot::D_METHOD("_scrape_configuration_warnings"),
        &FennaraExecutor::_scrape_configuration_warnings);
    godot::ClassDB::bind_method(
        godot::D_METHOD("_on_screenshot_scene_opened", "batch_generation"),
        &FennaraExecutor::_on_screenshot_scene_opened);
    godot::ClassDB::bind_method(
        godot::D_METHOD("_on_screenshot_capture", "batch_generation"),
        &FennaraExecutor::_on_screenshot_capture);
    godot::ClassDB::bind_method(
        godot::D_METHOD("_on_runtime_script_check_complete", "batch_generation"),
        &FennaraExecutor::_on_runtime_script_check_complete);

    ADD_SIGNAL(godot::MethodInfo(
        "all_tools_completed",
        godot::PropertyInfo(godot::Variant::ARRAY, "results")));
}

FennaraExecutor::FennaraExecutor() {
}

FennaraExecutor::~FennaraExecutor() {
    _cancel_active_async_tools();
    _validate_scene_runtime_cancelled.store(true);
    if (_batch_diag_thread.joinable()) {
        _batch_diag_thread.join();
    }
    if (_validate_scene_thread.joinable()) {
        _validate_scene_thread.join();
    }
    if (_runtime_script_thread.joinable()) {
        _runtime_script_thread.join();
    }
    if (_runtime_session_thread.joinable()) {
        _runtime_session_thread.join();
    }
}

void FennaraExecutor::set_snapshot_manager(FennaraSnapshotManager *mgr) {
    _snapshot_mgr = mgr;
}

void FennaraExecutor::set_execution_context(const godot::String &request_id,
                                     int session_index) {
    _execution_request_id = request_id;
    _execution_session_index = session_index;
}

} // namespace fennara
