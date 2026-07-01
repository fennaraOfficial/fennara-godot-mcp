#include "fennara/executor.hpp"

#include "fennara/lsp/csharp_build.hpp"
#include "fennara/runtime/runtime_scene_preflight.hpp"
#include "fennara/tools/runtime_session.hpp"
#include "fennara/tools/validate_scene.hpp"

#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>

namespace fennara {
namespace {

constexpr double kPollSeconds = 0.1;

godot::Dictionary make_runtime_session_error(const godot::String &message) {
    godot::Dictionary result;
    result["success"] = false;
    result["tool_name"] = "runtime_session";
    result["format_version"] = "runtime-session-result-v1";
    result["status"] = "blocked";
    result["error"] = message;
    return result;
}

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
    uint64_t batch_generation = _async_batch_generation;

    _runtime_session_running = true;
    _runtime_session_tool_index = pending.tool_index;
    _runtime_session_args = pending.args;
    _runtime_session_thread_done = false;
    _runtime_session_thread_result = godot::Dictionary();
    _runtime_session_build_result = godot::Dictionary();
    _runtime_session_preflight_result = godot::Dictionary();
    _runtime_session_script_context = godot::Dictionary();

    godot::String action =
        godot::String(pending.args.get("action", "status")).strip_edges().to_lower();
    if (action == "start") {
        godot::String scene_path =
            godot::String(pending.args.get("scene_path", "")).strip_edges();
        if (scene_path.is_empty()) {
            godot::Dictionary result =
                make_runtime_session_error("`scene_path` is required.");
            _runtime_session_running = false;
            _runtime_session_tool_index = -1;
            _runtime_session_args = godot::Dictionary();
            _runtime_session_phase = "";
            _runtime_session_build_result = godot::Dictionary();
            _runtime_session_preflight_result = godot::Dictionary();
            _runtime_session_script_context = godot::Dictionary();
            _on_async_tool_complete(result, pending.tool_index, "runtime_session", pending.args, batch_generation);
            _start_next_runtime_session();
            return;
        }

        if (_runtime_session_thread.joinable()) {
            _runtime_session_thread.join();
        }
        _runtime_session_phase = "build";
        _runtime_session_thread = std::thread([this]() {
            godot::Dictionary result = csharp_build::run_dotnet_build_if_needed();
            {
                std::lock_guard<std::mutex> lock(_runtime_session_mutex);
                _runtime_session_thread_result = result;
                _runtime_session_thread_done = true;
            }
        });

        godot::SceneTree *tree = get_tree();
        if (tree) {
            godot::Ref<godot::SceneTreeTimer> timer = tree->create_timer(kPollSeconds);
            timer->connect("timeout", callable_mp(this, &FennaraExecutor::_on_runtime_session_complete).bind(batch_generation));
        } else {
            _on_runtime_session_complete(batch_generation);
        }
        return;
    }

    if (_runtime_session_thread.joinable()) {
        _runtime_session_thread.join();
    }

    _runtime_session_phase = "execute";
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
        timer->connect("timeout", callable_mp(this, &FennaraExecutor::_on_runtime_session_complete).bind(batch_generation));
    } else {
        _on_runtime_session_complete(batch_generation);
    }
}

