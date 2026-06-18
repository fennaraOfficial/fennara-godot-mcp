#pragma once

#include <godot_cpp/variant/dictionary.hpp>

namespace fennara::runtime_debugger_snapshot {

godot::Dictionary capture(const godot::Dictionary &context = godot::Dictionary());

} // namespace fennara::runtime_debugger_snapshot
