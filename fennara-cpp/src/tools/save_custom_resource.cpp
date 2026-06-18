#include "fennara/tools/save_custom_resource.hpp"
#include "fennara/helpers.hpp"
#include "fennara/logger.hpp"
#include "fennara/snapshot_manager.hpp"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace fennara {

void FennaraSaveCustomResourceTool::_bind_methods() {
    godot::ClassDB::bind_static_method(
        "FennaraSaveCustomResourceTool", godot::D_METHOD("execute", "args"),
        &FennaraSaveCustomResourceTool::execute);
}

godot::Dictionary
FennaraSaveCustomResourceTool::execute(const godot::Dictionary &args) {
    godot::String resource_path = args.get("resource_path", "");
    godot::String resource_type = args.get("resource_type", "Resource");
    godot::Dictionary properties = args.get("properties", godot::Dictionary());
    godot::String script_path = args.get("script_path", "");

    FLOG_TOOL(godot::String("SaveResource: path=") + resource_path + " type=" + resource_type);

    if (resource_path.is_empty()) {
        FLOG_ERR("SaveResource: resource_path required");
        godot::Dictionary r;
        r["success"] = false;
        r["error"] = "resource_path is required";
        return _stamp_result(r, args);
    }

    // Ensure .tres extension
    if (!resource_path.ends_with(".tres")) {
        resource_path += ".tres";
    }

    godot::Ref<godot::Resource> resource;

    // Create resource instance
    if (resource_type.begins_with("res://")) {
        // Custom resource from script path
        godot::Ref<godot::Resource> script =
            godot::ResourceLoader::get_singleton()->load(resource_type);
        if (script.is_null()) {
            FLOG_ERR(godot::String("SaveResource: failed to load script ") + resource_type);
            godot::Dictionary r;
            r["success"] = false;
            r["error"] = "Failed to load resource script: " + resource_type;
            return _stamp_result(r, args);
        }
        // Instantiate via the script: create a Resource and set its script
        resource.instantiate();
        resource->set_script(script);
    } else {
        // Try class_name → script path lookup
        godot::String script_for_class =
            _get_script_path_for_class_name(resource_type);
        if (!script_for_class.is_empty()) {
            godot::Ref<godot::Resource> script =
                godot::ResourceLoader::get_singleton()->load(script_for_class);
            if (script.is_null()) {
                FLOG_ERR(godot::String("SaveResource: failed to load script for class ") + resource_type);
                godot::Dictionary r;
                r["success"] = false;
                r["error"] =
                    "Failed to load script for class: " + resource_type;
                return _stamp_result(r, args);
            }
            resource.instantiate();
            resource->set_script(script);
        } else if (godot::ClassDBSingleton::get_singleton()->can_instantiate(
                       resource_type)) {
            // Built-in resource type
            godot::Variant instance =
                godot::ClassDBSingleton::get_singleton()->instantiate(resource_type);
            if (instance.get_type() == godot::Variant::OBJECT) {
                godot::Object *obj = godot::Object::cast_to<godot::Object>(
                    instance.operator godot::Object *());
                auto *res = godot::Object::cast_to<godot::Resource>(obj);
                if (res) {
                    resource = godot::Ref<godot::Resource>(res);
                }
            }
        }

        if (resource.is_null()) {
            FLOG_ERR(godot::String("SaveResource: cannot instantiate type ") + resource_type);
            godot::Dictionary r;
            r["success"] = false;
            r["error"] = "Cannot instantiate resource type: " + resource_type +
                         ". Not a built-in class or registered class_name.";
            return _stamp_result(r, args);
        }
    }

    // Attach script if provided separately
    if (!script_path.is_empty() && script_path.begins_with("res://")) {
        godot::Ref<godot::Resource> script =
            godot::ResourceLoader::get_singleton()->load(script_path);
        if (script.is_null()) {
            godot::Dictionary r;
            r["success"] = false;
            r["error"] = "Failed to load script: " + script_path;
            return _stamp_result(r, args);
        }
        resource->set_script(script);
    }

    // Set properties
    godot::Array set_properties;
    godot::Array warnings;

    godot::Array prop_keys = properties.keys();
    for (int i = 0; i < prop_keys.size(); i++) {
        godot::String prop_name = prop_keys[i];
        godot::Variant value = parse_value(properties[prop_name]);

        // Check if property exists
        bool property_exists = false;
        godot::Array property_list = resource->get_property_list();
        for (int j = 0; j < property_list.size(); j++) {
            godot::Dictionary prop = property_list[j];
            if (godot::String(prop["name"]) == prop_name) {
                property_exists = true;
                break;
            }
        }

        if (property_exists) {
            resource->set(prop_name, value);
            set_properties.push_back(prop_name);
        } else {
            warnings.push_back("Property '" + prop_name +
                               "' not found in resource");
        }
    }

    // Create directory if needed
    godot::String dir_path = resource_path.get_base_dir();
    if (!dir_path.is_empty() &&
        !godot::DirAccess::dir_exists_absolute(dir_path)) {
        godot::Error dir_err =
            godot::DirAccess::make_dir_recursive_absolute(dir_path);
        if (dir_err != godot::OK) {
            godot::Dictionary r;
            r["success"] = false;
            r["error"] = "Failed to create directory: " + dir_path;
            return _stamp_result(r, args);
        }
    }

    // Snapshot before saving resource
    auto *snap = FennaraSnapshotManager::get_active();
    if (snap) {
        if (godot::FileAccess::file_exists(resource_path)) snap->snapshot_file(resource_path);
        else snap->snapshot_created(resource_path);
    }

    // Save the resource
    godot::Error save_err =
        godot::ResourceSaver::get_singleton()->save(resource, resource_path);
    if (save_err != godot::OK) {
        godot::Dictionary r;
        r["success"] = false;
        r["error"] = "Failed to save resource to " + resource_path;
        return _stamp_result(r, args);
    }

    FLOG_TOOL(godot::String("SaveResource: done, path=") + resource_path + " props=" + godot::String::num_int64(set_properties.size()));

    godot::Dictionary r;
    r["success"] = true;
    r["resource_path"] = resource_path;
    r["resource_type"] = resource_type;
    r["properties_set"] = set_properties;
    r["message"] = "Custom resource saved successfully to " + resource_path;

    if (!warnings.is_empty()) {
        r["warnings"] = warnings;
    }

    return _stamp_result(r, args);
}

