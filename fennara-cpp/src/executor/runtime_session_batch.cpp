#include "fennara/executor.hpp"

#include "fennara/tools/runtime_session.hpp"

#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>

namespace fennara {
namespace {

constexpr double kPollSeconds = 0.1;

} // namespace

void FennaraExecutor::_start_next_runtime_session() {
    if (_batch_cancelled || _runtime_session_running) {
        return;
    }
    if (_pending_runtime_sessions.empty()) {
        _start_next_runtime_script();
        return;
    }

    PendingRuntimeSession pending = _pending_runtime_sessions.front();
    _pending_runtime_sessions.erase(_pending_runtime_sessions.begin());

    _runtime_session_running = true;
    _runtime_session_tool_index = pending.tool_index;
    _runtime_session_args = pending.args;
    _runtime_session_thread_done = false;
    _runtime_session_thread_result = godot::Dictionary();

    if (_runtime_session_thread.joinable()) {
        _runtime_session_thread.join();
    }

    _runtime_session_thread = std::thread([this, args = pending.args]() {
        godot::Dictionary result = FennaraRuntimeSessionTool::execute(args);
        {
            std::lock_guard<std::mutex> lock(_runtime_session_mutex);
            _runtime_session_thread_result = result;
            _runtime_session_thread_done = true;
        }
    });

    godot::SceneTree *tree = get_tree();
    if (tree) {
        godot::Ref<godot::SceneTreeTimer> timer = tree->create_timer(kPollSeconds);
        timer->connect("timeout", callable_mp(this, &FennaraExecutor::_on_runtime_session_complete));
    } else {
        _on_runtime_session_complete();
    }
}

void FennaraExecutor::_on_runtime_session_complete() {
    if (_batch_cancelled) {
        return;
    }

    int idx = _runtime_session_tool_index;
    godot::Dictionary args = _runtime_session_args;

    bool done = false;
    godot::Dictionary result;
    {
        std::lock_guard<std::mutex> lock(_runtime_session_mutex);
        done = _runtime_session_thread_done;
        result = _runtime_session_thread_result;
    }

    if (!done) {
        godot::SceneTree *tree = get_tree();
        if (tree) {
            godot::Ref<godot::SceneTreeTimer> timer =
                tree->create_timer(kPollSeconds);
            timer->connect("timeout", callable_mp(this, &FennaraExecutor::_on_runtime_session_complete));
        }
        return;
    }

    if (_runtime_session_thread.joinable()) {
        _runtime_session_thread.join();
    }

    _runtime_session_running = false;
    _runtime_session_tool_index = -1;
    _runtime_session_args = godot::Dictionary();
    _runtime_session_thread_done = false;
    _runtime_session_thread_result = godot::Dictionary();

    _on_async_tool_complete(result, idx, "runtime_session", args);
    _start_next_runtime_session();
}

} // namespace fennara
