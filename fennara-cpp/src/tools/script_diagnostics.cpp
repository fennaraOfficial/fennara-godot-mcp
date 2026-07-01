#include "fennara/tools/script_diagnostics.hpp"
#include "fennara/addon_access.hpp"
#include "fennara/lsp/csharp_lsp.hpp"
#include "fennara/lsp/csharp_support.hpp"
#include "fennara/helpers.hpp"
#include "fennara/lsp/gdscript_lsp.hpp"
#include "fennara/logger.hpp"
#include "fennara/warning_capture.hpp"

#include "fennara/file_utils.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <mutex>

namespace fennara {

namespace {

constexpr int kMaxBatchFiles = 5;
constexpr int kSceneLoadScenesPerTick = 2;
constexpr uint64_t kSceneLoadBudgetMs = 8;
constexpr const char *kResultVersion = "script-diagnostics-result-v1";

std::mutex &diagnostic_logger_capture_mutex() {
    static std::mutex *mutex = new std::mutex;
    return *mutex;
}

bool is_script_path(const godot::String &path) {
    return path.ends_with(".gd") || path.ends_with(".cs");
}

godot::Dictionary make_summary(const godot::Array &files,
                               bool scan_project,
                               int checked_count,
                               int total_errors,
                               int total_warnings,
                               int total_info,
                               int total_hints) {
    int success_count = 0;
    int failure_count = 0;
    for (int i = 0; i < files.size(); i++) {
        godot::Dictionary file = files[i];
        if (godot::String(file.get("status", "")) == "success") {
            success_count++;
        } else {
            failure_count++;
        }
    }

    godot::Dictionary summary;
    summary["status"] = failure_count == 0 ? "success" :
        (success_count == 0 ? "failed" : "partial");
    summary["requested_count"] = files.size();
    summary["checked_count"] = checked_count;
    summary["success_count"] = success_count;
    summary["failure_count"] = failure_count;
    summary["total_errors"] = total_errors;
    summary["total_warnings"] = total_warnings;
    summary["total_info"] = total_info;
    summary["total_hints"] = total_hints;
    summary["total_diagnostics"] =
        total_errors + total_warnings + total_info + total_hints;
    summary["scan_project"] = scan_project;
    if (scan_project) {
        summary["scanned_file_count"] = checked_count;
    }
    return summary;
}

godot::Dictionary make_failed_file(const godot::String &path,
                                   const godot::String &error) {
    godot::Dictionary file;
    file["path"] = path;
    file["status"] = "failed";
    file["error"] = error;
    file["total_errors"] = 0;
    file["total_warnings"] = 0;
    file["total_info"] = 0;
    file["total_hints"] = 0;
    file["total_diagnostics"] = 0;
    file["diagnostics"] = godot::Array();
    return file;
}

godot::Dictionary make_blocked_file(const godot::Dictionary &blocked) {
    godot::Dictionary file = make_failed_file(
        blocked.get("blocked_path", ""),
        blocked.get("error", "Path is blocked."));
    file["status"] = "blocked";
    file["block_reason"] = blocked.get("block_reason", "");
    file["blocked_path"] = blocked.get("blocked_path", "");
    file["blocked_addon_root"] = blocked.get("blocked_addon_root", "");
    if (blocked.has("resolution")) {
        file["resolution"] = blocked["resolution"];
    }
    return file;
}

godot::Dictionary make_argument_error(const godot::String &message) {
    godot::Dictionary result;
    godot::Array files;
    result["success"] = false;
    result["tool_name"] = "script_diagnostics";
    result["format_version"] = kResultVersion;
    result["scan_project"] = false;
    result["summary"] = make_summary(files, false, 0, 0, 0, 0, 0);
    result["files"] = files;
    result["error"] = message;
    return result;
}

godot::Dictionary make_success_file(const godot::String &path,
                                    const godot::Dictionary &file_result) {
    if (godot::String(file_result.get("status", "")) == "failed") {
        return make_failed_file(
            path,
            file_result.get("error", "Diagnostics failed"));
    }

    int total_errors = static_cast<int>(file_result.get("total_errors", 0));
    int total_warnings = static_cast<int>(file_result.get("total_warnings", 0));
    int total_info = static_cast<int>(file_result.get("total_info", 0));
    int total_hints = static_cast<int>(file_result.get("total_hints", 0));

    godot::Dictionary file;
    file["path"] = path;
    file["status"] = "success";
    file["total_errors"] = total_errors;
    file["total_warnings"] = total_warnings;
    file["total_info"] = total_info;
    file["total_hints"] = total_hints;
    file["total_diagnostics"] =
        total_errors + total_warnings + total_info + total_hints;
    file["diagnostics"] = file_result.get("diagnostics", godot::Array());
    return file;
}

godot::Dictionary empty_file_result() {
    godot::Dictionary result;
    result["diagnostics"] = godot::Array();
    result["total_errors"] = 0;
    result["total_warnings"] = 0;
    result["total_info"] = 0;
    result["total_hints"] = 0;
    result["total_diagnostics"] = 0;
    return result;
}

void append_diagnostic_to_file_result(godot::Dictionary &file_result,
                                      const godot::Dictionary &diagnostic) {
    godot::Array diagnostics =
        file_result.get("diagnostics", godot::Array());
    diagnostics.append(diagnostic);
    file_result["diagnostics"] = diagnostics;

    godot::String severity = diagnostic.get("severity", "");
    if (severity == "error") {
        file_result["total_errors"] =
            static_cast<int>(file_result.get("total_errors", 0)) + 1;
    } else if (severity == "warning") {
        file_result["total_warnings"] =
            static_cast<int>(file_result.get("total_warnings", 0)) + 1;
    } else if (severity == "hint") {
        file_result["total_hints"] =
            static_cast<int>(file_result.get("total_hints", 0)) + 1;
    } else {
        file_result["total_info"] =
            static_cast<int>(file_result.get("total_info", 0)) + 1;
    }
    file_result["total_diagnostics"] =
        static_cast<int>(file_result.get("total_errors", 0)) +
        static_cast<int>(file_result.get("total_warnings", 0)) +
        static_cast<int>(file_result.get("total_info", 0)) +
        static_cast<int>(file_result.get("total_hints", 0));
}

godot::String normalize_script_path_for_match(const godot::String &path) {
    if (path.is_empty()) {
        return "";
    }
    if (path.begins_with("res://")) {
        return godot::ProjectSettings::get_singleton()->globalize_path(path)
            .replace("\\", "/");
    }
    if (path.is_absolute_path()) {
        return path.replace("\\", "/");
    }
    return file_utils::resolve_path(path).replace("\\", "/");
}

void collect_scene_paths_recursive(const godot::String &dir_path,
                                   godot::Array &scene_paths) {
    godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(dir_path);
    if (!dir.is_valid()) {
        return;
    }

    godot::String addon_root = addon_access::addon_root_for_path(dir_path);
    if (!addon_root.is_empty() &&
        !addon_access::is_addon_root_allowed(addon_root)) {
        return;
    }

    dir->list_dir_begin();
    while (true) {
        godot::String name = dir->get_next();
        if (name.is_empty()) {
            break;
        }
        if (name.begins_with(".")) {
            continue;
        }

        godot::String path = dir_path.path_join(name);
        if (dir->current_is_dir()) {
            collect_scene_paths_recursive(path, scene_paths);
        } else if (name.ends_with(".tscn")) {
            scene_paths.append(path);
        }
    }
    dir->list_dir_end();
}

godot::String extract_script_path_from_message(const godot::String &message,
                                               int &line) {
    int res_pos = message.find("res://");
    if (res_pos < 0) {
        return "";
    }

    int gd_pos = message.find(".gd:", res_pos);
    int cs_pos = message.find(".cs:", res_pos);
    int ext_pos = -1;
    int ext_len = 0;
    if (gd_pos >= 0 && (cs_pos < 0 || gd_pos < cs_pos)) {
        ext_pos = gd_pos;
        ext_len = 3;
    } else if (cs_pos >= 0) {
        ext_pos = cs_pos;
        ext_len = 3;
    }
    if (ext_pos < 0) {
        return "";
    }

    godot::String script_path = message.substr(
        res_pos, ext_pos + ext_len - res_pos);
    int line_start = ext_pos + ext_len + 1;
    int line_end = line_start;
    while (line_end < message.length()) {
        char32_t ch = message[line_end];
        if (ch < '0' || ch > '9') {
            break;
        }
        line_end++;
    }
    if (line_end > line_start) {
        line = message.substr(line_start, line_end - line_start).to_int();
    }
    return script_path;
}

godot::Dictionary script_origin_from_capture(const godot::Dictionary &entry) {
    godot::Dictionary origin;

    if (entry.has("script_backtrace") &&
        entry["script_backtrace"].get_type() == godot::Variant::ARRAY) {
        godot::Array frames = entry["script_backtrace"];
        for (int i = 0; i < frames.size(); i++) {
            if (frames[i].get_type() != godot::Variant::DICTIONARY) {
                continue;
            }
            godot::Dictionary frame = frames[i];
            godot::String file = frame.get("file", "");
            if (!is_script_path(file)) {
                continue;
            }
            origin["file"] = file;
            origin["line"] = frame.get("line", 0);
            origin["function"] = frame.get("function", "");
            origin["source"] = "script_backtrace";
            return origin;
        }
    }

    godot::String file = entry.get("file", "");
    if (is_script_path(file)) {
        origin["file"] = file;
        origin["line"] = entry.get("line", 0);
        origin["function"] = entry.get("function", "");
        origin["source"] = "logger_file";
        return origin;
    }

    int line = 0;
    file = extract_script_path_from_message(entry.get("message", ""), line);
    if (!file.is_empty()) {
        origin["file"] = file;
        origin["line"] = line;
        origin["function"] = entry.get("function", "");
        origin["source"] = "message";
    }
    return origin;
}

godot::Dictionary diagnose_shader_file(const godot::String &resolved_path,
                                       const godot::String &display_path);

godot::Dictionary requested_paths_from_valid_requests(
    const godot::Array &valid_requests) {
    godot::Dictionary requested_paths;
    for (int i = 0; i < valid_requests.size(); i++) {
        godot::Dictionary request = valid_requests[i];
        godot::String language = request.get("language", "");
        if (language != "gdscript" && language != "csharp") {
            continue;
        }
        godot::String resolved_path = request.get("resolved_path", "");
        requested_paths[normalize_script_path_for_match(resolved_path)] = true;
    }
    return requested_paths;
}

godot::Dictionary scene_load_skip_summary() {
    godot::Dictionary scene_summary;
    scene_summary["scene_load_enabled"] = false;
    scene_summary["scene_load_skipped"] = true;
    scene_summary["scene_load_skip_reason"] =
        "Skipped for scan_project:true to avoid instantiating every project scene in the open editor.";
    scene_summary["scene_load_may_miss"] =
        "Script errors that only occur when scripts are attached to or loaded through scenes may be missed, including missing unique nodes used by %Name, invalid scene NodePath wiring, broken exported scene/resource assignments, and script initialization side effects.";
    scene_summary["scene_load_scene_count"] = 0;
    scene_summary["scene_load_diagnostic_count"] = 0;
    return scene_summary;
}

int append_scene_load_diagnostics_for_scene(
    const godot::String &scene_path,
    const godot::Dictionary &requested_paths,
    godot::Dictionary &per_file) {
    godot::Node *root = nullptr;
    godot::Array captured;
    {
        std::lock_guard<std::mutex> lock(diagnostic_logger_capture_mutex());
        godot::Ref<FennaraWarningCapture> capture;
        capture.instantiate();
        godot::OS *os = godot::OS::get_singleton();
        if (os != nullptr) {
            os->add_logger(capture);
        }

        godot::Ref<godot::PackedScene> packed =
            godot::ResourceLoader::get_singleton()->load(
                scene_path, "PackedScene",
                godot::ResourceLoader::CACHE_MODE_IGNORE);
        if (packed.is_valid()) {
            root = packed->instantiate(godot::PackedScene::GEN_EDIT_STATE_MAIN);
        }

        if (os != nullptr) {
            os->remove_logger(capture);
        }
        captured = capture->get_captured();
    }

    if (root != nullptr) {
        root->queue_free();
    }

    int diagnostic_count = 0;
    for (int entry_idx = 0; entry_idx < captured.size(); entry_idx++) {
        if (captured[entry_idx].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary entry = captured[entry_idx];
        godot::String type = entry.get("type", "");
        if (type != "script_error" && type != "error" &&
            type != "warning") {
            continue;
        }

        godot::Dictionary origin = script_origin_from_capture(entry);
        godot::String script_file = origin.get("file", "");
        godot::String match_path =
            normalize_script_path_for_match(script_file);
        if (match_path.is_empty() || !requested_paths.has(match_path)) {
            continue;
        }

        godot::Dictionary diagnostic;
        diagnostic["severity"] = type == "warning" ? "warning" : "error";
        diagnostic["message"] = entry.get("message", "");
        diagnostic["file"] = script_file;
        diagnostic["line"] = origin.get("line", 0);
        diagnostic["column"] = 0;
        diagnostic["source"] = "scene_load";
        diagnostic["scene_path"] = scene_path;
        diagnostic["origin"] = origin.get("source", "");
        diagnostic["captured_type"] = type;
        diagnostic["captured_file"] = entry.get("file", "");
        diagnostic["captured_line"] = entry.get("line", 0);
        diagnostic["captured_function"] = entry.get("function", "");
        if (entry.has("script_backtrace")) {
            diagnostic["script_backtrace"] = entry["script_backtrace"];
        }

        godot::Array diagnostics = per_file.get(match_path, godot::Array());
        diagnostics.append(diagnostic);
        per_file[match_path] = diagnostics;
        diagnostic_count++;
    }

    return diagnostic_count;
}

godot::Dictionary build_script_diagnostics_result(
    const godot::Array &valid_requests,
    const godot::Array &initial_item_results,
    const godot::Dictionary &per_file,
    const godot::Dictionary &language_errors,
    const godot::Dictionary &scene_load_per_file,
    const godot::Dictionary &scene_load_summary,
    bool scan_project) {
    godot::Array item_results = initial_item_results.duplicate();
    int total_errors = 0;
    int total_warnings = 0;
    int total_info = 0;
    int total_hints = 0;
    int total_diagnostics = 0;
    int checked_count = 0;

    for (int i = 0; i < valid_requests.size(); i++) {
        godot::Dictionary request = valid_requests[i];
        godot::String file_path = request["file_path"];
        godot::String resolved_path = request["resolved_path"];
        godot::String language = request.get("language", "");

        if (language_errors.has(language)) {
            item_results.append(make_failed_file(
                file_path,
                language_errors.get(language, "Diagnostics failed")));
            continue;
        }

        godot::Dictionary file_result =
            per_file.get(resolved_path, empty_file_result());
        godot::String match_path =
            normalize_script_path_for_match(resolved_path);
        if (scene_load_per_file.has(match_path)) {
            godot::Array scene_diagnostics = scene_load_per_file[match_path];
            for (int scene_diag_idx = 0;
                 scene_diag_idx < scene_diagnostics.size();
                 scene_diag_idx++) {
                if (scene_diagnostics[scene_diag_idx].get_type() !=
                    godot::Variant::DICTIONARY) {
                    continue;
                }
                godot::Dictionary diagnostic =
                    scene_diagnostics[scene_diag_idx];
                diagnostic["file"] = file_path;
                append_diagnostic_to_file_result(file_result, diagnostic);
            }
        }
        int file_errors = static_cast<int>(file_result.get("total_errors", 0));
        int file_warnings =
            static_cast<int>(file_result.get("total_warnings", 0));
        int file_info = static_cast<int>(file_result.get("total_info", 0));
        int file_hints = static_cast<int>(file_result.get("total_hints", 0));
        int diagnostic_count =
            file_errors + file_warnings + file_info + file_hints;
        total_errors += file_errors;
        total_warnings += file_warnings;
        total_info += file_info;
        total_hints += file_hints;
        total_diagnostics += diagnostic_count;

        item_results.append(make_success_file(file_path, file_result));
        checked_count++;
    }

    FLOG_TOOL(godot::String("Diag: complete, files=") +
              godot::String::num_int64(checked_count) +
              " errors=" + godot::String::num_int64(total_errors) +
              " warnings=" + godot::String::num_int64(total_warnings) +
              " issues=" + godot::String::num_int64(total_diagnostics));
    godot::Dictionary summary = make_summary(
        item_results,
        scan_project,
        checked_count,
        total_errors,
        total_warnings,
        total_info,
        total_hints
    );
    godot::Array scene_summary_keys = scene_load_summary.keys();
    for (int i = 0; i < scene_summary_keys.size(); i++) {
        summary[scene_summary_keys[i]] =
            scene_load_summary[scene_summary_keys[i]];
    }

    godot::Dictionary result;
    result["success"] = int(summary["failure_count"]) == 0;
    result["tool_name"] = "script_diagnostics";
    result["format_version"] = kResultVersion;
    result["summary"] = summary;
    result["files"] = item_results;
    result["scan_project"] = scan_project;
    if (!(bool)result["success"]) {
        result["error"] = "One or more files could not be checked.";
    }
    return result;
}

bool is_shader_wrapper_error(const godot::String &message) {
    return message.find("Shader compilation failed") != -1 ||
           message.find("Method/function failed") != -1;
}

godot::Dictionary diagnose_shader_file(const godot::String &resolved_path,
                                       const godot::String &display_path) {
    godot::String content = file_utils::read_file_content(resolved_path);

    godot::Array captured;
    {
        std::lock_guard<std::mutex> lock(diagnostic_logger_capture_mutex());
        godot::Ref<FennaraWarningCapture> capture;
        capture.instantiate();
        godot::OS *os = godot::OS::get_singleton();
        if (os != nullptr) {
            os->add_logger(capture);
        }

        godot::Ref<godot::Shader> shader;
        shader.instantiate();
        shader->set_code(content);
        godot::RID shader_rid = shader->get_rid();
        (void)shader_rid;
        godot::List<godot::PropertyInfo> shader_params;
        shader->get_shader_uniform_list(&shader_params);

        if (os != nullptr) {
            os->remove_logger(capture);
        }
        captured = capture->get_captured();
    }
    godot::Array diagnostics;
    int total_errors = 0;
    int total_warnings = 0;
    bool has_actionable_error = false;

    for (int i = 0; i < captured.size(); i++) {
        if (captured[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary entry = captured[i];
        godot::String type = entry.get("type", "");
        if (type != "shader_error" && type != "error" && type != "warning") {
            continue;
        }
        godot::String message = entry.get("message", "");
        if (!is_shader_wrapper_error(message) && type != "warning") {
            has_actionable_error = true;
        }
    }

    for (int i = 0; i < captured.size(); i++) {
        if (captured[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary entry = captured[i];
        godot::String type = entry.get("type", "");
        if (type != "shader_error" && type != "error" && type != "warning") {
            continue;
        }

        godot::String message = entry.get("message", "");
        if (has_actionable_error && is_shader_wrapper_error(message)) {
            continue;
        }

        godot::String severity = type == "warning" ? "warning" : "error";
        godot::Dictionary diagnostic;
        diagnostic["severity"] = severity;
        diagnostic["message"] = message;
        diagnostic["file"] = display_path;
        diagnostic["line"] = entry.get("line", 0);
        diagnostic["column"] = 0;
        diagnostic["source"] = "shader_parser";
        diagnostic["type"] = type;
        diagnostics.append(diagnostic);

        if (severity == "warning") {
            total_warnings++;
        } else {
            total_errors++;
        }
    }

    godot::Dictionary result;
    result["diagnostics"] = diagnostics;
    result["total_errors"] = total_errors;
    result["total_warnings"] = total_warnings;
    result["total_info"] = 0;
    result["total_hints"] = 0;
    result["total_diagnostics"] = diagnostics.size();
    result["diagnostic_mode"] = "shader_parser";
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// Bind
// ---------------------------------------------------------------------------

void FennaraScriptDiagnosticsTool::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("execute", "args"),
                                &FennaraScriptDiagnosticsTool::execute);
    godot::ClassDB::bind_method(godot::D_METHOD("cancel"),
                                &FennaraScriptDiagnosticsTool::cancel);
    godot::ClassDB::bind_method(godot::D_METHOD("get_result"),
                                &FennaraScriptDiagnosticsTool::get_result);
    godot::ClassDB::bind_method(godot::D_METHOD("is_finished"),
                                &FennaraScriptDiagnosticsTool::is_finished);

    godot::ClassDB::bind_method(godot::D_METHOD("_worker"),
                                &FennaraScriptDiagnosticsTool::_worker);
    godot::ClassDB::bind_method(godot::D_METHOD("_finish_on_main_thread"),
                                &FennaraScriptDiagnosticsTool::_finish_on_main_thread);
    godot::ClassDB::bind_method(godot::D_METHOD("_process_scene_load_diagnostics"),
                                &FennaraScriptDiagnosticsTool::_process_scene_load_diagnostics);
    godot::ClassDB::bind_method(godot::D_METHOD("_on_complete"),
                                &FennaraScriptDiagnosticsTool::_on_complete);

    ADD_SIGNAL(godot::MethodInfo("complete",
        godot::PropertyInfo(godot::Variant::DICTIONARY, "result")));
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void FennaraScriptDiagnosticsTool::execute(const godot::Dictionary &args) {
    _cancelled.store(false);
    _args = args;
    _finished = false;
    _result = godot::Dictionary();

    _mutex.instantiate();
    _thread.instantiate();
    _thread->start(callable_mp(this, &FennaraScriptDiagnosticsTool::_worker));
}

void FennaraScriptDiagnosticsTool::cancel() {
    _cancelled.store(true);
}

godot::Dictionary FennaraScriptDiagnosticsTool::get_result() const {
    return _result;
}

bool FennaraScriptDiagnosticsTool::is_finished() const {
    return _finished;
}

FennaraScriptDiagnosticsTool::~FennaraScriptDiagnosticsTool() {
    cancel();
    if (_thread.is_valid() && _thread->is_started()) {
        _thread->wait_to_finish();
    }
}

bool FennaraScriptDiagnosticsTool::_is_cancelled() const {
    return _cancelled.load();
}

// ---------------------------------------------------------------------------
// Main-thread callback
// ---------------------------------------------------------------------------

void FennaraScriptDiagnosticsTool::_finish_with_result(
    const godot::Dictionary &result) {
    if (_is_cancelled()) {
        return;
    }

    _mutex->lock();
    _result = result;
    _finished = true;
    _pending_state = godot::Dictionary();
    _scene_load_requested_paths = godot::Dictionary();
    _scene_load_per_file = godot::Dictionary();
    _scene_load_summary = godot::Dictionary();
    _scene_load_scene_paths = godot::Array();
    _scene_load_index = 0;
    _mutex->unlock();

    call_deferred("_on_complete");
}

void FennaraScriptDiagnosticsTool::_finish_on_main_thread() {
    if (_is_cancelled()) {
        return;
    }

    godot::Dictionary state;
    {
        _mutex->lock();
        state = _pending_state;
        _mutex->unlock();
    }

    if (state.is_empty()) {
        _finish_with_result(make_argument_error("Diagnostics state was empty."));
        return;
    }

    bool scan_project = state.get("scan_project", false);
    godot::Array valid_requests = state.get("valid_requests", godot::Array());
    godot::Array item_results = state.get("item_results", godot::Array());
    godot::Dictionary per_file = state.get("per_file", godot::Dictionary());
    godot::Dictionary language_errors =
        state.get("language_errors", godot::Dictionary());
    if (scan_project) {
        godot::Dictionary result = build_script_diagnostics_result(
            valid_requests,
            item_results,
            per_file,
            language_errors,
            godot::Dictionary(),
            scene_load_skip_summary(),
            scan_project);
        _finish_with_result(result);
        return;
    }

    _scene_load_requested_paths =
        requested_paths_from_valid_requests(valid_requests);
    _scene_load_per_file = godot::Dictionary();
    _scene_load_summary = godot::Dictionary();
    _scene_load_summary["scene_load_enabled"] =
        !_scene_load_requested_paths.is_empty();
    _scene_load_summary["scene_load_skipped"] = false;
    _scene_load_summary["scene_load_scene_count"] = 0;
    _scene_load_summary["scene_load_diagnostic_count"] = 0;
    _scene_load_scene_paths = godot::Array();
    _scene_load_index = 0;

    if (_scene_load_requested_paths.is_empty()) {
        godot::Dictionary result = build_script_diagnostics_result(
            valid_requests,
            item_results,
            per_file,
            language_errors,
            _scene_load_per_file,
            _scene_load_summary,
            scan_project);
        _finish_with_result(result);
        return;
    }

    _scene_load_scene_paths = state.get("scene_paths", godot::Array());
    _scene_load_summary["scene_load_scene_count"] = _scene_load_scene_paths.size();
    _process_scene_load_diagnostics();
}

void FennaraScriptDiagnosticsTool::_process_scene_load_diagnostics() {
    if (_is_cancelled()) {
        return;
    }

    godot::Dictionary state;
    {
        _mutex->lock();
        state = _pending_state;
        _mutex->unlock();
    }

    if (state.is_empty()) {
        _finish_with_result(make_argument_error("Diagnostics state was empty."));
        return;
    }

    godot::Time *time = godot::Time::get_singleton();
    uint64_t start_ms = time == nullptr
        ? 0
        : static_cast<uint64_t>(time->get_ticks_msec());
    int processed = 0;
    int diagnostic_count = static_cast<int>(
        _scene_load_summary.get("scene_load_diagnostic_count", 0));

    while (_scene_load_index < _scene_load_scene_paths.size()) {
        if (_is_cancelled()) {
            return;
        }

        diagnostic_count += append_scene_load_diagnostics_for_scene(
            _scene_load_scene_paths[_scene_load_index],
            _scene_load_requested_paths,
            _scene_load_per_file);
        _scene_load_index++;
        processed++;

        if (processed >= kSceneLoadScenesPerTick) {
            break;
        }
        if (time != nullptr) {
            uint64_t elapsed = static_cast<uint64_t>(time->get_ticks_msec()) - start_ms;
            if (elapsed >= kSceneLoadBudgetMs) {
                break;
            }
        }
    }

    _scene_load_summary["scene_load_diagnostic_count"] = diagnostic_count;

    if (_is_cancelled()) {
        return;
    }

    if (_scene_load_index < _scene_load_scene_paths.size()) {
        godot::Engine *engine = godot::Engine::get_singleton();
        godot::SceneTree *tree = nullptr;
        if (engine != nullptr) {
            tree = godot::Object::cast_to<godot::SceneTree>(engine->get_main_loop());
        }
        if (tree != nullptr) {
            godot::Ref<godot::SceneTreeTimer> timer = tree->create_timer(0.01);
            timer->connect("timeout", callable_mp(
                this, &FennaraScriptDiagnosticsTool::_process_scene_load_diagnostics));
        } else {
            call_deferred("_process_scene_load_diagnostics");
        }
        return;
    }

    godot::Array valid_requests = state.get("valid_requests", godot::Array());
    godot::Array item_results = state.get("item_results", godot::Array());
    godot::Dictionary per_file = state.get("per_file", godot::Dictionary());
    godot::Dictionary language_errors =
        state.get("language_errors", godot::Dictionary());
    godot::Dictionary result = build_script_diagnostics_result(
        valid_requests,
        item_results,
        per_file,
        language_errors,
        _scene_load_per_file,
        _scene_load_summary,
        state.get("scan_project", false));
    _finish_with_result(result);
}

void FennaraScriptDiagnosticsTool::_on_complete() {
    if (_is_cancelled()) {
        return;
    }
    emit_signal("complete", _result);
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------

void FennaraScriptDiagnosticsTool::_worker() {
    if (_is_cancelled()) {
        return;
    }

    godot::Dictionary result;
    godot::Variant file_paths_var;
    godot::Array file_paths;
    godot::Array gd_files_to_check;
    godot::Array cs_files_to_check;
    godot::Array valid_requests;
    godot::Array item_results;
    bool scan_project = _args.get("scan_project", false);

    if (scan_project) {
        file_paths = file_utils::find_all_diagnostic_files();
        if (file_paths.is_empty()) {
            result = make_argument_error("No .gd, .cs, or .gdshader files found under res://");
            result["scan_project"] = true;
            goto done;
        }
    } else if (!_args.has("file_paths")) {
        result = make_argument_error("Missing required arg: file_paths (or set scan_project: true)");
        goto done;
    } else {
        file_paths_var = _args["file_paths"];
        if (file_paths_var.get_type() != godot::Variant::ARRAY) {
            result = make_argument_error("file_paths must be an array of strings");
            goto done;
        }

        file_paths = file_paths_var;
        if (file_paths.is_empty()) {
            result = make_argument_error("file_paths must contain at least one path");
            goto done;
        }
        if (file_paths.size() > kMaxBatchFiles) {
            result = make_argument_error(
                "file_paths supports at most " +
                godot::String::num_int64(kMaxBatchFiles) +
                " files per targeted call. Use scan_project: true for a whole-project scan."
            );
            goto done;
        }
    }

    FLOG_TOOL(godot::String("Diag: starting for ") +
              godot::String::num_int64(file_paths.size()) + " file(s)");

    for (int i = 0; i < file_paths.size(); i++) {
        godot::Variant item = file_paths[i];
        if (_is_cancelled()) {
            return;
        }

        if (item.get_type() != godot::Variant::STRING) {
            item_results.append(make_failed_file(
                "file_paths[" + godot::String::num_int64(i) + "]",
                "file_paths[" + godot::String::num_int64(i) +
                "] must be a string"
            ));
            continue;
        }

        godot::String file_path = item;
        godot::String resolved =
            scan_project ? file_path : file_utils::resolve_path(file_path);
        godot::Dictionary addon_block;
        if (!addon_access::is_path_allowed(
                scan_project ? file_utils::uri_to_res_path(file_utils::path_to_uri(resolved)) : file_path,
                false,
                addon_block)) {
            item_results.append(make_blocked_file(addon_block));
            continue;
        }
        if (!godot::FileAccess::file_exists(resolved)) {
            item_results.append(make_failed_file(file_path, "File not found: " + file_path));
            continue;
        }

        if (!resolved.ends_with(".gd") && !resolved.ends_with(".cs") &&
            !resolved.ends_with(".gdshader")) {
            item_results.append(make_failed_file(
                file_path,
                "script_diagnostics supports .gd, .cs, and .gdshader files."));
            continue;
        }

        if (resolved.ends_with(".cs")) {
            cs_files_to_check.append(resolved);
        } else if (resolved.ends_with(".gd")) {
            gd_files_to_check.append(resolved);
        }
        godot::Dictionary request;
        request["file_path"] = scan_project ? file_utils::uri_to_res_path(file_utils::path_to_uri(resolved)) : file_path;
        request["resolved_path"] = resolved;
        request["language"] = resolved.ends_with(".cs") ? "csharp" :
            (resolved.ends_with(".gdshader") ? "gdshader" : "gdscript");
        valid_requests.append(request);
    }

    if (valid_requests.is_empty()) {
        godot::Dictionary summary = make_summary(item_results, scan_project, 0, 0, 0, 0, 0);
        result["success"] = false;
        result["tool_name"] = "script_diagnostics";
        result["format_version"] = kResultVersion;
        result["scan_project"] = scan_project;
        result["summary"] = summary;
        result["files"] = item_results;
        result["error"] = "No valid files to check.";
        goto done;
    }

    {
        godot::Dictionary per_file;
        godot::Dictionary language_errors;

        if (!gd_files_to_check.is_empty()) {
            if (_is_cancelled()) {
                return;
            }

            godot::Dictionary gd_diag_result =
                gdscript_lsp::diagnose_files(gd_files_to_check, "fennara-diagnostics");
            if (!(bool)gd_diag_result.get("success", false)) {
                godot::String diag_error =
                    gd_diag_result.get("error", "GDScript diagnostics failed");
                language_errors["gdscript"] = diag_error;
                FLOG_ERR(godot::String("Diag: GDScript LSP diagnostics failed: ") + diag_error);
            } else {
                godot::Dictionary gd_per_file =
                    gd_diag_result.get("per_file", godot::Dictionary());
                godot::Array keys = gd_per_file.keys();
                for (int i = 0; i < keys.size(); i++) {
                    per_file[keys[i]] = gd_per_file[keys[i]];
                }
            }
        }

        if (!cs_files_to_check.is_empty()) {
            if (_is_cancelled()) {
                return;
            }

            godot::Dictionary cs_diag_result =
                csharp_lsp::diagnose_files(cs_files_to_check, "fennara-csharp-diagnostics");
            if (!(bool)cs_diag_result.get("success", false)) {
                godot::String diag_error =
                    cs_diag_result.get("error", "C# diagnostics failed");
                language_errors["csharp"] = diag_error;
                FLOG_ERR(godot::String("Diag: C# LSP diagnostics failed: ") + diag_error);
            } else {
                godot::Dictionary cs_per_file =
                    cs_diag_result.get("per_file", godot::Dictionary());
                godot::Array keys = cs_per_file.keys();
                for (int i = 0; i < keys.size(); i++) {
                    per_file[keys[i]] = cs_per_file[keys[i]];
                }
            }
        }

        for (int i = 0; i < valid_requests.size(); i++) {
            if (_is_cancelled()) {
                return;
            }

            godot::Dictionary request = valid_requests[i];
            godot::String language = request.get("language", "");
            if (language != "gdshader") {
                continue;
            }

            godot::String resolved_path = request["resolved_path"];
            godot::String display_path = request["file_path"];
            per_file[resolved_path] =
                diagnose_shader_file(resolved_path, display_path);
        }

        godot::Array scene_paths;
        if (!scan_project &&
            !requested_paths_from_valid_requests(valid_requests).is_empty()) {
            collect_scene_paths_recursive("res://", scene_paths);
        }

        godot::Dictionary state;
        state["valid_requests"] = valid_requests;
        state["item_results"] = item_results;
        state["per_file"] = per_file;
        state["language_errors"] = language_errors;
        state["scene_paths"] = scene_paths;
        state["scan_project"] = scan_project;

        _mutex->lock();
        _pending_state = state;
        _mutex->unlock();

        if (!_is_cancelled()) {
            call_deferred("_finish_on_main_thread");
        }
        return;
    }

done:
    if (_is_cancelled()) {
        return;
    }

    _mutex->lock();
    _result = result;
    _finished = true;
    _mutex->unlock();

    call_deferred("_on_complete");
}

} // namespace fennara
