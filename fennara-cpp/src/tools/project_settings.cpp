#include "fennara/tools/project_settings.hpp"
#include "fennara/helpers.hpp"
#include "fennara/logger.hpp"
#include "fennara/snapshot_manager.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_joypad_button.hpp>
#include <godot_cpp/classes/input_event_joypad_motion.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace fennara {

namespace {

constexpr int kFindSettingMaxResults = 100;

void snapshot_project_settings_file() {
    auto *snap = FennaraSnapshotManager::get_active();
    if (!snap) {
        return;
    }

    const godot::String path = "res://project.godot";
    if (godot::FileAccess::file_exists(path)) {
        snap->snapshot_file(path);
    } else {
        snap->snapshot_created(path);
    }
}

godot::String unquote_project_setting_part(const godot::String &value) {
    godot::String stripped = value.strip_edges();
    if (stripped.length() >= 2 && stripped.begins_with("\"") && stripped.ends_with("\"")) {
        return stripped.substr(1, stripped.length() - 2);
    }
    return stripped;
}

} // namespace

void FennaraProjectSettingsTool::_bind_methods() {
    godot::ClassDB::bind_static_method(
        "FennaraProjectSettingsTool", godot::D_METHOD("execute", "args"),
        &FennaraProjectSettingsTool::execute);
}

godot::Dictionary FennaraProjectSettingsTool::execute(const godot::Dictionary &args) {
    godot::String action = godot::String(args.get("action", "")).strip_edges();
    godot::String key = godot::String(args.get("key", "")).strip_edges();

    if (action.is_empty()) {
        godot::Dictionary err;
        err["success"] = false;
        err["error"] = "Missing required 'action' parameter (get/set/remove/list/find_setting)";
        return _stamp_result(err, args);
    }

    FLOG_TOOL(("project_settings: action=" + action + " key=" + key).utf8().get_data());

    if (action == "get") {
        if (key.is_empty()) {
            godot::Dictionary err;
            err["success"] = false;
            err["error"] = "Missing 'key' for get action";
            return _stamp_result(err, args);
        }
        return _stamp_result(_action_get(key), args);
    } else if (action == "set") {
        if (key.is_empty()) {
            godot::Dictionary err;
            err["success"] = false;
            err["error"] = "Missing 'key' for set action";
            return _stamp_result(err, args);
        }
        // Input actions get special handling
        if (key.begins_with("input/") && args.has("events")) {
            godot::String action_name = key.substr(6); // strip "input/"
            godot::Dictionary config;
            config["deadzone"] = args.get("deadzone", 0.5);
            config["events"] = args.get("events", godot::Array());
            return _stamp_result(_set_input_action(action_name, config), args);
        }
        if (!args.has("value")) {
            godot::Dictionary err;
            err["success"] = false;
            err["error"] = "Missing 'value' for set action";
            return _stamp_result(err, args);
        }
        return _stamp_result(_action_set(key, args.get("value", godot::Variant())), args);
    } else if (action == "remove") {
        if (key.is_empty()) {
            godot::Dictionary err;
            err["success"] = false;
            err["error"] = "Missing 'key' for remove action";
            return _stamp_result(err, args);
        }
        return _stamp_result(_action_remove(key), args);
    } else if (action == "list") {
        godot::String prefix = key.is_empty() ? godot::String(args.get("prefix", "")) : key;
        return _stamp_result(_action_list(prefix), args);
    } else if (action == "find_setting") {
        godot::String prefix = key.is_empty() ? godot::String(args.get("prefix", "")) : key;
        godot::String query = godot::String(args.get("query", "")).strip_edges();
        return _stamp_result(_action_find_setting(prefix, query), args);
    }

    godot::Dictionary err;
    err["success"] = false;
    err["error"] = "Unknown action: " + action + ". Use get/set/remove/list/find_setting";
    return _stamp_result(err, args);
}

godot::Dictionary FennaraProjectSettingsTool::_action_get(const godot::String &key) {
    auto *ps = godot::ProjectSettings::get_singleton();
    godot::Dictionary result;

    if (!ps->has_setting(key)) {
        result["success"] = false;
        result["error"] = "Setting not found: " + key;
        return result;
    }

    godot::Variant val = ps->get_setting(key);
    result["success"] = true;
    result["action"] = "get";
    result["key"] = key;
    result["value"] = godot::String(val);
    return result;
}

