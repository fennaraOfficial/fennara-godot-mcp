#pragma once

#include <godot_cpp/variant/string.hpp>

namespace fennara::tool_results {

int estimate_tokens(const godot::String &text);
godot::String code_fence(const godot::String &text,
                         const godot::String &language);
godot::String preview_text_by_budget(const godot::String &text,
                                     int budget_tokens);

} // namespace fennara::tool_results
