#include "fennara/lsp/csharp_build_issues.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_paths.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <cstdint>

namespace fennara::csharp_build_issues {
namespace {

constexpr uint64_t kTimeWindowPaddingMs = 5000;

godot::String normalize_path(godot::String path) {
    return path.replace("\\", "/").trim_suffix("/");
}

godot::String project_root() {
    godot::ProjectSettings *settings = godot::ProjectSettings::get_singleton();
    if (settings == nullptr) {
        return "";
    }
    return normalize_path(settings->globalize_path("res://"));
}

void append_if_present(godot::PackedStringArray &paths, const godot::String &path) {
    if (!path.is_empty() && !paths.has(path)) {
        paths.append(path);
    }
}

godot::PackedStringArray build_log_roots() {
    godot::PackedStringArray roots;

    godot::EditorInterface *editor = godot::EditorInterface::get_singleton();
    if (editor != nullptr && editor->get_editor_paths() != nullptr) {
        append_if_present(
            roots,
            editor->get_editor_paths()->get_data_dir().path_join("mono").path_join("build_logs"));
    }

    godot::OS *os = godot::OS::get_singleton();
    if (os == nullptr) {
        return roots;
    }

    const godot::String os_name = os->get_name();
    if (os_name == "Windows") {
        const godot::String app_data = os->get_environment("APPDATA");
        if (!app_data.is_empty()) {
            append_if_present(roots, app_data.path_join("Godot").path_join("mono").path_join("build_logs"));
        }
    } else if (os_name == "macOS") {
        const godot::String home = os->get_environment("HOME");
        if (!home.is_empty()) {
            append_if_present(
                roots,
                home.path_join("Library")
                    .path_join("Application Support")
                    .path_join("Godot")
                    .path_join("mono")
                    .path_join("build_logs"));
        }
    } else {
        const godot::String xdg_data_home = os->get_environment("XDG_DATA_HOME");
        if (!xdg_data_home.is_empty()) {
            append_if_present(roots, xdg_data_home.path_join("godot").path_join("mono").path_join("build_logs"));
        }
        const godot::String home = os->get_environment("HOME");
        if (!home.is_empty()) {
            append_if_present(
                roots,
                home.path_join(".local").path_join("share").path_join("godot").path_join("mono").path_join("build_logs"));
        }
    }

    return roots;
}

godot::Dictionary make_issue(const godot::PackedStringArray &columns) {
    godot::Dictionary issue;
    issue["severity"] = columns.size() > 0 ? columns[0] : godot::String();
    issue["file"] = columns.size() > 1 ? columns[1] : godot::String();
    issue["line"] = columns.size() > 2 ? columns[2].to_int() : 0;
    issue["column"] = columns.size() > 3 ? columns[3].to_int() : 0;
    issue["code"] = columns.size() > 4 ? columns[4] : godot::String();
    issue["message"] = columns.size() > 5 ? columns[5] : godot::String();
    issue["project"] = columns.size() > 6 ? columns[6] : godot::String();
    return issue;
}

bool issue_belongs_to_project(const godot::Dictionary &issue, const godot::String &root) {
    if (root.is_empty()) {
        return true;
    }
    const godot::String file = normalize_path(issue.get("file", ""));
    const godot::String project = normalize_path(issue.get("project", ""));
    return file.begins_with(root) || project.begins_with(root);
}

bool candidate_matches_time_window(const godot::Dictionary &candidate,
                                   uint64_t window_start_unix_ms,
                                   uint64_t window_end_unix_ms) {
    if (window_start_unix_ms == 0 || window_end_unix_ms == 0) {
        return true;
    }

    uint64_t modified_ms =
        static_cast<uint64_t>(static_cast<int64_t>(candidate.get("modified_unix", 0))) *
        1000;
    uint64_t start = window_start_unix_ms > kTimeWindowPaddingMs
                         ? window_start_unix_ms - kTimeWindowPaddingMs
                         : 0;
    uint64_t end = window_end_unix_ms + kTimeWindowPaddingMs;
    return modified_ms >= start && modified_ms <= end;
}

godot::Array parse_issues(const godot::String &path,
                          const godot::String &root,
                          bool require_project_match) {
    godot::Array issues;
    godot::Ref<godot::FileAccess> file =
        godot::FileAccess::open(path, godot::FileAccess::READ);
    if (file.is_null()) {
        return issues;
    }

    while (!file->eof_reached()) {
        godot::PackedStringArray columns = file->get_csv_line();
        if (columns.is_empty() || (columns.size() == 1 && columns[0].is_empty())) {
            continue;
        }
        godot::Dictionary issue = make_issue(columns);
        if (!require_project_match || issue_belongs_to_project(issue, root)) {
            issues.append(issue);
        }
    }
    return issues;
}

void scan_build_log_root(const godot::String &root,
                         godot::Array &candidates) {
    godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(root);
    if (dir.is_null()) {
        return;
    }

    dir->list_dir_begin();
    while (true) {
        godot::String name = dir->get_next();
        if (name.is_empty()) {
            break;
        }
        if (!dir->current_is_dir() || name == "." || name == "..") {
            continue;
        }

        const godot::String issues_path = root.path_join(name).path_join("msbuild_issues.csv");
        if (!godot::FileAccess::file_exists(issues_path)) {
            continue;
        }

        godot::Dictionary candidate;
        candidate["issues_path"] = issues_path;
        candidate["log_path"] = root.path_join(name).path_join("msbuild_log.txt");
        candidate["modified_unix"] =
            static_cast<int64_t>(godot::FileAccess::get_modified_time(issues_path));
        candidates.append(candidate);
    }
    dir->list_dir_end();
}

godot::Dictionary newest_project_candidate(const godot::Array &candidates,
                                           const godot::String &root,
                                           uint64_t window_start_unix_ms,
                                           uint64_t window_end_unix_ms,
                                           int &project_match_count,
                                           int &time_filtered_count) {
    godot::Dictionary newest;
    int64_t newest_time = -1;
    for (int i = 0; i < candidates.size(); i++) {
        if (candidates[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary candidate = candidates[i];
        godot::Array issues =
            parse_issues(candidate.get("issues_path", ""), root, true);
        if (issues.is_empty()) {
            continue;
        }
        project_match_count++;
        if (!candidate_matches_time_window(
                candidate, window_start_unix_ms, window_end_unix_ms)) {
            time_filtered_count++;
            continue;
        }
        int64_t modified = static_cast<int64_t>(candidate.get("modified_unix", 0));
        if (modified > newest_time) {
            newest_time = modified;
            candidate["issues"] = issues;
            newest = candidate;
        }
    }
    return newest;
}

} // namespace

godot::Dictionary latest_snapshot(uint64_t window_start_unix_ms,
                                  uint64_t window_end_unix_ms) {
    godot::Array candidates;
    godot::PackedStringArray roots = build_log_roots();
    for (int i = 0; i < roots.size(); i++) {
        scan_build_log_root(roots[i], candidates);
    }

    godot::Dictionary result;
    result["source"] = "godot_mono_build_logs";
    result["candidate_count"] = candidates.size();
    result["time_window_start_unix_ms"] =
        static_cast<int64_t>(window_start_unix_ms);
    result["time_window_end_unix_ms"] = static_cast<int64_t>(window_end_unix_ms);
    if (candidates.is_empty()) {
        result["issue_count"] = 0;
        result["issues"] = godot::Array();
        return result;
    }

    const godot::String root = project_root();
    int project_match_count = 0;
    int time_filtered_count = 0;
    godot::Dictionary selected =
        newest_project_candidate(candidates,
                                 root,
                                 window_start_unix_ms,
                                 window_end_unix_ms,
                                 project_match_count,
                                 time_filtered_count);
    bool project_matched = !selected.is_empty();
    if (selected.is_empty()) {
        result["issue_count"] = 0;
        result["issues"] = godot::Array();
        result["project_matched"] = false;
        result["project_match_candidate_count"] = project_match_count;
        result["time_filtered_candidate_count"] = time_filtered_count;
        return result;
    }

    godot::Array issues = selected.has("issues")
                              ? godot::Array(selected["issues"])
                              : parse_issues(selected.get("issues_path", ""), root, false);
    result["issues"] = issues;
    result["issue_count"] = issues.size();
    result["issues_path"] = selected.get("issues_path", "");
    result["log_path"] = selected.get("log_path", "");
    result["modified_unix"] = selected.get("modified_unix", 0);
    result["project_matched"] = project_matched;
    result["time_matched"] = true;
    result["project_match_candidate_count"] = project_match_count;
    result["time_filtered_candidate_count"] = time_filtered_count;
    return result;
}

} // namespace fennara::csharp_build_issues
