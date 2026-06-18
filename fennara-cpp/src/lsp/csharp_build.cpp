#include "fennara/lsp/csharp_build.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace fennara::csharp_build {
namespace {

uint64_t now_ms() {
    godot::Time *time = godot::Time::get_singleton();
    return time == nullptr ? 0 : static_cast<uint64_t>(time->get_ticks_msec());
}

godot::String project_root() {
    godot::ProjectSettings *settings = godot::ProjectSettings::get_singleton();
    return settings == nullptr ? godot::String() : settings->globalize_path("res://");
}

godot::String single_quote(const godot::String &value) {
    return "'" + value.replace("'", "''") + "'";
}

godot::String shell_quote(const godot::String &value) {
    return "\"" + value.replace("\"", "\\\"") + "\"";
}

godot::String join_args(const godot::PackedStringArray &args) {
    godot::PackedStringArray quoted;
    for (int i = 0; i < args.size(); i++) {
        godot::String arg = args[i];
        quoted.append(arg.find(" ") >= 0 ? shell_quote(arg) : arg);
    }
    return godot::String(" ").join(quoted);
}

godot::Dictionary run_command_blocking(const godot::String &command,
                                       const godot::PackedStringArray &args,
                                       const godot::String &display_command,
                                       const godot::String &working_directory) {
    uint64_t start = now_ms();
    godot::Array output;
    int exit_code = godot::OS::get_singleton()->execute(
        command, args, output, true, false);

    godot::Dictionary result;
    result["command"] = display_command.is_empty()
                            ? command + (args.is_empty() ? "" : " " + join_args(args))
                            : display_command;
    result["working_directory"] = working_directory;
    result["exit_code"] = exit_code;
    result["duration_seconds"] = (double)(now_ms() - start) / 1000.0;
    result["output"] = output.is_empty() ? godot::String() : godot::String(output[0]);
    result["status"] = exit_code == 0 ? "success" : "failed";
    return result;
}

} // namespace

godot::String find_root_csproj() {
    godot::Ref<godot::DirAccess> dir = godot::DirAccess::open("res://");
    if (dir.is_null()) {
        return "";
    }
    dir->list_dir_begin();
    while (true) {
        godot::String name = dir->get_next();
        if (name.is_empty()) {
            break;
        }
        if (!dir->current_is_dir() && name.ends_with(".csproj")) {
            dir->list_dir_end();
            return project_root().path_join(name);
        }
    }
    dir->list_dir_end();
    return "";
}

godot::Dictionary run_dotnet_build_if_needed() {
    godot::Dictionary result;
    godot::String csproj_path = find_root_csproj();
    if (csproj_path.is_empty()) {
        result["needed"] = false;
        result["status"] = "skipped";
        result["message"] = "No root .csproj found.";
        return result;
    }

    godot::String root = project_root();
    godot::OS *os = godot::OS::get_singleton();
    godot::PackedStringArray args;
    godot::String runner = "dotnet";

    if (os != nullptr && os->get_name() == "Windows") {
        runner = "powershell";
        args.append("-NoProfile");
        args.append("-ExecutionPolicy");
        args.append("Bypass");
        args.append("-Command");
        args.append("Set-Location -LiteralPath " + single_quote(root) + "; & dotnet build");
    } else {
        runner = "/bin/sh";
        args.append("-lc");
        args.append("cd " + single_quote(root) + " && dotnet build");
    }

    result = run_command_blocking(runner, args, "dotnet build", root);
    result["needed"] = true;
    result["project"] = csproj_path;
    return result;
}

} // namespace fennara::csharp_build
