#include "fennara/tools/file_ops/grep.hpp"

#include "fennara/addon_access.hpp"
#include "fennara/helpers.hpp"
#include "fennara/tools/file_ops/common.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace fennara::file_ops {

namespace {

godot::String bundled_rg_name() {
#if defined(_WIN32) || defined(_WIN64) || defined(WINDOWS_ENABLED)
    return "rg-windows-x86_64.exe";
#elif defined(__APPLE__)
#if defined(__aarch64__) || defined(__arm64__)
    return "rg-macos-arm64";
#else
    return "rg-macos-x86_64";
#endif
#elif defined(__linux__)
#if defined(__aarch64__) || defined(__arm64__)
    return "rg-linux-arm64";
#else
    return "rg-linux-x86_64";
#endif
#else
    return "rg";
#endif
}

godot::String legacy_rg_name() {
#if defined(_WIN32) || defined(_WIN64) || defined(WINDOWS_ENABLED)
    return "rg.exe";
#else
    return "rg";
#endif
}

}

// Resolve the bundled ripgrep binary path.
static godot::String get_rg_path() {
    godot::ProjectSettings *settings = godot::ProjectSettings::get_singleton();
    godot::String res_path = "res://addons/fennara/bin/" + bundled_rg_name();
    godot::String path = settings->globalize_path(res_path);

    godot::Array output;
    int exit_code = godot::OS::get_singleton()->execute(
        path, godot::PackedStringArray({"--version"}), output);
    if (exit_code == 0) {
        return path;
    }

    return settings->globalize_path("res://addons/fennara/bin/" + legacy_rg_name());
}

// Check if ripgrep binary exists and is executable.
static bool rg_available() {
    godot::String rg_path = get_rg_path();
    godot::Array output;
    int exit_code = godot::OS::get_singleton()->execute(
        rg_path, godot::PackedStringArray({"--version"}), output);
    return exit_code == 0;
}

static bool is_disallowed_rg_arg(const godot::String &arg) {
    return arg == "--json" || arg == "--no-json" || arg == "--files" ||
           arg == "--files-with-matches" || arg == "--files-without-match" ||
           arg == "-l" || arg == "-L" || arg == "-c" ||
           arg == "--count" || arg == "--count-matches" ||
           arg == "--type-list" || arg == "--help" || arg == "-h" ||
           arg == "--version" || arg == "-V" || arg == "-M" ||
           arg.begins_with("-M") || arg == "--max-columns" ||
           arg.begins_with("--max-columns=");
}

static bool rg_option_takes_value(const godot::String &arg) {
    return arg == "-A" || arg == "-B" || arg == "-C" || arg == "-E" ||
           arg == "-f" || arg == "-g" || arg == "-j" || arg == "-m" ||
           arg == "-M" || arg == "-r" || arg == "-t" ||
           arg == "-T" || arg == "--after-context" ||
           arg == "--before-context" || arg == "--context" ||
           arg == "--encoding" || arg == "--engine" || arg == "--file" ||
           arg == "--glob" || arg == "--iglob" || arg == "--max-columns" ||
           arg == "--max-count" || arg == "--max-depth" ||
           arg == "--max-filesize" || arg == "--path-separator" ||
           arg == "--pre" || arg == "--pre-glob" || arg == "--regexp" ||
           arg == "--replace" || arg == "--sort" || arg == "--sortr" ||
           arg == "--threads" || arg == "--type" || arg == "--type-add" ||
           arg == "--type-clear" || arg == "--type-not";
}

static bool args_include_context(const godot::PackedStringArray &args) {
    for (int i = 0; i < args.size(); i++) {
        godot::String arg = args[i];
        if (arg == "-A" || arg == "-B" || arg == "-C" ||
            arg == "--after-context" || arg == "--before-context" ||
            arg == "--context" || arg.begins_with("-A") ||
            arg.begins_with("-B") || arg.begins_with("-C") ||
            arg.begins_with("--after-context=") ||
            arg.begins_with("--before-context=") ||
            arg.begins_with("--context=")) {
            return true;
        }
    }
    return false;
}

static bool is_rg_executable_token(const godot::String &token) {
    godot::String lower = token.to_lower();
    return lower == "rg" || lower == "rg.exe" || lower.ends_with("/rg") ||
           lower.ends_with("\\rg") || lower.ends_with("/rg.exe") ||
           lower.ends_with("\\rg.exe");
}

static bool normalize_search_path(const godot::String &path_in,
                                  godot::String &normalized_path_out,
                                  godot::String &error_out) {
    return normalize_scoped_path(path_in, normalized_path_out, error_out, "rg",
                                 true);
}

