#define WEBVIEW_GTK

#include <gtk/gtk.h>
#include <webview/webview.h>

#include <chrono>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace {

struct Options {
    std::string url;
    std::string state_path;
    std::string log_path;
    bool debug = false;
};

struct Geometry {
    bool visible = true;
    int x = 100;
    int y = 100;
    int width = 420;
    int height = 640;
};

struct App {
    webview_t webview = nullptr;
    GtkWindow *window = nullptr;
    Options options;
    Geometry last_geometry;
    std::string last_raw_state;
    FILE *log = nullptr;
};

void log_line(App *app, const char *fmt, ...) {
    if (app == nullptr || app->log == nullptr) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    std::vfprintf(app->log, fmt, args);
    va_end(args);
    std::fprintf(app->log, "\n");
    std::fflush(app->log);
}

bool parse_bool(const std::string &value, bool fallback) {
    if (value == "1" || value == "true") {
        return true;
    }
    if (value == "0" || value == "false") {
        return false;
    }
    return fallback;
}

Geometry parse_state(const std::string &raw, Geometry fallback) {
    Geometry geometry = fallback;
    std::istringstream stream(raw);
    std::string line;

    while (std::getline(stream, line)) {
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key == "visible") {
            geometry.visible = parse_bool(value, geometry.visible);
        } else if (key == "x") {
            geometry.x = std::atoi(value.c_str());
        } else if (key == "y") {
            geometry.y = std::atoi(value.c_str());
        } else if (key == "width") {
            geometry.width = std::max(1, std::atoi(value.c_str()));
        } else if (key == "height") {
            geometry.height = std::max(1, std::atoi(value.c_str()));
        }
    }

    return geometry;
}

std::string read_file(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        return "";
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void apply_geometry(App *app, const Geometry &geometry, const char *reason) {
    if (app == nullptr || app->window == nullptr) {
        return;
    }

    gtk_window_resize(app->window, geometry.width, geometry.height);
    gtk_window_move(app->window, geometry.x, geometry.y);
    if (geometry.visible) {
        gtk_widget_show(GTK_WIDGET(app->window));
        gtk_window_present(app->window);
    } else {
        gtk_widget_hide(GTK_WIDGET(app->window));
    }

    app->last_geometry = geometry;
    log_line(app,
             "geometry %s visible=%d x=%d y=%d w=%d h=%d display=%s session=%s",
             reason,
             geometry.visible ? 1 : 0,
             geometry.x,
             geometry.y,
             geometry.width,
             geometry.height,
             std::getenv("WAYLAND_DISPLAY") != nullptr ? "wayland" : "x11-or-unknown",
             std::getenv("XDG_SESSION_TYPE") != nullptr ? std::getenv("XDG_SESSION_TYPE") : "");
}

gboolean poll_state(gpointer data) {
    App *app = static_cast<App *>(data);
    const std::string raw = read_file(app->options.state_path);
    if (!raw.empty() && raw != app->last_raw_state) {
        app->last_raw_state = raw;
        apply_geometry(app, parse_state(raw, app->last_geometry), "state");
    }
    return TRUE;
}

Options parse_args(int argc, char **argv) {
    Options options;
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                return "";
            }
            return argv[++i];
        };

        if (arg == "--url") {
            options.url = next();
        } else if (arg == "--state") {
            options.state_path = next();
        } else if (arg == "--log") {
            options.log_path = next();
        } else if (arg == "--debug") {
            options.debug = true;
        }
    }
    return options;
}

} // namespace

int main(int argc, char **argv) {
    App app;
    app.options = parse_args(argc, argv);
    app.log = std::fopen(app.options.log_path.c_str(), "a");

    log_line(&app, "fennara-chat-webview start pid=%d", static_cast<int>(getpid()));
    log_line(&app, "url=%s", app.options.url.c_str());
    log_line(&app, "state=%s", app.options.state_path.c_str());

    if (app.options.url.empty() || app.options.state_path.empty()) {
        log_line(&app, "missing required --url or --state");
        return 2;
    }

    const std::string initial_state = read_file(app.options.state_path);
    if (!initial_state.empty()) {
        app.last_raw_state = initial_state;
        app.last_geometry = parse_state(initial_state, app.last_geometry);
    }

    app.webview = webview_create(app.options.debug ? 1 : 0, nullptr);
    if (app.webview == nullptr) {
        log_line(&app, "webview_create returned null");
        return 3;
    }

    app.window = static_cast<GtkWindow *>(webview_get_native_handle(
        app.webview,
        WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW));
    if (app.window == nullptr) {
        log_line(&app, "native GtkWindow handle is null");
        webview_destroy(app.webview);
        return 4;
    }

    webview_set_title(app.webview, "Fennara");
    webview_set_size(app.webview, app.last_geometry.width, app.last_geometry.height, WEBVIEW_HINT_NONE);
    webview_navigate(app.webview, app.options.url.c_str());
    apply_geometry(&app, app.last_geometry, "initial");
    g_timeout_add(100, poll_state, &app);

    log_line(&app, "entering webview_run");
    const webview_error_t result = webview_run(app.webview);
    log_line(&app, "webview_run exited result=%d", static_cast<int>(result));
    webview_destroy(app.webview);
    log_line(&app, "fennara-chat-webview stop");

    if (app.log != nullptr) {
        std::fclose(app.log);
    }
    return 0;
}
