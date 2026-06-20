#include "fennara/tool_results/file_ops.hpp"

#include "fennara/tool_results/envelope.hpp"
#include "fennara/tool_results/markdown.hpp"

#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara::tool_results {

namespace {

constexpr int kBudgetTokens = 30000;

bool is_read_operation(const godot::String &operation) {
    return operation == "list" || operation == "glob" || operation == "rg";
}

godot::String join_args(const godot::Array &args) {
    godot::PackedStringArray parts;
    for (int i = 0; i < args.size(); i++) {
        parts.append(godot::String(args[i]));
    }
    return godot::String(" ").join(parts);
}

godot::String variant_json(const godot::Variant &value) {
    godot::Ref<godot::JSON> json;
    json.instantiate();
    return json->stringify(value, "  ");
}

godot::Array result_array(const godot::Dictionary &op,
                          const godot::String &key) {
    godot::Variant result_var = op.get("result", godot::Variant());
    if (result_var.get_type() != godot::Variant::DICTIONARY) {
        return godot::Array();
    }
    godot::Dictionary result = result_var;
    godot::Variant value = result.get(key, godot::Variant());
    if (value.get_type() == godot::Variant::ARRAY) {
        return value;
    }
    return godot::Array();
}

godot::String result_string(const godot::Dictionary &op,
                            const godot::String &key) {
    godot::Variant result_var = op.get("result", godot::Variant());
    if (result_var.get_type() != godot::Variant::DICTIONARY) {
        return "";
    }
    godot::Dictionary result = result_var;
    return result.get(key, "");
}

godot::Dictionary target_metadata(const godot::Dictionary &op) {
    godot::Dictionary meta;
    meta["index"] = op.get("index", 0);
    meta["operation"] = op.get("operation", "");
    meta["status"] = op.get("status", "");
    meta["args"] = op.get("args", godot::Array());
    meta["warning_count"] = godot::Array(op.get("warnings", godot::Array())).size();
    meta["error_count"] = godot::Array(op.get("errors", godot::Array())).size();
    meta["shown_detail_tokens"] = 0;
    meta["omitted_detail_tokens"] = 0;
    if (op.has("path")) meta["path"] = op["path"];
    if (op.has("source")) meta["source"] = op["source"];
    if (op.has("destination")) meta["destination"] = op["destination"];
    if (op.has("count")) meta["count"] = op["count"];
    if (op.has("total_items")) meta["total_items"] = op["total_items"];
    if (op.has("line_count")) meta["line_count"] = op["line_count"];
    if (op.has("error")) meta["error"] = op["error"];
    if (op.has("block_reason")) meta["block_reason"] = op["block_reason"];
    if (op.has("blocked_path")) meta["blocked_path"] = op["blocked_path"];
    if (op.has("blocked_addon_root")) meta["blocked_addon_root"] = op["blocked_addon_root"];
    return meta;
}

void append_array_lines(godot::PackedStringArray &lines,
                        const godot::Array &values,
                        const godot::String &prefix = "- ") {
    for (int i = 0; i < values.size(); i++) {
        lines.append(prefix + godot::String(values[i]));
    }
}

godot::PackedStringArray detail_lines_for_operation(const godot::Dictionary &op) {
    godot::String operation = op.get("operation", "");
    godot::PackedStringArray lines;

    if (operation == "rg") {
        godot::String output = result_string(op, "output");
        if (!output.is_empty()) {
            godot::PackedStringArray split = output.split("\n");
            for (int i = 0; i < split.size(); i++) {
                if (!split[i].is_empty()) {
                    lines.append(split[i]);
                }
            }
        }
        return lines;
    }

    if (operation == "glob") {
        append_array_lines(lines, result_array(op, "matches"), "");
        return lines;
    }

    if (operation == "list") {
        godot::Array directories = result_array(op, "directories");
        godot::Array files = result_array(op, "files");
        if (!directories.is_empty()) {
            lines.append("Directories:");
            append_array_lines(lines, directories);
        }
        if (!files.is_empty()) {
            lines.append("Files:");
            append_array_lines(lines, files);
        }
        return lines;
    }

    godot::Variant result_var = op.get("result", godot::Variant());
    if (result_var.get_type() == godot::Variant::DICTIONARY) {
        godot::Dictionary result = result_var;
        godot::Dictionary fallback;
        godot::Array keys = result.keys();
        for (int i = 0; i < keys.size(); i++) {
            godot::String key = keys[i];
            if (key == "success" || key == "op" || key == "error" ||
                key == "from" || key == "to" || key == "path" ||
                key == "type") {
                continue;
            }
            fallback[key] = result[key];
        }
        if (!fallback.is_empty()) {
            godot::String json = variant_json(fallback);
            godot::PackedStringArray split = json.split("\n");
            for (int i = 0; i < split.size(); i++) {
                lines.append(split[i]);
            }
        }
    }
    return lines;
}

godot::String operation_heading(const godot::Dictionary &op) {
    int64_t index = static_cast<int64_t>(op.get("index", 0));
    godot::String operation = op.get("operation", "");
    return "## " + godot::String::num_int64(index + 1) + ". " + operation;
}

godot::String operation_detail_heading(const godot::Dictionary &op,
                                       int fallback_index) {
    int64_t index = static_cast<int64_t>(op.get("index", fallback_index)) + 1;
    godot::String operation = op.get("operation", "");
    godot::String heading = "### Detail for operation " +
                            godot::String::num_int64(index);
    if (!operation.is_empty()) {
        heading += ": " + operation;
    }

    godot::String args = join_args(op.get("args", godot::Array()));
    if (!args.is_empty()) {
        heading += " " + args;
    }
    return heading;
}

godot::PackedStringArray operation_summary_lines(const godot::Dictionary &op) {
    godot::PackedStringArray lines;
    godot::String operation = op.get("operation", "");
    godot::Variant result_var = op.get("result", godot::Variant());
    godot::Dictionary result;
    if (result_var.get_type() == godot::Variant::DICTIONARY) {
        result = result_var;
    }

    lines.append(operation_heading(op));
    lines.append("Status: " + godot::String(op.get("status", "")));
    godot::String args = join_args(op.get("args", godot::Array()));
    if (!args.is_empty()) {
        lines.append("Args: " + args);
    }

    if (op.has("path")) lines.append("Path: " + godot::String(op.get("path", "")));
    if (op.has("source")) lines.append("Source: " + godot::String(op.get("source", "")));
    if (op.has("destination")) lines.append("Destination: " + godot::String(op.get("destination", "")));
    if (result.has("type")) lines.append("Type: " + godot::String(result.get("type", "")));
    if (result.has("count")) lines.append("Count: " + godot::String::num_int64(static_cast<int64_t>(result.get("count", 0))));
    if (result.has("total_items")) lines.append("Total items: " + godot::String::num_int64(static_cast<int64_t>(result.get("total_items", 0))));
    if (result.has("line_count")) lines.append("Output lines: " + godot::String::num_int64(static_cast<int64_t>(result.get("line_count", 0))));
    if (op.has("error")) lines.append("Error: " + godot::String(op.get("error", "")));
    if (op.has("resolution")) {
        lines.append("Resolution: " + godot::String(op.get("resolution", "")));
    }

    godot::Array warnings = op.get("warnings", godot::Array());
    if (!warnings.is_empty()) {
        lines.append("Warnings:");
        append_array_lines(lines, warnings);
    }
    godot::Array errors = op.get("errors", godot::Array());
    if (!errors.is_empty()) {
        lines.append("Errors:");
        append_array_lines(lines, errors);
    }
    return lines;
}

int append_budgeted(godot::PackedStringArray &out,
                    const godot::PackedStringArray &items,
                    int &remaining_tokens) {
    int shown = 0;
    for (int i = 0; i < items.size(); i++) {
        int tokens = estimate_tokens(items[i]);
        if (remaining_tokens - tokens < 0) {
            continue;
        }
        out.append(items[i]);
        remaining_tokens -= tokens;
        shown++;
    }
    return shown;
}

} // namespace