godot::Dictionary FennaraSaveCustomResourceTool::_stamp_result(
    godot::Dictionary result, const godot::Dictionary &args) {
    result["tool_name"] = "save_custom_resource";
    result["format_version"] = "save-custom-resource-result-v1";
    bool success = result.get("success", false);
    result["status"] = success ? "success" : "failed";

    godot::String resource_path = result.get("resource_path", args.get("resource_path", ""));
    if (!resource_path.is_empty() && !resource_path.ends_with(".tres")) {
        resource_path += ".tres";
    }
    if (!resource_path.is_empty()) {
        result["resource_path"] = resource_path;
    }
    if (!result.has("resource_type") && args.has("resource_type")) {
        result["resource_type"] = args.get("resource_type", "Resource");
    }
    if (!result.has("script_path") && args.has("script_path")) {
        result["script_path"] = args.get("script_path", "");
    }

    godot::Array properties_set = result.get("properties_set", godot::Array());
    godot::Array warnings = result.get("warnings", godot::Array());
    godot::Dictionary properties = args.get("properties", godot::Dictionary());

    godot::Dictionary summary;
    summary["status"] = result.get("status", "failed");
    summary["resource_path"] = result.get("resource_path", resource_path);
    summary["resource_type"] = result.get("resource_type", args.get("resource_type", "Resource"));
    summary["script_path"] = result.get("script_path", args.get("script_path", ""));
    summary["requested_property_count"] = properties.size();
    summary["set_property_count"] = properties_set.size();
    summary["warning_count"] = warnings.size();
    if (result.has("error")) {
        summary["error"] = result["error"];
    }
    result["summary"] = summary;
    return result;
}

godot::String FennaraSaveCustomResourceTool::_get_script_path_for_class_name(
    const godot::String &class_type) {
    godot::Array global_classes =
        godot::ProjectSettings::get_singleton()->get_setting(
            "_global_script_classes", godot::Array());
    for (int i = 0; i < global_classes.size(); i++) {
        godot::Dictionary cls = global_classes[i];
        if (godot::String(cls.get("class", "")) == class_type) {
            return cls.get("path", "");
        }
    }
    return "";
}

} // namespace fennara
