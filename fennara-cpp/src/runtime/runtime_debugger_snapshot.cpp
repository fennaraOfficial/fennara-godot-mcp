#include "fennara/runtime/runtime_debugger_snapshot.hpp"

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/tree.hpp>
#include <godot_cpp/classes/tree_item.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <string>
#include <unordered_map>

namespace fennara::runtime_debugger_snapshot {
namespace {

constexpr int kMaxRawEvents = 1000;
constexpr int kMaxIssues = 50;
constexpr int kMaxDetailLines = 40;

bool has_text(const godot::String &text, const godot::String &needle) {
    return text.find(needle) >= 0;
}

int score_tree(godot::Tree *tree);

godot::String row_text(godot::Tree *tree, godot::TreeItem *item) {
    if (tree == nullptr || item == nullptr) {
        return "";
    }

    godot::PackedStringArray parts;
    int columns = tree->get_columns();
    if (columns < 1) {
        columns = 1;
    }
    for (int column = 0; column < columns; column++) {
        godot::String text = item->get_text(column).strip_edges();
        if (!text.is_empty()) {
            parts.append(text);
        }
    }
    return godot::String(" | ").join(parts);
}

bool looks_like_stack_frame(const godot::String &text) {
    return (has_text(text, ".gd:") || has_text(text, ".cs:")) &&
           (has_text(text, " @ ") || has_text(text, "@ "));
}

bool looks_like_error_text(const godot::String &text) {
    if (text.is_empty()) {
        return false;
    }

    godot::String lower = text.to_lower();
    return has_text(lower, "error") ||
           has_text(lower, "warning") ||
           has_text(lower, "warn") ||
           has_text(lower, "script error") ||
           has_text(lower, "condition") ||
           has_text(lower, "function blocked") ||
           has_text(lower, "<c++") ||
           looks_like_stack_frame(text);
}

bool looks_like_source_reference(const godot::String &text) {
    godot::String lower = text.to_lower();
    return has_text(text, "res://") ||
           has_text(text, "user://") ||
           has_text(lower, ".gd:") ||
           has_text(lower, ".cs:") ||
           has_text(lower, ".tscn") ||
           has_text(lower, ".tres") ||
           has_text(lower, ".res") ||
           has_text(text, "<C++ Source>");
}

bool looks_like_specific_error_message(const godot::String &text) {
    godot::String lower = text.strip_edges().to_lower();
    return has_text(lower, "error:") ||
           has_text(lower, "warning:") ||
           has_text(lower, "script error") ||
           has_text(lower, "condition ") ||
           has_text(lower, "condition \"") ||
           has_text(lower, "function blocked") ||
           has_text(lower, "invalid ") ||
           has_text(lower, "failed") ||
           has_text(lower, "cannot ") ||
           has_text(lower, "null instance");
}

bool looks_like_warning_message(const godot::String &text) {
    godot::String lower = text.strip_edges().to_lower();
    return has_text(lower, "warning") ||
           has_text(lower, "deprecated") ||
           has_text(lower, "never used") ||
           has_text(lower, "integer division") ||
           has_text(lower, "narrowing conversion") ||
           has_text(lower, "shadowing") ||
           has_text(lower, "ternary") ||
           has_text(lower, "concave polygon is assigned");
}

int issue_priority(const godot::Dictionary &event) {
    godot::String message = event.get("message", "");
    godot::String lower = message.strip_edges().to_lower();
    if (has_text(lower, "invalid ") ||
        has_text(lower, "failed") ||
        has_text(lower, "cannot ") ||
        has_text(lower, "null instance") ||
        has_text(lower, "condition ") ||
        has_text(lower, "condition \"")) {
        return 2;
    }

    if (!looks_like_warning_message(message)) {
        godot::Array stack = event.get("stack", godot::Array());
        if (!stack.is_empty() ||
            has_text(lower, "error:") ||
            has_text(lower, "script error")) {
            return 1;
        }
    }
    return 0;
}

bool is_actionable_runtime_issue(const godot::Array &raw_lines,
                                 const godot::Array &stack) {
    if (!stack.is_empty()) {
        return true;
    }

    for (int i = 0; i < raw_lines.size(); i++) {
        godot::String line = raw_lines[i];
        if (looks_like_source_reference(line) ||
            looks_like_stack_frame(line) ||
            looks_like_specific_error_message(line)) {
            return true;
        }
    }
    return false;
}

bool path_has_segment(const godot::Node *node, const godot::String &segment) {
    if (node == nullptr || !node->is_inside_tree()) {
        return false;
    }
    return godot::String(node->get_path()).find("/" + segment + "/") >= 0;
}

bool is_editor_chrome_tree(const godot::Tree *tree) {
    const godot::Node *node = godot::Object::cast_to<godot::Node>(tree);
    return path_has_segment(node, "FileSystem") ||
           path_has_segment(node, "Scene") ||
           path_has_segment(node, "Inspector") ||
           path_has_segment(node, "Import") ||
           path_has_segment(node, "History") ||
           path_has_segment(node, "Fennara");
}

bool is_runtime_error_tree_candidate(godot::Tree *tree) {
    if (tree == nullptr || tree->get_root() == nullptr ||
        is_editor_chrome_tree(tree)) {
        return false;
    }
    return score_tree(tree) > 0;
}

void collect_item_lines(godot::Tree *tree,
                        godot::TreeItem *item,
                        godot::Array &lines,
                        godot::Array &stack,
                        int &detail_count) {
    if (item == nullptr || detail_count >= kMaxDetailLines) {
        return;
    }

    godot::String text = row_text(tree, item);
    if (!text.is_empty()) {
        lines.append(text);
        if (looks_like_stack_frame(text)) {
            stack.append(text);
        }
        detail_count++;
    }

    godot::TreeItem *child = item->get_first_child();
    while (child != nullptr && detail_count < kMaxDetailLines) {
        collect_item_lines(tree, child, lines, stack, detail_count);
        child = child->get_next();
    }
}

int score_tree_item(godot::Tree *tree, godot::TreeItem *item) {
    if (item == nullptr) {
        return 0;
    }

    int score = looks_like_error_text(row_text(tree, item)) ? 1 : 0;
    godot::TreeItem *child = item->get_first_child();
    while (child != nullptr) {
        score += score_tree_item(tree, child);
        child = child->get_next();
    }
    return score;
}

int score_tree(godot::Tree *tree) {
    if (tree == nullptr || tree->get_root() == nullptr) {
        return 0;
    }
    return score_tree_item(tree, tree->get_root());
}

void find_error_trees(godot::Node *node, godot::Array &trees) {
    if (node == nullptr) {
        return;
    }

    if (godot::Tree *tree = godot::Object::cast_to<godot::Tree>(node)) {
        if (is_runtime_error_tree_candidate(tree)) {
            trees.append(tree);
        }
    }

    int child_count = node->get_child_count();
    for (int i = 0; i < child_count; i++) {
        find_error_trees(node->get_child(i), trees);
    }
}

godot::Tree *best_error_tree() {
    godot::EditorInterface *editor = godot::EditorInterface::get_singleton();
    if (editor == nullptr) {
        return nullptr;
    }

    godot::Node *base =
        godot::Object::cast_to<godot::Node>(editor->get_base_control());
    if (base == nullptr) {
        return nullptr;
    }

    godot::Array trees;
    find_error_trees(base, trees);

    godot::Tree *best = nullptr;
    int best_score = 0;
    for (int i = 0; i < trees.size(); i++) {
        godot::Tree *tree = godot::Object::cast_to<godot::Tree>(trees[i]);
        int score = score_tree(tree);
        if (score > best_score) {
            best = tree;
            best_score = score;
        }
    }
    return best;
}

std::string variant_key(const godot::Variant &value) {
    godot::String text(value);
    godot::CharString utf8 = text.utf8();
    return utf8.get_data();
}

godot::String normalized_issue_key(const godot::Dictionary &event) {
    godot::PackedStringArray parts;
    parts.append(godot::String(event.get("message", "")));

    godot::Array stack = event.get("stack", godot::Array());
    for (int i = 0; i < stack.size(); i++) {
        parts.append(godot::String(stack[i]));
    }

    godot::Array raw_lines = event.get("raw_lines", godot::Array());
    for (int i = 1; i < raw_lines.size(); i++) {
        godot::String line = raw_lines[i];
        if (looks_like_stack_frame(line) || has_text(line, "<C++ Source>")) {
            parts.append(line);
        }
    }
    return godot::String("\n").join(parts);
}

void append_grouped_event(const godot::Dictionary &event,
                          godot::Array &issues,
                          std::unordered_map<std::string, int> &issue_by_key,
                          bool &truncated) {
    std::string key = variant_key(normalized_issue_key(event));
    auto found = issue_by_key.find(key);
    if (found != issue_by_key.end()) {
        int issue_index = found->second;
        godot::Dictionary issue = issues[issue_index];
        int count = static_cast<int>(issue.get("count", 1));
        issue["count"] = count + 1;
        issue["last_message"] = event.get("message", "");
        issues[issue_index] = issue;
        return;
    }

    if (issues.size() >= kMaxIssues) {
        truncated = true;
        return;
    }

    godot::Dictionary issue = event;
    issue["count"] = 1;
    issue["first_message"] = event.get("message", "");
    issue["last_message"] = event.get("message", "");
    issue["priority"] = issue_priority(event);
    issue_by_key[key] = issues.size();
    issues.append(issue);
}

godot::Array group_events(const godot::Array &events,
                          int &total_event_count,
                          bool &truncated) {
    godot::Array issues;
    std::unordered_map<std::string, int> issue_by_key;
    total_event_count = events.size();
    truncated = false;

    for (int priority = 2; priority >= 0; priority--) {
        for (int i = 0; i < events.size(); i++) {
            if (events[i].get_type() != godot::Variant::DICTIONARY) {
                continue;
            }
            godot::Dictionary event = events[i];
            if (issue_priority(event) != priority) {
                continue;
            }
            append_grouped_event(event, issues, issue_by_key, truncated);
        }
    }
    return issues;
}

godot::Array collect_tree_events(godot::Tree *tree, bool &raw_truncated) {
    godot::Array events;
    raw_truncated = false;
    if (tree == nullptr || tree->get_root() == nullptr) {
        return events;
    }

    godot::TreeItem *root = tree->get_root();
    godot::TreeItem *item = root->get_first_child();
    if (item == nullptr && !row_text(tree, root).is_empty()) {
        item = root;
    }

    while (item != nullptr && events.size() < kMaxRawEvents) {
        godot::Array raw_lines;
        godot::Array stack;
        int detail_count = 0;
        collect_item_lines(tree, item, raw_lines, stack, detail_count);

        bool is_error = false;
        for (int i = 0; i < raw_lines.size(); i++) {
            if (looks_like_error_text(raw_lines[i])) {
                is_error = true;
                break;
            }
        }

        if (is_error && !raw_lines.is_empty() &&
            is_actionable_runtime_issue(raw_lines, stack)) {
            godot::Dictionary error;
            error["message"] = raw_lines[0];
            error["raw_lines"] = raw_lines;
            error["stack"] = stack;
            error["detail_count"] = raw_lines.size();
            events.append(error);
        }

        if (item == root) {
            break;
        }
        item = item->get_next();
    }

    if (item != nullptr) {
        raw_truncated = true;
    }
    return events;
}

godot::Dictionary make_summary(const godot::Array &errors,
                               const godot::String &source,
                               const godot::Dictionary &context,
                               int total_event_count,
                               bool truncated) {
    int stack_frame_count = 0;
    for (int i = 0; i < errors.size(); i++) {
        if (errors[i].get_type() != godot::Variant::DICTIONARY) {
            continue;
        }
        godot::Dictionary error = errors[i];
        godot::Array stack = error.get("stack", godot::Array());
        stack_frame_count += stack.size();
    }

    bool current_run_observed =
        static_cast<bool>(context.get("current_run_observed", false));
    godot::Dictionary summary;
    summary["error_count"] = errors.size();
    summary["unique_issue_count"] = errors.size();
    summary["total_event_count"] = total_event_count;
    summary["stack_frame_count"] = stack_frame_count;
    summary["source"] = source;
    summary["truncated"] = truncated;
    summary["run_id"] = context.get("run_id", 0);
    summary["current_run_observed"] = current_run_observed;
    summary["is_playing_scene"] = context.get("is_playing_scene", false);
    summary["playing_scene"] = context.get("playing_scene", "");
    summary["started_at_ms"] = context.get("started_at_ms", 0);
    summary["stopped_at_ms"] = context.get("stopped_at_ms", 0);
    summary["latest_debugger_session_id"] =
        context.get("latest_debugger_session_id", -1);
    if (!current_run_observed) {
        summary["freshness_warning"] =
            "Fennara has not observed a scene run since the plugin started; "
            "results may be a current editor UI snapshot rather than current-run errors.";
    }
    return summary;
}

} // namespace

godot::Dictionary capture(const godot::Dictionary &context) {
    godot::Tree *tree = best_error_tree();
    bool raw_truncated = false;
    godot::Array events = collect_tree_events(tree, raw_truncated);
    int total_event_count = 0;
    bool group_truncated = false;
    godot::Array errors = group_events(events, total_event_count, group_truncated);
    godot::String source =
        tree == nullptr ? "debugger_session" : "debugger_errors_tree";
    bool truncated = raw_truncated || group_truncated;

    godot::Dictionary result;
    result["source"] = source;
    result["summary"] =
        make_summary(errors, source, context, total_event_count, truncated);
    result["errors"] = errors;
    result["captured_messages"] = godot::Array();
    result["tree_path"] =
        tree && tree->is_inside_tree() ? godot::String(tree->get_path())
                                       : godot::String();
    return result;
}

} // namespace fennara::runtime_debugger_snapshot
