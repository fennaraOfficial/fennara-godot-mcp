#include "fennara/tool_results/get_class_info.hpp"

#include "fennara/tool_results/envelope.hpp"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara::tool_results {

namespace {

godot::String class_label(const godot::Dictionary &klass, int index) {
    godot::String name = klass.get("class_name", "");
    if (!name.is_empty()) {
        return name;
    }
    return "class_names[" + godot::String::num_int64(index) + "]";
}

godot::String scope_for_classes(const godot::Array &classes) {
    godot::PackedStringArray names;
    for (int i = 0; i < classes.size(); i++) {
        if (classes[i].get_type() != godot::Variant::DICTIONARY) {
            names.append("class_names[" + godot::String::num_int64(i) + "]");
            continue;
        }
        godot::Dictionary klass = classes[i];
        names.append(class_label(klass, i));
    }
    return godot::String::num_int64(classes.size()) +
           (classes.size() == 1 ? " class: " : " classes: ") +
           godot::String(", ").join(names);
}

godot::Dictionary target_metadata(const godot::Dictionary &klass) {
    godot::Dictionary target;
    target["class_name"] = klass.get("class_name", "");
    target["status"] = klass.get("status", "");
    target["branch"] = klass.get("branch", "");
    target["local_only"] = klass.get("local_only", true);
    target["inherits"] = klass.get("inherits", "");
    target["property_count"] = klass.get("property_count", 0);
    target["inherited_by_count"] = klass.get("inherited_by_count", 0);
    target["text_line_count"] = klass.get("text_line_count", 0);
    target["previewed"] = false;
    if (klass.has("error")) {
        target["error"] = klass["error"];
    }
    return target;
}

godot::String failed_section(const godot::Dictionary &klass, int index) {
    godot::PackedStringArray lines;
    lines.append("## " + class_label(klass, index));
    lines.append("Status: failed");
    if (klass.has("error")) {
        lines.append("Error:\n" + godot::String(klass.get("error", "")));
    }
    return godot::String("\n").join(lines);
}

godot::String success_section(const godot::Dictionary &klass, int index) {
    godot::PackedStringArray lines;
    lines.append("## " + class_label(klass, index));
    lines.append("Status: success");
    godot::String branch = klass.get("branch", "");
    if (!branch.is_empty()) {
        lines.append("Docs branch: " + branch);
    }
    int64_t text_lines = static_cast<int64_t>(klass.get("text_line_count", 0));
    if (text_lines > 0) {
        lines.append("Text lines: " + godot::String::num_int64(text_lines));
    }
    lines.append("");
    lines.append(godot::String(klass.get("text", "")));
    return godot::String("\n").join(lines);
}

} // namespace

godot::Dictionary format_get_class_info(const godot::Dictionary &raw_result) {
    bool raw_success = raw_result.get("success", false);
    godot::Array classes = raw_result.get("classes", godot::Array());
    godot::Dictionary summary = raw_result.get("summary", godot::Dictionary());

    godot::Array targets;
    godot::PackedStringArray sections;
    int raw_success_count = 0;
    int raw_failure_count = 0;

    for (int i = 0; i < classes.size(); i++) {
        if (classes[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary klass = classes[i];
        godot::String status = klass.get("status", "");
        if (status == "success") {
            raw_success_count++;
            sections.append(success_section(klass, i));
        } else {
            raw_failure_count++;
            sections.append(failed_section(klass, i));
        }
        targets.append(target_metadata(klass));
    }

    godot::String status = "success";
    if (classes.size() == 0 && raw_result.has("error")) {
        status = "failed";
    } else if (raw_failure_count > 0 && raw_success_count == 0) {
        status = "failed";
    } else if (raw_failure_count > 0) {
        status = "partial";
    }

    godot::PackedStringArray header;
    header.append("Tool: get_class_info");
    header.append("Status: " + status);
    header.append(classes.size() > 0 ? "Scope: " + scope_for_classes(classes) : "Scope: unknown");
    if (!summary.is_empty()) {
        header.append(
            "Totals: " +
            godot::String::num_int64(static_cast<int64_t>(summary.get("success_count", 0))) +
            " succeeded, " +
            godot::String::num_int64(static_cast<int64_t>(summary.get("failure_count", 0))) +
            " failed, " +
            godot::String::num_int64(static_cast<int64_t>(summary.get("total_text_lines", 0))) +
            " text lines"
        );
    }
    if (classes.size() == 0 && raw_result.has("error")) {
        header.append("");
        header.append("Error:\n" + godot::String(raw_result.get("error", "")));
    }
    sections.insert(0, godot::String("\n").join(header));

    godot::Dictionary metadata = make_base_metadata("get_class_info", "get_class_info-md-v1", status);
    metadata["targets"] = targets;
    metadata["previewed"] = false;
    return make_envelope(godot::String("\n\n").join(sections), metadata, raw_success);
}

} // namespace fennara::tool_results
