#include "fennara/common/batch_summary.hpp"

namespace fennara::batch_summary {

godot::Dictionary build(const godot::Array &results,
                        const godot::String &label) {
    int success_count = 0;
    int failure_count = 0;
    for (int i = 0; i < results.size(); i++) {
        godot::Dictionary item = results[i];
        if ((bool)item.get("success", false)) {
            success_count++;
        } else {
            failure_count++;
        }
    }

    godot::Dictionary summary;
    summary["label"] = label;
    summary["total_requested"] = results.size();
    summary["success_count"] = success_count;
    summary["failure_count"] = failure_count;
    summary["message"] =
        "Processed " + godot::String::num_int64(results.size()) + " " + label +
        " (" + godot::String::num_int64(success_count) + " succeeded, " +
        godot::String::num_int64(failure_count) + " failed)";
    return summary;
}

godot::Dictionary build_result(const godot::Array &results,
                               const godot::String &label) {
    godot::Dictionary summary = build(results, label);
    godot::Dictionary result;
    result["success"] = static_cast<int>(summary["failure_count"]) == 0;
    result["summary"] = summary;
    result["results"] = results;
    if (!(bool)result["success"]) {
        result["error"] = summary["message"];
    }
    return result;
}

} // namespace fennara::batch_summary
