#include "fennara/lsp/csharp_lsp.hpp"

#include "fennara/lsp/csharp_lsp_internal.hpp"
#include "fennara/lsp/csharp_support.hpp"
#include "fennara/file_utils.hpp"
#include "fennara/logger.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace fennara::csharp_lsp {
namespace {

struct SessionState {
    godot::Ref<godot::FileAccess> stdio;
    int pid = -1;
    godot::String lsp_path;
    godot::String project_path;
    godot::String root_uri;
};

SessionState &session_state() {
    static SessionState *state = new SessionState();
    return *state;
}

std::mutex &session_mutex() {
    static std::mutex *mutex = new std::mutex();
    return *mutex;
}

std::atomic_bool &warmup_in_progress() {
    static std::atomic_bool *warming = new std::atomic_bool(false);
    return *warming;
}

std::atomic_int &diagnostics_priority_count() {
    static std::atomic_int *count = new std::atomic_int(0);
    return *count;
}

struct DiagnosticsPriorityScope {
    DiagnosticsPriorityScope() {
        diagnostics_priority_count().fetch_add(1);
    }

    ~DiagnosticsPriorityScope() {
        diagnostics_priority_count().fetch_sub(1);
    }
};

void wait_for_diagnostics_priority_to_clear(const godot::String &reason) {
    bool logged = false;
    while (diagnostics_priority_count().load() > 0) {
        if (!logged) {
            Logger::log_activity(reason);
            logged = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void clear_session() {
    SessionState &state = session_state();
    state.stdio.unref();
    state.pid = -1;
    state.lsp_path = "";
    state.project_path = "";
    state.root_uri = "";
    internal::clear_open_documents();
}

bool session_alive() {
    SessionState &state = session_state();
    return state.stdio.is_valid() && state.pid > 0 &&
           godot::OS::get_singleton()->is_process_running(state.pid);
}

void shutdown_warm_server_locked() {
    SessionState &state = session_state();
    if (session_alive()) {
        internal::shutdown(state.stdio, state.pid);
    }
    clear_session();
}

void flatten_symbols(godot::Array symbols, godot::Array &out) {
    for (int i = 0; i < symbols.size(); i++) {
        if (symbols[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary symbol = symbols[i];
        out.append(symbol);
        godot::Variant children_var = symbol.get("children", godot::Array());
        if (children_var.get_type() == godot::Variant::ARRAY) {
            flatten_symbols(children_var, out);
        }
    }
}

godot::Dictionary failed_file_result(const godot::String &error) {
    godot::Dictionary file = internal::empty_file_result();
    file["status"] = "failed";
    file["error"] = error;
    return file;
}

godot::String find_first_csharp_file() {
    godot::Array files = file_utils::find_all_diagnostic_files();
    for (int i = 0; i < files.size(); i++) {
        godot::String path = files[i];
        if (path.ends_with(".cs")) {
            return path;
        }
    }
    return "";
}

void prime_session_with_diagnostics(SessionState &state) {
    godot::String abs_path = find_first_csharp_file();
    if (abs_path.is_empty()) {
        Logger::log_activity(
            "C# LSP warmup skipped diagnostic prime: no C# files found");
        return;
    }

    godot::String res_path =
        file_utils::uri_to_res_path(internal::file_uri(abs_path));
    Logger::log_activity(godot::String("C# LSP warmup requesting diagnostics for ") +
                         res_path);
    internal::notify_file_changed(state.stdio, abs_path);
    internal::open_document(state.stdio, abs_path);
    godot::Dictionary response =
        internal::request_document_diagnostics(state.stdio, abs_path, 1);
    if (response.has("result")) {
        Logger::log_activity(
            godot::String("C# LSP warmup diagnostics ready for ") + res_path);
        return;
    }

    Logger::log_activity(
        godot::String("C# LSP warmup diagnostic prime did not return for ") +
        res_path);
}

godot::Dictionary start_session_for_paths(const godot::String &lsp_path,
                                          const godot::String &project_path,
                                          const godot::String &root_uri,
                                          const godot::String &client_name) {
    if (lsp_path.is_empty() || project_path.is_empty() || root_uri.is_empty()) {
        return internal::failure("C# LSP warmup skipped: missing project path.");
    }

    SessionState &state = session_state();
    if (session_alive() && state.lsp_path == lsp_path &&
        state.project_path == project_path && state.root_uri == root_uri) {
        godot::Dictionary result;
        result["success"] = true;
        result["reused"] = true;
        result["project_path"] = state.project_path;
        result["lsp_path"] = state.lsp_path;
        return result;
    }

    if (session_alive()) {
        shutdown_warm_server_locked();
    }
    clear_session();

    godot::Dictionary process =
        godot::OS::get_singleton()->execute_with_pipe(lsp_path, godot::PackedStringArray(), false);
    if (!process.has("stdio")) {
        return internal::failure("Failed to start csharp-ls.");
    }

    godot::Ref<godot::FileAccess> stdio = process["stdio"];
    int pid = static_cast<int>(process.get("pid", -1));
    if (stdio.is_null()) {
        if (pid > 0) {
            godot::OS::get_singleton()->kill(pid);
        }
        return internal::failure("Failed to open csharp-ls stdio pipe.");
    }

    godot::Dictionary init =
        internal::initialize(stdio, client_name, root_uri, project_path.get_file());
    if (!init.has("result")) {
        internal::shutdown(stdio, pid);
        return internal::failure("csharp-ls initialization timed out or failed.");
    }
    internal::pump_until_workspace_ready(stdio, internal::kDiagnosticsTimeoutMs);

    state.stdio = stdio;
    state.pid = pid;
    state.lsp_path = lsp_path;
    state.project_path = project_path;
    state.root_uri = root_uri;

    prime_session_with_diagnostics(state);

    godot::Dictionary result;
    result["success"] = true;
    result["reused"] = false;
    result["project_path"] = state.project_path;
    result["lsp_path"] = state.lsp_path;
    return result;
}

godot::Dictionary start_session_from_project_scan(const godot::String &client_name) {
    godot::Dictionary csharp_status = csharp_support::inspect_project();
    if (godot::String(csharp_status.get("state", "")) != "ready") {
        return internal::failure(
            csharp_support::diagnostics_unavailable_message(csharp_status));
    }

    godot::String lsp_path = csharp_status.get("lsp_path", "");
    godot::Dictionary selected_project =
        csharp_status.get("selected_project", godot::Dictionary());
    godot::String project_path = selected_project.get("absolute_path", "");
    godot::String project_root =
        godot::ProjectSettings::get_singleton()->globalize_path("res://");
    godot::String root_uri = internal::file_uri(project_root);
    return start_session_for_paths(lsp_path, project_path, root_uri, client_name);
}

} // namespace

godot::Dictionary warmup(const godot::String &client_name) {
    std::lock_guard<std::mutex> lock(session_mutex());
    godot::Dictionary result = start_session_from_project_scan(client_name);
    if ((bool)result.get("success", false)) {
        FLOG_SYS(godot::String("C# LSP warmup ready: ") +
                 godot::String(result.get("project_path", "")));
    } else {
        FLOG_SYS(godot::String("C# LSP warmup skipped: ") +
                 godot::String(result.get("error", "")));
    }
    return result;
}

void warmup_async(const godot::String &lsp_path,
                  const godot::String &project_path,
                  const godot::String &project_root,
                  const godot::String &client_name) {
    godot::String root_uri = internal::file_uri(project_root);
    warmup_in_progress().store(true);
    std::thread([lsp_path, project_path, root_uri, client_name]() {
        Logger::log_activity("C# LSP background warmup started");
        std::lock_guard<std::mutex> lock(session_mutex());
        godot::Dictionary result =
            start_session_for_paths(lsp_path, project_path, root_uri, client_name);
        if ((bool)result.get("success", false)) {
            Logger::log_activity(godot::String("C# LSP background warmup ready: ") +
                                 godot::String(result.get("project_path", "")));
        } else {
            Logger::log_activity(godot::String("C# LSP background warmup skipped: ") +
                                 godot::String(result.get("error", "")));
        }
        warmup_in_progress().store(false);
        Logger::log_activity("C# LSP background warmup complete");
    }).detach();
}

godot::Dictionary diagnose_files(const godot::Array &files,
                                 const godot::String &client_name) {
    DiagnosticsPriorityScope priority_scope;
    bool waited_for_warmup = warmup_in_progress().load();
    if (waited_for_warmup) {
        Logger::log_activity("C# LSP diagnostics waiting for background warmup");
    }

    std::lock_guard<std::mutex> lock(session_mutex());
    if (waited_for_warmup) {
        Logger::log_activity(
            "C# LSP background warmup complete; continuing diagnostics");
    }

    godot::Dictionary session = start_session_from_project_scan(client_name);
    if (!(bool)session.get("success", false)) {
        return session;
    }
    bool reused = session.get("reused", false);
    FLOG_TOOL(godot::String("C# LSP diagnostics checking files=") +
              godot::String::num_int64(files.size()) +
              " reused=" + (reused ? "true" : "false"));

    godot::Dictionary per_file;
    godot::Array failed_files;
    SessionState &state = session_state();
    for (int i = 0; i < files.size(); i++) {
        godot::String abs_path = files[i];
        per_file[abs_path] = internal::empty_file_result();

        // Keep all C# diagnostics on the one managed csharp-ls connection.
        // Opening and requesting one document at a time avoids a burst of
        // overlapping diagnostic requests while preserving the warm LSP state.
        internal::notify_file_changed(state.stdio, abs_path);
        internal::open_document(state.stdio, abs_path);
        godot::Dictionary response =
            internal::request_document_diagnostics(state.stdio, abs_path, 100 + i);
        if (!response.has("result")) {
            godot::String res_path =
                file_utils::uri_to_res_path(internal::file_uri(abs_path));
            godot::String error =
                "csharp-ls did not return textDocument/diagnostic for " +
                res_path;
            FLOG_ERR(error);
            per_file[abs_path] = failed_file_result(error);

            godot::Dictionary failed;
            failed["path"] = res_path;
            failed["error"] = error;
            failed_files.append(failed);
            continue;
        }
        per_file[abs_path] = internal::file_result_from_document_diagnostics(response);
    }

    godot::Dictionary result;
    result["success"] = true;
    result["per_file"] = per_file;
    result["failed_files"] = failed_files;
    result["project_path"] = session.get("project_path", "");
    result["lsp_path"] = session.get("lsp_path", "");
    result["reused_lsp"] = session.get("reused", false);
    return result;
}

godot::Dictionary document_symbols(const godot::Array &files,
                                   const godot::String &client_name) {
    bool waited_for_warmup = warmup_in_progress().load();
    if (waited_for_warmup) {
        Logger::log_activity("C# LSP indexing waiting for background warmup");
    }

    godot::Dictionary per_file;
    godot::Array failed_files;
    godot::String project_path;
    godot::String lsp_path;
    bool first_file = true;
    bool reused_lsp = false;

    for (int i = 0; i < files.size(); i++) {
        godot::String abs_path = files[i];
        wait_for_diagnostics_priority_to_clear(
            "C# LSP indexing yielded to diagnostics");

        godot::Dictionary response;
        {
            std::lock_guard<std::mutex> lock(session_mutex());
            if (waited_for_warmup) {
                Logger::log_activity(
                    "C# LSP background warmup complete; continuing indexing");
                waited_for_warmup = false;
            }

            godot::Dictionary session =
                start_session_from_project_scan(client_name);
            if (!(bool)session.get("success", false)) {
                return session;
            }
            if (first_file) {
                reused_lsp = session.get("reused", false);
                project_path = session.get("project_path", "");
                lsp_path = session.get("lsp_path", "");
                FLOG_TOOL(godot::String("C# LSP indexing symbols files=") +
                          godot::String::num_int64(files.size()) +
                          " reused=" + (reused_lsp ? "true" : "false"));
                first_file = false;
            }

            SessionState &state = session_state();

            // Keep C# indexing on the same managed csharp-ls connection as
            // diagnostics. The shared session mutex prevents indexing and
            // diagnostics from interleaving requests on the same stdio pipe,
            // while locking per file lets diagnostics cut ahead between files.
            internal::notify_file_changed(state.stdio, abs_path);
            internal::open_document(state.stdio, abs_path);
            response = internal::request_document_symbols(
                state.stdio, abs_path, 500 + i);
        }

        if (!response.has("result")) {
            godot::Dictionary failed;
            failed["path"] = file_utils::uri_to_res_path(internal::file_uri(abs_path));
            failed["error"] =
                "csharp-ls did not return textDocument/documentSymbol";
            failed_files.append(failed);
            continue;
        }

        godot::Array flat;
        godot::Variant result_var = response["result"];
        if (result_var.get_type() == godot::Variant::ARRAY) {
            flatten_symbols(result_var, flat);
        }
        per_file[abs_path] = flat;
    }

    godot::Dictionary result;
    result["success"] = true;
    result["per_file"] = per_file;
    result["failed_files"] = failed_files;
    result["project_path"] = project_path;
    result["lsp_path"] = lsp_path;
    result["reused_lsp"] = reused_lsp;
    return result;
}

void shutdown_warm_server() {
    std::lock_guard<std::mutex> lock(session_mutex());
    shutdown_warm_server_locked();
}

} // namespace fennara::csharp_lsp