void FennaraExecutor::_on_runtime_session_complete(uint64_t batch_generation) {
    if (_batch_cancelled || batch_generation != _async_batch_generation) {
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
            timer->connect("timeout", callable_mp(this, &FennaraExecutor::_on_runtime_session_complete).bind(batch_generation));
        }
        return;
    }

    if (_runtime_session_thread.joinable()) {
        _runtime_session_thread.join();
    }

    godot::String phase = _runtime_session_phase;
    if (phase == "build") {
        _runtime_session_build_result = result;
        if ((bool)result.get("needed", false) &&
            godot::String(result.get("status", "")) != "success") {
            godot::Dictionary blocked = make_runtime_session_error(
                "C# project build failed. Runtime session was not started.");
            blocked["csharp_build"] = result;

            int idx = _runtime_session_tool_index;
            godot::Dictionary args = _runtime_session_args;
            _runtime_session_running = false;
            _runtime_session_tool_index = -1;
            _runtime_session_args = godot::Dictionary();
            _runtime_session_phase = "";
            _runtime_session_build_result = godot::Dictionary();
            _runtime_session_preflight_result = godot::Dictionary();
            _runtime_session_script_context = godot::Dictionary();
            _runtime_session_thread_done = false;
            _runtime_session_thread_result = godot::Dictionary();
            _on_async_tool_complete(blocked, idx, "runtime_session", args, batch_generation);
            _start_next_runtime_session();
            return;
        }

        godot::String scene_path =
            godot::String(_runtime_session_args.get("scene_path", "")).strip_edges();
        godot::Dictionary validate_args;
        godot::Array scene_paths;
        scene_paths.append(scene_path);
        validate_args["scene_paths"] = scene_paths;
        validate_args["skip_runtime"] = true;
        if (_runtime_session_args.has("_fennara_tool_artifact_dir")) {
            validate_args["_fennara_tool_artifact_dir"] =
                godot::String(_runtime_session_args["_fennara_tool_artifact_dir"]).path_join("preflight");
        }
        _runtime_session_preflight_result =
            FennaraValidateSceneTool::execute(validate_args);
        godot::Dictionary summary =
            _runtime_session_preflight_result.get("summary", godot::Dictionary());
        if (!(bool)_runtime_session_preflight_result.get("success", false) ||
            static_cast<int>(summary.get("errors", 0)) > 0) {
            godot::Dictionary blocked = make_runtime_session_error(
                "Scene preflight failed. Runtime session was not started.");
            blocked["csharp_build"] = _runtime_session_build_result;
            blocked["preflight"] = _runtime_session_preflight_result;

            int idx = _runtime_session_tool_index;
            godot::Dictionary args = _runtime_session_args;
            _runtime_session_running = false;
            _runtime_session_tool_index = -1;
            _runtime_session_args = godot::Dictionary();
            _runtime_session_phase = "";
            _runtime_session_build_result = godot::Dictionary();
            _runtime_session_preflight_result = godot::Dictionary();
            _runtime_session_script_context = godot::Dictionary();
            _runtime_session_thread_done = false;
            _runtime_session_thread_result = godot::Dictionary();
            _on_async_tool_complete(blocked, idx, "runtime_session", args, batch_generation);
            _start_next_runtime_session();
            return;
        }

        _runtime_session_script_context =
            runtime_scene_preflight::collect_scene_script_context(scene_path);
        _runtime_session_thread_done = false;
        _runtime_session_thread_result = godot::Dictionary();
        _runtime_session_phase = "script_preflight";
        godot::Dictionary script_context = _runtime_session_script_context;
        _runtime_session_thread = std::thread([this, script_context]() {
            godot::Dictionary result =
                runtime_scene_preflight::diagnose_collected_scripts(script_context);
            {
                std::lock_guard<std::mutex> lock(_runtime_session_mutex);
                _runtime_session_thread_result = result;
                _runtime_session_thread_done = true;
            }
        });

        godot::SceneTree *tree = get_tree();
        if (tree) {
            godot::Ref<godot::SceneTreeTimer> timer = tree->create_timer(kPollSeconds);
            timer->connect("timeout", callable_mp(this, &FennaraExecutor::_on_runtime_session_complete).bind(batch_generation));
        } else {
            _on_runtime_session_complete(batch_generation);
        }
        return;
    }

    if (phase == "script_preflight") {
        godot::Dictionary script_preflight = result;
        if (!(bool)script_preflight.get("success", false)) {
            godot::Dictionary blocked = make_runtime_session_error(
                "Scene/autoload script diagnostics failed. Runtime session was not started.");
            blocked["csharp_build"] = _runtime_session_build_result;
            blocked["preflight"] = _runtime_session_preflight_result;
            blocked["script_preflight"] = script_preflight;

            int idx = _runtime_session_tool_index;
            godot::Dictionary args = _runtime_session_args;
            _runtime_session_running = false;
            _runtime_session_tool_index = -1;
            _runtime_session_args = godot::Dictionary();
            _runtime_session_phase = "";
            _runtime_session_build_result = godot::Dictionary();
            _runtime_session_preflight_result = godot::Dictionary();
            _runtime_session_script_context = godot::Dictionary();
            _runtime_session_thread_done = false;
            _runtime_session_thread_result = godot::Dictionary();
            _on_async_tool_complete(blocked, idx, "runtime_session", args, batch_generation);
            _start_next_runtime_session();
            return;
        }

        _runtime_session_thread_done = false;
        _runtime_session_thread_result = godot::Dictionary();
        _runtime_session_phase = "start_daemon";
        godot::Dictionary args = _runtime_session_args;
        godot::Dictionary build_result = _runtime_session_build_result;
        godot::Dictionary preflight = _runtime_session_preflight_result;
        _runtime_session_thread = std::thread(
            [this, args, build_result, preflight, script_preflight]() {
                godot::Dictionary result =
                    FennaraRuntimeSessionTool::execute_start_after_preflight(
                        args, build_result, preflight, script_preflight);
                {
                    std::lock_guard<std::mutex> lock(_runtime_session_mutex);
                    _runtime_session_thread_result = result;
                    _runtime_session_thread_done = true;
                }
            });

        godot::SceneTree *tree = get_tree();
        if (tree) {
            godot::Ref<godot::SceneTreeTimer> timer = tree->create_timer(kPollSeconds);
            timer->connect("timeout", callable_mp(this, &FennaraExecutor::_on_runtime_session_complete).bind(batch_generation));
        } else {
            _on_runtime_session_complete(batch_generation);
        }
        return;
    }

    _runtime_session_running = false;
    _runtime_session_tool_index = -1;
    _runtime_session_args = godot::Dictionary();
    _runtime_session_thread_done = false;
    _runtime_session_thread_result = godot::Dictionary();
    _runtime_session_phase = "";
    _runtime_session_build_result = godot::Dictionary();
    _runtime_session_preflight_result = godot::Dictionary();
    _runtime_session_script_context = godot::Dictionary();

    _on_async_tool_complete(result, idx, "runtime_session", args, batch_generation);
    _start_next_runtime_session();
}

} // namespace fennara
