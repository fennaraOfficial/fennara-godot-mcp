#pragma once

#ifdef __linux__

#include <stddef.h>
#include <stdint.h>

#define FENNARA_LINUX_CEF_BRIDGE_API_VERSION 1u

extern "C" {

enum fennara_cef_event_flags {
    FENNARA_CEF_EVENTFLAG_NONE = 0,
    FENNARA_CEF_EVENTFLAG_SHIFT_DOWN = 1 << 1,
    FENNARA_CEF_EVENTFLAG_CONTROL_DOWN = 1 << 2,
    FENNARA_CEF_EVENTFLAG_ALT_DOWN = 1 << 3,
    FENNARA_CEF_EVENTFLAG_LEFT_MOUSE_BUTTON = 1 << 4,
    FENNARA_CEF_EVENTFLAG_MIDDLE_MOUSE_BUTTON = 1 << 5,
    FENNARA_CEF_EVENTFLAG_RIGHT_MOUSE_BUTTON = 1 << 6,
    FENNARA_CEF_EVENTFLAG_COMMAND_DOWN = 1 << 7,
    FENNARA_CEF_EVENTFLAG_IS_KEY_PAD = 1 << 9,
    FENNARA_CEF_EVENTFLAG_IS_LEFT = 1 << 10,
    FENNARA_CEF_EVENTFLAG_IS_RIGHT = 1 << 11,
    FENNARA_CEF_EVENTFLAG_IS_REPEAT = 1 << 13,
};

enum fennara_cef_mouse_button_type {
    FENNARA_CEF_MOUSE_BUTTON_LEFT = 0,
    FENNARA_CEF_MOUSE_BUTTON_MIDDLE = 1,
    FENNARA_CEF_MOUSE_BUTTON_RIGHT = 2,
};

enum fennara_cef_key_event_type {
    FENNARA_CEF_KEYEVENT_RAWKEYDOWN = 0,
    FENNARA_CEF_KEYEVENT_KEYUP = 2,
    FENNARA_CEF_KEYEVENT_CHAR = 3,
};

struct fennara_cef_bridge_callbacks {
    void (*log)(const char *message, void *user_data);
    void (*paint)(const uint8_t *rgba, int width, int height, void *user_data);
    void *user_data;
};

struct fennara_cef_bridge_settings {
    int argc;
    char **argv;
    const char *subprocess_path;
    const char *resources_path;
    const char *locales_path;
    const char *root_cache_path;
    const char *cache_path;
    const char *log_file;
};

struct fennara_cef_bridge_browser_config {
    const char *url;
    int width;
    int height;
};

struct fennara_cef_bridge_mouse_event {
    int x;
    int y;
    uint32_t modifiers;
};

struct fennara_cef_bridge_key_event {
    int type;
    uint32_t modifiers;
    int windows_key_code;
    int native_key_code;
    uint16_t character;
    uint16_t unmodified_character;
};

struct fennara_cef_bridge_browser;

struct fennara_cef_bridge_api {
    uint32_t version;
    size_t size;
    int (*execute_process)(const fennara_cef_bridge_settings *settings,
                           const fennara_cef_bridge_callbacks *callbacks);
    int (*initialize)(const fennara_cef_bridge_settings *settings,
                      const fennara_cef_bridge_callbacks *callbacks);
    void (*do_message_loop_work)();
    void (*shutdown)();
    fennara_cef_bridge_browser *(*create_browser)(const fennara_cef_bridge_browser_config *config,
                                                  const fennara_cef_bridge_callbacks *callbacks);
    void (*close_browser)(fennara_cef_bridge_browser *browser);
    void (*resize_browser)(fennara_cef_bridge_browser *browser, int width, int height);
    void (*set_browser_hidden)(fennara_cef_bridge_browser *browser, int hidden);
    void (*set_browser_focus)(fennara_cef_bridge_browser *browser, int focused);
    void (*send_mouse_move)(fennara_cef_bridge_browser *browser,
                            const fennara_cef_bridge_mouse_event *event,
                            int mouse_leave);
    void (*send_mouse_click)(fennara_cef_bridge_browser *browser,
                             const fennara_cef_bridge_mouse_event *event,
                             int button,
                             int mouse_up,
                             int click_count);
    void (*send_mouse_wheel)(fennara_cef_bridge_browser *browser,
                             const fennara_cef_bridge_mouse_event *event,
                             int delta_x,
                             int delta_y);
    void (*send_key_event)(fennara_cef_bridge_browser *browser,
                           const fennara_cef_bridge_key_event *event);
};

typedef const fennara_cef_bridge_api *(*fennara_linux_cef_bridge_get_api_t)(uint32_t version);

const fennara_cef_bridge_api *fennara_linux_cef_bridge_get_api(uint32_t version);

} // extern "C"

#endif
