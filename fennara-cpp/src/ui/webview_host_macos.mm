#ifdef __APPLE__

#include "webview_backend.hpp"

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <objc/runtime.h>

#include <dispatch/dispatch.h>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <string>

static const NSUInteger kMaxPasteImageBytes = 8 * 1024 * 1024;

@interface FennaraMacWebViewDelegate : NSObject <WKUIDelegate, WKScriptMessageHandler>
@property(nonatomic, assign) WKWebView *webView;
@end

@implementation FennaraMacWebViewDelegate

- (void)webView:(WKWebView *)webView
    runOpenPanelWithParameters:(WKOpenPanelParameters *)parameters
              initiatedByFrame:(WKFrameInfo *)frame
             completionHandler:(void (^)(NSArray<NSURL *> *URLs))completionHandler {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = parameters.allowsMultipleSelection;
    panel.allowedFileTypes = @[@"png", @"jpg", @"jpeg", @"webp", @"gif"];

    void (^finish)(NSModalResponse) = ^(NSModalResponse result) {
        if (result == NSModalResponseOK) {
            completionHandler(panel.URLs);
            return;
        }
        completionHandler(nil);
    };

    NSWindow *window = webView.window;
    if (window != nil) {
        [panel beginSheetModalForWindow:window completionHandler:finish];
    } else {
        finish([panel runModal]);
    }
}

- (void)userContentController:(WKUserContentController *)userContentController
      didReceiveScriptMessage:(WKScriptMessage *)message {
    if (![message.name isEqualToString:@"fennaraPasteboard"]) {
        return;
    }
    [self sendPasteboardImageToWebView];
}

