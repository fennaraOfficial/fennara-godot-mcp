#pragma once

#include <godot_cpp/classes/editor_plugin.hpp>
#include "fennara/ui/dock.hpp"

namespace fennara {

class FennaraLocalBridge;

class FennaraPlugin : public godot::EditorPlugin {
    GDCLASS(FennaraPlugin, godot::EditorPlugin)

protected:
    static void _bind_methods();

private:
    FennaraDock *dock_instance = nullptr;
    FennaraLocalBridge *local_bridge = nullptr;
    void _configure_editor_settings();
    void _ensure_export_presets_exclude_fennara();
    bool _is_export_preset_section(const godot::String &section) const;
    godot::PackedStringArray _split_export_filter(const godot::String &raw) const;
    void _ensure_runtime_helper_autoload();
    void _inspect_csharp_support();
    void _warm_csharp_lsp();

public:
    FennaraPlugin();
    ~FennaraPlugin() = default;

    void _enter_tree() override;
    void _exit_tree() override;
    void _process(double delta) override;
};

} // namespace fennara
