#include "fennara/register_types.hpp"

#include "fennara/executor.hpp"
#include "fennara/local_bridge.hpp"
#include "fennara/tools/get_scene_tree.hpp"
#include "fennara/tools/script_diagnostics.hpp"
#include "fennara/tools/write_or_update_file.hpp"
#include "fennara/tools/save_custom_resource.hpp"
#include "fennara/tools/screenshot_scene.hpp"
#include "fennara/tools/get_node_properties/get_node_properties.hpp"
#include "fennara/tools/get_class_info/get_class_info.hpp"
#include "fennara/tools/validate_scene.hpp"
#include "fennara/tools/run_scene_edit_script.hpp"
#include "fennara/tools/project_settings.hpp"
#include "fennara/tools/runtime_script.hpp"
#include "fennara/tools/runtime_session.hpp"
#include "fennara/tools/scrape_editor.hpp"
#include "fennara/snapshot_manager.hpp"
#include "fennara/warning_capture.hpp"

#include "fennara/ui/dock.hpp"
#include "fennara/ui/fennara_plugin.hpp"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/classes/editor_plugin_registration.hpp>

void initialize_fennara(godot::ModuleInitializationLevel p_level) {
    if (p_level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        // Keep tool registration centralized so release builds package the same plugin surface.
        godot::ClassDB::register_class<fennara::FennaraExecutor>();
        godot::ClassDB::register_class<fennara::FennaraGetSceneTreeTool>();
        godot::ClassDB::register_class<fennara::FennaraWriteOrUpdateFileTool>();
        godot::ClassDB::register_class<fennara::FennaraScriptDiagnosticsTool>();
        godot::ClassDB::register_class<fennara::FennaraSaveCustomResourceTool>();
        godot::ClassDB::register_class<fennara::FennaraScreenshotSceneTool>();
        godot::ClassDB::register_class<fennara::FennaraGetNodePropertiesTool>();
        godot::ClassDB::register_class<fennara::FennaraGetClassInfoTool>();
        godot::ClassDB::register_class<fennara::FennaraValidateSceneTool>();
        godot::ClassDB::register_class<fennara::FennaraRunSceneEditScriptContext>();
        godot::ClassDB::register_class<fennara::FennaraRunSceneEditScriptTool>();
        godot::ClassDB::register_class<fennara::FennaraProjectSettingsTool>();
        godot::ClassDB::register_class<fennara::FennaraRuntimeSessionTool>();
        godot::ClassDB::register_class<fennara::FennaraRuntimeScriptTool>();
        godot::ClassDB::register_class<fennara::FennaraScrapeEditorTool>();
        godot::ClassDB::register_class<fennara::FennaraLocalBridge>();
        godot::ClassDB::register_class<fennara::FennaraSnapshotManager>();
        godot::ClassDB::register_class<fennara::FennaraWarningCapture>();

        godot::ClassDB::register_class<fennara::FennaraDock>();
    }

    if (p_level == godot::MODULE_INITIALIZATION_LEVEL_EDITOR) {
        godot::ClassDB::register_class<fennara::FennaraPlugin>();
        godot::EditorPlugins::add_by_type<fennara::FennaraPlugin>();
    }
}

void uninitialize_fennara(godot::ModuleInitializationLevel p_level) {
    if (p_level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    if (p_level == godot::MODULE_INITIALIZATION_LEVEL_EDITOR) {
        godot::EditorPlugins::remove_by_type<fennara::FennaraPlugin>();
    }
}

extern "C" {
GDExtensionBool GDE_EXPORT fennara_entry(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    const GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization *r_initialization) {
    godot::GDExtensionBinding::InitObject init_obj(
        p_get_proc_address, p_library, r_initialization);

    init_obj.register_initializer(initialize_fennara);
    init_obj.register_terminator(uninitialize_fennara);
    init_obj.set_minimum_library_initialization_level(
        godot::MODULE_INITIALIZATION_LEVEL_SCENE);

    return init_obj.init();
}
}
