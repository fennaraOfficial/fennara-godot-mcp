#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara::csharp_support {

godot::Dictionary inspect_project();
godot::String diagnostics_unavailable_message(const godot::Dictionary &status);

} // namespace fennara::csharp_support
