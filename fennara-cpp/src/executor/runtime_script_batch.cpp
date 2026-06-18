#include "fennara/executor.hpp"

#include "fennara/tools/runtime_script.hpp"

#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>
#include <godot_cpp/classes/time.hpp>

namespace fennara {
namespace {

constexpr double kPollSeconds = 0.1;

} // namespace

void FennaraExecutor::_start_next_runtime_script() {
    if (_batch_cancelled || _runtime_script_running) {
        return;
    }
    if (_pending_runtime_scripts.empty()) {
        _start_next_screenshot_scene();
        return;
    }

    PendingRuntimeScript pending = _pending_runtime_scripts.front();
    _pending_runtime_scripts.erase(_pending_runtime_scripts.begin());

    _runtime_script_running = true;
    _runtime_script_tool_index = pending.tool_index;
    _runtime_script_args = pending.args;
    _runtime_script_run_id = "";
    _runtime_script_wait_started_ms = 0;
    _runtime_script_thread_done = false;
    _runtime_script_thread_result = godot::Dictionary();

    if (_runtime_script_thread.joinable()) {
        _runtime_script_thread.join();
    }

    _runtime_script_thread = std::thread([this, args = pending.args]() {
        godot::Dictionary result = FennaraRuntimeScriptTool::submit(args);
        {
            std::lock_guard<std::mutex> lock(_runtime_script_mutex);
            _runtime_script_thread_result = result;
            _runtime_script_thread_done = true;
        }
    });

    godot::SceneTree *tree = get_tree();
    if (tree) {
        godot::Ref<godot::SceneTreeTimer> timer = tree->create_timer(kPollSeconds);
        timer->connect("timeout", callable_mp(this, &FennaraExecutor::_on_runtime_script_check_complete));
    } else {
        _on_runtime_script_check_complete();
    }
}

void FennaraExecutor::_on_runtime_script_check_complete() {
    if (_batch_cancelled) {
        return;
    }

    int idx = _runtime_script_tool_index;
    godot::Dictionary args = _runtime_script_args;

    bool done = false;
    godot::Dictionary result;
    {
        std::lock_guard<std::mutex> lock(_runtime_script_mutex);
        done = _runtime_script_thread_done;
        result = _runtime_script_thread_result;
    }

    if (!done) {
        godot::SceneTree *tree = get_tree();
        if (tree) {
            godot::Ref<godot::SceneTreeTimer> timer =
                tree->create_timer(kPollSeconds);
            timer->connect("timeout", callable_mp(this, &FennaraExecutor::_on_runtime_script_check_complete));
        }
        return;
    }

    if (_runtime_script_thread.joinable()) {
        _runtime_script_thread.join();
    }

    _runtime_script_running = false;
    _runtime_script_tool_index = -1;
    _runtime_script_run_id = "";
    _runtime_script_args = godot::Dictionary();
    _runtime_script_wait_started_ms = 0;
    _runtime_script_thread_done = false;
    _runtime_script_thread_result = godot::Dictionary();

    _on_async_tool_complete(result, idx, "runtime_script", args);
    _start_next_runtime_script();
}

} // namespace fennara