godot::Dictionary FennaraProjectSettingsTool::_action_set(const godot::String &key, const godot::Variant &value) {
    auto *ps = godot::ProjectSettings::get_singleton();
    godot::Dictionary result;

    snapshot_project_settings_file();
    ps->set_setting(key, value);
    ps->save();

    result["success"] = true;
    result["action"] = "set";
    result["key"] = key;
    result["value"] = godot::String(value);
    result["output"] = "Set " + key + " = " + godot::String(value);
    return result;
}

godot::Dictionary FennaraProjectSettingsTool::_action_remove(const godot::String &key) {
    auto *ps = godot::ProjectSettings::get_singleton();
    godot::Dictionary result;

    if (!ps->has_setting(key)) {
        result["success"] = false;
        result["error"] = "Setting not found: " + key;
        return result;
    }

    snapshot_project_settings_file();
    ps->set_setting(key, godot::Variant());
    ps->save();

    result["success"] = true;
    result["action"] = "remove";
    result["key"] = key;
    result["output"] = "Removed setting: " + key;
    return result;
}

godot::Dictionary FennaraProjectSettingsTool::_action_list(const godot::String &prefix) {
    godot::Dictionary result;

    godot::PackedStringArray settings;
    const godot::String path = "res://project.godot";
    if (!godot::FileAccess::file_exists(path)) {
        result["success"] = false;
        result["error"] = "project.godot not found";
        return result;
    }

    godot::Ref<godot::FileAccess> file = godot::FileAccess::open(path, godot::FileAccess::READ);
    if (file.is_null()) {
        result["success"] = false;
        result["error"] = "Failed to read project.godot";
        return result;
    }

    godot::String section;
    godot::PackedStringArray lines = file->get_as_text().split("\n");
    for (int i = 0; i < lines.size(); i++) {
        godot::String line = godot::String(lines[i]).strip_edges();
        if (line.is_empty() || line.begins_with(";")) {
            continue;
        }
        if (line.begins_with("[") && line.ends_with("]")) {
            section = unquote_project_setting_part(line.substr(1, line.length() - 2));
            continue;
        }

        int equal_pos = line.find("=");
        if (equal_pos <= 0 || section.is_empty()) {
            continue;
        }

        godot::String key = unquote_project_setting_part(line.substr(0, equal_pos));
        godot::String setting = section + "/" + key;
        if (prefix.is_empty() || setting.begins_with(prefix)) {
            settings.push_back(setting);
        }
    }

    result["success"] = true;
    result["action"] = "list";
    result["prefix"] = prefix;
    result["settings"] = settings;
    result["count"] = settings.size();
    result["source"] = "project.godot";
    return result;
}

godot::Dictionary FennaraProjectSettingsTool::_action_find_setting(
    const godot::String &prefix, const godot::String &query) {
    auto *ps = godot::ProjectSettings::get_singleton();
    godot::Dictionary result;
    godot::String clean_prefix = prefix.strip_edges();
    godot::String clean_query = query.strip_edges();

    if (clean_prefix.is_empty() && clean_query.is_empty()) {
        result["success"] = false;
        result["action"] = "find_setting";
        result["error"] = "find_setting requires a non-empty 'prefix', 'key', or 'query' so it does not return every Godot ProjectSettings key";
        return result;
    }

    godot::PackedStringArray matches;
    int total_matches = 0;
    godot::TypedArray<godot::Dictionary> props = ps->get_property_list();
    for (int i = 0; i < props.size(); i++) {
        godot::Dictionary prop = props[i];
        godot::String name = prop.get("name", "");
        bool prefix_matches = clean_prefix.is_empty() || name.begins_with(clean_prefix);
        bool query_matches = clean_query.is_empty() || name.contains(clean_query);
        if (prefix_matches && query_matches) {
            total_matches++;
            if (matches.size() < kFindSettingMaxResults) {
                matches.push_back(name);
            }
        }
    }

    result["success"] = true;
    result["action"] = "find_setting";
    result["prefix"] = clean_prefix;
    result["query"] = clean_query;
    result["settings"] = matches;
    result["count"] = matches.size();
    result["total_count"] = total_matches;
    if (matches.size() < total_matches) {
        result["truncated"] = true;
        result["truncated_message"] = "Showing first " +
                                      godot::String::num_int64(matches.size()) + " of " +
                                      godot::String::num_int64(total_matches) +
                                      " matching settings. Use a narrower prefix or query.";
    }
    return result;
}

