#include "fennara/tool_results/script_diagnostics.hpp"

#include "fennara/tool_results/envelope.hpp"
#include "fennara/tool_results/markdown.hpp"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara::tool_results {

namespace {

int targeted_budget_tokens(int target_count) {
    if (target_count <= 1) return 10000;
    if (target_count == 2) return 14000;
    if (target_count == 3) return 18000;
    if (target_count == 4) return 22000;
    return 26000;
}

int diagnostics_budget_tokens(const godot::Dictionary &args, int target_count) {
    bool scan_project = args.get("scan_project", false);
    return scan_project ? 30000 : targeted_budget_tokens(target_count);
}

godot::String count_text(const godot::Dictionary &file) {
    return godot::String::num_int64(static_cast<int64_t>(file.get("total_errors", 0))) + " errors, " +
           godot::String::num_int64(static_cast<int64_t>(file.get("total_warnings", 0))) + " warnings, " +
           godot::String::num_int64(static_cast<int64_t>(file.get("total_info", 0))) + " info, " +
           godot::String::num_int64(static_cast<int64_t>(file.get("total_hints", 0))) + " hints, " +
           godot::String::num_int64(static_cast<int64_t>(file.get("total_diagnostics", 0))) + " diagnostics";
}

godot::String totals_text(const godot::Dictionary &summary) {
    return godot::String::num_int64(static_cast<int64_t>(summary.get("total_errors", 0))) + " errors, " +
           godot::String::num_int64(static_cast<int64_t>(summary.get("total_warnings", 0))) + " warnings, " +
           godot::String::num_int64(static_cast<int64_t>(summary.get("total_info", 0))) + " info, " +
           godot::String::num_int64(static_cast<int64_t>(summary.get("total_hints", 0))) + " hints, " +
           godot::String::num_int64(static_cast<int64_t>(summary.get("total_diagnostics", 0))) + " diagnostics";
}

int severity_rank(const godot::String &severity) {
    if (severity == "error") return 0;
    if (severity == "warning") return 1;
    if (severity == "info") return 2;
    return 3;
}

godot::String severity_heading(const godot::String &severity) {
    if (severity == "error") return "### Errors";
    if (severity == "warning") return "### Warnings";
    if (severity == "info") return "### Info";
    return "### Hints";
}

bool file_has_diagnostics(const godot::Dictionary &file) {
    return static_cast<int>(file.get("total_diagnostics", 0)) > 0;
}

int file_priority(const godot::Dictionary &file) {
    if (godot::String(file.get("status", "")) != "success") return 0;
    if (static_cast<int>(file.get("total_errors", 0)) > 0) return 1;
    if (static_cast<int>(file.get("total_warnings", 0)) > 0) return 2;
    if (static_cast<int>(file.get("total_info", 0)) > 0 ||
        static_cast<int>(file.get("total_hints", 0)) > 0) return 3;
    return 4;
}

godot::Array sorted_file_indices(const godot::Array &files, bool scan_project) {
    godot::Array indices;
    for (int priority = 0; priority <= 4; priority++) {
        for (int i = 0; i < files.size(); i++) {
            if (files[i].get_type() != godot::Variant::DICTIONARY) {
                continue;
            }
            godot::Dictionary file = files[i];
            if (scan_project && priority == 4) {
                continue;
            }
            if (file_priority(file) == priority) {
                indices.append(i);
            }
        }
    }
    return indices;
}

godot::String diagnostic_bullet(const godot::Dictionary &diag) {
    int64_t line = static_cast<int64_t>(diag.get("line", 0));
    int64_t column = static_cast<int64_t>(diag.get("column", 0));
    godot::String severity = diag.get("severity", "");
    godot::String message = diag.get("message", "");
    godot::String bullet = "- line " + godot::String::num_int64(line) + ":" +
           godot::String::num_int64(column) + " " + severity + ": " + message;
    if (godot::String(diag.get("source", "")) == "scene_load") {
        bullet += " (scene_load";
        godot::String scene_path = diag.get("scene_path", "");
        if (!scene_path.is_empty()) {
            bullet += " from " + scene_path;
        }
        bullet += ")";
    }
    return bullet;
}

godot::Array diagnostics_for_severity(const godot::Dictionary &file,
                                      const godot::String &severity) {
    godot::Array out;
    godot::Array diagnostics = file.get("diagnostics", godot::Array());
    for (int i = 0; i < diagnostics.size(); i++) {
        if (diagnostics[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary diag = diagnostics[i];
        if (godot::String(diag.get("severity", "")) == severity) {
            out.append(diag);
        }
    }
    return out;
}

godot::Dictionary target_metadata_base(const godot::Dictionary &file) {
    godot::Dictionary target;
    target["file_path"] = file.get("path", "");
    target["status"] = file.get("status", "");
    target["total_errors"] = file.get("total_errors", 0);
    target["total_warnings"] = file.get("total_warnings", 0);
    target["total_info"] = file.get("total_info", 0);
    target["total_hints"] = file.get("total_hints", 0);
    target["total_diagnostics"] = file.get("total_diagnostics", 0);
    target["shown_diagnostics"] = 0;
    target["omitted_diagnostics"] = file.get("total_diagnostics", 0);
    if (file.has("error")) {
        target["error"] = file["error"];
    }
    if (file.has("block_reason")) {
        target["block_reason"] = file["block_reason"];
    }
    if (file.has("blocked_path")) {
        target["blocked_path"] = file["blocked_path"];
    }
    if (file.has("blocked_addon_root")) {
        target["blocked_addon_root"] = file["blocked_addon_root"];
    }
    return target;
}

void increment_shown(godot::Dictionary &target) {
    int shown = static_cast<int>(target.get("shown_diagnostics", 0)) + 1;
    int total = static_cast<int>(target.get("total_diagnostics", 0));
    target["shown_diagnostics"] = shown;
    target["omitted_diagnostics"] = total > shown ? total - shown : 0;
}

} // namespace

godot::Dictionary format_script_diagnostics(const godot::Dictionary &args,
                                              const godot::Dictionary &raw_result) {
    bool raw_success = raw_result.get("success", false);
    godot::Array files = raw_result.get("files", godot::Array());
    godot::Dictionary summary = raw_result.get("summary", godot::Dictionary());
    bool scan_project = raw_result.get("scan_project", args.get("scan_project", false));
    int budget_tokens = diagnostics_budget_tokens(args, files.size());

    godot::PackedStringArray header;
    godot::PackedStringArray sections;
    godot::Array targets;
    bool previewed = false;

    int clean_files = 0;
    int diagnostic_files = 0;
    int failed_files = 0;
    for (int i = 0; i < files.size(); i++) {
        if (files[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary file = files[i];
        if (godot::String(file.get("status", "")) != "success") {
            failed_files++;
        } else if (file_has_diagnostics(file)) {
            diagnostic_files++;
        } else {
            clean_files++;
        }
        targets.append(target_metadata_base(file));
    }

    godot::String status = "success";
    if (files.size() == 0 && raw_result.has("error")) {
        status = "failed";
    } else if (failed_files > 0 && failed_files == files.size()) {
        status = "failed";
    } else if (failed_files > 0) {
        status = "partial";
    }

    header.append("Tool: script_diagnostics");
    header.append("Status: " + status);
    if (scan_project) {
        int64_t scanned = static_cast<int64_t>(summary.get("scanned_file_count", summary.get("checked_count", files.size())));
        header.append("Scope: project scan, " + godot::String::num_int64(scanned) + " files scanned");
    } else {
        godot::PackedStringArray paths;
        for (int i = 0; i < files.size(); i++) {
            if (files[i].get_type() == godot::Variant::DICTIONARY) {
                godot::Dictionary file = files[i];
                paths.append(godot::String(file.get("path", "")));
            }
        }
        header.append("Scope: " + godot::String::num_int64(files.size()) +
                      (files.size() == 1 ? " file: " : " files: ") +
                      godot::String(", ").join(paths));
    }
    header.append("Totals: " + totals_text(summary));
    if ((bool)summary.get("scene_load_skipped", false)) {
        header.append("Scene-load diagnostics: skipped");
        header.append("Skip reason: " + godot::String(summary.get(
            "scene_load_skip_reason", "Scene-load diagnostics were skipped.")));
        header.append("May miss: " + godot::String(summary.get(
            "scene_load_may_miss",
            "Errors that only occur when scripts are loaded through scenes may be missed.")));
    }
    if (scan_project) {
        header.append("Clean files: " + godot::String::num_int64(clean_files));
        header.append("Files with diagnostics: " + godot::String::num_int64(diagnostic_files));
        header.append("Failed files: " + godot::String::num_int64(failed_files));
    }
    if (files.size() == 0 && raw_result.has("error")) {
        header.append("");
        header.append("Error:\n" + godot::String(raw_result.get("error", "")));
    }

    int used_tokens = estimate_tokens(godot::String("\n").join(header));
    godot::Array ordered_indices = sorted_file_indices(files, scan_project);
    int remaining_tokens = budget_tokens - used_tokens;
    int visible_file_count = ordered_indices.size();
    int per_file_budget = visible_file_count > 0 ? remaining_tokens / visible_file_count : remaining_tokens;

    for (int idx = 0; idx < ordered_indices.size(); idx++) {
        int file_index = static_cast<int>(ordered_indices[idx]);
        godot::Dictionary file = files[file_index];
        godot::Dictionary target = targets[file_index];
        godot::PackedStringArray lines;
        lines.append("## " + godot::String(file.get("path", "")));
        lines.append("Status: " + godot::String(file.get("status", "")));
        if (godot::String(file.get("status", "")) == "success") {
            lines.append("Diagnostics: " + count_text(file));
        } else if (file.has("error")) {
            lines.append("Error:\n" + godot::String(file.get("error", "")));
            if (file.has("resolution")) {
                lines.append("Resolution:\n" + godot::String(file.get("resolution", "")));
            }
        }

        int section_tokens = estimate_tokens(godot::String("\n").join(lines));
        int detail_budget = per_file_budget - section_tokens;
        if (detail_budget < 1) {
            detail_budget = 1;
        }

        for (int severity_index = 0; severity_index < 4; severity_index++) {
            godot::String severity = severity_index == 0 ? "error" :
                (severity_index == 1 ? "warning" :
                 (severity_index == 2 ? "info" : "hint"));
            godot::Array diagnostics = diagnostics_for_severity(file, severity);
            if (diagnostics.is_empty()) {
                continue;
            }

            godot::PackedStringArray bullets;
            for (int d = 0; d < diagnostics.size(); d++) {
                godot::Dictionary diag = diagnostics[d];
                godot::String bullet = diagnostic_bullet(diag);
                int bullet_tokens = estimate_tokens(bullet);
                if (detail_budget - bullet_tokens < 0) {
                    previewed = true;
                    continue;
                }
                bullets.append(bullet);
                detail_budget -= bullet_tokens;
                increment_shown(target);
            }

            if (!bullets.is_empty()) {
                lines.append("");
                lines.append(severity_heading(severity));
                lines.append(godot::String("\n").join(bullets));
            }
        }

        if (static_cast<int>(target.get("omitted_diagnostics", 0)) > 0) {
            previewed = true;
            lines.append("");
            lines.append("Omitted: additional diagnostics exceeded model-facing size limit.");
        }

        targets[file_index] = target;
        sections.append(godot::String("\n").join(lines));
    }

    if (previewed && status == "success") {
        status = "partial";
        header.set(1, "Status: partial");
    }

    sections.insert(0, godot::String("\n").join(header));

    godot::Dictionary metadata = make_base_metadata("script_diagnostics", "script_diagnostics-md-v1", status);
    metadata["scan_project"] = scan_project;
    metadata["budget_tokens"] = budget_tokens;
    metadata["previewed"] = previewed;
    metadata["targets"] = targets;
    return make_envelope(godot::String("\n\n").join(sections), metadata, raw_success);
}

} // namespace fennara::tool_results
