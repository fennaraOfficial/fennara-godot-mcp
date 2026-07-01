#include "fennara/executor.hpp"

#include "fennara/tools/validate_scene.hpp"

#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>

namespace fennara {

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
    uint64_t batch_generation = _async_batch_generation;

    _validate_scene_running = true;
    _validate_scene_tool_index = pending.tool_index;
    _validate_scene_args = pending.args;
    _validate_scene_results = godot::Array();
    _validate_scene_runtime_eligible_paths = godot::Array();
    _validate_scene_index = 0;
    _validate_scene_thread_done = false;
    _validate_scene_thread_result = godot::Dictionary();

    godot::Variant scene_paths_var = pending.args.get("scene_paths", godot::Variant());
    if (scene_paths_var.get_type() != godot::Variant::ARRAY) {
        godot::Dictionary result = FennaraValidateSceneTool::execute(pending.args);
        _validate_scene_running = false;
        _validate_scene_tool_index = -1;
        _validate_scene_args = godot::Dictionary();
        _on_async_tool_complete(result, pending.tool_index, "validate_scene", pending.args, batch_generation);
        _start_next_validate_scene();
        return;
    }

    _validate_scene_paths = scene_paths_var;
    if (_validate_scene_paths.is_empty() || _validate_scene_paths.size() > 10) {
        godot::Dictionary result = FennaraValidateSceneTool::execute(pending.args);
        _validate_scene_running = false;
        _validate_scene_tool_index = -1;
        _validate_scene_args = godot::Dictionary();
        _validate_scene_paths = godot::Array();
        _on_async_tool_complete(result, pending.tool_index, "validate_scene", pending.args, batch_generation);
        _start_next_validate_scene();
        return;
    }

    _process_next_validate_scene(batch_generation);
}

void FennaraExecutor::_process_next_validate_scene(uint64_t batch_generation) {
    if (_batch_cancelled || batch_generation != _async_batch_generation) {
        return;
    }

    if (_validate_scene_index < _validate_scene_paths.size()) {
        godot::Dictionary scene_result =
            FennaraValidateSceneTool::validate_scene_item(
                _validate_scene_paths[_validate_scene_index],
                _validate_scene_index);
        if (FennaraValidateSceneTool::is_runtime_eligible_scene(scene_result)) {
            _validate_scene_runtime_eligible_paths.append(
                scene_result.get("scene_path", ""));
        } else if (godot::String(scene_result.get("status", "")) == "success") {
            scene_result["runtime_check"] = "skipped";
            scene_result["runtime_skip_reason"] = "structural_errors";
        }
        _validate_scene_results.append(scene_result);
        _validate_scene_index++;

        godot::SceneTree *tree = get_tree();
        if (tree != nullptr) {
            godot::Ref<godot::SceneTreeTimer> timer = tree->create_timer(0.01);
            timer->connect("timeout", callable_mp(
                this, &FennaraExecutor::_process_next_validate_scene).bind(batch_generation));
        } else {
            _process_next_validate_scene(batch_generation);
        }
        return;
    }

    if (_validate_scene_thread.joinable()) {
        _validate_scene_thread.join();
    }

    _validate_scene_thread_done = false;
    _validate_scene_thread_result = godot::Dictionary();
    _validate_scene_runtime_cancelled.store(false);
    godot::Dictionary args = _validate_scene_args;
    godot::Array runtime_eligible_paths = _validate_scene_runtime_eligible_paths;
    _validate_scene_thread = std::thread([this, args, runtime_eligible_paths]() {
        godot::Dictionary runtime_batch =
            FennaraValidateSceneTool::run_runtime_checks_for_scenes(
                args, runtime_eligible_paths, &_validate_scene_runtime_cancelled);
        {
            std::lock_guard<std::mutex> lock(_validate_scene_mutex);
            _validate_scene_thread_result = runtime_batch;
            _validate_scene_thread_done = true;
        }
    });

    godot::SceneTree *tree = get_tree();
    if (tree != nullptr) {
        godot::Ref<godot::SceneTreeTimer> timer = tree->create_timer(0.1);
        timer->connect("timeout", callable_mp(
            this, &FennaraExecutor::_on_validate_scene_runtime_complete).bind(batch_generation));
    } else {
        _on_validate_scene_runtime_complete(batch_generation);
    }
}

void FennaraExecutor::_on_validate_scene_runtime_complete(uint64_t batch_generation) {
    if (_batch_cancelled || batch_generation != _async_batch_generation) {
        return;
    }

    bool done = false;
    godot::Dictionary runtime_batch;
    {
        std::lock_guard<std::mutex> lock(_validate_scene_mutex);
        done = _validate_scene_thread_done;
        runtime_batch = _validate_scene_thread_result;
    }

    if (!done) {
        godot::SceneTree *tree = get_tree();
        if (tree != nullptr) {
            godot::Ref<godot::SceneTreeTimer> timer = tree->create_timer(0.1);
            timer->connect("timeout", callable_mp(
                this, &FennaraExecutor::_on_validate_scene_runtime_complete).bind(batch_generation));
        }
        return;
    }

    if (_validate_scene_thread.joinable()) {
        _validate_scene_thread.join();
    }

    godot::Dictionary result =
        FennaraValidateSceneTool::build_result_from_scenes(
            _validate_scene_args,
            _validate_scene_paths,
            _validate_scene_results,
            runtime_batch);

    int tool_index = _validate_scene_tool_index;
    godot::Dictionary args = _validate_scene_args;
    _validate_scene_running = false;
    _validate_scene_tool_index = -1;
    _validate_scene_args = godot::Dictionary();
    _validate_scene_paths = godot::Array();
    _validate_scene_results = godot::Array();
    _validate_scene_runtime_eligible_paths = godot::Array();
    _validate_scene_index = 0;
    _validate_scene_thread_done = false;
    _validate_scene_thread_result = godot::Dictionary();

    _on_async_tool_complete(result, tool_index, "validate_scene", args, batch_generation);
    _start_next_validate_scene();
}

} // namespace fennara
