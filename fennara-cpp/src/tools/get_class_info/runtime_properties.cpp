#include "fennara/tools/get_class_info/get_class_info.hpp"
#include "fennara/tools/get_class_info/internal.hpp"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace fennara::get_class_info {

namespace {

godot::Dictionary &runtime_properties_cache() {
    static godot::Dictionary cache;
    return cache;
}
godot::PackedStringArray usage_flags_to_strings(int usage) {
    godot::PackedStringArray flags;
    if (usage == 0) {
        flags.append("NONE");
        return flags;
    }

    struct UsageFlag {
        int bit;
        const char *name;
    };

    static const UsageFlag kFlags[] = {
        {godot::PROPERTY_USAGE_STORAGE, "STORAGE"},
        {godot::PROPERTY_USAGE_EDITOR, "EDITOR"},
        {godot::PROPERTY_USAGE_INTERNAL, "INTERNAL"},
        {godot::PROPERTY_USAGE_CHECKABLE, "CHECKABLE"},
        {godot::PROPERTY_USAGE_CHECKED, "CHECKED"},
        {godot::PROPERTY_USAGE_GROUP, "GROUP"},
        {godot::PROPERTY_USAGE_CATEGORY, "CATEGORY"},
        {godot::PROPERTY_USAGE_SUBGROUP, "SUBGROUP"},
        {godot::PROPERTY_USAGE_CLASS_IS_BITFIELD, "CLASS_IS_BITFIELD"},
        {godot::PROPERTY_USAGE_NO_INSTANCE_STATE, "NO_INSTANCE_STATE"},
        {godot::PROPERTY_USAGE_RESTART_IF_CHANGED, "RESTART_IF_CHANGED"},
        {godot::PROPERTY_USAGE_SCRIPT_VARIABLE, "SCRIPT_VARIABLE"},
        {godot::PROPERTY_USAGE_STORE_IF_NULL, "STORE_IF_NULL"},
        {godot::PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED, "UPDATE_ALL_IF_MODIFIED"},
        {godot::PROPERTY_USAGE_SCRIPT_DEFAULT_VALUE, "SCRIPT_DEFAULT_VALUE"},
        {godot::PROPERTY_USAGE_CLASS_IS_ENUM, "CLASS_IS_ENUM"},
        {godot::PROPERTY_USAGE_NIL_IS_VARIANT, "NIL_IS_VARIANT"},
        {godot::PROPERTY_USAGE_ARRAY, "ARRAY"},
        {godot::PROPERTY_USAGE_ALWAYS_DUPLICATE, "ALWAYS_DUPLICATE"},
        {godot::PROPERTY_USAGE_NEVER_DUPLICATE, "NEVER_DUPLICATE"},
        {godot::PROPERTY_USAGE_HIGH_END_GFX, "HIGH_END_GFX"},
        {godot::PROPERTY_USAGE_NODE_PATH_FROM_SCENE_ROOT, "NODE_PATH_FROM_SCENE_ROOT"},
        {godot::PROPERTY_USAGE_RESOURCE_NOT_PERSISTENT, "RESOURCE_NOT_PERSISTENT"},
        {godot::PROPERTY_USAGE_KEYING_INCREMENTS, "KEYING_INCREMENTS"},
        {godot::PROPERTY_USAGE_DEFERRED_SET_RESOURCE, "DEFERRED_SET_RESOURCE"},
        {godot::PROPERTY_USAGE_EDITOR_INSTANTIATE_OBJECT, "EDITOR_INSTANTIATE_OBJECT"},
        {godot::PROPERTY_USAGE_EDITOR_BASIC_SETTING, "EDITOR_BASIC_SETTING"},
        {godot::PROPERTY_USAGE_READ_ONLY, "READ_ONLY"},
        {godot::PROPERTY_USAGE_SECRET, "SECRET"},
    };

    for (const UsageFlag &flag : kFlags) {
        if (usage & flag.bit) {
            flags.append(flag.name);
        }
    }
    return flags;
}

} // namespace

