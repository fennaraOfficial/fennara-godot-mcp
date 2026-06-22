#pragma once

#ifdef __linux__

#include <include/capi/cef_app_capi.h>
#include <include/capi/cef_browser_capi.h>
#include <include/capi/cef_client_capi.h>
#include <include/capi/cef_render_handler_capi.h>
#include <include/internal/cef_string.h>

namespace fennara::linux_cef_runtime::capi {

struct cef_api_t {
    decltype(&::cef_execute_process) cef_execute_process = nullptr;
    decltype(&::cef_initialize) cef_initialize = nullptr;
    decltype(&::cef_shutdown) cef_shutdown = nullptr;
    decltype(&::cef_do_message_loop_work) cef_do_message_loop_work = nullptr;
    decltype(&::cef_browser_host_create_browser_sync) cef_browser_host_create_browser_sync = nullptr;
    decltype(&::cef_string_utf8_to_utf16) cef_string_utf8_to_utf16 = nullptr;
    decltype(&::cef_string_utf16_clear) cef_string_utf16_clear = nullptr;
};

} // namespace fennara::linux_cef_runtime::capi

#endif
