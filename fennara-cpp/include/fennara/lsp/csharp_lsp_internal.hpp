#pragma once

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara::csharp_lsp::internal {

constexpr int kDiagnosticsTimeoutMs = 30000;
constexpr int kSymbolsTimeoutMs = 15000;
constexpr int kShutdownTimeoutMs = 1000;

godot::String file_uri(const godot::String &abs_path);
godot::Dictionary empty_file_result();
godot::Dictionary failure(const godot::String &error);

godot::Dictionary initialize(const godot::Ref<godot::FileAccess> &stdio,
                             const godot::String &client_name,
                             const godot::String &root_uri,
                             const godot::String &workspace_name);
void pump_until_workspace_ready(const godot::Ref<godot::FileAccess> &stdio,
                                int timeout_ms);
void notify_file_changed(const godot::Ref<godot::FileAccess> &stdio,
                         const godot::String &abs_path);
void open_document(const godot::Ref<godot::FileAccess> &stdio,
                   const godot::String &abs_path);
godot::Dictionary request_document_diagnostics(
    const godot::Ref<godot::FileAccess> &stdio,
    const godot::String &abs_path,
    int request_id);
godot::Dictionary request_document_symbols(
    const godot::Ref<godot::FileAccess> &stdio,
    const godot::String &abs_path,
    int request_id);
godot::Dictionary file_result_from_document_diagnostics(
    const godot::Dictionary &response);
void shutdown(const godot::Ref<godot::FileAccess> &stdio, int pid);
void clear_open_documents();

} // namespace fennara::csharp_lsp::internal