godot::Array collect_runtime_properties(const godot::String &class_name) {
    godot::Dictionary &cache = runtime_properties_cache();
    if (cache.has(class_name)) {
        return cache[class_name];
    }

    godot::Array properties;

    auto *cdb = godot::ClassDBSingleton::get_singleton();
    godot::TypedArray<godot::Dictionary> prop_list;
    godot::TypedArray<godot::Dictionary> class_prop_list;
    godot::TypedArray<godot::Dictionary> instance_prop_list;
    godot::Variant instantiated;
    godot::Object *instance = nullptr;
    const bool safe_to_live_instantiate =
        cdb->can_instantiate(class_name) &&
        (class_name == "RefCounted" || class_name == "Resource" ||
         cdb->is_parent_class(class_name, "RefCounted") ||
         cdb->is_parent_class(class_name, "Resource"));

    if (safe_to_live_instantiate) {
        instantiated = cdb->instantiate(class_name);
        if (instantiated.get_type() == godot::Variant::OBJECT) {
            instance = instantiated;
        }
    }

    class_prop_list = cdb->class_get_property_list(class_name, false);
    if (instance != nullptr) {
        instance_prop_list = instance->get_property_list();
    }

    godot::Dictionary seen_names;
    for (int i = 0; i < class_prop_list.size(); i++) {
        godot::Dictionary prop_info = class_prop_list[i];
        godot::String pname = prop_info.get("name", "");
        prop_list.append(prop_info);
        if (!pname.is_empty()) {
            seen_names[pname] = true;
        }
    }
    for (int i = 0; i < instance_prop_list.size(); i++) {
        godot::Dictionary prop_info = instance_prop_list[i];
        godot::String pname = prop_info.get("name", "");
        if (!pname.is_empty() && seen_names.has(pname)) {
            continue;
        }
        prop_list.append(prop_info);
        if (!pname.is_empty()) {
            seen_names[pname] = true;
        }
    }

    godot::String current_category;
    for (int i = 0; i < prop_list.size(); i++) {
        godot::Dictionary prop_info = prop_list[i];
        godot::String pname = prop_info["name"];
        int usage = prop_info["usage"];
        int type = prop_info["type"];

        if (usage & 64) {
            current_category = pname;
            continue;
        }
        if (usage & 128 || usage & 256) {
            continue;
        }
        if (!(usage & (godot::PROPERTY_USAGE_EDITOR |
                       godot::PROPERTY_USAGE_STORAGE))) {
            continue;
        }
        if (usage & godot::PROPERTY_USAGE_INTERNAL) {
            continue;
        }
        if (pname == "script" || pname == "_bundled" || pname.begins_with(".")) {
            continue;
        }

        godot::Variant default_val =
            cdb->class_get_property_default_value(class_name, pname);

        godot::Dictionary prop;
        prop["name"] = pname;
        prop["type"] = FennaraGetClassInfoTool::_type_to_string(type);
        prop["default"] = FennaraGetClassInfoTool::_serialize_default(default_val);
        prop["usage"] = usage;
        prop["usage_flags"] = usage_flags_to_strings(usage);
        if (!current_category.is_empty()) {
            prop["category"] = current_category;
        }

        godot::String hint_string = prop_info["hint_string"];
        int hint = prop_info["hint"];
        if (hint != 0 && !hint_string.is_empty()) {
            prop["hint"] = FennaraGetClassInfoTool::_hint_to_string(hint);
            prop["hint_string"] = hint_string;
        }

        godot::String class_name_field = prop_info.get("class_name", "");
        if (!class_name_field.is_empty()) {
            prop["class_name"] = class_name_field;
        }

        properties.append(prop);
    }

    cache[class_name] = properties;
    return properties;
}


} // namespace fennara::get_class_info
