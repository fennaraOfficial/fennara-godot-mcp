#include "fennara/tool_results/read_file.hpp"

#include "fennara/tool_results/envelope.hpp"
#include "fennara/tool_results/markdown.hpp"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara::tool_results {

namespace {

constexpr int kMinLargeTextPreviewTokens = 2000;

int read_file_budget_tokens(int target_count) {
    if (target_count <= 1) return 10000;
    if (target_count == 2) return 14000;
    if (target_count == 3) return 18000;
    if (target_count == 4) return 22000;
    return 26000;
}

godot::String file_path_for_heading(const godot::Dictionary &file, int index) {
    godot::String path = file.get("path", "");
    if (!path.is_empty()) {
        return path;
    }
    return "file_paths[" + godot::String::num_int64(index) + "]";
}

godot::String scope_for_files(const godot::Array &files) {
    godot::PackedStringArray paths;
    for (int i = 0; i < files.size(); i++) {
        if (files[i].get_type() != godot::Variant::DICTIONARY) {
            paths.append("file_paths[" + godot::String::num_int64(i) + "]");
            continue;
        }
        godot::Dictionary file = files[i];
        paths.append(file_path_for_heading(file, i));
    }
    return godot::String::num_int64(files.size()) +
           (files.size() == 1 ? " file: " : " files: ") +
           godot::String(", ").join(paths);
}

godot::String line_summary(const godot::Dictionary &file) {
    godot::Variant range_var = file.get("range", godot::Variant());
    if (range_var.get_type() != godot::Variant::DICTIONARY) {
        return "";
    }
    godot::Dictionary range = range_var;
    int64_t start_line = static_cast<int64_t>(range.get("start_line", 0));
    int64_t end_line = static_cast<int64_t>(range.get("end_line", 0));
    int64_t total_lines = static_cast<int64_t>(range.get("total_lines", 0));
    if (start_line <= 0 || end_line <= 0 || total_lines <= 0) {
        return "";
    }
    return "Lines: " + godot::String::num_int64(start_line) + "-" +
           godot::String::num_int64(end_line) + " of " +
           godot::String::num_int64(total_lines);
}

godot::Dictionary target_metadata(const godot::Dictionary &file) {
    godot::Dictionary target;
    godot::String kind = file.get("kind", "");
    target["path"] = file.get("path", "");
    target["kind"] = kind;
    target["status"] = file.get("status", "");

    if (kind == "text") {
        godot::Variant range_var = file.get("range", godot::Variant());
        if (range_var.get_type() == godot::Variant::DICTIONARY) {
            godot::Dictionary range = range_var;
            target["start_line"] = range.get("start_line", 0);
            target["end_line"] = range.get("end_line", 0);
            target["returned_lines"] = range.get("returned_lines", 0);
            target["total_lines"] = range.get("total_lines", 0);
        }
    } else if (kind == "image") {
        godot::Variant image_var = file.get("image", godot::Variant());
        if (image_var.get_type() == godot::Variant::DICTIONARY) {
            godot::Dictionary image = image_var;
            target["mime_type"] = image.get("mime_type", "");
            target["byte_size"] = image.get("byte_size", 0);
            target["width"] = image.get("width", 0);
            target["height"] = image.get("height", 0);
        }
    }

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

godot::String text_section(const godot::Dictionary &file,
                           int index,
                           const godot::String &text,
                           bool previewed,
                           int shown_tokens,
                           int total_tokens) {
    godot::PackedStringArray lines;
    lines.append("## " + file_path_for_heading(file, index));
    lines.append(godot::String("Status: ") + (previewed ? "partial" : "success"));
    godot::String summary = line_summary(file);
    if (!summary.is_empty()) {
        lines.append(summary);
    }
    if (previewed) {
        lines.append(
            "Shown: preview, about " + godot::String::num_int64(shown_tokens) +
            " of " + godot::String::num_int64(total_tokens) + " estimated tokens"
        );
        lines.append("Omitted: remaining content exceeded model-facing size limit");
        lines.append("Hint: use read_file with start_line/end_line for a narrower range.");
    }

    lines.append("");
    lines.append(code_fence(text, godot::String(file.get("language", "text"))));
    return godot::String("\n").join(lines);
}

godot::String image_section(const godot::Dictionary &file, int index) {
    godot::PackedStringArray lines;
    lines.append("## " + file_path_for_heading(file, index));
    lines.append("Status: success");

    godot::Variant image_var = file.get("image", godot::Variant());
    if (image_var.get_type() == godot::Variant::DICTIONARY) {
        godot::Dictionary image = image_var;
        godot::String mime = image.get("mime_type", "");
        int64_t width = static_cast<int64_t>(image.get("width", 0));
        int64_t height = static_cast<int64_t>(image.get("height", 0));
        godot::String image_line = "Image:";
        if (width > 0 && height > 0) {
            image_line += " " + godot::String::num_int64(width) + "x" +
                          godot::String::num_int64(height);
        }
        if (!mime.is_empty()) {
            image_line += " " + mime;
        } else {
            godot::String format = image.get("format", "");
            if (!format.is_empty()) {
                image_line += " " + format;
            }
        }
        lines.append(image_line);
        int64_t byte_size = static_cast<int64_t>(image.get("byte_size", 0));
        if (byte_size > 0) {
            lines.append("Byte size: " + godot::String::num_int64(byte_size));
        }
    }
    return godot::String("\n").join(lines);
}

godot::Dictionary image_payload_for_file(const godot::Dictionary &file) {
    godot::Dictionary payload;
    godot::Variant image_var = file.get("image", godot::Variant());
    if (image_var.get_type() != godot::Variant::DICTIONARY) {
        return payload;
    }

    godot::Dictionary image = image_var;
    godot::String base64 = image.get("base64", "");
    if (base64.is_empty()) {
        return payload;
    }

    payload["image_base64"] = base64;
    payload["mime_type"] = image.get("mime_type", "image/png");
    payload["format"] = image.get("format", "png");
    payload["path"] = file.get("path", "");
    payload["width"] = image.get("width", 0);
    payload["height"] = image.get("height", 0);
    payload["byte_size"] = image.get("byte_size", 0);
    return payload;
}

godot::String failed_section(const godot::Dictionary &file, int index) {
    godot::PackedStringArray lines;
    lines.append("## " + file_path_for_heading(file, index));
    lines.append("Status: " + godot::String(file.get("status", "failed")));
    if (file.has("error")) {
        lines.append("Error:\n" + godot::String(file.get("error", "")));
    }
    if (file.has("resolution")) {
        lines.append("Resolution:\n" + godot::String(file.get("resolution", "")));
    }
    return godot::String("\n").join(lines);
}

} // namespace

godot::Dictionary format_read_file(const godot::Dictionary &raw_result) {
    bool success = raw_result.get("success", false);
    godot::Variant files_var = raw_result.get("files", godot::Variant());
    godot::Array files;
    if (files_var.get_type() == godot::Variant::ARRAY) {
        files = files_var;
    }

    godot::Array targets;
    godot::Array image_payloads;
    int budget_tokens = read_file_budget_tokens(files.size());
    int used_tokens = 0;
    godot::PackedStringArray sections;
    godot::Array large_indices;
    godot::Array section_entries;
    bool has_raw_failure = !success;
    int raw_success_count = 0;
    int raw_failure_count = 0;
    bool has_preview = false;

    for (int i = 0; i < files.size(); i++) {
        if (files[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary file = files[i];
        godot::String kind = file.get("kind", "");
        godot::String raw_status = file.get("status", "");
        if (raw_status == "success") {
            raw_success_count++;
        } else {
            raw_failure_count++;
        }
        targets.append(target_metadata(file));

        godot::String section;
        godot::String text;
        bool large_text = false;
        if (kind == "text") {
            text = file.get("text", "");
            section = text_section(file, i, text, false, estimate_tokens(text), estimate_tokens(text));
            large_text = estimate_tokens(section) > kMinLargeTextPreviewTokens;
        } else if (kind == "image") {
            section = image_section(file, i);
            godot::Dictionary image_payload = image_payload_for_file(file);
            if (!image_payload.is_empty()) {
                image_payloads.append(image_payload);
            }
        } else {
            section = failed_section(file, i);
            has_raw_failure = true;
        }

        godot::Dictionary boxed;
        boxed["tokens"] = estimate_tokens(section);
        boxed["large_text"] = large_text;
        boxed["section"] = section;
        boxed["text"] = text;
        boxed["file"] = file;
        boxed["source_index"] = i;
        section_entries.append(boxed);
        if (large_text) {
            large_indices.append(section_entries.size() - 1);
        }
    }

    for (int i = 0; i < section_entries.size(); i++) {
        godot::Dictionary entry = section_entries[i];
        if ((bool)entry.get("large_text", false)) {
            continue;
        }
        sections.append(godot::String(entry.get("section", "")));
        used_tokens += static_cast<int>(entry.get("tokens", 0));
    }

    int remaining_tokens = budget_tokens - used_tokens;
    int large_count = large_indices.size();
    int per_large_budget = large_count > 0 ? remaining_tokens / large_count : 0;
    if (large_count > 0 &&
        per_large_budget < kMinLargeTextPreviewTokens &&
        remaining_tokens > 0) {
        per_large_budget = remaining_tokens / large_count;
    }

    for (int i = 0; i < large_indices.size(); i++) {
        int entry_index = static_cast<int>(large_indices[i]);
        godot::Dictionary entry = section_entries[entry_index];
        godot::Dictionary file = entry["file"];
        godot::String text = entry.get("text", "");
        int section_budget = per_large_budget <= 0 ? 1 : per_large_budget;
        godot::String preview = preview_text_by_budget(text, section_budget);
        bool previewed = preview.length() < text.length();
        if (previewed) {
            has_preview = true;
        }
        sections.append(text_section(
            file,
            static_cast<int>(entry.get("source_index", 0)),
            preview,
            previewed,
            estimate_tokens(preview),
            estimate_tokens(text)
        ));
    }

    godot::String status = "success";
    if (files.size() == 0 && raw_result.has("error")) {
        status = "failed";
    } else if (raw_failure_count > 0 && raw_success_count == 0) {
        status = "failed";
    } else if (has_raw_failure || raw_failure_count > 0 || has_preview) {
        status = "partial";
    }

    godot::PackedStringArray header;
    header.append("Tool: read_file");
    header.append("Status: " + status);
    header.append(files.size() > 0 ? "Scope: " + scope_for_files(files) : "Scope: unknown");
    if (files.size() == 0 && raw_result.has("error")) {
        header.append("");
        header.append("Error:\n" + godot::String(raw_result.get("error", "")));
    }
    sections.insert(0, godot::String("\n").join(header));

    godot::Dictionary metadata = make_base_metadata("read_file", "read_file-md-v1", status);
    metadata["targets"] = targets;
    metadata["budget_tokens"] = budget_tokens;
    metadata["previewed"] = has_preview;
    godot::Dictionary envelope =
        make_envelope(godot::String("\n\n").join(sections), metadata, success);
    if (!image_payloads.is_empty()) {
        godot::Dictionary first_image = image_payloads[0];
        envelope["image_base64"] = first_image.get("image_base64", "");
        envelope["mime_type"] = first_image.get("mime_type", "image/png");
        envelope["format"] = first_image.get("format", "png");
        envelope["images"] = image_payloads;
    }
    return envelope;
}

} // namespace fennara::tool_results
