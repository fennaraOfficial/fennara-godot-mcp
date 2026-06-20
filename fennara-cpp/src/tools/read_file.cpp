#include "fennara/tools/read_file.hpp"
#include "fennara/addon_access.hpp"
#include "fennara/helpers.hpp"
#include "fennara/logger.hpp"
#include "fennara/tools/file_ops/common.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/marshalls.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace fennara {

namespace {

constexpr int kMaxBatchFiles = 5;
constexpr const char *kResultVersion = "read-file-result-v1";

godot::String extension_of(const godot::String &path) {
    int dot = path.rfind(".");
    if (dot < 0) return "";
    return path.substr(dot + 1).to_lower();
}

bool is_supported_image_extension(const godot::String &ext) {
    return ext == "png" || ext == "jpg" || ext == "jpeg" ||
           ext == "webp" || ext == "gif";
}

godot::String mime_for_image_extension(const godot::String &ext) {
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "webp") return "image/webp";
    if (ext == "gif") return "image/gif";
    return "image/png";
}

godot::String language_for_extension(const godot::String &ext) {
    if (ext == "gd") return "gdscript";
    if (ext == "json") return "json";
    if (ext == "ts") return "ts";
    if (ext == "tsx") return "tsx";
    if (ext == "js") return "js";
    if (ext == "cpp" || ext == "hpp" || ext == "h") return "cpp";
    if (ext == "py") return "python";
    if (ext == "md") return "markdown";
    if (ext == "tres") return "ini";
    return "text";
}

godot::Dictionary make_failed_file(const godot::String &path,
                                   const godot::String &error) {
    godot::Dictionary result;
    result["status"] = "failed";
    result["kind"] = "failed";
    result["path"] = path;
    result["error"] = error;
    return result;
}

godot::Dictionary make_blocked_file(const godot::Dictionary &blocked) {
    godot::Dictionary result = make_failed_file(
        blocked.get("blocked_path", ""),
        blocked.get("error", "Path is blocked."));
    result["status"] = "blocked";
    result["block_reason"] = blocked.get("block_reason", "");
    result["blocked_path"] = blocked.get("blocked_path", "");
    result["blocked_addon_root"] = blocked.get("blocked_addon_root", "");
    if (blocked.has("resolution")) {
        result["resolution"] = blocked["resolution"];
    }
    return result;
}

godot::Dictionary build_summary(const godot::Array &files) {
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
    summary["status"] = failure_count == 0 ? "success" : "failed";
    summary["requested_count"] = files.size();
    summary["success_count"] = success_count;
    summary["failure_count"] = failure_count;
    summary["message"] =
        "Processed " + godot::String::num_int64(files.size()) + " file(s) (" +
        godot::String::num_int64(success_count) + " succeeded, " +
        godot::String::num_int64(failure_count) + " failed)";
    return summary;
}

godot::Dictionary make_argument_error(const godot::String &message) {
    godot::Dictionary result;
    godot::Array files;
    godot::Dictionary summary;
    summary["status"] = "failed";
    summary["requested_count"] = 0;
    summary["success_count"] = 0;
    summary["failure_count"] = 1;
    summary["message"] = message;

    result["success"] = false;
    result["tool_name"] = "read_file";
    result["format_version"] = kResultVersion;
    result["summary"] = summary;
    result["files"] = files;
    result["error"] = message;
    return result;
}

std::filesystem::path native_path_for(const godot::String &path) {
    godot::String globalized =
        godot::ProjectSettings::get_singleton()->globalize_path(path);
    std::string utf8 = globalized.utf8().get_data();
    return std::filesystem::u8path(utf8);
}

bool native_file_exists(const std::filesystem::path &path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

bool read_native_bytes(const std::filesystem::path &path,
                       godot::PackedByteArray &bytes_out,
                       godot::String &error_out) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error_out = "Cannot open";
        return false;
    }

    stream.unsetf(std::ios::skipws);
    std::vector<char> data((std::istream_iterator<char>(stream)),
                           std::istream_iterator<char>());
    if (stream.bad()) {
        error_out = "Failed while reading";
        return false;
    }

    bytes_out.resize(static_cast<int64_t>(data.size()));
    if (!data.empty()) {
        std::memcpy(bytes_out.ptrw(), data.data(), data.size());
    }
    return true;
}

