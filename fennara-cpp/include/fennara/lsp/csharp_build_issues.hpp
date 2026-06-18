#pragma once

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <cstdint>

namespace fennara::csharp_build_issues {

godot::Dictionary latest_snapshot(uint64_t window_start_unix_ms = 0,
                                  uint64_t window_end_unix_ms = 0);

} // namespace fennara::csharp_build_issues
