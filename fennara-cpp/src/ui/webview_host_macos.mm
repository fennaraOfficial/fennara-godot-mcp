#ifdef __APPLE__

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/string.hpp>

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <dispatch/dispatch.h>
#include <string>

namespace fennara {
namespace mac_webview {

namespace {

struct Geometry {
    bool visible = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

int owner_window_id(godot::Control *owner) {
    if (owner == nullptr) {
        return 0;
    }
    godot::Window *window = owner->get_window();
    if (window == nullptr) {
        return 0;
    }
    return window->get_window_id();
}

Geometry compute_geometry(godot::Control *owner) {
    Geometry geometry;
    if (owner == nullptr || !owner->is_visible_in_tree()) {
        return geometry;
    }

    godot::Vector2 size = owner->get_size();
    if (size.x <= 0 || size.y <= 0) {
        return geometry;
    }

    godot::Vector2 screen_position = owner->get_screen_position();
    geometry.visible = true;
    geometry.x = static_cast<int>(screen_position.x);
    geometry.y = static_cast<int>(screen_position.y);
    geometry.width = static_cast<int>(size.x);
    geometry.height = static_cast<int>(size.y);
    return geometry;
}

void run_on_main_sync(dispatch_block_t block) {
    if ([NSThread isMainThread]) {
        block();
        return;
    }
    dispatch_sync(dispatch_get_main_queue(), block);
}

NSRect frame_for_geometry(NSWindow *window, Geometry geometry) {
    NSView *content = [window contentView];
    NSScreen *screen = [window screen] ?: [NSScreen mainScreen];
    NSRect screen_frame = [screen frame];
    CGFloat y_from_bottom = NSMaxY(screen_frame) - geometry.y - geometry.height;
    NSRect screen_rect = NSMakeRect(
        geometry.x,
        y_from_bottom,
        geometry.width,
        geometry.height);
    NSRect window_rect = [window convertRectFromScreen:screen_rect];
    return [content convertRect:window_rect fromView:nil];
}

} // namespace

bool start(void **webview, void **parent_window, godot::Control *owner, const godot::String &url) {
    if (webview == nullptr || parent_window == nullptr || owner == nullptr) {
        return false;
    }

    godot::DisplayServer *display = godot::DisplayServer::get_singleton();
    if (display == nullptr) {
        return false;
    }

    int64_t native_window = display->window_get_native_handle(
        godot::DisplayServer::WINDOW_HANDLE,
        owner_window_id(owner));
    if (native_window == 0) {
        return false;
    }

    std::string url_utf8 = url.utf8().get_data();
    __block bool ok = false;
    run_on_main_sync(^{
        NSWindow *window = reinterpret_cast<NSWindow *>(native_window);
        NSView *content = [window contentView];
        if (content == nil) {
            return;
        }

        WKWebViewConfiguration *configuration = [[WKWebViewConfiguration alloc] init];
        WKWebView *view = [[WKWebView alloc] initWithFrame:NSZeroRect configuration:configuration];
        [configuration release];
        if (view == nil) {
            return;
        }

        [view setHidden:YES];
        [content addSubview:view];

        NSString *url_string = [NSString stringWithUTF8String:url_utf8.c_str()];
        NSURL *ns_url = [NSURL URLWithString:url_string];
        if (ns_url != nil) {
            [view loadRequest:[NSURLRequest requestWithURL:ns_url]];
        }

        *webview = view;
        *parent_window = window;
        ok = true;
    });
    return ok;
}

void resize_to(void *webview, void *parent_window, godot::Control *owner) {
    if (webview == nullptr || parent_window == nullptr) {
        return;
    }

    Geometry geometry = compute_geometry(owner);
    run_on_main_sync(^{
        WKWebView *view = reinterpret_cast<WKWebView *>(webview);
        NSWindow *window = reinterpret_cast<NSWindow *>(parent_window);
        if (!geometry.visible) {
            [view setHidden:YES];
            return;
        }

        [view setFrame:frame_for_geometry(window, geometry)];
        [view setHidden:NO];
    });
}

void set_visible(void *webview, bool visible) {
    if (webview == nullptr) {
        return;
    }

    run_on_main_sync(^{
        WKWebView *view = reinterpret_cast<WKWebView *>(webview);
        [view setHidden:visible ? NO : YES];
    });
}

void stop(void **webview, void **parent_window) {
    if (webview == nullptr || *webview == nullptr) {
        if (parent_window != nullptr) {
            *parent_window = nullptr;
        }
        return;
    }

    void *view_ptr = *webview;
    run_on_main_sync(^{
        WKWebView *view = reinterpret_cast<WKWebView *>(view_ptr);
        [view removeFromSuperview];
        [view release];
    });

    *webview = nullptr;
    if (parent_window != nullptr) {
        *parent_window = nullptr;
    }
}

} // namespace mac_webview
} // namespace fennara

#endif
