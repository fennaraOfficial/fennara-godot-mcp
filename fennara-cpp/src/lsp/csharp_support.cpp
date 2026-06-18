#include "fennara/lsp/csharp_support.hpp"

#include "fennara/app_paths.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>

namespace fennara::csharp_support {
namespace {

constexpr int kMaxScanDepth = 5;

bool should_skip_dir(const godot::String &name) {
    const godot::String lower = name.to_lower();
    return lower == ".godot" || lower == ".git" || lower == ".import" ||
           lower == "addons" || lower == "bin" || lower == "obj" ||
           lower == "node_modules";
}

godot::String project_root() {
    godot::ProjectSettings *settings = godot::ProjectSettings::get_singleton();
    if (settings == nullptr) {
        return "";
    }
    return settings->globalize_path("res://");
}

godot::String to_res_path(const godot::String &absolute_path,
                          const godot::String &root) {
    if (root.is_empty() || !absolute_path.begins_with(root)) {
        return absolute_path;
    }
    godot::String relative = absolute_path.substr(root.length()).trim_prefix("/");
    return "res://" + relative;
}

void append_project_file(godot::Array &projects,
                         const godot::String &path,
                         const godot::String &root) {
    godot::Dictionary item;
    item["path"] = to_res_path(path, root);
    item["absolute_path"] = path;
    item["type"] =
        (path.ends_with(".sln") || path.ends_with(".slnx")) ? "solution" : "project";
    projects.append(item);
}

void scan_dir(const godot::String &dir_path,
              const godot::String &root,
              int depth,
              godot::Array &projects) {
    if (depth > kMaxScanDepth) {
        return;
    }

    godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(dir_path);
    if (dir.is_null()) {
        return;
    }

    dir->list_dir_begin();
    while (true) {
        godot::String name = dir->get_next();
        if (name.is_empty()) {
            break;
        }
        if (name == "." || name == "..") {
            continue;
        }

        const godot::String child = dir_path.path_join(name);
        if (dir->current_is_dir()) {
            if (!should_skip_dir(name)) {
                scan_dir(child, root, depth + 1, projects);
            }
            continue;
        }

        if (name.ends_with(".csproj") || name.ends_with(".sln") ||
            name.ends_with(".slnx")) {
            append_project_file(projects, child, root);
        }
    }
    dir->list_dir_end();
}

godot::String setting_string(const godot::String &key) {
    godot::ProjectSettings *settings = godot::ProjectSettings::get_singleton();
    if (settings == nullptr || !settings->has_setting(key)) {
        return "";
    }
    return godot::String(settings->get_setting(key, "")).strip_edges();
}

godot::String absolute_under_root(const godot::String &root,
                                  const godot::String &relative) {
    godot::String clean = relative.strip_edges().replace("\\", "/");
    if (clean.is_empty() || clean == ".") {
        return root;
    }
    if (clean.begins_with("res://")) {
        godot::ProjectSettings *settings = godot::ProjectSettings::get_singleton();
        return settings == nullptr ? clean : settings->globalize_path(clean);
    }
    if (clean.is_absolute_path()) {
        return clean;
    }
    return root.path_join(clean);
}

godot::Array filter_projects_in_dir(const godot::Array &projects,
                                    const godot::String &dir_path,
                                    const godot::String &type) {
    godot::Array matches;
    godot::String dir = dir_path.replace("\\", "/").trim_suffix("/");
    for (int i = 0; i < projects.size(); i++) {
        godot::Dictionary project = projects[i];
        if (type != godot::String(project.get("type", ""))) {
            continue;
        }
        godot::String path = godot::String(project.get("absolute_path", "")).replace("\\", "/");
        if (path.get_base_dir().trim_suffix("/").to_lower() == dir.to_lower()) {
            matches.append(project);
        }
    }
    return matches;
}

godot::Array filter_projects_by_name(const godot::Array &projects,
                                     const godot::String &name_without_ext,
                                     const godot::String &type) {
    godot::Array matches;
    if (name_without_ext.is_empty()) {
        return matches;
    }
    godot::String expected = name_without_ext.to_lower();
    for (int i = 0; i < projects.size(); i++) {
        godot::Dictionary project = projects[i];
        if (type != godot::String(project.get("type", ""))) {
            continue;
        }
        godot::String base = godot::String(project.get("absolute_path", ""))
                                 .get_file()
                                 .get_basename()
                                 .to_lower();
        if (base == expected) {
            matches.append(project);
        }
    }
    return matches;
}

godot::Dictionary first_match(const godot::Array &matches) {
    return matches.size() == 1 ? godot::Dictionary(matches[0]) : godot::Dictionary();
}

godot::Dictionary select_project(const godot::Array &projects,
                                 const godot::String &root) {
    godot::String project_dir_setting = setting_string("dotnet/project/project_directory");
    godot::String solution_dir_setting = setting_string("dotnet/project/solution_directory");
    godot::String assembly_name = setting_string("dotnet/project/assembly_name");

    godot::String project_dir = absolute_under_root(root, project_dir_setting);
    godot::Dictionary selected =
        first_match(filter_projects_in_dir(projects, project_dir, "project"));
    if (!selected.is_empty()) {
        selected["selection_reason"] = "dotnet_project_directory";
        return selected;
    }

    godot::String solution_dir = absolute_under_root(root, solution_dir_setting);
    selected = first_match(filter_projects_in_dir(projects, solution_dir, "solution"));
    if (!selected.is_empty()) {
        selected["selection_reason"] = "dotnet_solution_directory";
        return selected;
    }

    selected = first_match(filter_projects_by_name(projects, assembly_name, "project"));
    if (!selected.is_empty()) {
        selected["selection_reason"] = "dotnet_assembly_name";
        return selected;
    }

    selected = first_match(filter_projects_by_name(projects, assembly_name, "solution"));
    if (!selected.is_empty()) {
        selected["selection_reason"] = "dotnet_assembly_name_solution";
        return selected;
    }

    godot::Array root_projects = filter_projects_in_dir(projects, root, "project");
    selected = first_match(root_projects);
    if (!selected.is_empty()) {
        selected["selection_reason"] = "single_root_project";
        return selected;
    }

    godot::Array root_solutions = filter_projects_in_dir(projects, root, "solution");
    selected = first_match(root_solutions);
    if (!selected.is_empty()) {
        selected["selection_reason"] = "single_root_solution";
        return selected;
    }

    if (projects.size() == 1) {
        selected = projects[0];
        selected["selection_reason"] = "single_candidate";
        return selected;
    }

    return godot::Dictionary();
}

} // namespace

godot::Dictionary inspect_project() {
    godot::Dictionary status;
    godot::String lsp_path = app_paths::csharp_ls_binary_path();
    bool lsp_installed = !lsp_path.is_empty() && godot::FileAccess::file_exists(lsp_path);
    godot::String root = project_root();
    godot::Array projects;

    if (!root.is_empty()) {
        scan_dir(root, root, 0, projects);
    }

    status["lsp_path"] = lsp_path;
    status["lsp_installed"] = lsp_installed;
    status["project_root"] = root;
    status["projects"] = projects;
    status["project_count"] = projects.size();
    status["dotnet_project_directory"] = setting_string("dotnet/project/project_directory");
    status["dotnet_solution_directory"] = setting_string("dotnet/project/solution_directory");
    status["dotnet_assembly_name"] = setting_string("dotnet/project/assembly_name");

    if (!lsp_installed) {
        status["state"] = "lsp_not_installed";
        status["message"] = "C# LSP not available. Install C# support from the Fennara installer.";
    } else if (projects.is_empty()) {
        status["state"] = "no_csharp_project";
        status["message"] = "No .csproj, .sln, or .slnx file found in this Godot project.";
    } else {
        godot::Dictionary selected = select_project(projects, root);
        if (selected.is_empty()) {
            status["state"] = "multiple_csharp_projects";
            status["message"] = "Multiple C# projects found, need selection.";
        } else {
            status["state"] = "ready";
            status["selected_project"] = selected;
            status["message"] =
                projects.size() == 1
                    ? "C# LSP is installed and one C# project was found."
                    : "C# LSP is installed and the main C# project was selected.";
        }
    }

    return status;
}

godot::String diagnostics_unavailable_message(const godot::Dictionary &status) {
    godot::String state = status.get("state", "");
    if (state == "ready") {
        return "C# LSP is available.";
    }
    return status.get("message", "C# LSP not available.");
}

} // namespace fennara::csharp_support