bool read_native_text(const std::filesystem::path &path,
                      godot::String &text_out,
                      godot::String &error_out) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error_out = "Cannot open";
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(stream)),
                     std::istreambuf_iterator<char>());
    if (stream.bad()) {
        error_out = "Failed while reading";
        return false;
    }

    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    text_out = godot::String::utf8(text.c_str(), text.length());
    return true;
}

bool read_godot_bytes(const godot::String &path,
                      godot::PackedByteArray &bytes_out,
                      godot::String &error_out) {
    godot::Ref<godot::FileAccess> file =
        godot::FileAccess::open(path, godot::FileAccess::READ);
    if (!file.is_valid()) {
        error_out = "Cannot open";
        return false;
    }

    uint64_t length = file->get_length();
    bytes_out = file->get_buffer(length);
    file->close();
    return true;
}

bool read_godot_text(const godot::String &path,
                     godot::String &text_out,
                     godot::String &error_out) {
    godot::Ref<godot::FileAccess> file =
        godot::FileAccess::open(path, godot::FileAccess::READ);
    if (!file.is_valid()) {
        error_out = "Cannot open";
        return false;
    }

    text_out = file->get_as_text();
    file->close();
    return true;
}

void append_image_dimensions(godot::Dictionary &result,
                             const godot::PackedByteArray &bytes,
                             const godot::String &ext) {
    godot::Ref<godot::Image> image;
    image.instantiate();

    godot::Error err = godot::ERR_UNAVAILABLE;
    if (ext == "png") {
        err = image->load_png_from_buffer(bytes);
    } else if (ext == "jpg" || ext == "jpeg") {
        err = image->load_jpg_from_buffer(bytes);
    } else if (ext == "webp") {
        err = image->load_webp_from_buffer(bytes);
    }

    if (err == godot::OK) {
        result["width"] = image->get_width();
        result["height"] = image->get_height();
    }
}

