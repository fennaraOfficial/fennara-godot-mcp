use axum::{
    extract::Path,
    http::{StatusCode, Uri, header},
    response::{IntoResponse, Response},
};

const CACHE_CONTROL: &str = "no-store";

struct Asset {
    content_type: &'static str,
    body: &'static [u8],
}

pub(crate) async fn chat_index() -> Response {
    asset_response("index.html")
}

pub(crate) async fn chat_index_redirect(uri: Uri) -> Response {
    let target = match uri.query() {
        Some(query) => format!("/chat/?{query}"),
        None => "/chat/".to_string(),
    };
    (StatusCode::TEMPORARY_REDIRECT, [(header::LOCATION, target)]).into_response()
}

pub(crate) async fn chat_asset(Path(path): Path<String>) -> Response {
    let path = path.trim_start_matches('/');
    if path.is_empty() || path.contains("..") || path.contains('\\') {
        return StatusCode::NOT_FOUND.into_response();
    }
    asset_response(path)
}

fn asset_response(path: &str) -> Response {
    let Some(asset) = asset(path) else {
        return StatusCode::NOT_FOUND.into_response();
    };
    (
        [
            (header::CONTENT_TYPE, asset.content_type),
            (header::CACHE_CONTROL, CACHE_CONTROL),
        ],
        asset.body,
    )
        .into_response()
}

fn asset(path: &str) -> Option<Asset> {
    let asset = match path {
        "index.html" => Asset {
            content_type: "text/html; charset=utf-8",
            body: include_bytes!("../../../../../../godot/addons/fennara/dist/index.html"),
        },
        "app.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/app.js"
        )),
        "attachment-manager.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/attachment-manager.js"
        )),
        "chat-navigation.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/chat-navigation.js"
        )),
        "command-palette.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/command-palette.js"
        )),
        "composer-actions.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/composer-actions.js"
        )),
        "daemon-client.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/daemon-client.js"
        )),
        "effort-controls.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/effort-controls.js"
        )),
        "model-picker.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/model-picker.js"
        )),
        "overlay-manager.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/overlay-manager.js"
        )),
        "project-file-links.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/project-file-links.js"
        )),
        "project-status.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/project-status.js"
        )),
        "provider-popovers.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/provider-popovers.js"
        )),
        "settings-panel.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/settings-panel.js"
        )),
        "shell-bindings.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/shell-bindings.js"
        )),
        "stored-transcript.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/stored-transcript.js"
        )),
        "transcript-renderer.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/transcript-renderer.js"
        )),
        "usage-summary.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/usage-summary.js"
        )),
        "styles.css" => css(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/styles.css"
        )),
        "styles/base.css" => css(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/styles/base.css"
        )),
        "styles/chat.css" => css(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/styles/chat.css"
        )),
        "styles/controls.css" => css(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/styles/controls.css"
        )),
        "styles/drawer.css" => css(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/styles/drawer.css"
        )),
        "styles/icons.css" => css(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/styles/icons.css"
        )),
        "styles/model-picker.css" => css(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/styles/model-picker.css"
        )),
        "styles/responsive.css" => css(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/styles/responsive.css"
        )),
        "styles/settings.css" => css(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/styles/settings.css"
        )),
        "vendor/markdown-it.min.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/vendor/markdown-it.min.js"
        )),
        "vendor/markdown-it-task-lists.min.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/vendor/markdown-it-task-lists.min.js"
        )),
        "vendor/purify.min.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/vendor/purify.min.js"
        )),
        _ => return None,
    };
    Some(asset)
}

fn css(body: &'static [u8]) -> Asset {
    Asset {
        content_type: "text/css; charset=utf-8",
        body,
    }
}

fn js(body: &'static [u8]) -> Asset {
    Asset {
        content_type: "text/javascript; charset=utf-8",
        body,
    }
}

#[cfg(test)]
mod tests {
    use super::asset;

    #[test]
    fn browser_chat_assets_referenced_by_html_are_embedded() {
        let index = include_str!("../../../../../../godot/addons/fennara/dist/index.html");
        for line in index.lines() {
            assert_referenced_asset(line, "<script src=\"./");
            assert_referenced_asset(line, "<link rel=\"stylesheet\" href=\"./");
        }
    }

    #[test]
    fn browser_chat_stylesheet_imports_are_embedded() {
        let styles = include_str!("../../../../../../godot/addons/fennara/dist/styles.css");
        for line in styles.lines() {
            assert_referenced_asset(line, "@import \"./");
        }
    }

    fn assert_referenced_asset(line: &str, prefix: &str) {
        let Some(start) = line.find(prefix) else {
            return;
        };
        let path_start = start + prefix.len();
        let Some(path_end) = line[path_start..].find('"') else {
            return;
        };
        let path = &line[path_start..path_start + path_end];
        assert!(asset(path).is_some(), "missing embedded chat asset: {path}");
    }
}
