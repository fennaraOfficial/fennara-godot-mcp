#include "fennara/tools/validate_scene.hpp"

#include "fennara/helpers.hpp"
#include "fennara/logger.hpp"
#include "fennara/tools/scene_io.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <atomic>
#include <chrono>
#include <thread>

namespace fennara {

namespace {

constexpr int kMaxBatchScenes = 10;
constexpr double kRuntimeRunSeconds = 3.0;
constexpr int kRuntimeWorkers = 3;
constexpr const char *kLocalDaemonHost = "127.0.0.1";
constexpr int kLocalDaemonPort = 41287;
constexpr const char *kRunGodotScenesBatchPath = "/runtime/run-godot-scenes-batch";

uint64_t now_ms() {
    godot::Time *time = godot::Time::get_singleton();
    return time == nullptr ? 0 : static_cast<uint64_t>(time->get_ticks_msec());
}

uint64_t unix_ms() {
    godot::Time *time = godot::Time::get_singleton();
    return time == nullptr
        ? 0
        : static_cast<uint64_t>(time->get_unix_time_from_system() * 1000.0);
}

godot::String project_root() {
    godot::ProjectSettings *settings = godot::ProjectSettings::get_singleton();
    return settings == nullptr ? godot::String() : settings->globalize_path("res://");
}

godot::String user_abs_path(const godot::String &path) {
    godot::ProjectSettings *settings = godot::ProjectSettings::get_singleton();
    return settings == nullptr ? path : settings->globalize_path(path);
}

godot::String make_runtime_check_id() {
    return godot::String("validate-runtime-") +
           godot::String::num_int64(static_cast<int64_t>(unix_ms())) + "-" +
           godot::String::num_int64(static_cast<int64_t>(now_ms()));
}

godot::String resolve_godot_executable() {
    godot::OS *os = godot::OS::get_singleton();
    if (os == nullptr) {
        return "";
    }
    godot::String current = os->get_executable_path();
    if (current.is_empty()) {
        return "";
    }
    if (os->get_name() == "Windows" && !current.ends_with("_console.exe")) {
        godot::String console = current.trim_suffix(".exe") + "_console.exe";
        if (godot::FileAccess::file_exists(console)) {
            return console;
        }
    }
    return current;
}

godot::Dictionary post_local_daemon_json(const godot::String &path,
                                         const godot::Dictionary &payload,
                                         int timeout_ms,
                                         const std::atomic_bool *cancelled) {
    godot::Dictionary result;
    if (cancelled != nullptr && cancelled->load()) {
        result["success"] = false;
        result["status"] = "cancelled";
        result["error"] = "Runtime validation cancelled.";
        return result;
    }

    godot::Ref<godot::HTTPClient> http;
    http.instantiate();
    if (http.is_null()) {
        result["success"] = false;
        result["error"] = "Failed to create HTTPClient for local daemon.";
        return result;
    }

    godot::Error err = http->connect_to_host(kLocalDaemonHost, kLocalDaemonPort);
    if (err != godot::OK) {
        result["success"] = false;
        result["error"] = "Failed to connect to local Fennara daemon.";
        return result;
    }

    godot::PackedStringArray headers;
    headers.append("Content-Type: application/json");
    headers.append("Accept: application/json");
    godot::PackedByteArray body = godot::JSON::stringify(payload).to_utf8_buffer();
    uint64_t deadline = now_ms() + static_cast<uint64_t>(timeout_ms);
    bool request_sent = false;
    godot::String response_body;

    while (now_ms() < deadline) {
        if (cancelled != nullptr && cancelled->load()) {
            result["success"] = false;
            result["status"] = "cancelled";
            result["error"] = "Runtime validation cancelled.";
            return result;
        }

        http->poll();
        godot::HTTPClient::Status status = http->get_status();
        if (status == godot::HTTPClient::STATUS_CANT_RESOLVE ||
            status == godot::HTTPClient::STATUS_CANT_CONNECT ||
            status == godot::HTTPClient::STATUS_CONNECTION_ERROR) {
            result["success"] = false;
            result["error"] = "Failed to connect to local Fennara daemon.";
            return result;
        }

        if (status == godot::HTTPClient::STATUS_CONNECTED && !request_sent) {
            err = http->request_raw(godot::HTTPClient::METHOD_POST, path, headers, body);
            if (err != godot::OK) {
                result["success"] = false;
                result["error"] = "Failed to send local daemon request.";
                return result;
            }
            request_sent = true;
        }

        if (status == godot::HTTPClient::STATUS_BODY) {
            godot::PackedByteArray chunk = http->read_response_body_chunk();
            if (!chunk.is_empty()) {
                response_body += chunk.get_string_from_utf8();
            }
            if (http->get_status() != godot::HTTPClient::STATUS_BODY && http->has_response()) {
                break;
            }
        } else if (request_sent && status == godot::HTTPClient::STATUS_CONNECTED &&
                   http->has_response()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!http->has_response()) {
        result["success"] = false;
        result["error"] = "Timed out waiting for local Fennara daemon.";
        return result;
    }

    godot::Variant parsed = godot::JSON::parse_string(response_body);
    if (parsed.get_type() == godot::Variant::DICTIONARY) {
        result = parsed;
    } else {
        result["response_body"] = response_body;
    }
    bool http_success = http->get_response_code() >= 200 && http->get_response_code() < 300;
    bool payload_ok = result.get("ok", false);
    result["success"] = http_success && payload_ok;
    result["response_code"] = http->get_response_code();
    if (!(bool)result.get("success", false) && !result.has("error")) {
        result["error"] = "Local daemon request failed.";
    }
    return result;
}

godot::Dictionary run_runtime_batch(const godot::Array &scene_paths,
                                    const godot::String &artifact_dir_res,
                                    const std::atomic_bool *cancelled) {
    godot::Dictionary result;
    if (scene_paths.is_empty()) {
        result["success"] = true;
        result["skipped"] = true;
        result["reason"] = "No scenes were eligible for runtime checks.";
        return result;
    }

    godot::String artifact_dir_abs = user_abs_path(artifact_dir_res);
    godot::DirAccess::make_dir_recursive_absolute(artifact_dir_abs);

    godot::Dictionary payload;
    payload["executable"] = resolve_godot_executable();
    payload["working_directory"] = project_root();
    payload["scene_paths"] = scene_paths;
    payload["run_seconds"] = kRuntimeRunSeconds;
    payload["worker_count"] = kRuntimeWorkers;
    payload["artifact_dir"] = artifact_dir_abs;

    int timeout_ms = static_cast<int>(
        ((static_cast<double>(scene_paths.size() + kRuntimeWorkers - 1) /
          static_cast<double>(kRuntimeWorkers)) *
             kRuntimeRunSeconds +
         20.0) *
        1000.0);
    godot::Dictionary daemon =
        post_local_daemon_json(
            kRunGodotScenesBatchPath, payload, timeout_ms, cancelled);
    daemon["artifact_dir_res_path"] = artifact_dir_res;
    daemon["artifact_dir_abs_path"] = artifact_dir_abs;
    daemon["run_seconds"] = kRuntimeRunSeconds;
    daemon["worker_count"] = kRuntimeWorkers;
    return daemon;
}

bool runtime_batch_failed(const godot::Dictionary &runtime_batch,
                          bool skip_runtime) {
    if (skip_runtime || (bool)runtime_batch.get("skipped", false)) {
        return false;
    }
    bool runtime_success = runtime_batch.get("success", false);
    int runtime_crashes = static_cast<int>(runtime_batch.get("crash_count", 0));
    int runtime_errors = static_cast<int>(runtime_batch.get("error_count", 0));
    return !runtime_success || runtime_crashes > 0 || runtime_errors > 0;
}

godot::String validation_status(int success_count,
                                int failure_count,
                                bool runtime_failed) {
    if (failure_count == 0 && !runtime_failed) {
        return "success";
    }
    if (success_count == 0) {
        return "failed";
    }
    return "partial";
}

godot::Dictionary issue_summary_for(const godot::Array &issues,
                                    int checks_run) {
    int errors = 0;
    int warnings = 0;
    int notes = 0;
    for (int i = 0; i < issues.size(); i++) {
        if (issues[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary issue = issues[i];
        godot::String sev = issue.get("severity", "");
        if (sev == "error") {
            errors++;
        } else if (sev == "warning") {
            warnings++;
        } else if (sev == "info") {
            notes++;
        }
    }

    godot::Dictionary summary;
    summary["checks_run"] = checks_run;
    summary["total_issues"] = errors + warnings;
    summary["errors"] = errors;
    summary["warnings"] = warnings;
    summary["notes"] = notes;
    return summary;
}

void apply_issue_summary(godot::Dictionary &item_result,
                         const godot::Array &issues,
                         int checks_run) {
    godot::Dictionary issue_summary = issue_summary_for(issues, checks_run);
    item_result["issues"] = issues;
    item_result["issue_summary"] = issue_summary;
    item_result["checks_run"] = checks_run;
    item_result["total_issues"] = issue_summary["total_issues"];
    item_result["errors"] = issue_summary["errors"];
    item_result["warnings"] = issue_summary["warnings"];
    item_result["notes"] = issue_summary["notes"];
}

godot::String runtime_artifact_user_path(const godot::String &abs_path,
                                         const godot::String &artifact_dir_abs,
                                         const godot::String &artifact_dir_res) {
    godot::String normalized_abs = abs_path.replace("\\", "/");
    godot::String normalized_root = artifact_dir_abs.replace("\\", "/").trim_suffix("/");
    if (normalized_abs.is_empty() || normalized_root.is_empty() ||
        !normalized_abs.begins_with(normalized_root)) {
        return abs_path;
    }
    godot::String relative = normalized_abs.substr(normalized_root.length()).trim_prefix("/");
    return relative.is_empty() ? artifact_dir_res : artifact_dir_res.path_join(relative);
}

} // namespace

void FennaraValidateSceneTool::_bind_methods() {
    godot::ClassDB::bind_static_method(
        "FennaraValidateSceneTool",
        godot::D_METHOD("execute", "args"),
        &FennaraValidateSceneTool::execute);
}

godot::Dictionary FennaraValidateSceneTool::validate_scene_item(
    const godot::Variant &item, int index) {
    if (item.get_type() != godot::Variant::STRING) {
        godot::Dictionary item_result;
        item_result["status"] = "failed";
        item_result["scene_path"] = "";
        item_result["error"] =
            "scene_paths[" + godot::String::num_int64(index) +
            "] must be a string";
        return item_result;
    }

    godot::Dictionary item_result;
    godot::String scene_path = normalize_path(item);

    FLOG_TOOL(godot::String("validate_scene: path=") + scene_path);

    if (!godot::FileAccess::file_exists(scene_path)) {
        item_result["status"] = "failed";
        item_result["scene_path"] = scene_path;
        item_result["error"] = "Scene file not found: " + scene_path;
        return item_result;
    }

    godot::Array issues;
    FennaraValidateSceneTool::_check_missing_ext_resources(scene_path, issues);
    FennaraValidateSceneTool::_check_cyclic_dependencies(scene_path, issues);

    godot::Ref<godot::PackedScene> packed =
        scene_io::load_packed_scene(
            scene_path, godot::ResourceLoader::CACHE_MODE_IGNORE);
    godot::Ref<godot::SceneState> state;
    if (packed.is_valid()) {
        state = packed->get_state();
    }
    if (!state.is_valid()) {
        if (!issues.is_empty()) {
            item_result["status"] = "success";
            item_result["scene_path"] = scene_path;
            apply_issue_summary(item_result, issues, 2);
            return item_result;
        }

        item_result["status"] = "failed";
        item_result["scene_path"] = scene_path;
        item_result["error"] = "Failed to load scene: " + scene_path;
        return item_result;
    }

    FennaraValidateSceneTool::_check_script_extends_mismatch(state, issues);
    FennaraValidateSceneTool::_check_unset_export_vars(state, issues);
    FennaraValidateSceneTool::_check_invalid_node_paths(scene_path, issues);
    FennaraValidateSceneTool::_check_script_node_references(scene_path, issues);
    FennaraValidateSceneTool::_check_duplicate_siblings(scene_path, issues);

    item_result["status"] = "success";
    item_result["scene_path"] = scene_path;
    apply_issue_summary(item_result, issues, 7);

    FLOG_TOOL(godot::String("validate_scene: done, issues=") +
              godot::String::num_int64(static_cast<int>(item_result["total_issues"])) +
              " errors=" + godot::String::num_int64(static_cast<int>(item_result["errors"])) +
              " warnings=" + godot::String::num_int64(static_cast<int>(item_result["warnings"])) +
              " notes=" + godot::String::num_int64(static_cast<int>(item_result["notes"])));
    return item_result;
}

bool FennaraValidateSceneTool::is_runtime_eligible_scene(
    const godot::Dictionary &scene_result) {
    return godot::String(scene_result.get("status", "")) == "success" &&
           static_cast<int>(scene_result.get("errors", 0)) == 0;
}

godot::Dictionary FennaraValidateSceneTool::run_runtime_checks_for_scenes(
    const godot::Dictionary &args,
    const godot::Array &runtime_eligible_scene_paths,
    const std::atomic_bool *cancelled) {
    bool skip_runtime = args.get("skip_runtime", false);
    godot::String artifact_dir =
        godot::String(args.get("_fennara_tool_artifact_dir", "")).strip_edges();
    godot::String validate_artifact_dir_res = artifact_dir.is_empty()
        ? godot::String("user://.fennara/tool_logs/manual/validate_scene")
              .path_join(make_runtime_check_id())
        : artifact_dir.path_join("validate_scene");
    godot::String runtime_run_dir_res = validate_artifact_dir_res.path_join("runtime");

    godot::Dictionary runtime_batch;
    if (skip_runtime) {
        runtime_batch["success"] = true;
        runtime_batch["skipped"] = true;
        runtime_batch["status"] = "skipped";
        runtime_batch["reason"] = "Runtime checks skipped by caller.";
        runtime_batch["results"] = godot::Array();
    } else {
        runtime_batch =
            run_runtime_batch(
                runtime_eligible_scene_paths, runtime_run_dir_res, cancelled);
    }
    runtime_batch["validate_artifact_dir_res_path"] = validate_artifact_dir_res;
    runtime_batch["validate_artifact_dir_abs_path"] =
        user_abs_path(validate_artifact_dir_res);
    return runtime_batch;
}

godot::Dictionary FennaraValidateSceneTool::build_result_from_scenes(
    const godot::Dictionary &args,
    const godot::Array &scene_paths,
    const godot::Array &scenes,
    const godot::Dictionary &runtime_batch) {
    godot::Dictionary result;
    bool skip_runtime = args.get("skip_runtime", false);

    godot::String artifact_dir =
        godot::String(args.get("_fennara_tool_artifact_dir", "")).strip_edges();
    godot::String validate_artifact_dir_res = artifact_dir.is_empty()
        ? godot::String("user://.fennara/tool_logs/manual/validate_scene")
              .path_join(make_runtime_check_id())
        : artifact_dir.path_join("validate_scene");
    validate_artifact_dir_res = runtime_batch.get(
        "validate_artifact_dir_res_path", validate_artifact_dir_res);
    godot::String validate_artifact_dir_abs = runtime_batch.get(
        "validate_artifact_dir_abs_path", user_abs_path(validate_artifact_dir_res));
    godot::String runtime_run_dir_res = validate_artifact_dir_res.path_join("runtime");

    godot::Array final_scenes = scenes.duplicate();
    godot::Dictionary runtime_by_scene;
    godot::Array runtime_results = runtime_batch.get("results", godot::Array());
    godot::String runtime_artifact_abs = runtime_batch.get("artifact_dir_abs_path", "");
    for (int i = 0; i < runtime_results.size(); i++) {
        if (runtime_results[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary runtime = runtime_results[i];
        runtime["raw_log_user_path"] = runtime_artifact_user_path(
            runtime.get("raw_log_path", ""), runtime_artifact_abs, runtime_run_dir_res);
        runtime["compacted_log_user_path"] = runtime_artifact_user_path(
            runtime.get("compacted_log_path", ""), runtime_artifact_abs, runtime_run_dir_res);
        godot::String scene_path = runtime.get("scene_path", "");
        if (!scene_path.is_empty()) {
            runtime_by_scene[scene_path] = runtime;
        }
    }
    for (int i = 0; i < final_scenes.size(); i++) {
        if (final_scenes[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary scene = final_scenes[i];
        godot::String scene_path = scene.get("scene_path", "");
        if (runtime_by_scene.has(scene_path)) {
            scene["runtime_check"] = runtime_by_scene[scene_path];
            final_scenes[i] = scene;
        } else if (godot::String(scene.get("status", "")) == "success" &&
                   static_cast<int>(scene.get("errors", 0)) == 0 &&
                   !(bool)runtime_batch.get("success", false)) {
            scene["runtime_check"] = "failed";
            scene["runtime_error"] = runtime_batch.get("error", "Runtime batch failed.");
            final_scenes[i] = scene;
        }
    }

    int success_count = 0;
    int failure_count = 0;
    int total_issues = 0;
    int errors = 0;
    int warnings = 0;
    int notes = 0;
    int checks_run = 0;
    int runtime_checked_count = 0;
    for (int i = 0; i < final_scenes.size(); i++) {
        if (final_scenes[i].get_type() != godot::Variant::DICTIONARY) {
            failure_count++;
            continue;
        }
        godot::Dictionary scene = final_scenes[i];
        if (godot::String(scene.get("status", "")) == "success") {
            success_count++;
            total_issues += static_cast<int>(scene.get("total_issues", 0));
            errors += static_cast<int>(scene.get("errors", 0));
            warnings += static_cast<int>(scene.get("warnings", 0));
            notes += static_cast<int>(scene.get("notes", 0));
            checks_run += static_cast<int>(scene.get("checks_run", 0));
            if (is_runtime_eligible_scene(scene)) {
                runtime_checked_count++;
            }
        } else {
            failure_count++;
        }
    }

    bool runtime_failed = runtime_batch_failed(runtime_batch, skip_runtime);

    godot::Dictionary summary;
    summary["status"] =
        validation_status(success_count, failure_count, runtime_failed);
    summary["requested_count"] = scene_paths.size();
    summary["checked_count"] = final_scenes.size();
    summary["success_count"] = success_count;
    summary["failure_count"] = failure_count;
    summary["total_issues"] = total_issues;
    summary["errors"] = errors;
    summary["warnings"] = warnings;
    summary["notes"] = notes;
    summary["checks_run"] = checks_run;
    summary["runtime_checked_count"] = runtime_checked_count;
    summary["runtime_skipped"] =
        skip_runtime || (bool)runtime_batch.get("skipped", false);
    summary["runtime_failed"] = runtime_failed;
    bool runtime_success = runtime_batch.get("success", false);
    summary["runtime_status"] = runtime_batch.get("status", runtime_success ? "success" : "failed");
    summary["runtime_crash_count"] = runtime_batch.get("crash_count", 0);
    summary["runtime_error_count"] = runtime_batch.get("error_count", 0);
    summary["runtime_warning_count"] = runtime_batch.get("warning_count", 0);
    summary["runtime_compacted_log_path"] = runtime_artifact_user_path(
        runtime_batch.get("compacted_log_path", ""), runtime_artifact_abs, runtime_run_dir_res);
    summary["runtime_compacted_log_absolute_path"] =
        runtime_batch.get("compacted_log_path", "");
    summary["runtime_results_path"] = runtime_artifact_user_path(
        runtime_batch.get("results_path", ""), runtime_artifact_abs, runtime_run_dir_res);
    summary["runtime_results_absolute_path"] = runtime_batch.get("results_path", "");
    summary["runtime_raw_logs_dir"] = runtime_artifact_user_path(
        runtime_batch.get("raw_logs_dir", ""), runtime_artifact_abs, runtime_run_dir_res);
    summary["runtime_raw_logs_absolute_dir"] = runtime_batch.get("raw_logs_dir", "");
    summary["artifact_dir"] = validate_artifact_dir_res;
    summary["artifact_absolute_dir"] = validate_artifact_dir_abs;

    result["success"] = failure_count == 0 && !runtime_failed;
    result["tool_name"] = "validate_scene";
    result["format_version"] = "validate-scene-result-v1";
    result["summary"] = summary;
    result["scenes"] = final_scenes;
    result["runtime_batch"] = runtime_batch;
    result["artifact_dir"] = validate_artifact_dir_res;
    result["artifact_absolute_dir"] = validate_artifact_dir_abs;

    if (!(bool)result["success"]) {
        if (runtime_failed) {
            result["error"] = godot::String("Runtime validation failed: ") +
                godot::String(runtime_batch.get("error", summary["runtime_status"]));
        } else if (failure_count == final_scenes.size()) {
            result["error"] = "Failed to validate requested scene(s)";
        } else {
            result["error"] = "Some scene(s) could not be validated";
        }
    }

    godot::DirAccess::make_dir_recursive_absolute(validate_artifact_dir_abs);
    godot::String result_json_abs =
        validate_artifact_dir_abs.path_join("validate_scene_result.json");
    godot::Ref<godot::FileAccess> result_file =
        godot::FileAccess::open(result_json_abs, godot::FileAccess::WRITE);
    if (result_file.is_valid()) {
        summary["result_json_path"] =
            validate_artifact_dir_res.path_join("validate_scene_result.json");
        summary["result_json_absolute_path"] = result_json_abs;
        result["summary"] = summary;
        result_file->store_string(godot::JSON::stringify(result, "\t"));
        result_file->store_string("\n");
    }
    return result;
}

godot::Dictionary FennaraValidateSceneTool::execute(const godot::Dictionary &args) {
    godot::Dictionary result;

    if (!args.has("scene_paths")) {
        result["success"] = false;
        result["tool_name"] = "validate_scene";
        result["format_version"] = "validate-scene-result-v1";
        result["error"] = "Missing required arg: scene_paths";
        return result;
    }

    godot::Variant scene_paths_var = args["scene_paths"];
    if (scene_paths_var.get_type() != godot::Variant::ARRAY) {
        result["success"] = false;
        result["tool_name"] = "validate_scene";
        result["format_version"] = "validate-scene-result-v1";
        result["error"] = "scene_paths must be an array of strings";
        return result;
    }

    godot::Array scene_paths = scene_paths_var;
    bool skip_runtime = args.get("skip_runtime", false);
    if (scene_paths.is_empty()) {
        result["success"] = false;
        result["tool_name"] = "validate_scene";
        result["format_version"] = "validate-scene-result-v1";
        result["error"] = "scene_paths must contain at least one path";
        return result;
    }
    if (scene_paths.size() > kMaxBatchScenes) {
        result["success"] = false;
        result["tool_name"] = "validate_scene";
        result["format_version"] = "validate-scene-result-v1";
        result["error"] =
            "scene_paths supports at most " +
            godot::String::num_int64(kMaxBatchScenes) +
            " scenes per call. Split larger requests into multiple calls.";
        return result;
    }

    auto validate_single_scene = [&](const godot::String &raw_scene_path) {
        godot::Dictionary item_result;
        godot::String scene_path = normalize_path(raw_scene_path);

        FLOG_TOOL(godot::String("validate_scene: path=") + scene_path);

        if (!godot::FileAccess::file_exists(scene_path)) {
            item_result["status"] = "failed";
            item_result["scene_path"] = scene_path;
            item_result["error"] = "Scene file not found: " + scene_path;
            return item_result;
        }

        godot::Array issues;
        FennaraValidateSceneTool::_check_missing_ext_resources(scene_path, issues);
        FennaraValidateSceneTool::_check_cyclic_dependencies(scene_path, issues);

        godot::Ref<godot::PackedScene> packed =
            scene_io::load_packed_scene(
                scene_path, godot::ResourceLoader::CACHE_MODE_IGNORE);
        godot::Ref<godot::SceneState> state;
        if (packed.is_valid()) {
            state = packed->get_state();
        }
        if (!state.is_valid()) {
            if (!issues.is_empty()) {
                item_result["status"] = "success";
                item_result["scene_path"] = scene_path;
                apply_issue_summary(item_result, issues, 2);
                return item_result;
            }

            item_result["status"] = "failed";
            item_result["scene_path"] = scene_path;
            item_result["error"] = "Failed to load scene: " + scene_path;
            return item_result;
        }

        FennaraValidateSceneTool::_check_script_extends_mismatch(state, issues);
        FennaraValidateSceneTool::_check_unset_export_vars(state, issues);
        FennaraValidateSceneTool::_check_invalid_node_paths(scene_path, issues);
        FennaraValidateSceneTool::_check_script_node_references(scene_path, issues);
        FennaraValidateSceneTool::_check_duplicate_siblings(scene_path, issues);

        item_result["status"] = "success";
        item_result["scene_path"] = scene_path;
        apply_issue_summary(item_result, issues, 7);

        FLOG_TOOL(godot::String("validate_scene: done, issues=") +
                  godot::String::num_int64(static_cast<int>(item_result["total_issues"])) +
                  " errors=" + godot::String::num_int64(static_cast<int>(item_result["errors"])) +
                  " warnings=" + godot::String::num_int64(static_cast<int>(item_result["warnings"])) +
                  " notes=" + godot::String::num_int64(static_cast<int>(item_result["notes"])));
        return item_result;
    };

    godot::Array scenes;
    godot::Array runtime_eligible_scene_paths;
    for (int i = 0; i < scene_paths.size(); i++) {
        godot::Variant item = scene_paths[i];
        if (item.get_type() != godot::Variant::STRING) {
            godot::Dictionary item_result;
            item_result["status"] = "failed";
            item_result["scene_path"] = "";
            item_result["error"] =
                "scene_paths[" + godot::String::num_int64(i) +
                "] must be a string";
            scenes.append(item_result);
            continue;
        }

        godot::Dictionary scene_result = validate_single_scene(item);
        if (godot::String(scene_result.get("status", "")) == "success" &&
            static_cast<int>(scene_result.get("errors", 0)) == 0) {
            runtime_eligible_scene_paths.append(scene_result.get("scene_path", ""));
        } else if (godot::String(scene_result.get("status", "")) == "success") {
            scene_result["runtime_check"] = "skipped";
            scene_result["runtime_skip_reason"] = "structural_errors";
        }
        scenes.append(scene_result);
    }

    godot::String artifact_dir =
        godot::String(args.get("_fennara_tool_artifact_dir", "")).strip_edges();
    godot::String validate_artifact_dir_res = artifact_dir.is_empty()
        ? godot::String("user://.fennara/tool_logs/manual/validate_scene")
              .path_join(make_runtime_check_id())
        : artifact_dir.path_join("validate_scene");
    godot::String validate_artifact_dir_abs = user_abs_path(validate_artifact_dir_res);
    godot::String runtime_run_dir_res = validate_artifact_dir_res.path_join("runtime");
    godot::Dictionary runtime_batch;
    if (skip_runtime) {
        runtime_batch["success"] = true;
        runtime_batch["skipped"] = true;
        runtime_batch["status"] = "skipped";
        runtime_batch["reason"] = "Runtime checks skipped by caller.";
        runtime_batch["results"] = godot::Array();
    } else {
        runtime_batch =
            run_runtime_batch(runtime_eligible_scene_paths, runtime_run_dir_res, nullptr);
    }
    runtime_batch["validate_artifact_dir_res_path"] = validate_artifact_dir_res;
    runtime_batch["validate_artifact_dir_abs_path"] = validate_artifact_dir_abs;

    return build_result_from_scenes(args, scene_paths, scenes, runtime_batch);
}

godot::String FennaraValidateSceneTool::_build_node_path(
    const godot::Ref<godot::SceneState> &state, int node_idx) {
    if (node_idx == 0) {
        return godot::String(state->get_node_name(0));
    }
    return godot::String(state->get_node_path(node_idx, false));
}

bool FennaraValidateSceneTool::_inherits_class(
    const godot::StringName &class_name,
    const godot::StringName &base_class) {
    if (class_name == base_class) return true;
    return godot::ClassDB::is_parent_class(class_name, base_class);
}

void FennaraValidateSceneTool::_add_issue(
    godot::Array &issues,
    const godot::String &node_path,
    const godot::String &check_name,
    const godot::String &severity,
    const godot::String &message,
    const godot::Dictionary &extra) {
    godot::Dictionary issue;
    issue["node"] = node_path;
    issue["node_path"] = node_path;
    issue["check"] = check_name;
    issue["severity"] = severity;
    issue["message"] = message;

    godot::Array keys = extra.keys();
    for (int i = 0; i < keys.size(); i++) {
        issue[keys[i]] = extra[keys[i]];
    }

    issues.append(issue);
}

godot::String FennaraValidateSceneTool::_get_script_path(
    const godot::Ref<godot::SceneState> &state, int node_idx) {
    int prop_count = state->get_node_property_count(node_idx);
    for (int p = 0; p < prop_count; p++) {
        godot::String prop_name =
            godot::String(state->get_node_property_name(node_idx, p));
        if (prop_name == "script") {
            godot::Variant val = state->get_node_property_value(node_idx, p);
            if (val.get_type() == godot::Variant::OBJECT) {
                godot::Object *obj = val;
                auto *res = godot::Object::cast_to<godot::Resource>(obj);
                if (res && !res->get_path().is_empty()) {
                    return res->get_path();
                }
            }
            break;
        }
    }
    return godot::String();
}

} // namespace fennara
