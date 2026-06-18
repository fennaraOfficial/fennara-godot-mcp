#pragma once

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara::csharp_lsp {

godot::Dictionary warmup(const godot::String &client_name);

void warmup_async(const godot::String &lsp_path,
                  const godot::String &project_path,
                  const godot::String &project_root,
                  const godot::String &client_name);

godot::Dictionary diagnose_files(const godot::Array &files,
                                 const godot::String &client_name);

godot::Dictionary document_symbols(const godot::Array &files,
                                   const godot::String &client_name);

void shutdown_warm_server();

} // namespace fennara::csharp_lsp
