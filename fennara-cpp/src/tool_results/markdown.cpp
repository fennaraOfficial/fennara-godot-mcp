#include "fennara/tool_results/markdown.hpp"

namespace fennara::tool_results {

namespace {
constexpr int kEstimatedCharsPerToken = 4;
}

int estimate_tokens(const godot::String &text) {
    int tokens = text.length() / kEstimatedCharsPerToken;
    return tokens <= 0 && !text.is_empty() ? 1 : tokens;
}

godot::String code_fence(const godot::String &text,
                         const godot::String &language) {
    godot::String fence = "```";
    godot::String result = fence;
    if (!language.is_empty()) {
        result += language;
    }
    return result + "\n" + text + "\n" + fence;
}

godot::String preview_text_by_budget(const godot::String &text,
                                     int budget_tokens) {
    int char_budget = budget_tokens * kEstimatedCharsPerToken;
    if (char_budget <= 0 || text.is_empty()) {
        return "";
    }
    if (text.length() <= char_budget) {
        return text;
    }

    godot::String prefix = text.substr(0, char_budget);
    int newline = prefix.rfind("\n");
    if (newline > 0) {
        return prefix.substr(0, newline);
    }
    return prefix;
}

} // namespace fennara::tool_results
