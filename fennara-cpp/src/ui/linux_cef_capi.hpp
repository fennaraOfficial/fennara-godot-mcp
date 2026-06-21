#pragma once

#ifdef __linux__

#include <cstddef>
#include <cstdint>

namespace fennara::linux_cef_runtime::capi {

struct cef_accessibility_handler_t;
struct cef_audio_handler_t;
struct cef_browser_t;
struct cef_browser_host_t;
struct cef_client_t;
struct cef_command_handler_t;
struct cef_context_menu_handler_t;
struct cef_dialog_handler_t;
struct cef_dictionary_value_t;
struct cef_display_handler_t;
struct cef_download_handler_t;
struct cef_drag_data_t;
struct cef_drag_handler_t;
struct cef_find_handler_t;
struct cef_focus_handler_t;
struct cef_frame_handler_t;
struct cef_frame_t;
struct cef_jsdialog_handler_t;
struct cef_keyboard_handler_t;
struct cef_life_span_handler_t;
struct cef_load_handler_t;
struct cef_key_event_t;
struct cef_permission_handler_t;
struct cef_print_handler_t;
struct cef_process_message_t;
struct cef_render_handler_t;
struct cef_request_context_t;
struct cef_request_handler_t;

using cef_window_handle_t = unsigned long;
using cef_color_t = uint32_t;

enum cef_log_severity_t : uint32_t {
    LOGSEVERITY_DEFAULT = 0,
    LOGSEVERITY_WARNING = 3,
};

enum cef_log_items_t : uint32_t {
    LOG_ITEMS_DEFAULT = 0,
};

enum cef_runtime_style_t : uint32_t {
    CEF_RUNTIME_STYLE_DEFAULT = 0,
    CEF_RUNTIME_STYLE_ALLOY = 2,
};

enum cef_paint_element_type_t : uint32_t {
    PET_VIEW = 0,
    PET_POPUP = 1,
};

enum cef_process_id_t : uint32_t {
    PID_BROWSER = 0,
    PID_RENDERER = 1,
};

enum cef_mouse_button_type_t : uint32_t {
    MBT_LEFT = 0,
    MBT_MIDDLE = 1,
    MBT_RIGHT = 2,
};

enum cef_event_flags_t : uint32_t {
    EVENTFLAG_NONE = 0,
    EVENTFLAG_CAPS_LOCK_ON = 1 << 0,
    EVENTFLAG_SHIFT_DOWN = 1 << 1,
    EVENTFLAG_CONTROL_DOWN = 1 << 2,
    EVENTFLAG_ALT_DOWN = 1 << 3,
    EVENTFLAG_LEFT_MOUSE_BUTTON = 1 << 4,
    EVENTFLAG_MIDDLE_MOUSE_BUTTON = 1 << 5,
    EVENTFLAG_RIGHT_MOUSE_BUTTON = 1 << 6,
    EVENTFLAG_COMMAND_DOWN = 1 << 7,
    EVENTFLAG_NUM_LOCK_ON = 1 << 8,
    EVENTFLAG_IS_KEY_PAD = 1 << 9,
    EVENTFLAG_IS_LEFT = 1 << 10,
    EVENTFLAG_IS_RIGHT = 1 << 11,
    EVENTFLAG_ALTGR_DOWN = 1 << 12,
    EVENTFLAG_IS_REPEAT = 1 << 13,
    EVENTFLAG_PRECISION_SCROLLING_DELTA = 1 << 14,
    EVENTFLAG_SCROLL_BY_PAGE = 1 << 15,
};

enum cef_key_event_type_t : uint32_t {
    KEYEVENT_RAWKEYDOWN = 0,
    KEYEVENT_KEYDOWN = 1,
    KEYEVENT_KEYUP = 2,
    KEYEVENT_CHAR = 3,
};

struct cef_string_t {
    char16_t *str = nullptr;
    size_t length = 0;
    void (*dtor)(char16_t *str) = nullptr;
};

struct cef_rect_t {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct cef_main_args_t {
    int argc = 0;
    char **argv = nullptr;
};

struct cef_base_ref_counted_t {
    size_t size = 0;
    void (*add_ref)(cef_base_ref_counted_t *self) = nullptr;
    int (*release)(cef_base_ref_counted_t *self) = nullptr;
    int (*has_one_ref)(cef_base_ref_counted_t *self) = nullptr;
    int (*has_at_least_one_ref)(cef_base_ref_counted_t *self) = nullptr;
};

struct cef_mouse_event_t {
    int x = 0;
    int y = 0;
    uint32_t modifiers = 0;
};

struct cef_key_event_t {
    size_t size = sizeof(cef_key_event_t);
    cef_key_event_type_t type = KEYEVENT_RAWKEYDOWN;
    uint32_t modifiers = 0;
    int windows_key_code = 0;
    int native_key_code = 0;
    int is_system_key = 0;
    char16_t character = 0;
    char16_t unmodified_character = 0;
    int focus_on_editable_field = 0;
};

struct cef_window_info_t {
    size_t size = sizeof(cef_window_info_t);
    cef_string_t window_name;
    cef_rect_t bounds;
    cef_window_handle_t parent_window = 0;
    int windowless_rendering_enabled = 0;
    int shared_texture_enabled = 0;
    int external_begin_frame_enabled = 0;
    cef_window_handle_t window = 0;
    cef_runtime_style_t runtime_style = CEF_RUNTIME_STYLE_ALLOY;
};

struct cef_settings_t {
    size_t size = sizeof(cef_settings_t);
    int no_sandbox = 0;
    cef_string_t browser_subprocess_path;
    cef_string_t framework_dir_path;
    cef_string_t main_bundle_path;
    int multi_threaded_message_loop = 0;
    int external_message_pump = 0;
    int windowless_rendering_enabled = 0;
    int command_line_args_disabled = 0;
    cef_string_t cache_path;
    cef_string_t root_cache_path;
    int persist_session_cookies = 0;
    cef_string_t user_agent;
    cef_string_t user_agent_product;
    cef_string_t locale;
    cef_string_t log_file;
    cef_log_severity_t log_severity = LOGSEVERITY_DEFAULT;
    cef_log_items_t log_items = LOG_ITEMS_DEFAULT;
    cef_string_t javascript_flags;
    cef_string_t resources_dir_path;
    cef_string_t locales_dir_path;
    int remote_debugging_port = 0;
    int uncaught_exception_stack_size = 0;
    cef_color_t background_color = 0;
    cef_string_t accept_language_list;
    cef_string_t cookieable_schemes_list;
    int cookieable_schemes_exclude_defaults = 0;
    cef_string_t chrome_policy_id;
    int chrome_app_icon_id = 0;
    int disable_signal_handlers = 0;
    int use_views_default_popup = 0;
};

struct cef_browser_settings_t {
    size_t size = sizeof(cef_browser_settings_t);
    int windowless_frame_rate = 30;
    cef_string_t standard_font_family;
    cef_string_t fixed_font_family;
    cef_string_t serif_font_family;
    cef_string_t sans_serif_font_family;
    cef_string_t cursive_font_family;
    cef_string_t fantasy_font_family;
    int default_font_size = 0;
    int default_fixed_font_size = 0;
    int minimum_font_size = 0;
    int minimum_logical_font_size = 0;
    cef_string_t default_encoding;
    uint32_t remote_fonts = 0;
    uint32_t javascript = 0;
    uint32_t javascript_close_windows = 0;
    uint32_t javascript_access_clipboard = 0;
    uint32_t javascript_dom_paste = 0;
    uint32_t image_loading = 0;
    uint32_t image_shrink_standalone_to_fit = 0;
    uint32_t text_area_resize = 0;
    uint32_t tab_to_links = 0;
    uint32_t local_storage = 0;
    uint32_t databases_deprecated = 0;
    uint32_t webgl = 0;
    cef_color_t background_color = 0xffffffff;
    uint32_t chrome_status_bubble = 0;
    uint32_t chrome_zoom_bubble = 0;
    uint32_t ax_viewport_collapse = 0;
};

struct cef_browser_t {
    cef_base_ref_counted_t base;
    int (*is_valid)(cef_browser_t *self) = nullptr;
    cef_browser_host_t *(*get_host)(cef_browser_t *self) = nullptr;
};

struct cef_browser_host_t {
    cef_base_ref_counted_t base;
    cef_browser_t *(*get_browser)(cef_browser_host_t *self) = nullptr;
    void (*close_browser)(cef_browser_host_t *self, int force_close) = nullptr;
    int (*try_close_browser)(cef_browser_host_t *self) = nullptr;
    int (*is_ready_to_be_closed)(cef_browser_host_t *self) = nullptr;
    void (*set_focus)(cef_browser_host_t *self, int focus) = nullptr;
    cef_window_handle_t (*get_window_handle)(cef_browser_host_t *self) = nullptr;
    cef_window_handle_t (*get_opener_window_handle)(cef_browser_host_t *self) = nullptr;
    int (*get_opener_identifier)(cef_browser_host_t *self) = nullptr;
    int (*has_view)(cef_browser_host_t *self) = nullptr;
    cef_client_t *(*get_client)(cef_browser_host_t *self) = nullptr;
    cef_request_context_t *(*get_request_context)(cef_browser_host_t *self) = nullptr;
    void *padding_until_was_resized[22] = {};
    void (*was_resized)(cef_browser_host_t *self) = nullptr;
    void (*was_hidden)(cef_browser_host_t *self, int hidden) = nullptr;
    void (*notify_screen_info_changed)(cef_browser_host_t *self) = nullptr;
    void (*invalidate)(cef_browser_host_t *self, cef_paint_element_type_t type) = nullptr;
    void (*send_key_event)(cef_browser_host_t *self, const cef_key_event_t *event) = nullptr;
    void (*send_mouse_click_event)(cef_browser_host_t *self,
                                   const cef_mouse_event_t *event,
                                   cef_mouse_button_type_t type,
                                   int mouse_up,
                                   int click_count) = nullptr;
    void (*send_mouse_move_event)(cef_browser_host_t *self,
                                  const cef_mouse_event_t *event,
                                  int mouse_leave) = nullptr;
    void (*send_mouse_wheel_event)(cef_browser_host_t *self,
                                   const cef_mouse_event_t *event,
                                   int delta_x,
                                   int delta_y) = nullptr;
    void (*send_focus_event)(cef_browser_host_t *self, int set_focus) = nullptr;
};

struct cef_render_handler_t {
    cef_base_ref_counted_t base;
    cef_accessibility_handler_t *(*get_accessibility_handler)(cef_render_handler_t *self) = nullptr;
    int (*get_root_screen_rect)(cef_render_handler_t *self, cef_browser_t *browser, cef_rect_t *rect) = nullptr;
    void (*get_view_rect)(cef_render_handler_t *self, cef_browser_t *browser, cef_rect_t *rect) = nullptr;
    int (*get_screen_point)(cef_render_handler_t *self,
                            cef_browser_t *browser,
                            int view_x,
                            int view_y,
                            int *screen_x,
                            int *screen_y) = nullptr;
    int (*get_screen_info)(cef_render_handler_t *self, cef_browser_t *browser, void *screen_info) = nullptr;
    void (*on_popup_show)(cef_render_handler_t *self, cef_browser_t *browser, int show) = nullptr;
    void (*on_popup_size)(cef_render_handler_t *self, cef_browser_t *browser, const cef_rect_t *rect) = nullptr;
    void (*on_paint)(cef_render_handler_t *self,
                     cef_browser_t *browser,
                     cef_paint_element_type_t type,
                     size_t dirty_rects_count,
                     const cef_rect_t *dirty_rects,
                     const void *buffer,
                     int width,
                     int height) = nullptr;
    void *remaining_callbacks[9] = {};
};

struct cef_client_t {
    cef_base_ref_counted_t base;
    cef_audio_handler_t *(*get_audio_handler)(cef_client_t *self) = nullptr;
    cef_command_handler_t *(*get_command_handler)(cef_client_t *self) = nullptr;
    cef_context_menu_handler_t *(*get_context_menu_handler)(cef_client_t *self) = nullptr;
    cef_dialog_handler_t *(*get_dialog_handler)(cef_client_t *self) = nullptr;
    cef_display_handler_t *(*get_display_handler)(cef_client_t *self) = nullptr;
    cef_download_handler_t *(*get_download_handler)(cef_client_t *self) = nullptr;
    cef_drag_handler_t *(*get_drag_handler)(cef_client_t *self) = nullptr;
    cef_find_handler_t *(*get_find_handler)(cef_client_t *self) = nullptr;
    cef_focus_handler_t *(*get_focus_handler)(cef_client_t *self) = nullptr;
    cef_frame_handler_t *(*get_frame_handler)(cef_client_t *self) = nullptr;
    cef_permission_handler_t *(*get_permission_handler)(cef_client_t *self) = nullptr;
    cef_jsdialog_handler_t *(*get_jsdialog_handler)(cef_client_t *self) = nullptr;
    cef_keyboard_handler_t *(*get_keyboard_handler)(cef_client_t *self) = nullptr;
    cef_life_span_handler_t *(*get_life_span_handler)(cef_client_t *self) = nullptr;
    cef_load_handler_t *(*get_load_handler)(cef_client_t *self) = nullptr;
    cef_print_handler_t *(*get_print_handler)(cef_client_t *self) = nullptr;
    cef_render_handler_t *(*get_render_handler)(cef_client_t *self) = nullptr;
    cef_request_handler_t *(*get_request_handler)(cef_client_t *self) = nullptr;
    int (*on_process_message_received)(cef_client_t *self,
                                       cef_browser_t *browser,
                                       cef_frame_t *frame,
                                       cef_process_id_t source_process,
                                       cef_process_message_t *message) = nullptr;
};

struct cef_api_t {
    int (*cef_initialize)(const cef_main_args_t *args,
                          const cef_settings_t *settings,
                          void *application,
                          void *windows_sandbox_info) = nullptr;
    void (*cef_shutdown)() = nullptr;
    void (*cef_do_message_loop_work)() = nullptr;
    cef_browser_t *(*cef_browser_host_create_browser_sync)(const cef_window_info_t *window_info,
                                                           cef_client_t *client,
                                                           const cef_string_t *url,
                                                           const cef_browser_settings_t *settings,
                                                           cef_dictionary_value_t *extra_info,
                                                           cef_request_context_t *request_context) = nullptr;
    int (*cef_string_utf8_to_utf16)(const char *src, size_t src_len, cef_string_t *output) = nullptr;
    void (*cef_string_utf16_clear)(cef_string_t *str) = nullptr;
};

static_assert(sizeof(cef_string_t) == 24);
static_assert(sizeof(cef_mouse_event_t) == 12);
static_assert(sizeof(cef_key_event_t) == 40);
static_assert(sizeof(cef_window_info_t) == 88);
static_assert(sizeof(cef_settings_t) == 448);
static_assert(sizeof(cef_browser_settings_t) == 264);
static_assert(sizeof(cef_base_ref_counted_t) == 40);
static_assert(offsetof(cef_browser_t, get_host) == 48);
static_assert(offsetof(cef_browser_host_t, was_resized) == 304);
static_assert(offsetof(cef_browser_host_t, send_key_event) == 336);
static_assert(offsetof(cef_browser_host_t, send_focus_event) == 368);
static_assert(sizeof(cef_render_handler_t) == 176);
static_assert(sizeof(cef_client_t) == 192);

} // namespace fennara::linux_cef_runtime::capi

#endif