- (void)sendPasteboardImageToWebView {
    WKWebView *view = self.webView;
    if (view == nil) {
        return;
    }

    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    NSData *imageData = [pasteboard dataForType:NSPasteboardTypePNG];
    NSString *mimeType = @"image/png";
    NSString *name = @"pasted-image.png";
    if (![self imageDataIsSmallEnough:imageData]) {
        [self sendPasteboardError:@"Image is too large. Try a smaller screenshot."];
        return;
    }

    if (imageData == nil) {
        NSData *tiffData = [pasteboard dataForType:NSPasteboardTypeTIFF];
        if (tiffData != nil) {
            if (![self imageDataIsSmallEnough:tiffData]) {
                [self sendPasteboardError:@"Image is too large. Try a smaller screenshot."];
                return;
            }
            NSImageRep *rep = [NSBitmapImageRep imageRepWithData:tiffData];
            if ([rep isKindOfClass:[NSBitmapImageRep class]]) {
                imageData = [(NSBitmapImageRep *)rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                if (![self imageDataIsSmallEnough:imageData]) {
                    [self sendPasteboardError:@"Image is too large. Try a smaller screenshot."];
                    return;
                }
            }
        }
    }

    if (imageData == nil) {
        NSArray *urls = [pasteboard readObjectsForClasses:@[[NSURL class]]
                                                  options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
        for (NSURL *url in urls) {
            NSString *extension = url.pathExtension.lowercaseString;
            if ([extension isEqualToString:@"png"]) {
                mimeType = @"image/png";
            } else if ([extension isEqualToString:@"jpg"] || [extension isEqualToString:@"jpeg"]) {
                mimeType = @"image/jpeg";
            } else if ([extension isEqualToString:@"webp"]) {
                mimeType = @"image/webp";
            } else if ([extension isEqualToString:@"gif"]) {
                mimeType = @"image/gif";
            } else {
                continue;
            }

            NSNumber *fileSize = nil;
            if ([url getResourceValue:&fileSize forKey:NSURLFileSizeKey error:nil] &&
                fileSize != nil &&
                [fileSize unsignedLongLongValue] > kMaxPasteImageBytes) {
                [self sendPasteboardError:@"Image is too large. Try a smaller screenshot."];
                return;
            }
            imageData = [NSData dataWithContentsOfURL:url];
            if (![self imageDataIsSmallEnough:imageData]) {
                [self sendPasteboardError:@"Image is too large. Try a smaller screenshot."];
                return;
            }
            name = url.lastPathComponent.length > 0 ? url.lastPathComponent : name;
            if (imageData != nil) {
                break;
            }
        }
    }

    if (imageData == nil || imageData.length == 0) {
        return;
    }

    NSDictionary *payload = @{
        @"base64": [imageData base64EncodedStringWithOptions:0],
        @"mime_type": mimeType,
        @"name": name,
        @"size": @(imageData.length),
    };
    NSData *jsonData = [NSJSONSerialization dataWithJSONObject:payload options:0 error:nil];
    if (jsonData == nil) {
        return;
    }
    NSString *json = [[NSString alloc] initWithData:jsonData encoding:NSUTF8StringEncoding];
    if (json == nil) {
        return;
    }

    NSString *script = [NSString stringWithFormat:
        @"window.FennaraNativePasteboard&&window.FennaraNativePasteboard.receiveImage(%@)", json];
    [view evaluateJavaScript:script completionHandler:nil];
    [json release];
}

- (BOOL)imageDataIsSmallEnough:(NSData *)imageData {
    return imageData == nil || imageData.length <= kMaxPasteImageBytes;
}

- (void)sendPasteboardError:(NSString *)message {
    WKWebView *view = self.webView;
    if (view == nil || message.length == 0) {
        return;
    }
    NSDictionary *payload = @{@"message": message};
    NSData *jsonData = [NSJSONSerialization dataWithJSONObject:payload options:0 error:nil];
    if (jsonData == nil) {
        return;
    }
    NSString *json = [[NSString alloc] initWithData:jsonData encoding:NSUTF8StringEncoding];
    if (json == nil) {
        return;
    }
    NSString *script = [NSString stringWithFormat:
        @"window.FennaraNativePasteboard&&window.FennaraNativePasteboard.receiveError(%@)", json];
    [view evaluateJavaScript:script completionHandler:nil];
    [json release];
}

@end

namespace fennara {
namespace mac_webview {

namespace {

char kMacWebViewDelegateKey;

godot::String ptr_string(const void *ptr) {
    return godot::String::num_uint64(reinterpret_cast<uint64_t>(ptr), 16);
}

godot::String rect_string(NSRect rect) {
    return "x=" + godot::String::num(rect.origin.x) +
           " y=" + godot::String::num(rect.origin.y) +
           " w=" + godot::String::num(rect.size.width) +
           " h=" + godot::String::num(rect.size.height);
}

void output_log(const godot::String &message) {
    godot::UtilityFunctions::print(godot::String("[Fennara] ") + message);
}

bool debug_logging_enabled() {
    const char *generic = std::getenv("FENNARA_WEBVIEW_DEBUG");
    const char *mac = std::getenv("FENNARA_MAC_WEBVIEW_DEBUG");
    return (generic != nullptr && std::string(generic) == "1") ||
           (mac != nullptr && std::string(mac) == "1");
}

void debug_log(const godot::String &message) {
    if (debug_logging_enabled()) {
        output_log(message);
    }
}

godot::String bool_string(bool value) {
    return value ? "true" : "false";
}

godot::String window_debug_string(NSWindow *window) {
    if (window == nil) {
        return "nil";
    }
    NSView *content = [window contentView];
    return "ptr=" + ptr_string(window) +
           " key=" + bool_string([window isKeyWindow]) +
           " main=" + bool_string([window isMainWindow]) +
           " visible=" + bool_string([window isVisible]) +
           " miniaturized=" + bool_string([window isMiniaturized]) +
           " frame=(" + rect_string([window frame]) + ")" +
           " content=" + ptr_string(content) +
           " content_frame=(" + rect_string(content != nil ? [content frame] : NSZeroRect) + ")" +
           " content_bounds=(" + rect_string(content != nil ? [content bounds] : NSZeroRect) + ")" +
           " scale=" + godot::String::num([window backingScaleFactor]);
}

godot::String view_debug_string(WKWebView *view) {
    if (view == nil) {
        return "nil";
    }
    return "ptr=" + ptr_string(view) +
           " hidden=" + bool_string([view isHidden]) +
           " frame=(" + rect_string([view frame]) + ")" +
           " bounds=(" + rect_string([view bounds]) + ")" +
           " superview=" + ptr_string([view superview]) +
           " window=" + ptr_string([view window]);
}

struct Geometry {
    bool visible = false;
    double global_x = 0.0;
    double global_y = 0.0;
    double width = 0.0;
    double height = 0.0;
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
        debug_log("macOS webview geometry hidden owner_null=" +
                  godot::String(owner == nullptr ? "true" : "false"));
        return geometry;
    }

    godot::Vector2 size = owner->get_size();
    if (size.x <= 0 || size.y <= 0) {
        debug_log("macOS webview geometry empty size=" +
                  godot::String::num(size.x) + "x" + godot::String::num(size.y));
        return geometry;
    }

    godot::Vector2 screen_position = owner->get_screen_position();
    godot::Vector2 global_position = owner->get_global_position();
    godot::Vector2 position = owner->get_position();
    geometry.visible = true;
    geometry.global_x = global_position.x;
    geometry.global_y = global_position.y;
    geometry.width = size.x;
    geometry.height = size.y;
    debug_log("macOS webview geometry visible screen=" +
              godot::String::num(screen_position.x) + "," +
              godot::String::num(screen_position.y) +
              " global=" + godot::String::num(global_position.x) + "," +
              godot::String::num(global_position.y) +
              " local=" + godot::String::num(position.x) + "," +
              godot::String::num(position.y) +
              " size=" + godot::String::num(size.x) + "x" + godot::String::num(size.y) +
              " visible_tree=" + bool_string(owner->is_visible_in_tree()) +
              " owner_window_id=" + godot::String::num_int64(owner_window_id(owner)));
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
    NSRect bounds = content != nil ? [content bounds] : NSZeroRect;
    CGFloat backing_scale = [window backingScaleFactor];
    if (backing_scale <= 0.0) {
        backing_scale = 1.0;
    }

    CGFloat x = geometry.global_x / backing_scale;
    CGFloat width = geometry.width / backing_scale;
    CGFloat height = geometry.height / backing_scale;
    CGFloat y = NSMaxY(bounds) - ((geometry.global_y + geometry.height) / backing_scale);
    NSRect local_rect = NSMakeRect(x, y, width, height);

    if (!NSIntersectsRect(local_rect, bounds)) {
        local_rect = bounds;
    }

    debug_log("macOS webview frame local window=" + ptr_string(window) +
              " content=" + ptr_string(content) +
              " backing_scale=" + godot::String::num(backing_scale) +
              " global_input=(" + godot::String::num(geometry.global_x) + "," +
              godot::String::num(geometry.global_y) + " " +
              godot::String::num(geometry.width) + "x" +
              godot::String::num(geometry.height) + ")" +
              " frame=(" + rect_string(local_rect) + ")" +
              " content_bounds=(" + rect_string(bounds) + ")" +
              " window_frame=(" + rect_string([window frame]) + ")");
    return local_rect;
}

NSWindow *native_window_for_owner(godot::Control *owner) {
    godot::DisplayServer *display = godot::DisplayServer::get_singleton();
    if (display == nullptr || owner == nullptr) {
        return nil;
    }

    int64_t native_window = display->window_get_native_handle(
        godot::DisplayServer::WINDOW_HANDLE,
        owner_window_id(owner));
    if (native_window == 0) {
        output_log("macOS webview native window missing owner_window_id=" +
                   godot::String::num_int64(owner_window_id(owner)));
        return nil;
    }
    debug_log("macOS webview native window owner_window_id=" +
              godot::String::num_int64(owner_window_id(owner)) +
              " ptr=" + ptr_string(reinterpret_cast<void *>(native_window)));
    return reinterpret_cast<NSWindow *>(native_window);
}

} // namespace

bool start(void **webview, void **parent_window, godot::Control *owner, const godot::String &url) {
    if (webview == nullptr || parent_window == nullptr || owner == nullptr) {
        return false;
    }

    NSWindow *target_window = native_window_for_owner(owner);
    if (target_window == nil) {
        output_log("macOS webview start failed: target window nil");
        return false;
    }

    std::string url_utf8 = url.utf8().get_data();
    __block bool ok = false;
    run_on_main_sync(^{
        NSView *content = [target_window contentView];
        if (content == nil) {
            output_log("macOS webview start failed: content view nil target=" +
                       ptr_string(target_window));
            return;
        }

        WKUserContentController *user_content = [[WKUserContentController alloc] init];
        WKWebViewConfiguration *configuration = [[WKWebViewConfiguration alloc] init];
        configuration.userContentController = user_content;
        WKWebView *view = [[WKWebView alloc] initWithFrame:NSZeroRect configuration:configuration];
        [configuration release];
        [user_content release];
        if (view == nil) {
            output_log(
                "macOS webview start failed: WKWebView nil. WebKit.framework is normally "
                "included with macOS; restart Godot or check the macOS installation. "
                "Fennara MCP tools still work without the built-in chat dock.");
            return;
        }

        FennaraMacWebViewDelegate *delegate = [[FennaraMacWebViewDelegate alloc] init];
        delegate.webView = view;
        [view setUIDelegate:delegate];
        [view.configuration.userContentController addScriptMessageHandler:delegate
                                                                     name:@"fennaraPasteboard"];
        objc_setAssociatedObject(view, &kMacWebViewDelegateKey, delegate,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [delegate release];

        [view setHidden:YES];
        [content addSubview:view];

        NSString *url_string = [NSString stringWithUTF8String:url_utf8.c_str()];
        NSURL *ns_url = [NSURL URLWithString:url_string];
        if (ns_url != nil) {
            [view loadRequest:[NSURLRequest requestWithURL:ns_url]];
        }

        *webview = view;
        *parent_window = target_window;
        debug_log("macOS webview start ok view={" + view_debug_string(view) +
                  "} window={" + window_debug_string(target_window) + "}");
        ok = true;
    });
    return ok;
}

void resize_to(void *webview, void **parent_window, godot::Control *owner) {
    if (webview == nullptr || parent_window == nullptr) {
        return;
    }

    Geometry geometry = compute_geometry(owner);
    NSWindow *target_window = native_window_for_owner(owner);
    run_on_main_sync(^{
        WKWebView *view = reinterpret_cast<WKWebView *>(webview);
        NSWindow *current_window = reinterpret_cast<NSWindow *>(*parent_window);
        debug_log("macOS webview resize begin view={" + view_debug_string(view) +
                  "} current_window={" + window_debug_string(current_window) +
                  "} target_window={" + window_debug_string(target_window) +
                  "} visible=" + godot::String(geometry.visible ? "true" : "false"));
        if (!geometry.visible) {
            debug_log("macOS webview resize hiding: geometry not visible");
            [view setHidden:YES];
            return;
        }
        if (target_window == nil) {
            output_log("macOS webview resize hiding: target window nil");
            [view setHidden:YES];
            return;
        }

        if (target_window != current_window) {
            NSView *content = [target_window contentView];
            if (content == nil) {
                output_log("macOS webview resize hiding: reparent content nil target=" +
                           ptr_string(target_window));
                [view setHidden:YES];
                return;
            }
            [view removeFromSuperview];
            [content addSubview:view];
            *parent_window = target_window;
            current_window = target_window;
            debug_log("macOS webview reparented view=" + ptr_string(view) +
                      " new_window=" + ptr_string(target_window) +
                      " content=" + ptr_string(content));
        } else {
            debug_log("macOS webview reparent skipped: target equals current window");
        }

        NSRect final_frame = frame_for_geometry(current_window, geometry);
        debug_log("macOS webview frame before set view={" + view_debug_string(view) + "}");
        [view setFrame:final_frame];
        debug_log("macOS webview final frame=(" + rect_string(final_frame) +
                  ") view_after_set={" + view_debug_string(view) + "} hidden=false");
        [view setHidden:NO];
        debug_log("macOS webview visible after setHidden view={" + view_debug_string(view) + "}");
    });
}

void set_visible(void *webview, bool visible) {
    if (webview == nullptr) {
        return;
    }

    run_on_main_sync(^{
        WKWebView *view = reinterpret_cast<WKWebView *>(webview);
        debug_log("macOS webview set_visible view=" + ptr_string(view) +
                  " visible=" + godot::String(visible ? "true" : "false"));
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
        debug_log("macOS webview stop view=" + ptr_string(view));
        [view.configuration.userContentController removeScriptMessageHandlerForName:@"fennaraPasteboard"];
        [view setUIDelegate:nil];
        objc_setAssociatedObject(view, &kMacWebViewDelegateKey, nil,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [view removeFromSuperview];
        [view release];
    });

    *webview = nullptr;
    if (parent_window != nullptr) {
        *parent_window = nullptr;
    }
}

} // namespace mac_webview

namespace webview_backend {

class MacWebviewBackend : public NativeWebviewBackend {
public:
    ~MacWebviewBackend() override {
        stop();
    }

    bool start(godot::Control *owner, const godot::String &url) override {
        if (started) {
            return true;
        }

        if (mac_webview::start(&webview, &parent_window, owner, url)) {
            current_url = url;
            started = true;
            resize_to(owner);
            return true;
        }
        output_error("Web chat native macOS webview could not start");
        return false;
    }

    void resize_to(godot::Control *owner) override {
        if (!started || owner == nullptr) {
            return;
        }
        mac_webview::resize_to(webview, &parent_window, owner);
    }

    void set_visible(bool visible) override {
        if (!started) {
            return;
        }
        mac_webview::set_visible(webview, visible);
    }

    void stop() override {
        if (!started) {
            return;
        }
        mac_webview::stop(&webview, &parent_window);
        current_url = "";
        started = false;
    }

    bool is_started() const override {
        return started;
    }

private:
    void *webview = nullptr;
    void *parent_window = nullptr;
    godot::String current_url;
    bool started = false;
};

std::unique_ptr<NativeWebviewBackend> create_backend() {
    return std::make_unique<MacWebviewBackend>();
}

} // namespace webview_backend
} // namespace fennara

#endif
