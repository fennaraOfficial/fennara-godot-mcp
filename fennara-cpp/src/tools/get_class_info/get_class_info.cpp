#include "fennara/tools/get_class_info/get_class_info.hpp"
#include "fennara/logger.hpp"
#include "fennara/tools/get_class_info/internal.hpp"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace fennara {

namespace {

constexpr int kMaxBatchClasses = 3;
constexpr const char *kDefaultDocsBranch = "master";

int count_text_lines(const godot::String &text) {
    if (text.is_empty()) {
        return 0;
    }
    int lines = 1;
    for (int i = 0; i < text.length(); i++) {
        if (text[i] == '\n' && i < text.length() - 1) {
            lines++;
        }
    }
    return lines;
}

} // namespace

void FennaraGetClassInfoTool::_bind_methods() {
    godot::ClassDB::bind_static_method(
        "FennaraGetClassInfoTool",
        godot::D_METHOD("execute", "args"),
        &FennaraGetClassInfoTool::execute);
}

godot::Dictionary FennaraGetClassInfoTool::execute(
    const godot::Dictionary &args) {
    godot::String branch =
        godot::String(args.get("branch", kDefaultDocsBranch)).strip_edges();
    if (branch.is_empty()) {
        branch = kDefaultDocsBranch;
    }

    auto execute_single_class = [&](const godot::String &raw_class_name) {
        godot::Dictionary result;
        godot::String class_name = raw_class_name.strip_edges();

        if (class_name.is_empty()) {
            result["status"] = "failed";
            result["class_name"] = "";
            result["error"] = "class_name must be a non-empty string";
            return result;
        }

        FLOG_TOOL(godot::String("get_class_info: class=") + class_name +
                  " branch=" + branch + " local_only=true");

        auto *cdb = godot::ClassDBSingleton::get_singleton();
        if (!cdb->class_exists(class_name)) {
            result["status"] = "failed";
            result["class_name"] = class_name;
            result["error"] = "Class not found: " + class_name +
                              ". Make sure it is a valid Godot built-in class.";
            return result;
        }

        godot::Array properties = get_class_info::collect_runtime_properties(class_name);
        godot::PackedStringArray inherits_chain = get_class_info::collect_inherits_chain(class_name);
        godot::PackedStringArray inherited_by = get_class_info::collect_inherited_by(class_name);
        get_class_info::ClassDocumentation docs =
            get_class_info::collect_docs_for_class_info(class_name, branch, inherits_chain);

        godot::String text = get_class_info::render_docs_text(docs);
        text += get_class_info::render_runtime_hierarchy_text(inherits_chain, inherited_by);
        text += get_class_info::render_runtime_properties_text(properties, docs);

        result["status"] = "success";
        result["class_name"] = class_name;
        result["branch"] = branch;
        result["local_only"] = true;
        result["inherits"] = inherits_chain.size() > 0 ? godot::String(inherits_chain[0]) : godot::String();
        result["property_count"] = properties.size();
        result["inherited_by_count"] = inherited_by.size();
        result["text_line_count"] = count_text_lines(text);
        result["text"] = text;
        result["format"] = "raw_text";
        return result;
    };

    godot::Dictionary result;

    if (args.has("class_names")) {
        godot::Variant class_names_var = args["class_names"];
        if (class_names_var.get_type() != godot::Variant::ARRAY) {
            result["success"] = false;
            result["tool_name"] = "get_class_info";
            result["format_version"] = "get-class-info-result-v1";
            result["error"] = "class_names must be an array of strings";
            return result;
        }

        godot::Array class_names = class_names_var;
        if (class_names.is_empty()) {
            result["success"] = false;
            result["tool_name"] = "get_class_info";
            result["format_version"] = "get-class-info-result-v1";
            result["error"] = "class_names must contain at least one class";
            return result;
        }
        if (class_names.size() > kMaxBatchClasses) {
            result["success"] = false;
            result["tool_name"] = "get_class_info";
            result["format_version"] = "get-class-info-result-v1";
            result["error"] =
                "class_names supports at most " +
                godot::String::num_int64(kMaxBatchClasses) +
                " classes per call. Split larger requests into multiple calls.";
            return result;
        }

        godot::Array classes;
        for (int i = 0; i < class_names.size(); i++) {
            godot::Variant item = class_names[i];
            if (item.get_type() != godot::Variant::STRING) {
                godot::Dictionary item_result;
                item_result["status"] = "failed";
                item_result["class_name"] = "";
                item_result["error"] =
                    "class_names[" + godot::String::num_int64(i) +
                    "] must be a string";
                classes.append(item_result);
                continue;
            }

            classes.append(execute_single_class(item));
        }

        int success_count = 0;
        int failure_count = 0;
        int total_text_lines = 0;
        for (int i = 0; i < classes.size(); i++) {
            if (classes[i].get_type() != godot::Variant::DICTIONARY) {
                failure_count++;
                continue;
            }
            godot::Dictionary klass = classes[i];
            if (godot::String(klass.get("status", "")) == "success") {
                success_count++;
                total_text_lines += static_cast<int>(klass.get("text_line_count", 0));
            } else {
                failure_count++;
            }
        }

        godot::Dictionary summary;
        summary["status"] = failure_count == 0 ? "success" :
            (success_count == 0 ? "failed" : "partial");
        summary["requested_count"] = class_names.size();
        summary["checked_count"] = classes.size();
        summary["success_count"] = success_count;
        summary["failure_count"] = failure_count;
        summary["total_text_lines"] = total_text_lines;

        result["success"] = failure_count == 0;
        result["tool_name"] = "get_class_info";
        result["format_version"] = "get-class-info-result-v1";
        result["summary"] = summary;
        result["classes"] = classes;
        result["format"] = "raw_text";
        if (!(bool)result["success"]) {
            result["error"] = failure_count == classes.size()
                ? "Failed to inspect requested class(es)"
                : "Some class(es) could not be inspected";
        }
        return result;
    }

    result["success"] = false;
    result["tool_name"] = "get_class_info";
    result["format_version"] = "get-class-info-result-v1";
    result["error"] = "Missing required arg: class_names";
    return result;
}

} // namespace fennara
