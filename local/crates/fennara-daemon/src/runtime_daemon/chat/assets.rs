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
        "model-picker.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/model-picker.js"
        )),
        "transcript-renderer.js" => js(include_bytes!(
            "../../../../../../godot/addons/fennara/dist/transcript-renderer.js"
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
