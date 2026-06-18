#include "fennara/logger.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace fennara {

const char *Logger::LOG_DIR = "user://.fennara/logs";
const char *Logger::LEGACY_LOG_PATH = "user://.fennara/fennara.log";
const char *Logger::INCIDENT_LOG_PATH = "user://.fennara/incidents.jsonl";
godot::String *Logger::_session_log_path = nullptr;
uint64_t Logger::_incident_counter = 0;
int Logger::_activity_output_log_count = 0;

void Logger::init() {
    rotate_if_needed();

    if (_session_log_path == nullptr) {
        _session_log_path = memnew(godot::String);
    }
    *_session_log_path = current_log_path();

    auto file = godot::FileAccess::open(*_session_log_path, godot::FileAccess::READ_WRITE);
    if (file.is_null() && !godot::FileAccess::file_exists(*_session_log_path)) {
        file = godot::FileAccess::open(*_session_log_path, godot::FileAccess::WRITE);
    }
    if (file.is_valid()) {
        file->seek_end();
        file->store_line(godot::String(""));
        file->store_line(godot::String("========================================"));
        file->store_line(godot::String("[") + timestamp() + "] [SYS] === Fennara session started ===");
    }
}

void Logger::log(const char *category, const godot::String &message) {
    godot::String path = _session_log_path == nullptr || _session_log_path->is_empty() ? current_log_path() : *_session_log_path;
    auto file = godot::FileAccess::open(path, godot::FileAccess::READ_WRITE);
    if (file.is_null() && !godot::FileAccess::file_exists(path)) {
        file = godot::FileAccess::open(path, godot::FileAccess::WRITE);
    }
    if (file.is_valid()) {
        file->seek_end();
        file->store_line(godot::String("[") + timestamp() + "] [" + category + "] " + message);
    }
}

void Logger::log(const char *category, const char *message) {
    log(category, godot::String(message));
}

void Logger::log_context(const char *category, const godot::String &message, const godot::Dictionary &fields) {
    godot::String context = format_context(fields);
    if (context.is_empty()) {
        log(category, message);
        return;
    }

    if (message.is_empty()) {
        log(category, context);
        return;
    }

    log(category, message + godot::String(" | ") + context);
}

void Logger::log_activity(const godot::String &message) {
    godot::String clean_message = message.strip_edges();
    if (clean_message.is_empty()) {
        return;
    }

    log("ACTIVITY", clean_message);
    compact_activity_output_if_needed();
    godot::UtilityFunctions::print(godot::String("[Fennara] ") + clean_message);
    _activity_output_log_count++;
}

godot::String Logger::record_incident(const godot::String &stage,
                                      const godot::String &error_code,
                                      const godot::String &message,
                                      const godot::Dictionary &fields,
                                      const godot::String &severity) {
    godot::String incident_id = generate_incident_id();

    godot::Dictionary entry = fields;
    entry["incident_id"] = incident_id;
    entry["created_at"] = timestamp();
    entry["stage"] = stage;
    entry["error_code"] = error_code;
    entry["severity"] = severity.is_empty() ? godot::String("error") : severity;
    entry["message"] = message;

    rotate_file_if_needed(INCIDENT_LOG_PATH, "user://.fennara/incidents.jsonl.old");
    append_json_line(INCIDENT_LOG_PATH, entry);

    godot::Dictionary log_fields = fields;
    log_fields["incident_id"] = incident_id;
    log_fields["stage"] = stage;
    log_fields["error_code"] = error_code;
    log_fields["severity"] = severity.is_empty() ? godot::String("error") : severity;
    log_context("ERR", message, log_fields);

    return incident_id;
}

godot::String Logger::format_context(const godot::Dictionary &fields) {
    if (fields.is_empty()) {
        return "";
    }

    godot::Array keys = fields.keys();
    keys.sort();

    godot::PackedStringArray parts;
    for (int i = 0; i < keys.size(); i++) {
        godot::String key = keys[i];
        parts.append(key + "=" + format_context_value(fields[key]));
    }

    return godot::String(" ").join(parts);
}

godot::String Logger::format_context_value(const godot::Variant &value) {
    godot::String rendered;
    switch (value.get_type()) {
        case godot::Variant::NIL:
            rendered = "null";
            break;
        case godot::Variant::BOOL:
            rendered = static_cast<bool>(value) ? "true" : "false";
            break;
        case godot::Variant::INT:
        case godot::Variant::FLOAT:
            rendered = godot::String(value);
            break;
        case godot::Variant::STRING:
            rendered = static_cast<godot::String>(value);
            break;
        default: {
            godot::Ref<godot::JSON> json;
            json.instantiate();
            rendered = json->stringify(value);
            break;
        }
    }

    rendered = rendered.replace("\\", "\\\\").replace("\r", "\\r").replace("\n", "\\n").replace("\"", "\\\"");
    if (rendered.contains(" ") || rendered.contains("=") || rendered.contains("|")) {
        rendered = godot::String("\"") + rendered + godot::String("\"");
    }
    return rendered;
}

void Logger::rotate_if_needed() {
    rotate_file_if_needed(INCIDENT_LOG_PATH, "user://.fennara/incidents.jsonl.old");
}

