#include "fennara/executor.hpp"

#include "fennara/tools/validate_scene.hpp"

#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>

namespace fennara {

namespace {

constexpr double kPollSeconds = 0.1;

} // namespace

void FennaraExecutor::_start_next_validate_scene() {
    if (_batch_cancelled || _validate_scene_running) {
        return;
    }

    if (_pending_validate_scenes.empty()) {
        _start_next_runtime_session();
        return;
    }

    PendingValidateScene pending = _pending_validate_scenes.front();
    _pending_validate_scenes.erase(_pending_validate_scenes.begin());

    _validate_scene_running = true;
    _validate_scene_tool_index = pending.tool_index;
    _validate_scene_args = pending.args;
    _validate_scene_thread_done = false;
    _validate_scene_thread_result = godot::Dictionary();

    if (_validate_scene_thread.joinable()) {
        _validate_scene_thread.join();
    }

    _validate_scene_thread = std::thread([this, args = pending.args]() {
        godot::Dictionary result = FennaraValidateSceneTool::execute(args);
        {
            std::lock_guard<std::mutex> lock(_validate_scene_mutex);
            _validate_scene_thread_result = result;
            _validate_scene_thread_done = true;
        }
    });

    godot::SceneTree *tree = get_tree();
    if (tree) {
        godot::Ref<godot::SceneTreeTimer> timer = tree->create_timer(kPollSeconds);
        timer->connect("timeout", callable_mp(this, &FennaraExecutor::_on_validate_scene_complete));
    } else {
        _on_validate_scene_complete();
    }
}

void FennaraExecutor::_on_validate_scene_complete() {
    if (_batch_cancelled) {
        return;
    }

    int idx = _validate_scene_tool_index;
    godot::Dictionary args = _validate_scene_args;

    bool done = false;
    godot::Dictionary result;
    {
        std::lock_guard<std::mutex> lock(_validate_scene_mutex);
        done = _validate_scene_thread_done;
        result = _validate_scene_thread_result;
    }

    if (!done) {
        godot::SceneTree *tree = get_tree();
        if (tree) {
            godot::Ref<godot::SceneTreeTimer> timer = tree->create_timer(kPollSeconds);
            timer->connect("timeout", callable_mp(this, &FennaraExecutor::_on_validate_scene_complete));
        }
        return;
    }

    if (_validate_scene_thread.joinable()) {
        _validate_scene_thread.join();
    }

    _validate_scene_tool_index = -1;
    _validate_scene_args = godot::Dictionary();
    _validate_scene_running = false;
    _validate_scene_thread_done = false;
    _validate_scene_thread_result = godot::Dictionary();

    _on_async_tool_complete(result, idx, "validate_scene", args);
    _start_next_validate_scene();
}

} // namespace fennara
