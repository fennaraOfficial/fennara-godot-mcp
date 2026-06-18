#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara::csharp_build {

godot::String find_root_csproj();
godot::Dictionary run_dotnet_build_if_needed();

} // namespace fennara::csharp_build
