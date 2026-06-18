#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>

namespace fennara::tool_call_log {

void log_received(const godot::String &session_id,
                  const godot::String &request_id,
                  const godot::String &tool,
                  const godot::Dictionary &input);

void log_started(const godot::String &session_id,
                 const godot::String &request_id,
                 const godot::String &tool);

void log_completed(const godot::String &session_id,
                   const godot::String &request_id,
                   const godot::String &tool,
                   const godot::Dictionary &input,
                   const godot::Dictionary &result,
                   bool ok,
                   uint64_t started_at_ms);

void log_failed(const godot::String &session_id,
                const godot::String &request_id,
                const godot::String &tool,
                const godot::Dictionary &input,
                const godot::String &error,
                uint64_t started_at_ms);

godot::String session_log_dir(const godot::String &session_id);
godot::String result_artifact_dir(const godot::String &session_id,
                                  const godot::String &request_id,
                                  const godot::String &tool);

} // namespace fennara::tool_call_log