godot::Dictionary read_single_file(const godot::String &file_path,
                                   const godot::Dictionary &shared_args) {
    godot::Dictionary result;

    if (file_path.is_empty()) {
        FLOG_ERR("Read: file_path required");
        return make_failed_file(file_path, "file_path required");
    }
    FLOG_TOOL(godot::String("Read: path=") + file_path);

    godot::String normalized;
    godot::String scope_error;
    if (!file_ops::normalize_scoped_path(file_path, normalized, scope_error,
                                         "read_file", true)) {
        FLOG_ERR(godot::String("Read: blocked out-of-scope path ") + file_path);
        return make_failed_file(file_path, scope_error);
    }
    bool user_path = normalized.begins_with("user://");
    if (!user_path) {
        godot::Dictionary addon_block;
        if (!addon_access::is_path_allowed(normalized, false, addon_block)) {
            FLOG_ERR(godot::String("Read: blocked addon path ") + normalized);
            return make_blocked_file(addon_block);
        }

        if (normalized != "res://project.godot" && is_protected_path(normalized)) {
            FLOG_ERR(godot::String("Read: protected path ") + file_path);
            return make_failed_file(normalized, protected_path_error(normalized));
        }
    }

    if (normalized.ends_with(".tscn")) {
        FLOG_ERR(godot::String("Read: rejected .tscn file ") + file_path);
        return make_failed_file(
            normalized,
            "Cannot read .tscn scene files. Use get_scene_tree tool to view scene structure."
        );
    }

    godot::String path = normalized;
    std::filesystem::path native_path = native_path_for(path);

    if (!native_file_exists(native_path) &&
        !godot::FileAccess::file_exists(path)) {
        FLOG_ERR(godot::String("Read: file not found ") + file_path);
        return make_failed_file(normalized, "File not found: " + file_path);
    }

    godot::String ext = extension_of(normalized);
    if (is_supported_image_extension(ext)) {
        godot::PackedByteArray bytes;
        godot::String read_error;
        if (!read_native_bytes(native_path, bytes, read_error) &&
            !read_godot_bytes(path, bytes, read_error)) {
            FLOG_ERR(godot::String("Read: cannot open image ") + file_path);
            return make_failed_file(normalized, read_error + ": " + file_path);
        }

        result["status"] = "success";
        result["kind"] = "image";
        result["path"] = normalized;
        godot::Dictionary image;
        image["mime_type"] = mime_for_image_extension(ext);
        image["format"] = ext;
        image["byte_size"] = static_cast<int64_t>(bytes.size());
        image["base64"] =
            godot::Marshalls::get_singleton()->raw_to_base64(bytes);
        append_image_dimensions(image, bytes, ext);
        result["image"] = image;
        FLOG_TOOL(godot::String("Read: image done, bytes=") +
                  godot::String::num_int64(bytes.size()));
        return result;
    }

    godot::String text;
    godot::String read_error;
    if (!read_native_text(native_path, text, read_error) &&
        !read_godot_text(path, text, read_error)) {
        FLOG_ERR(godot::String("Read: cannot open text ") + file_path);
        return make_failed_file(normalized, read_error + ": " + file_path);
    }

    godot::PackedStringArray lines = text.replace("\r\n", "\n").split("\n");

    int total_lines = lines.size();
    int start_line = shared_args.get("start_line", 1);
    int end_line = shared_args.get("end_line", total_lines);

    if (start_line < 1) {
        start_line = 1;
    }
    if (end_line > total_lines) {
        end_line = total_lines;
    }
    if (start_line > end_line) {
        FLOG_ERR(godot::String("Read: invalid range start=") +
                 godot::String::num_int64(start_line) + " end=" +
                 godot::String::num_int64(end_line));
        return make_failed_file(
            normalized,
            "start_line (" + godot::String::num_int64(start_line) +
            ") cannot be greater than end_line (" +
            godot::String::num_int64(end_line) + ")"
        );
    }

    int start_idx = start_line - 1;
    int end_idx = end_line - 1;

    godot::PackedStringArray formatted;
    for (int i = start_idx; i <= end_idx; i++) {
        godot::String line =
            godot::String::num_int64(i + 1) + " => " + lines[i];
        formatted.append(line);
    }

    result["status"] = "success";
    result["kind"] = "text";
    result["path"] = normalized;
    result["language"] = language_for_extension(ext);
    godot::Dictionary range;
    range["start_line"] = start_line;
    range["end_line"] = start_line + formatted.size() - 1;
    range["returned_lines"] = formatted.size();
    range["total_lines"] = total_lines;
    result["range"] = range;
    result["text"] = godot::String("\n").join(formatted);
    FLOG_TOOL(godot::String("Read: done, lines=") +
              godot::String::num_int64(total_lines) + " returned=" +
              godot::String::num_int64(formatted.size()));
    return result;
}

} // namespace

void FennaraReadFileTool::_bind_methods() {
    godot::ClassDB::bind_static_method(
        "FennaraReadFileTool", godot::D_METHOD("execute", "args"),
        &FennaraReadFileTool::execute);
}

godot::Dictionary FennaraReadFileTool::execute(const godot::Dictionary &args) {
    godot::Dictionary result;

    if (!args.has("file_paths")) {
        return make_argument_error("Missing required arg: file_paths");
    }

    godot::Variant file_paths_var = args["file_paths"];
    if (file_paths_var.get_type() != godot::Variant::ARRAY) {
        return make_argument_error("file_paths must be an array of strings");
    }

    godot::Array file_paths = file_paths_var;
    if (file_paths.is_empty()) {
        return make_argument_error("file_paths must contain at least one path");
    }
    if (file_paths.size() > kMaxBatchFiles) {
        return make_argument_error(
            "file_paths supports at most " +
            godot::String::num_int64(kMaxBatchFiles) +
            " files per call. Split larger requests into multiple calls."
        );
    }

    godot::Array files;
    for (int i = 0; i < file_paths.size(); i++) {
        godot::Variant item = file_paths[i];
        if (item.get_type() != godot::Variant::STRING) {
            files.append(make_failed_file(
                "",
                "file_paths[" + godot::String::num_int64(i) +
                "] must be a string"
            ));
            continue;
        }

        files.append(read_single_file(item, args));
    }

    godot::Dictionary summary = build_summary(files);
    result["success"] = int(summary["failure_count"]) == 0;
    result["tool_name"] = "read_file";
    result["format_version"] = kResultVersion;
    result["summary"] = summary;
    result["files"] = files;
    if (!(bool)result["success"]) {
        result["error"] = summary["message"];
    }
    return result;
}

} // namespace fennara
