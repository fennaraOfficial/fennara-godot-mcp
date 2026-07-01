#include "fennara/tools/validate_scene.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/reg_ex.hpp>
#include <godot_cpp/classes/reg_ex_match.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>

namespace fennara {

namespace {

godot::Ref<godot::Script> s_load_gdscript(const godot::String &script_path) {
    if (script_path.is_empty() || script_path.contains("::")) {
        return godot::Ref<godot::Script>();
    }
    if (!godot::FileAccess::file_exists(script_path)) {
        return godot::Ref<godot::Script>();
    }
    return godot::ResourceLoader::get_singleton()->load(
        script_path, "GDScript",
        godot::ResourceLoader::CACHE_MODE_IGNORE);
}

godot::String s_script_path_from_state(
    const godot::Ref<godot::SceneState> &state,
    int node_idx) {
    int prop_count = state->get_node_property_count(node_idx);
    for (int p = 0; p < prop_count; p++) {
        godot::String prop_name =
            godot::String(state->get_node_property_name(node_idx, p));
        if (prop_name != "script") {
            continue;
        }
        godot::Variant val = state->get_node_property_value(node_idx, p);
        if (val.get_type() != godot::Variant::OBJECT) {
            return "";
        }
        godot::Object *obj = val;
        auto *res = godot::Object::cast_to<godot::Resource>(obj);
        return res ? res->get_path() : "";
    }
    return "";
}

godot::String s_instanced_root_script_path(
    const godot::Ref<godot::SceneState> &state,
    int node_idx,
    godot::String &instance_scene_path) {
    godot::Ref<godot::PackedScene> instance = state->get_node_instance(node_idx);
    if (!instance.is_valid()) {
        return "";
    }

    instance_scene_path = instance->get_path();
    godot::Ref<godot::SceneState> instance_state = instance->get_state();
    if (!instance_state.is_valid() || instance_state->get_node_count() <= 0) {
        return "";
    }
    return s_script_path_from_state(instance_state, 0);
}

godot::Dictionary s_scene_props_for_node(
    const godot::Ref<godot::SceneState> &state,
    int node_idx) {
    godot::Dictionary scene_props;
    int prop_count = state->get_node_property_count(node_idx);
    for (int p = 0; p < prop_count; p++) {
        godot::String pname =
            godot::String(state->get_node_property_name(node_idx, p));
        scene_props[pname] = state->get_node_property_value(node_idx, p);
    }
    return scene_props;
}

bool s_is_identifier_char(char32_t c) {
    return c == '_' ||
           (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z');
}

int s_count_identifier_mentions(const godot::String &text,
                                const godot::String &identifier) {
    if (text.is_empty() || identifier.is_empty()) {
        return 0;
    }

    int count = 0;
    int pos = 0;
    while (true) {
        pos = text.find(identifier, pos);
        if (pos < 0) {
            break;
        }

        int after_pos = pos + identifier.length();
        bool has_identifier_before =
            pos > 0 && s_is_identifier_char(text[pos - 1]);
        bool has_identifier_after =
            after_pos < text.length() && s_is_identifier_char(text[after_pos]);
        if (!has_identifier_before && !has_identifier_after) {
            count++;
        }
        pos = after_pos;
    }
    return count;
}

bool s_script_references_property_beyond_export(
    const godot::String &script_path,
    const godot::String &property_name) {
    godot::Ref<godot::FileAccess> file =
        godot::FileAccess::open(script_path, godot::FileAccess::READ);
    if (!file.is_valid()) {
        return false;
    }

    // Cheap signal: one occurrence is normally the export declaration; more
    // occurrences mean the script likely reads or writes the property.
    return s_count_identifier_mentions(file->get_as_text(), property_name) > 1;
}

} // namespace

void FennaraValidateSceneTool::_check_script_extends_mismatch(
    const godot::Ref<godot::SceneState> &state, godot::Array &issues) {
    int count = state->get_node_count();
    for (int i = 0; i < count; i++) {
        godot::String script_path = _get_script_path(state, i);
        godot::Ref<godot::Script> script = s_load_gdscript(script_path);
        if (!script.is_valid()) continue;

        godot::StringName script_base = script->get_instance_base_type();
        if (script_base == godot::StringName()) continue;

        godot::StringName node_type = state->get_node_type(i);
        if (node_type == godot::StringName()) continue;

        if (!_inherits_class(node_type, script_base)) {
            _add_issue(issues, _build_node_path(state, i),
                       "script_extends_mismatch", "warning",
                       godot::String("Script extends '") +
                           godot::String(script_base) +
                           "' but node type is '" +
                           godot::String(node_type) + "'");
        }
    }
}

void FennaraValidateSceneTool::_check_unset_export_vars(
    const godot::Ref<godot::SceneState> &state, godot::Array &issues) {
    int count = state->get_node_count();
    for (int i = 0; i < count; i++) {
        godot::String script_path = _get_script_path(state, i);
        godot::String instance_scene_path;
        if (script_path.is_empty()) {
            script_path = s_instanced_root_script_path(
                state, i, instance_scene_path);
        }

        godot::Ref<godot::Script> script = s_load_gdscript(script_path);
        if (!script.is_valid()) continue;

        godot::Dictionary scene_props = s_scene_props_for_node(state, i);

        godot::TypedArray<godot::Dictionary> prop_list =
            script->get_script_property_list();
        for (int p = 0; p < prop_list.size(); p++) {
            godot::Dictionary prop_info = prop_list[p];
            int usage = prop_info.get("usage", 0);
            if (!(usage & godot::PROPERTY_USAGE_STORAGE)) continue;

            int type = prop_info.get("type", 0);
            int hint = prop_info.get("hint", 0);
            bool is_resource_export =
                (hint == 17) || (type == godot::Variant::OBJECT);
            if (!is_resource_export) continue;

            godot::String prop_name = prop_info.get("name", "");
            if (prop_name.is_empty()) continue;

            if (scene_props.has(prop_name)) {
                godot::Variant val = scene_props[prop_name];
                if (val.get_type() != godot::Variant::NIL) continue;
            }

            godot::Variant default_val =
                script->get_property_default_value(godot::StringName(prop_name));
            if (default_val.get_type() != godot::Variant::NIL) {
                continue;
            }

            godot::String hint_string = prop_info.get("hint_string", "");
            godot::String type_label =
                hint_string.is_empty() ? "Object" : hint_string;

            godot::Dictionary extra;
            extra["property"] = prop_name;
            extra["type"] = type_label;
            if (!instance_scene_path.is_empty()) {
                extra["instance_scene"] = instance_scene_path;
            }

            bool referenced_in_script =
                s_script_references_property_beyond_export(
                    script_path, prop_name);
            extra["referenced_in_script"] = referenced_in_script;

            godot::String message;
            godot::String severity = referenced_in_script ? "warning" : "info";
            if (referenced_in_script) {
                message = godot::String("Unset Resource export var '") +
                    prop_name + "' (type: " + type_label + ")";
            } else {
                message = godot::String("Exported Resource var '") +
                    prop_name + "' (type: " + type_label + ") is unset";
            }
            if (!instance_scene_path.is_empty()) {
                message += " inherited from instance " + instance_scene_path;
            }
            if (referenced_in_script) {
                message +=
                    " and referenced by the script; verify it is optional/null-guarded or assign it in the scene";
            } else {
                message += ". This is OK if optional or assigned at runtime";
            }
            _add_issue(issues, _build_node_path(state, i),
                       "unset_export_var", severity,
                       message,
                       extra);
        }
    }
}

namespace {

void s_collect_regex_matches(
    const godot::Ref<godot::RegEx> &re, const godot::String &line,
    int group, godot::Array &out) {
    godot::TypedArray<godot::RegExMatch> matches = re->search_all(line);
    for (int m = 0; m < matches.size(); m++) {
        godot::Ref<godot::RegExMatch> match = matches[m];
        godot::String val = match->get_string(group);
        if (!val.is_empty()) out.append(val);
    }
}

godot::String s_strip_inline_comment(const godot::String &line) {
    int hash_pos = line.find("#");
    if (hash_pos <= 0) return line;

    int quote_count = 0;
    for (int i = 0; i < hash_pos; i++) {
        if (line[i] == '"') quote_count++;
    }
    if (quote_count % 2 != 0) return line;

    return line.substr(0, hash_pos);
}

godot::Node *s_get_receiver_node(
    godot::Node *script_node, godot::Node *root,
    const godot::Dictionary &alias_nodes,
    const godot::String &receiver_name) {
    if (receiver_name.is_empty() || receiver_name == "self") {
        return script_node;
    }

    if (!alias_nodes.has(receiver_name)) return nullptr;

    godot::Variant alias_value = alias_nodes[receiver_name];
    if (alias_value.get_type() != godot::Variant::OBJECT) return nullptr;

    godot::Object *obj = alias_value;
    godot::Node *alias_node = godot::Object::cast_to<godot::Node>(obj);
    if (!alias_node) return nullptr;

    godot::Node *cursor = alias_node;
    while (cursor) {
        if (cursor == root) return alias_node;
        cursor = cursor->get_parent();
    }

    return nullptr;
}

bool s_ref_goes_above_root(
    godot::Node *base_node, godot::Node *root, const godot::String &ref) {
    godot::PackedStringArray segs = ref.split("/");
    int up = 0;
    for (int i = 0; i < segs.size(); i++) {
        if (segs[i] == "..") up++;
        else break;
    }

    int depth = 0;
    godot::Node *cursor = base_node;
    while (cursor != root && cursor != nullptr) {
        depth++;
        cursor = cursor->get_parent();
    }

    return up > depth;
}

void s_try_register_alias(
    const godot::Ref<godot::RegEx> &re_assign_dollar,
    const godot::Ref<godot::RegEx> &re_assign_dollar_quoted,
    const godot::Ref<godot::RegEx> &re_assign_get_node,
    const godot::String &line,
    godot::Node *script_node,
    godot::Node *root,
    godot::Dictionary &alias_nodes) {
    godot::Ref<godot::RegExMatch> match = re_assign_dollar->search(line);
    if (match.is_null()) {
        match = re_assign_dollar_quoted->search(line);
    }

    if (!match.is_null()) {
        godot::String alias_name = match->get_string(1);
        godot::String ref_path = match->get_string(2);
        godot::Node *target =
            script_node->get_node_or_null(godot::NodePath(ref_path));
        if (target) alias_nodes[alias_name] = target;
        return;
    }

    match = re_assign_get_node->search(line);
    if (match.is_null()) return;

    godot::String alias_name = match->get_string(1);
    godot::String receiver_name = match->get_string(2);
    godot::String ref_path = match->get_string(3);

    godot::Node *receiver =
        s_get_receiver_node(script_node, root, alias_nodes, receiver_name);
    if (!receiver) return;

    godot::Node *target = receiver->get_node_or_null(godot::NodePath(ref_path));
    if (target) alias_nodes[alias_name] = target;
}

void s_check_script_refs_recursive(
    godot::Node *node, godot::Node *root,
    const godot::Ref<godot::RegEx> &re_dollar,
    const godot::Ref<godot::RegEx> &re_dollar_quoted,
    const godot::Ref<godot::RegEx> &re_get_node,
    const godot::Ref<godot::RegEx> &re_assign_dollar,
    const godot::Ref<godot::RegEx> &re_assign_dollar_quoted,
    const godot::Ref<godot::RegEx> &re_assign_get_node,
    godot::Array &issues) {

    godot::Ref<godot::Script> script = node->get_script();
    if (script.is_valid()) {
        godot::String spath = script->get_path();
        if (spath.get_extension() == "gd" && !spath.contains("::") &&
            godot::FileAccess::file_exists(spath)) {
            godot::String node_scene_path =
                godot::String(root->get_path_to(node));

            godot::Ref<godot::FileAccess> f =
                godot::FileAccess::open(spath, godot::FileAccess::READ);
            if (f.is_valid()) {
                godot::String content = f->get_as_text();
                f.unref();

                godot::PackedStringArray lines = content.split("\n");
                godot::Dictionary alias_nodes;
                for (int li = 0; li < lines.size(); li++) {
                    godot::String line =
                        s_strip_inline_comment(lines[li]).strip_edges();
                    if (line.is_empty() || line.begins_with("#")) continue;

                    s_try_register_alias(
                        re_assign_dollar, re_assign_dollar_quoted,
                        re_assign_get_node, line, node, root, alias_nodes);

                    godot::Array paths;
                    s_collect_regex_matches(re_dollar, line, 1, paths);
                    s_collect_regex_matches(re_dollar_quoted, line, 1, paths);
                    for (int pi = 0; pi < paths.size(); pi++) {
                        godot::String ref = paths[pi];
                        godot::Node *target =
                            node->get_node_or_null(godot::NodePath(ref));
                        if (target) continue;
                        if (s_ref_goes_above_root(node, root, ref)) continue;

                        godot::Dictionary issue;
                        issue["node"] = node_scene_path;
                        issue["node_path"] = node_scene_path;
                        issue["check"] = "invalid_script_node_ref";
                        issue["severity"] = "warning";
                        issue["message"] =
                            godot::String("Script '") + spath +
                            "' line " +
                            godot::String::num_int64(li + 1) +
                            ": node path '" + ref +
                            "' does not resolve to any node";
                        issue["script"] = spath;
                        issue["line"] = li + 1;
                        issue["ref_path"] = ref;
                        issues.append(issue);
                    }

                    godot::TypedArray<godot::RegExMatch> get_node_matches =
                        re_get_node->search_all(line);
                    for (int mi = 0; mi < get_node_matches.size(); mi++) {
                        godot::Ref<godot::RegExMatch> match =
                            get_node_matches[mi];
                        godot::String receiver_name = match->get_string(1);
                        godot::String ref = match->get_string(2);

                        godot::Node *receiver = s_get_receiver_node(
                            node, root, alias_nodes, receiver_name);
                        if (!receiver) continue;

                        godot::Node *target =
                            receiver->get_node_or_null(godot::NodePath(ref));
                        if (target) continue;
                        if (s_ref_goes_above_root(receiver, root, ref)) continue;

                        godot::Dictionary issue;
                        issue["node"] = node_scene_path;
                        issue["node_path"] = node_scene_path;
                        issue["check"] = "invalid_script_node_ref";
                        issue["severity"] = "warning";
                        issue["message"] =
                            godot::String("Script '") + spath +
                            "' line " +
                            godot::String::num_int64(li + 1) +
                            ": node path '" + ref +
                            "' does not resolve to any node";
                        issue["script"] = spath;
                        issue["line"] = li + 1;
                        issue["ref_path"] = ref;
                        issues.append(issue);
                    }
                }
            }
        }
    }

    for (int c = 0; c < node->get_child_count(); c++) {
        s_check_script_refs_recursive(
            node->get_child(c), root,
            re_dollar, re_dollar_quoted, re_get_node,
            re_assign_dollar, re_assign_dollar_quoted, re_assign_get_node,
            issues);
    }
}

} // namespace

void FennaraValidateSceneTool::_check_script_node_references(
    const godot::String &scene_path, godot::Array &issues) {
    godot::Ref<godot::PackedScene> packed =
        godot::ResourceLoader::get_singleton()->load(
            scene_path, "PackedScene",
            godot::ResourceLoader::CACHE_MODE_IGNORE);
    if (!packed.is_valid()) return;

    godot::Node *root = packed->instantiate();
    if (!root) return;

    godot::Ref<godot::RegEx> re_dollar;
    re_dollar.instantiate();
    re_dollar->compile("\\$(%?[A-Za-z_]\\w*(?:/\\w+)*)");

    godot::Ref<godot::RegEx> re_dollar_quoted;
    re_dollar_quoted.instantiate();
    re_dollar_quoted->compile("\\$\"([^\"]+)\"");

    godot::Ref<godot::RegEx> re_get_node;
    re_get_node.instantiate();
    re_get_node->compile(
        "(?:\\b([A-Za-z_]\\w*|self)\\s*\\.\\s*)?"
        "get_node(?:_or_null)?\\(\\s*[\"']([^\"']+)[\"']\\s*\\)");

    godot::Ref<godot::RegEx> re_assign_dollar;
    re_assign_dollar.instantiate();
    re_assign_dollar->compile(
        "(?:@onready\\s+)?var\\s+([A-Za-z_]\\w*)\\s*(?::[^=]+)?="
        "\\s*\\$(%?[A-Za-z_]\\w*(?:/\\w+)*)");

    godot::Ref<godot::RegEx> re_assign_dollar_quoted;
    re_assign_dollar_quoted.instantiate();
    re_assign_dollar_quoted->compile(
        "(?:@onready\\s+)?var\\s+([A-Za-z_]\\w*)\\s*(?::[^=]+)?="
        "\\s*\\$\"([^\"]+)\"");

    godot::Ref<godot::RegEx> re_assign_get_node;
    re_assign_get_node.instantiate();
    re_assign_get_node->compile(
        "(?:@onready\\s+)?var\\s+([A-Za-z_]\\w*)\\s*(?::[^=]+)?="
        "\\s*(?:([A-Za-z_]\\w*|self)\\s*\\.\\s*)?"
        "get_node(?:_or_null)?\\(\\s*[\"']([^\"']+)[\"']\\s*\\)");

    s_check_script_refs_recursive(
        root, root, re_dollar, re_dollar_quoted, re_get_node,
        re_assign_dollar, re_assign_dollar_quoted, re_assign_get_node,
        issues);

    root->queue_free();
}

} // namespace fennara