godot::Dictionary FennaraProjectSettingsTool::_set_input_action(const godot::String &action_name, const godot::Dictionary &config) {
    auto *ps = godot::ProjectSettings::get_singleton();
    godot::Dictionary result;

    double deadzone = config.get("deadzone", 0.5);
    godot::Array raw_events = config.get("events", godot::Array());

    godot::Array built_events;
    for (int i = 0; i < raw_events.size(); i++) {
        godot::Dictionary evt = raw_events[i];
        godot::Variant event = _build_input_event(evt);
        if (event.get_type() == godot::Variant::NIL) {
            result["success"] = false;
            result["error"] = "Failed to build event #" + godot::String::num_int64(i) +
                              ": unknown event_type '" + godot::String(evt.get("event_type", "")) + "'";
            return result;
        }
        built_events.push_back(event);
    }

    godot::Dictionary action_dict;
    action_dict["deadzone"] = deadzone;
    action_dict["events"] = built_events;

    snapshot_project_settings_file();
    godot::String setting_key = "input/" + action_name;
    ps->set_setting(setting_key, action_dict);
    ps->save();

    result["success"] = true;
    result["action"] = "set";
    result["key"] = setting_key;
    result["input_action"] = action_name;
    result["event_count"] = built_events.size();
    result["deadzone"] = deadzone;
    result["output"] = "Set input action '" + action_name + "' with " +
                       godot::String::num_int64(built_events.size()) + " event(s), deadzone=" +
                       godot::String::num(deadzone, 2);
    return result;
}

godot::Dictionary FennaraProjectSettingsTool::_stamp_result(
    godot::Dictionary result, const godot::Dictionary &args) {
    result["tool_name"] = "project_settings";
    result["format_version"] = "project-settings-result-v1";
    bool success = result.get("success", false);
    result["status"] = success ? "success" : "failed";

    if (!result.has("action") && args.has("action")) {
        result["action"] = godot::String(args.get("action", "")).strip_edges();
    }
    if (!result.has("key") && args.has("key")) {
        result["key"] = godot::String(args.get("key", "")).strip_edges();
    }
    if (!result.has("prefix") && args.has("prefix")) {
        result["prefix"] = godot::String(args.get("prefix", "")).strip_edges();
    }
    if (!result.has("query") && args.has("query")) {
        result["query"] = godot::String(args.get("query", "")).strip_edges();
    }

    godot::Dictionary summary;
    summary["status"] = result.get("status", "failed");
    summary["action"] = result.get("action", "");
    summary["key"] = result.get("key", "");
    summary["prefix"] = result.get("prefix", "");
    summary["query"] = result.get("query", "");
    summary["count"] = result.get("count", 0);
    summary["total_count"] = result.get("total_count", result.get("count", 0));
    summary["event_count"] = result.get("event_count", 0);
    if (result.has("error")) {
        summary["error"] = result["error"];
    }
    result["summary"] = summary;
    return result;
}

godot::Variant FennaraProjectSettingsTool::_build_input_event(const godot::Dictionary &evt) {
    godot::String type = godot::String(evt.get("event_type", "")).strip_edges();

    if (type == "key") {
        godot::Ref<godot::InputEventKey> e;
        e.instantiate();
        if (evt.has("keycode")) e->set_keycode((godot::Key)(int)evt["keycode"]);
        if (evt.has("physical_keycode")) e->set_physical_keycode((godot::Key)(int)evt["physical_keycode"]);
        if (evt.has("ctrl_pressed")) e->set_ctrl_pressed((bool)evt["ctrl_pressed"]);
        if (evt.has("shift_pressed")) e->set_shift_pressed((bool)evt["shift_pressed"]);
        if (evt.has("alt_pressed")) e->set_alt_pressed((bool)evt["alt_pressed"]);
        if (evt.has("meta_pressed")) e->set_meta_pressed((bool)evt["meta_pressed"]);
        return e;
    } else if (type == "mouse_button") {
        godot::Ref<godot::InputEventMouseButton> e;
        e.instantiate();
        if (evt.has("button_index")) e->set_button_index((godot::MouseButton)(int)evt["button_index"]);
        return e;
    } else if (type == "joypad_button") {
        godot::Ref<godot::InputEventJoypadButton> e;
        e.instantiate();
        if (evt.has("button_index")) e->set_button_index((godot::JoyButton)(int)evt["button_index"]);
        return e;
    } else if (type == "joypad_motion") {
        godot::Ref<godot::InputEventJoypadMotion> e;
        e.instantiate();
        if (evt.has("axis")) e->set_axis((godot::JoyAxis)(int)evt["axis"]);
        if (evt.has("axis_value")) e->set_axis_value((double)evt["axis_value"]);
        return e;
    }

    return godot::Variant(); // nil = unknown type
}

} // namespace fennara