static godot::Array collect_search_paths(const godot::Dictionary &op,
                                         const godot::Array &extra_paths,
                                         godot::Array &errors) {
    godot::Array normalized_paths;

    godot::Array raw_paths = fennara::safe_get_array(op, "paths");
    if (!raw_paths.is_empty()) {
        for (int i = 0; i < raw_paths.size(); i++) {
            godot::String normalized_path;
            godot::String error;
            if (!normalize_search_path(godot::String(raw_paths[i]), normalized_path,
                                       error)) {
                errors.append(error);
                continue;
            }
            normalized_paths.append(normalized_path);
        }
    }

    if (normalized_paths.is_empty()) {
        godot::String normalized_path;
        godot::String error;
        if (!normalize_search_path(op.get("path", godot::String("res://")),
                                   normalized_path, error)) {
            errors.append(error);
        } else {
            normalized_paths.append(normalized_path);
        }
    }

    for (int i = 0; i < extra_paths.size(); i++) {
        godot::String normalized_path;
        godot::String error;
        if (!normalize_search_path(godot::String(extra_paths[i]), normalized_path,
                                   error)) {
            errors.append(error);
            continue;
        }
        normalized_paths.append(normalized_path);
    }

    return normalized_paths;
}

static bool split_command_args_and_paths(const godot::PackedStringArray &tokens,
                                         godot::PackedStringArray &args_out,
                                         godot::Array &paths_out,
                                         godot::String &error_out) {
    bool consume_only_paths = false;
    bool saw_pattern = false;
    bool explicit_pattern_mode = false;

    for (int i = 0; i < tokens.size(); i++) {
        godot::String token = tokens[i];
        if (token.is_empty()) {
            continue;
        }

        if (i == 0 && is_rg_executable_token(token)) {
            continue;
        }

        if (consume_only_paths) {
            paths_out.append(token);
            continue;
        }

        if (token == "--") {
            consume_only_paths = true;
            continue;
        }

        if (token == "-e" || token == "--regexp") {
            explicit_pattern_mode = true;
            args_out.append(token);
            if (i + 1 >= tokens.size()) {
                error_out = godot::String("rg argument requires a value: ") + token;
                return false;
            }
            args_out.append(tokens[++i]);
            continue;
        }

        if (token.begins_with("-e") && token.length() > 2) {
            explicit_pattern_mode = true;
            args_out.append(token);
            continue;
        }

        if (token.begins_with("--regexp=")) {
            explicit_pattern_mode = true;
            args_out.append(token);
            continue;
        }

        if (token.begins_with("-")) {
            args_out.append(token);
            if (rg_option_takes_value(token)) {
                if (i + 1 >= tokens.size()) {
                    error_out =
                        godot::String("rg argument requires a value: ") + token;
                    return false;
                }
                args_out.append(tokens[++i]);
            }
            continue;
        }

        if (!saw_pattern && !explicit_pattern_mode) {
            args_out.append(token);
            saw_pattern = true;
            continue;
        }

        paths_out.append(token);
    }

    return true;
}

