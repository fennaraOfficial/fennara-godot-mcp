#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#define FLOG(cat, msg) fennara::Logger::log(cat, msg)
#define FLOG_CTX(cat, msg, fields) fennara::Logger::log_context(cat, msg, fields)
#define FLOG_UI(msg)   FLOG("UI", msg)
#define FLOG_NET(msg)  FLOG("NET", msg)
#define FLOG_NET_CTX(msg, fields) FLOG_CTX("NET", msg, fields)
#define FLOG_TOOL(msg) FLOG("TOOL", msg)
#define FLOG_AI(msg)   FLOG("AI", msg)
#define FLOG_ERR(msg)  FLOG("ERR", msg)
#define FLOG_ERR_CTX(msg, fields) FLOG_CTX("ERR", msg, fields)
#define FLOG_SYS(msg)  FLOG("SYS", msg)

namespace fennara {

class Logger {
public:
    static void init();
    static void log(const char *category, const godot::String &message);
    static void log(const char *category, const char *message);
    static void log_context(const char *category, const godot::String &message, const godot::Dictionary &fields);
    static void log_activity(const godot::String &message);
    static godot::String record_incident(
        const godot::String &stage,
        const godot::String &error_code,
        const godot::String &message,
        const godot::Dictionary &fields = godot::Dictionary(),
        const godot::String &severity = "error");

private:
    static constexpr int64_t MAX_FILE_SIZE = 50 * 1024 * 1024; // 50MB
    static const char *LOG_DIR;
    static const char *LEGACY_LOG_PATH;
    static const char *INCIDENT_LOG_PATH;
    static godot::String *_session_log_path;
    static uint64_t _incident_counter;
    static int _activity_output_log_count;

    static godot::String format_context(const godot::Dictionary &fields);
    static godot::String format_context_value(const godot::Variant &value);
    static godot::String generate_incident_id();
    static godot::String current_date();
    static godot::String current_log_path();
    static bool ensure_log_dir();
    static void append_json_line(const char *path, const godot::Dictionary &entry);
    static void rotate_file_if_needed(const char *path, const char *old_path);
    static void rotate_if_needed();
    static void compact_activity_output_if_needed();
    static godot::String timestamp();
};

} // namespace fennara
