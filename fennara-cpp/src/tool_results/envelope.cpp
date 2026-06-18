#include "fennara/tool_results/envelope.hpp"

namespace fennara::tool_results {

namespace {
constexpr const char *kEnvelopeVersion = "tool-result-md-v1";
}

godot::Dictionary make_base_metadata(const godot::String &tool_name,
                                     const godot::String &formatter_version,
                                     const godot::String &status) {
    godot::Dictionary metadata;
    metadata["tool_name"] = tool_name;
    metadata["format"] = "markdown";
    metadata["formatter_version"] = formatter_version;
    metadata["envelope_version"] = kEnvelopeVersion;
    metadata["status"] = status;
    return metadata;
}

godot::Dictionary make_envelope(const godot::String &content,
                                const godot::Dictionary &metadata,
                                bool success) {
    godot::Dictionary envelope;
    envelope["success"] = success;
    envelope["content"] = content;
    envelope["metadata"] = metadata;
    return envelope;
}

bool is_envelope(const godot::Dictionary &result) {
    return result.has("content") &&
           result.get("content", godot::Variant()).get_type() == godot::Variant::STRING &&
           result.has("metadata") &&
           result.get("metadata", godot::Variant()).get_type() == godot::Variant::DICTIONARY;
}

} // namespace fennara::tool_results