godot::Dictionary format_file_ops(const godot::Dictionary &raw_result) {
    godot::Array operations = raw_result.get("operations", godot::Array());
    godot::Dictionary summary = raw_result.get("summary", godot::Dictionary());
    bool raw_success = raw_result.get("success", false);

    godot::PackedStringArray header;
    header.append("Tool: file_ops");
    header.append("Status: pending");
    header.append("Operations: " + godot::String::num_int64(operations.size()));
    header.append("Warnings: " + godot::String::num_int64(godot::Array(raw_result.get("warnings", godot::Array())).size()));
    header.append("Errors: " + godot::String::num_int64(godot::Array(raw_result.get("errors", godot::Array())).size()));
    if (!summary.is_empty()) {
        header.append(
            "Totals: " +
            godot::String::num_int64(static_cast<int64_t>(summary.get("success_count", 0))) +
            " succeeded, " +
            godot::String::num_int64(static_cast<int64_t>(summary.get("failure_count", 0))) +
            " failed"
        );
    }
    if (raw_result.has("error")) {
        header.append("Error: " + godot::String(raw_result.get("error", "")));
    }

    godot::PackedStringArray sections;
    godot::Array metadata_ops;
    godot::Array read_indices;
    bool previewed = false;
    int used_tokens = estimate_tokens(godot::String("\n").join(header));

    for (int i = 0; i < operations.size(); i++) {
        if (operations[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary op = operations[i];
        godot::String operation = op.get("operation", "");
        metadata_ops.append(target_metadata(op));

        godot::PackedStringArray summary_lines = operation_summary_lines(op);
        sections.append(godot::String("\n").join(summary_lines));
        used_tokens += estimate_tokens(godot::String("\n").join(summary_lines));

        if (is_read_operation(operation)) {
            read_indices.append(i);
        }
    }

    int remaining_tokens = kBudgetTokens - used_tokens;
    if (remaining_tokens < 1) {
        remaining_tokens = 1;
    }
    int per_read_budget = read_indices.size() > 0 ? remaining_tokens / read_indices.size() : 0;

    for (int r = 0; r < read_indices.size(); r++) {
        int op_index = static_cast<int>(read_indices[r]);
        if (op_index < 0 || op_index >= operations.size() ||
            operations[op_index].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary op = operations[op_index];
        godot::PackedStringArray detail_lines = detail_lines_for_operation(op);
        if (detail_lines.is_empty()) {
            continue;
        }

        godot::PackedStringArray detail_section;
        detail_section.append(operation_detail_heading(op, op_index));
        int detail_budget = per_read_budget <= 0 ? 1 : per_read_budget;
        int shown = append_budgeted(detail_section, detail_lines, detail_budget);
        int shown_tokens = estimate_tokens(godot::String("\n").join(detail_section));
        int total_tokens = estimate_tokens(godot::String("\n").join(detail_lines));
        if (shown < detail_lines.size()) {
            previewed = true;
            detail_section.append("Omitted: remaining file_ops output exceeded model-facing size limit.");
        }
        sections.append(godot::String("\n").join(detail_section));

        if (op_index < metadata_ops.size() &&
            metadata_ops[op_index].get_type() == godot::Variant::DICTIONARY) {
            godot::Dictionary meta = metadata_ops[op_index];
            meta["shown_detail_tokens"] = shown_tokens;
            meta["omitted_detail_tokens"] = total_tokens > shown_tokens ? total_tokens - shown_tokens : 0;
            metadata_ops[op_index] = meta;
        }
    }

    godot::String status = "success";
    int failure_count = static_cast<int>(summary.get("failure_count", 0));
    int success_count = static_cast<int>(summary.get("success_count", 0));
    if (operations.size() == 0 && raw_result.has("error")) {
        status = "failed";
    } else if (failure_count > 0 && success_count == 0) {
        status = "failed";
    } else if (failure_count > 0 || previewed) {
        status = "partial";
    }
    header.set(1, "Status: " + status);
    sections.insert(0, godot::String("\n").join(header));

    godot::Dictionary metadata = make_base_metadata("file_ops", "file_ops-md-v1", status);
    metadata["operations"] = metadata_ops;
    metadata["budget_tokens"] = kBudgetTokens;
    metadata["previewed"] = previewed;
    return make_envelope(godot::String("\n\n").join(sections), metadata, raw_success);
}

} // namespace fennara::tool_results