void Logger::compact_activity_output_if_needed() {
    constexpr int MAX_ACTIVITY_OUTPUT_LINES = 200;
    if (_activity_output_log_count < MAX_ACTIVITY_OUTPUT_LINES) {
        return;
    }

    bool cleared = false;
    godot::EditorInterface *editor = godot::EditorInterface::get_singleton();
    godot::Control *base = editor ? editor->get_base_control() : nullptr;
    if (base) {
        godot::Array children = base->find_children("*", "", true, false);
        for (int i = 0; i < children.size(); i++) {
            godot::Object *child = godot::Object::cast_to<godot::Object>(children[i]);
            if (!child || godot::String(child->get_class()) != "EditorLog") {
                continue;
            }
            if (child->has_method("clear")) {
                godot::Dictionary details;
                godot::Node *node = godot::Object::cast_to<godot::Node>(child);
                details["node_path"] = node && node->is_inside_tree() ? godot::String(node->get_path()) : godot::String();
                details["activity_output_log_count"] = _activity_output_log_count;
                log_context("SYS", "EditorLog activity compaction clearing editor log", details);
                child->call("clear");
                cleared = true;
                break;
            }
        }
    }

    _activity_output_log_count = 0;
    godot::String marker = cleared
        ? "Output activity compacted. Continuing with fresh Fennara logs."
        : "Output activity compacted above this point.";
    log("ACTIVITY", marker);
    godot::UtilityFunctions::print(godot::String("[Fennara] ") + marker);
    _activity_output_log_count++;
}

godot::String Logger::current_date() {
    auto dict = godot::Time::get_singleton()->get_datetime_dict_from_system();
    return godot::String("{0}-{1}-{2}").format(
        godot::Array::make(
            godot::String::num_int64((int64_t)dict["year"]),
            godot::String::num_int64((int64_t)dict["month"]).lpad(2, "0"),
            godot::String::num_int64((int64_t)dict["day"]).lpad(2, "0")
        )
    );
}

godot::String Logger::current_log_path() {
    if (!ensure_log_dir()) {
        return LEGACY_LOG_PATH;
    }
    return godot::String(LOG_DIR) + "/fennara_" + current_date() + ".log";
}

bool Logger::ensure_log_dir() {
    auto dir = godot::DirAccess::open("user://");
    if (!dir.is_valid()) {
        return false;
    }
    if (dir->dir_exists(".fennara/logs")) {
        return true;
    }
    return dir->make_dir_recursive(".fennara/logs") == godot::OK;
}

godot::String Logger::generate_incident_id() {
    ++_incident_counter;
    auto dict = godot::Time::get_singleton()->get_datetime_dict_from_system();
    uint64_t ticks = godot::Time::get_singleton()->get_ticks_msec();

    godot::String date_part = godot::String("{0}{1}{2}").format(
        godot::Array::make(
            godot::String::num_int64((int64_t)dict["year"]),
            godot::String::num_int64((int64_t)dict["month"]).lpad(2, "0"),
            godot::String::num_int64((int64_t)dict["day"]).lpad(2, "0")
        )
    );

    return godot::String("PLG-") + date_part + "-" +
           godot::String::num_uint64(ticks % 1000000000ULL).lpad(9, "0") + "-" +
           godot::String::num_uint64(_incident_counter % 100000ULL).lpad(5, "0");
}

void Logger::append_json_line(const char *path, const godot::Dictionary &entry) {
    ensure_log_dir();
    auto file = godot::FileAccess::open(path, godot::FileAccess::READ_WRITE);
    if (file.is_null()) {
        file = godot::FileAccess::open(path, godot::FileAccess::WRITE);
    }
    if (!file.is_valid()) {
        return;
    }
    godot::Ref<godot::JSON> json;
    json.instantiate();

    file->seek_end();
    file->store_line(json->stringify(entry));
}

void Logger::rotate_file_if_needed(const char *path, const char *old_path) {
    if (!godot::FileAccess::file_exists(path)) {
        return;
    }

    auto file = godot::FileAccess::open(path, godot::FileAccess::READ);
    if (file.is_null()) {
        return;
    }

    int64_t size = file->get_length();
    file.unref();

    if (size <= MAX_FILE_SIZE) {
        return;
    }

    auto dir = godot::DirAccess::open("user://");
    if (!dir.is_valid()) {
        return;
    }

    godot::String current_name = godot::String(path).replace("user://", "");
    godot::String old_name = godot::String(old_path).replace("user://", "");
    if (dir->file_exists(old_name)) {
        dir->remove(old_name);
    }
    dir->rename(current_name, old_name);
}

godot::String Logger::timestamp() {
    auto dict = godot::Time::get_singleton()->get_datetime_dict_from_system();
    return godot::String("{0}-{1}-{2} {3}:{4}:{5}").format(
        godot::Array::make(
            godot::String::num_int64((int64_t)dict["year"]),
            godot::String::num_int64((int64_t)dict["month"]).lpad(2, "0"),
            godot::String::num_int64((int64_t)dict["day"]).lpad(2, "0"),
            godot::String::num_int64((int64_t)dict["hour"]).lpad(2, "0"),
            godot::String::num_int64((int64_t)dict["minute"]).lpad(2, "0"),
            godot::String::num_int64((int64_t)dict["second"]).lpad(2, "0")
        )
    );
}

} // namespace fennara
