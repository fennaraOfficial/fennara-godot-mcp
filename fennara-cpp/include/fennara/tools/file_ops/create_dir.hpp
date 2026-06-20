#pragma once

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace fennara::file_ops {

godot::Dictionary create_dir(const godot::Dictionary &op, godot::Array &warnings,
                             godot::Array &errors);

} // namespace fennara::file_ops