godot::Dictionary rg(const godot::Dictionary &op, godot::Array &warnings,
                     godot::Array &errors) {
    godot::Dictionary result;
    result["op"] = "rg";

    godot::String pattern = op.get("pattern", "");
    godot::String file_pattern = op.get("file_pattern", "");
    bool ignore_case = op.get("ignore_case", false);
    int context_lines = op.get("context_lines", 0);
    bool include_unimportant = op.get("include_unimportant", false);
    int max_results = op.get("max_results", 0);
    godot::Array extra_paths;

    godot::PackedStringArray rg_args;
    if (op.has("args")) {
        godot::Array raw_args = fennara::safe_get_array(op, "args");
        godot::PackedStringArray provided_args;
        for (int i = 0; i < raw_args.size(); i++) {
            provided_args.append(godot::String(raw_args[i]));
        }

        godot::String parse_error;
        godot::PackedStringArray parsed_args;
        if (!split_command_args_and_paths(provided_args, parsed_args, extra_paths,
                                          parse_error)) {
            errors.append(parse_error);
            result["success"] = false;
            result["error"] = parse_error;
            return result;
        }

        for (int i = 0; i < parsed_args.size(); i++) {
            rg_args.append(parsed_args[i]);
        }
    }

    if (op.has("command")) {
        godot::PackedStringArray parsed_command;
        godot::String parse_error;
        if (!tokenize_command(godot::String(op["command"]), parsed_command,
                              parse_error)) {
            errors.append(parse_error);
            result["success"] = false;
            result["error"] = parse_error;
            return result;
        }

        godot::PackedStringArray parsed_args;
        if (!split_command_args_and_paths(parsed_command, parsed_args, extra_paths,
                                          parse_error)) {
            errors.append(parse_error);
            result["success"] = false;
            result["error"] = parse_error;
            return result;
        }

        for (int i = 0; i < parsed_args.size(); i++) {
            rg_args.append(parsed_args[i]);
        }
    }

    if (!pattern.is_empty() && !op.has("args") && !op.has("command")) {
        rg_args.append(pattern);
    }

    if (rg_args.is_empty()) {
        godot::String msg =
            "rg operation requires 'pattern', 'args', or 'command'";
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    for (int i = 0; i < rg_args.size(); i++) {
        if (is_disallowed_rg_arg(rg_args[i])) {
            godot::String msg =
                godot::String("Unsupported rg argument for managed search: ") +
                rg_args[i];
            errors.append(msg);
            result["success"] = false;
            result["error"] = msg;
            return result;
        }
    }

    if (!pattern.is_empty() && (op.has("args") || op.has("command"))) {
        bool has_explicit_pattern = false;
        for (int i = 0; i < rg_args.size(); i++) {
            godot::String arg = rg_args[i];
            if (!arg.begins_with("-") || arg == "-e" || arg == "--regexp" ||
                arg.begins_with("-e") || arg.begins_with("--regexp=")) {
                has_explicit_pattern = true;
                break;
            }
        }
        if (!has_explicit_pattern) {
            rg_args.append(pattern);
        }
    }

    godot::Array path_errors;
    godot::Array search_paths = collect_search_paths(op, extra_paths, path_errors);
    for (int i = 0; i < search_paths.size(); i++) {
        godot::Dictionary addon_block;
        if (!fennara::addon_access::is_path_allowed(search_paths[i], false, addon_block)) {
            path_errors.append(addon_block.get("error", "Search path is blocked."));
            result.merge(addon_block);
            result["op"] = "rg";
            result["paths"] = search_paths;
            break;
        }
    }
    if (!path_errors.is_empty()) {
        for (int i = 0; i < path_errors.size(); i++) {
            errors.append(path_errors[i]);
        }
        result["success"] = false;
        result["error"] = godot::String(path_errors[0]);
        return result;
    }

    result["pattern"] = pattern;
    result["paths"] = search_paths;

    // Check ripgrep availability
    godot::String rg_path = get_rg_path();
    if (!rg_available()) {
        godot::String msg =
            "ripgrep (rg) not found at: " + rg_path +
            ". Ensure the platform ripgrep binary is in addons/fennara/bin/.";
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    // Build ripgrep arguments. Keep executable, paths, and metadata filters
    // constrained; model-facing output budgets are applied by the backend.
    godot::PackedStringArray final_args;

    if (ignore_case) {
        final_args.append("-i");
    }

    if (op.get("include_hidden", false)) {
        final_args.append("--hidden");
    }

    if (context_lines > 0) {
        final_args.append("-C");
        final_args.append(godot::String::num_int64(context_lines));
    }

    if (max_results > 0) {
        final_args.append("-m");
        final_args.append(godot::String::num_int64(max_results));
    }

    if (!file_pattern.is_empty()) {
        final_args.append("--glob");
        final_args.append(file_pattern);
    }

    // Ignore binary files.
    final_args.append("--no-messages");
    final_args.append("--with-filename");
    final_args.append("--no-heading");
    final_args.append("--path-separator");
    final_args.append("/");

    if (!include_unimportant) {
        final_args.append("--glob");
        final_args.append("!*.uid");
        final_args.append("--glob");
        final_args.append("!*.import");
        final_args.append("--glob");
        final_args.append("!*.remap");
    }

    for (int i = 0; i < rg_args.size(); i++) {
        final_args.append(rg_args[i]);
    }

    final_args.append("--");
    for (int i = 0; i < search_paths.size(); i++) {
        godot::String search_path = search_paths[i];
        final_args.append(
            godot::ProjectSettings::get_singleton()->globalize_path(search_path));
    }

    // Execute ripgrep
    godot::Array output;
    int exit_code =
        godot::OS::get_singleton()->execute(rg_path, final_args, output);

    // exit code 1 means no matches (not an error)
    // exit code 2+ means actual error
    if (exit_code >= 2) {
        godot::String out_text =
            output.size() > 0 ? godot::String(output[0]) : "";
        godot::String msg =
            godot::String("ripgrep error (exit ") +
            godot::String::num_int64(exit_code) + "): " + out_text;
        errors.append(msg);
        result["success"] = false;
        result["error"] = msg;
        return result;
    }

    godot::String out_text = output.size() > 0 ? godot::String(output[0]) : "";

    godot::String project_path =
        godot::ProjectSettings::get_singleton()->globalize_path("res://")
            .replace("\\", "/");
    godot::String normalized_output = out_text.replace("\\", "/");
    normalized_output = normalized_output.replace(project_path, "res://");

    int output_lines = normalized_output.is_empty()
                           ? 0
                           : normalized_output.split("\n").size();

    bool has_context_output = context_lines > 0 || args_include_context(rg_args);
    int count = -1;
    if (!normalized_output.is_empty() && !has_context_output) {
        count = 0;
        godot::PackedStringArray lines = normalized_output.split("\n");
        for (int i = 0; i < lines.size(); i++) {
            godot::String line = lines[i];
            if (!line.is_empty() && !line.begins_with("--")) count++;
        }
    }

    result["success"] = true;
    result["output"] = normalized_output;
    if (count >= 0) result["count"] = count;
    result["line_count"] = output_lines;
    result["args"] = final_args;
    return result;
}

} // namespace fennara::file_ops
