#include "fennara/executor.hpp"
#include "fennara/lsp/csharp_lsp.hpp"
#include "fennara/file_utils.hpp"
#include "fennara/lsp/gdscript_lsp.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>

#include <thread>
#include <vector>

namespace fennara {

namespace {

godot::String tool_name_from_call(const godot::Dictionary &tool_call) {
    if (tool_call.has("function")) {
        return godot::Dictionary(tool_call["function"]).get("name", "");
    }
    return tool_call.get("name", "");
}

godot::Dictionary tool_args_from_call(const godot::Dictionary &tool_call) {
    if (tool_call.has("function")) {
        godot::Dictionary func = tool_call["function"];
        godot::String args_str = func.get("arguments", "{}");
        godot::Variant parsed = godot::JSON::parse_string(args_str);
        if (parsed.get_type() == godot::Variant::DICTIONARY) {
            return parsed;
        }
        return godot::Dictionary();
    }
    return tool_call.get("args", godot::Dictionary());
}

void merge_per_file(godot::Dictionary &into, const godot::Dictionary &from) {
    godot::Array keys = from.keys();
    for (int i = 0; i < keys.size(); i++) {
        into[keys[i]] = from[keys[i]];
    }
}

godot::Dictionary empty_diagnostics() {
    godot::Dictionary file_result;
    file_result["diagnostics"] = godot::Array();
    file_result["total_errors"] = 0;
    file_result["total_warnings"] = 0;
    file_result["total_info"] = 0;
    file_result["total_hints"] = 0;
    file_result["total_diagnostics"] = 0;
    return file_result;
}

godot::Dictionary run_mixed_script_diagnostics(const godot::Array &files_to_check) {
    godot::Array gd_files;
    godot::Array cs_files;
    for (int i = 0; i < files_to_check.size(); i++) {
        godot::String path = files_to_check[i];
        if (path.ends_with(".gd")) {
            gd_files.append(path);
        } else if (path.ends_with(".cs")) {
            cs_files.append(path);
        }
    }

    godot::Dictionary per_file;
    if (!gd_files.is_empty()) {
        godot::Dictionary gd_result =
            gdscript_lsp::diagnose_files(gd_files, "fennara-sync-batch-diagnostics");
        if (!(bool)gd_result.get("success", false)) {
            return gd_result;
        }
        merge_per_file(per_file, gd_result.get("per_file", godot::Dictionary()));
    }

    if (!cs_files.is_empty()) {
        godot::Dictionary cs_result =
            csharp_lsp::diagnose_files(cs_files, "fennara-csharp-sync-batch-diagnostics");
        if (!(bool)cs_result.get("success", false)) {
            return cs_result;
        }
        merge_per_file(per_file, cs_result.get("per_file", godot::Dictionary()));
    }

    godot::Dictionary result;
    result["success"] = true;
    result["per_file"] = per_file;
    return result;
}

void apply_file_diagnostics(godot::Dictionary &result,
                            const godot::Dictionary &file_diag,
                            bool diagnostic_success) {
    result["diagnostics"] = file_diag.get("diagnostics", godot::Array());
    result["total_errors"] = file_diag.get("total_errors", 0);
    result["total_warnings"] = file_diag.get("total_warnings", 0);
    result["total_info"] = file_diag.get("total_info", 0);
    result["total_hints"] = file_diag.get("total_hints", 0);
    result["total_diagnostics"] = file_diag.get(
        "total_diagnostics",
        godot::Array(file_diag.get("diagnostics", godot::Array())).size());
    result["diagnostic_success"] = diagnostic_success;
    result["diagnostic_mode"] = "lsp";

    godot::Dictionary summary = result.get("summary", godot::Dictionary());
    summary["diagnostic_success"] = diagnostic_success;
    summary["diagnostic_mode"] = "lsp";
    summary["total_errors"] = result.get("total_errors", 0);
    summary["total_warnings"] = result.get("total_warnings", 0);
    summary["total_info"] = result.get("total_info", 0);
    summary["total_hints"] = result.get("total_hints", 0);
    summary["diagnostic_count"] = result.get("total_diagnostics", 0);
    result["summary"] = summary;
}

void append_sync_write_diagnostics(const godot::Array &tool_calls,
                                   godot::Array &results) {
    godot::Array files_to_check;
    godot::Dictionary index_to_path;

    for (int i = 0; i < tool_calls.size(); i++) {
        godot::String name = tool_name_from_call(tool_calls[i]);
        if (name != "write_or_update_file") {
            continue;
        }

        godot::Variant result_variant = results[i];
        if (result_variant.get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary result = result_variant;
        if (!(bool)result.get("success", false)) {
            continue;
        }

        godot::String path = result.get("file_path", tool_args_from_call(tool_calls[i]).get("file_path", ""));
        if (!(path.ends_with(".gd") || path.ends_with(".cs"))) {
            continue;
        }

        godot::String abs_path = file_utils::resolve_path(path);
        if (!godot::FileAccess::file_exists(abs_path)) {
            continue;
        }

        files_to_check.append(abs_path);
        index_to_path[i] = abs_path;
    }

    if (files_to_check.is_empty()) {
        return;
    }

    godot::Dictionary diag_result = run_mixed_script_diagnostics(files_to_check);
    bool diagnostic_success = diag_result.get("success", false);
    godot::Dictionary per_file = diag_result.get("per_file", godot::Dictionary());
    godot::Array indices = index_to_path.keys();
    for (int i = 0; i < indices.size(); i++) {
        int result_index = static_cast<int>(indices[i]);
        godot::String abs_path = index_to_path[indices[i]];
        godot::Dictionary result = results[result_index];
        godot::Dictionary file_diag =
            per_file.has(abs_path) ? godot::Dictionary(per_file[abs_path]) : empty_diagnostics();
        apply_file_diagnostics(result, file_diag, diagnostic_success);
        if (!diagnostic_success) {
            result["diagnostic_error"] =
                diag_result.get("error", "Diagnostics failed");
        }
        results[result_index] = result;
    }
}

} // namespace

godot::Array FennaraExecutor::execute_tool_calls(const godot::Array &tool_calls) {
    int count = tool_calls.size();
    godot::Array results;
    results.resize(count);

    if (count <= 1) {
        if (count == 1) {
            godot::Dictionary tc = tool_calls[0];
            results[0] = execute_tool(tool_name_from_call(tc), tool_args_from_call(tc));
            append_sync_write_diagnostics(tool_calls, results);
        }
        return results;
    }

    struct Task {
        int index;
        godot::String name;
        godot::Dictionary args;
        godot::Dictionary result;
    };

    std::vector<Task> threaded;
    std::vector<Task> main_only;

    for (int i = 0; i < count; i++) {
        godot::Dictionary tc = tool_calls[i];
        godot::String name = tool_name_from_call(tc);
        godot::Dictionary args = tool_args_from_call(tc);

        if (_is_thread_safe(name, args)) {
            threaded.push_back({i, name, args, {}});
        } else {
            main_only.push_back({i, name, args, {}});
        }
    }

    std::vector<std::thread> threads;
    threads.reserve(threaded.size());
    for (auto &task : threaded) {
        threads.emplace_back([&task]() {
            task.result = execute_tool(task.name, task.args);
        });
    }

    for (auto &task : main_only) {
        task.result = execute_tool(task.name, task.args);
        results[task.index] = task.result;
    }

    for (size_t i = 0; i < threads.size(); i++) {
        threads[i].join();
        results[threaded[i].index] = threaded[i].result;
    }

    append_sync_write_diagnostics(tool_calls, results);
    return results;
}

} // namespace fennara
