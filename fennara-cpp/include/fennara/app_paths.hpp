#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara::app_paths {

godot::PackedStringArray app_dir_candidates();
godot::String app_dir();
godot::String csharp_ls_binary_path();
godot::String daemon_binary_path();
godot::String docs_cache_dir();
godot::String runtime_state_path();
godot::PackedStringArray runtime_state_read_paths();
godot::Dictionary read_json_first_existing(const godot::PackedStringArray &paths,
                                           godot::String *used_path = nullptr);
bool write_json(const godot::String &path, const godot::Dictionary &data);

} // namespace fennara::app_paths
