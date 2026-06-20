#include "fennara/tools/file_ops/file_ops.hpp"
#include "fennara/logger.hpp"

#include "fennara/tools/file_ops/copy.hpp"
#include "fennara/tools/file_ops/create_dir.hpp"
#include "fennara/tools/file_ops/delete_op.hpp"
#include "fennara/tools/file_ops/glob.hpp"
#include "fennara/tools/file_ops/grep.hpp"
#include "fennara/tools/file_ops/list.hpp"
#include "fennara/tools/file_ops/move.hpp"

#include <future>
#include <vector>

namespace fennara {

namespace {

struct FileOpsOperationExecution {
    godot::Dictionary result;
    godot::Dictionary request;
    godot::Array warnings;
    godot::Array errors;
};

static bool is_read_only_operation(const godot::String &operation) {
    return operation == "list" || operation == "glob" || operation == "rg";
}

static FileOpsOperationExecution execute_operation(const godot::Dictionary &op) {
    FileOpsOperationExecution execution;
    execution.request = op;

    if (!op.has("operation")) {
        execution.result["success"] = false;
        execution.result["error"] = "Operation missing 'operation' field";
        execution.errors.append("Operation missing 'operation' field");
        return execution;
    }

    godot::String operation = op["operation"];
    godot::String op_path = op.get(
        "path", op.get("source", op.get("destination", op.get("pattern", ""))));
    FLOG_TOOL(godot::String("FO: op=") + operation + " path=" +
              godot::String(op_path));

    if (operation == "copy") {
        execution.result =
            file_ops::copy(op, execution.warnings, execution.errors);
    } else if (operation == "move") {
        execution.result =
            file_ops::move(op, execution.warnings, execution.errors);
    } else if (operation == "delete") {
        execution.result =
            file_ops::delete_op(op, execution.warnings, execution.errors);
    } else if (operation == "create_dir") {
        execution.result =
            file_ops::create_dir(op, execution.warnings, execution.errors);
    } else if (operation == "list") {
        execution.result =
            file_ops::list(op, execution.warnings, execution.errors);
    } else if (operation == "glob") {
        execution.result =
            file_ops::glob(op, execution.warnings, execution.errors);
    } else if (operation == "rg") {
        execution.result = file_ops::rg(op, execution.warnings,
                                            execution.errors);
    } else {
        godot::String msg = godot::String("Unknown operation: ") + operation;
        execution.errors.append(msg);
        execution.result["success"] = false;
        execution.result["error"] = msg;
    }

    if (!(bool)execution.result.get("success", false)) {
        godot::String op_err = execution.result.get("error", "unknown");
        FLOG_ERR(godot::String("FO: op=") + operation + " failed: " + op_err);
    }

    return execution;
}

static godot::Array args_for_operation(const godot::Dictionary &op) {
    godot::Variant args_var = op.get("args", godot::Variant());
    if (args_var.get_type() == godot::Variant::ARRAY) {
        return args_var;
    }
    return godot::Array();
}

static godot::Dictionary normalize_operation_result(
    const FileOpsOperationExecution &execution,
    int index) {
    godot::Dictionary op_result;
    godot::String operation = execution.request.get(
        "operation", execution.result.get("op", ""));
    bool success = execution.result.get("success", false);

    op_result["index"] = index;
    op_result["operation"] = operation;
    op_result["status"] = execution.result.get("status", success ? "success" : "failed");
    op_result["args"] = args_for_operation(execution.request);
    op_result["result"] = execution.result;
    op_result["warnings"] = execution.warnings;
    op_result["errors"] = execution.errors;

    godot::String source = execution.result.get("from", execution.result.get("source", ""));
    godot::String destination = execution.result.get("to", execution.result.get("destination", ""));
    godot::String path = execution.result.get("path", "");
    if (!source.is_empty()) op_result["source"] = source;
    if (!destination.is_empty()) op_result["destination"] = destination;
    if (!path.is_empty()) op_result["path"] = path;

    if (execution.result.has("count")) op_result["count"] = execution.result["count"];
    if (execution.result.has("total_items")) op_result["total_items"] = execution.result["total_items"];
    if (execution.result.has("line_count")) op_result["line_count"] = execution.result["line_count"];
    if (execution.result.has("error")) op_result["error"] = execution.result["error"];
    if (execution.result.has("block_reason")) op_result["block_reason"] = execution.result["block_reason"];
    if (execution.result.has("blocked_path")) op_result["blocked_path"] = execution.result["blocked_path"];
    if (execution.result.has("blocked_addon_root")) op_result["blocked_addon_root"] = execution.result["blocked_addon_root"];
    if (execution.result.has("resolution")) op_result["resolution"] = execution.result["resolution"];
    return op_result;
}

static void append_execution(const FileOpsOperationExecution &execution,
                             godot::Array &operations, godot::Array &warnings,
                             godot::Array &errors, bool &overall_success,
                             int index) {
    operations.append(normalize_operation_result(execution, index));

    for (int i = 0; i < execution.warnings.size(); i++) {
        warnings.append(execution.warnings[i]);
    }

    for (int i = 0; i < execution.errors.size(); i++) {
        errors.append(execution.errors[i]);
    }

    if (!(bool)execution.result.get("success", false)) {
        overall_success = false;
    }
}

} // namespace

void FennaraFileOpsTool::_bind_methods() {
    godot::ClassDB::bind_static_method(
        "FennaraFileOpsTool", godot::D_METHOD("execute", "args"),
        &FennaraFileOpsTool::execute);
}

godot::Dictionary FennaraFileOpsTool::execute(const godot::Dictionary &args) {
    godot::Dictionary result;

    if (!args.has("operations")) {
        result["success"] = false;
        result["tool_name"] = "file_ops";
        result["format_version"] = "file-ops-result-v1";
        result["error"] = "Missing required parameter: operations";
        return result;
    }

    godot::Array operations = args["operations"];
    if (operations.is_empty()) {
        result["success"] = false;
        result["tool_name"] = "file_ops";
        result["format_version"] = "file-ops-result-v1";
        result["error"] = "No operations specified";
        return result;
    }

    godot::Array operation_results;
    godot::Array warnings;
    godot::Array errors;
    bool overall_success = true;
    bool parallel = args.get("parallel", true);

    for (int i = 0; i < operations.size(); i++) {
        godot::Dictionary op = operations[i];
        if (!op.has("operation")) {
            append_execution(execute_operation(op), operation_results, warnings,
                             errors, overall_success, i);
            continue;
        }

        godot::String operation = op["operation"];
        if (parallel && is_read_only_operation(operation)) {
            std::vector<std::future<FileOpsOperationExecution>> futures;
            int block_start = i;

            while (i < operations.size()) {
                godot::Dictionary block_op = operations[i];
                if (!block_op.has("operation") ||
                    !is_read_only_operation(block_op["operation"])) {
                    break;
                }

                futures.push_back(std::async(std::launch::async, [block_op]() {
                    return execute_operation(block_op);
                }));
                i++;
            }

            for (size_t future_index = 0; future_index < futures.size();
                 future_index++) {
                append_execution(futures[future_index].get(), operation_results,
                                 warnings, errors, overall_success,
                                 block_start +
                                     static_cast<int>(future_index));
            }

            i--;
            continue;
        }

        append_execution(execute_operation(op), operation_results, warnings,
                         errors, overall_success, i);
    }

    int success_count = 0;
    int failure_count = 0;
    for (int i = 0; i < operation_results.size(); i++) {
        if (operation_results[i].get_type() != godot::Variant::DICTIONARY) {
            failure_count++;
            continue;
        }
        godot::Dictionary op_result = operation_results[i];
        if (godot::String(op_result.get("status", "")) == "success") {
            success_count++;
        } else {
            failure_count++;
        }
    }

    godot::Dictionary summary;
    summary["status"] = failure_count == 0 ? "success" :
        (success_count == 0 ? "failed" : "partial");
    summary["requested_count"] = operations.size();
    summary["completed_count"] = operation_results.size();
    summary["success_count"] = success_count;
    summary["failure_count"] = failure_count;
    summary["warning_count"] = warnings.size();
    summary["error_count"] = errors.size();

    result["success"] = overall_success;
    result["tool_name"] = "file_ops";
    result["format_version"] = "file-ops-result-v1";
    result["summary"] = summary;
    result["operations"] = operation_results;
    result["warnings"] = warnings;
    result["errors"] = errors;
    result["parallel"] = parallel;
    if (!overall_success && errors.size() > 0) {
        godot::String summary;
        for (int i = 0; i < errors.size() && i < 3; i++) {
            if (i > 0) summary += "; ";
            summary += godot::String(errors[i]);
        }
        if (errors.size() > 3) {
            summary += godot::String(" (and ") + godot::String::num_int64(errors.size() - 3) + " more)";
        }
        result["error"] = summary;
    }
    return result;
}

} // namespace fennara
