use serde_json::{Value, json};

use crate::runtime_daemon::{godot_bridge, state::AppState};

const READ_FILE_SCHEMA: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../schemas/tools/read_file.json"
));
const FILE_OPS_SCHEMA: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../schemas/tools/file_ops.json"
));
const SCRIPT_DIAGNOSTICS_SCHEMA: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../schemas/tools/script_diagnostics.json"
));
const GET_CLASS_INFO_SCHEMA: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../schemas/tools/get_class_info.json"
));
const GET_SCENE_TREE_SCHEMA: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../schemas/tools/get_scene_tree.json"
));
const GET_NODE_PROPERTIES_SCHEMA: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../schemas/tools/get_node_properties.json"
));
const VALIDATE_SCENE_SCHEMA: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../schemas/tools/validate_scene.json"
));
const SCREENSHOT_SCENE_SCHEMA: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../schemas/tools/screenshot_scene.json"
));
const SCRAPE_EDITOR_SCHEMA: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../schemas/tools/scrape_editor.json"
));
const PROJECT_SETTINGS_SCHEMA: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../schemas/tools/project_settings.json"
));
const WRITE_OR_UPDATE_FILE_SCHEMA: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../schemas/tools/write_or_update_file.json"
));
const RUN_SCENE_EDIT_SCRIPT_SCHEMA: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../schemas/tools/run_scene_edit_script.json"
));
const SAVE_CUSTOM_RESOURCE_SCHEMA: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../schemas/tools/save_custom_resource.json"
));
const RUNTIME_SESSION_SCHEMA: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../schemas/tools/runtime_session.json"
));
const RUNTIME_SCRIPT_SCHEMA: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../schemas/tools/runtime_script.json"
));
const ALLOWED_TOOL_NAMES: &[&str] = &[
    "read_file",
    "file_ops",
    "script_diagnostics",
    "get_class_info",
    "get_scene_tree",
    "get_node_properties",
    "validate_scene",
    "screenshot_scene",
    "scrape_editor",
    "project_settings",
    "write_or_update_file",
    "run_scene_edit_script",
    "save_custom_resource",
    "runtime_session",
    "runtime_script",
];
const TOOL_SCHEMAS: &[&str] = &[
    READ_FILE_SCHEMA,
    FILE_OPS_SCHEMA,
    SCRIPT_DIAGNOSTICS_SCHEMA,
    GET_CLASS_INFO_SCHEMA,
    GET_SCENE_TREE_SCHEMA,
    GET_NODE_PROPERTIES_SCHEMA,
    VALIDATE_SCENE_SCHEMA,
    SCREENSHOT_SCENE_SCHEMA,
    SCRAPE_EDITOR_SCHEMA,
    PROJECT_SETTINGS_SCHEMA,
    WRITE_OR_UPDATE_FILE_SCHEMA,
    RUN_SCENE_EDIT_SCRIPT_SCHEMA,
    SAVE_CUSTOM_RESOURCE_SCHEMA,
    RUNTIME_SESSION_SCHEMA,
    RUNTIME_SCRIPT_SCHEMA,
];

#[derive(Clone, Debug)]
pub(crate) struct ExecutedTool {
    pub(crate) ok: bool,
    pub(crate) raw_result: Value,
    pub(crate) mcp_markdown: String,
    pub(crate) plugin_markdown: String,
    pub(crate) metadata: Value,
    pub(crate) target_keys: Vec<String>,
    pub(crate) model_followup_messages: Vec<Value>,
}

pub(crate) fn definitions() -> Vec<Value> {
    TOOL_SCHEMAS
        .iter()
        .map(|schema| openrouter_tool_from_schema(schema))
        .collect()
}

pub(crate) fn is_allowed_tool(name: &str) -> bool {
    ALLOWED_TOOL_NAMES.contains(&name)
}

pub(crate) async fn execute(state: &AppState, name: &str, arguments: &Value) -> ExecutedTool {
    if !is_allowed_tool(name) {
        return failed_tool(name, format!("Unsupported plugin chat tool: {name}"));
    }

    let response = godot_bridge::call_tool_value(state, name, arguments.clone()).await;
    let ok = response.get("ok").and_then(Value::as_bool).unwrap_or(false);
    let raw_result = response
        .get("raw_result")
        .cloned()
        .unwrap_or_else(|| response.clone());
    let formatted = response.get("formatted_result").cloned().unwrap_or_else(
        || json!({ "content": response.get("result").cloned().unwrap_or(Value::Null) }),
    );
    let mut metadata = formatted
        .get("metadata")
        .cloned()
        .unwrap_or_else(|| json!({ "tool_name": name }));
    if let Some(plugin_metadata) = response
        .get("plugin_metadata")
        .filter(|value| value.is_object())
    {
        metadata["plugin_metadata"] = plugin_metadata.clone();
    }
    let mcp_markdown = markdown_from_response(&response, &formatted, name);
    let plugin_markdown = plugin_markdown_for(name, &mcp_markdown, &metadata, &raw_result, ok);
    let target_keys = target_keys_from_metadata(&metadata);
    let model_followup_messages = model_followups_for(name, &raw_result);

    ExecutedTool {
        ok,
        raw_result,
        mcp_markdown,
        plugin_markdown,
        metadata,
        target_keys,
        model_followup_messages,
    }
}

fn failed_tool(name: &str, error: String) -> ExecutedTool {
    let markdown = format!("Tool: {name}\nStatus: failed\nError: {error}");
    ExecutedTool {
        ok: false,
        raw_result: json!({ "success": false, "error": error }),
        mcp_markdown: markdown.clone(),
        plugin_markdown: markdown,
        metadata: json!({
            "tool_name": name,
            "status": "failed",
            "format": "markdown",
        }),
        target_keys: Vec::new(),
        model_followup_messages: Vec::new(),
    }
}

fn openrouter_tool_from_schema(schema: &str) -> Value {
    let schema = serde_json::from_str::<Value>(schema).unwrap_or_else(|_| json!({}));
    let description = schema
        .get("description")
        .cloned()
        .or_else(|| {
            schema
                .get("description_lines")
                .and_then(Value::as_array)
                .map(|lines| {
                    Value::String(
                        lines
                            .iter()
                            .filter_map(Value::as_str)
                            .collect::<Vec<_>>()
                            .join("\n"),
                    )
                })
        })
        .unwrap_or(Value::String(String::new()));
    json!({
        "type": "function",
        "function": {
            "name": schema.get("name").cloned().unwrap_or(Value::String("unknown".to_string())),
            "description": description,
            "parameters": schema.get("parameters").cloned().unwrap_or_else(|| json!({
                "type": "object",
                "additionalProperties": false
            }))
        }
    })
}

fn markdown_from_response(response: &Value, formatted: &Value, name: &str) -> String {
    if let Some(content) = formatted.get("content").and_then(Value::as_str) {
        return content.to_string();
    }
    if let Some(result) = response.get("result").and_then(Value::as_str) {
        return result.to_string();
    }
    if let Some(error) = response.get("error").and_then(Value::as_str) {
        return format!("Tool: {name}\nStatus: failed\nError: {error}");
    }
    format!("Tool: {name}\nStatus: failed\nError: Tool returned an unsupported result shape.")
}

fn plugin_markdown_for(
    name: &str,
    mcp_markdown: &str,
    metadata: &Value,
    raw_result: &Value,
    ok: bool,
) -> String {
    let status = if ok { "completed" } else { "failed" };
    let targets = target_keys_from_metadata(metadata);
    let mut markdown = if targets.is_empty() {
        format!("{mcp_markdown}\n\nPlugin chat: {name} {status}.")
    } else {
        format!(
            "{mcp_markdown}\n\nPlugin chat: {name} {status} for {}.",
            targets.join(", ")
        )
    };
    if (name == "screenshot_scene" || name == "read_file") && ok {
        let image_markdown = inline_image_markdown(raw_result, name);
        if !image_markdown.is_empty() {
            markdown.push_str("\n\n");
            markdown.push_str(&image_markdown);
        }
    }
    markdown
}

fn target_keys_from_metadata(metadata: &Value) -> Vec<String> {
    let target_keys: Vec<String> = metadata
        .get("targets")
        .and_then(Value::as_array)
        .map(|targets| {
            targets
                .iter()
                .filter_map(target_key)
                .filter(|path| !path.is_empty())
                .collect()
        })
        .unwrap_or_default();
    if target_keys.is_empty() {
        target_key(metadata).into_iter().collect()
    } else {
        target_keys
    }
}

fn target_key(target: &Value) -> Option<String> {
    if let Some(path) = target.get("file_path").and_then(Value::as_str) {
        return Some(normalize_res_path(path));
    }
    if let Some(class_name) = target.get("class_name").and_then(Value::as_str) {
        return Some(class_name.to_string());
    }
    if let Some(editor_target) = target.get("target").and_then(Value::as_str) {
        return Some(editor_target.to_string());
    }
    for key in [
        "resource_path",
        "script_path",
        "log_path",
        "session_id",
        "key",
        "prefix",
        "query",
    ] {
        if let Some(value) = target.get(key).and_then(Value::as_str) {
            if !value.is_empty() {
                return Some(if value.starts_with("res://") {
                    normalize_res_path(value)
                } else {
                    value.to_string()
                });
            }
        }
    }
    let scene_path = target
        .get("scene_path")
        .and_then(Value::as_str)
        .map(normalize_res_path);
    if let Some(scene_path) = scene_path {
        let node_path = target
            .get("resolved_path")
            .or_else(|| target.get("node_path"))
            .and_then(Value::as_str)
            .unwrap_or_default();
        if node_path.is_empty() {
            Some(scene_path)
        } else {
            Some(format!("{scene_path}#{node_path}"))
        }
    } else {
        None
    }
}

fn model_followups_for(name: &str, raw_result: &Value) -> Vec<Value> {
    if name == "read_file" {
        return read_file_model_images(raw_result);
    }
    if name != "screenshot_scene" {
        return Vec::new();
    }
    screenshot_model_images(raw_result, name)
}

fn read_file_model_images(raw_result: &Value) -> Vec<Value> {
    raw_result
        .get("files")
        .and_then(Value::as_array)
        .map(|files| {
            files
                .iter()
                .filter_map(|file| {
                    let image = file.get("image")?;
                    let image_part = read_file_image_content_part(image)?;
                    let path = file.get("path").and_then(Value::as_str).unwrap_or("image");
                    Some(json!({
                        "role": "user",
                        "content": [
                            { "type": "text", "text": format!("[Image read from {path}]") },
                            image_part
                        ]
                    }))
                })
                .collect()
        })
        .unwrap_or_default()
}

fn screenshot_model_images(raw_result: &Value, tool_name: &str) -> Vec<Value> {
    let mut messages = Vec::new();
    if let Some(image) = image_content_part(raw_result) {
        messages.push(json!({
            "role": "user",
            "content": [
                { "type": "text", "text": format!("[Screenshot from {tool_name}]") },
                image
            ]
        }));
        return messages;
    }
    if let Some(images) = raw_result.get("images").and_then(Value::as_array) {
        for (index, image) in images.iter().enumerate() {
            if let Some(image_part) = image_content_part(image) {
                let view = image
                    .get("view")
                    .and_then(Value::as_str)
                    .filter(|value| !value.is_empty())
                    .map(|value| format!(" {value}"))
                    .unwrap_or_else(|| format!(" {}", index + 1));
                messages.push(json!({
                    "role": "user",
                    "content": [
                        { "type": "text", "text": format!("[Screenshot from {tool_name}{view}]") },
                        image_part
                    ]
                }));
            }
        }
    }
    messages
}

fn image_content_part(image: &Value) -> Option<Value> {
    let image_base64 = image.get("image_base64").and_then(Value::as_str)?;
    if image_base64.is_empty() {
        return None;
    }
    let mime_type = image
        .get("mime_type")
        .and_then(Value::as_str)
        .filter(|mime| !mime.is_empty())
        .unwrap_or("image/png");
    Some(json!({
        "type": "image_url",
        "image_url": {
            "url": format!("data:{mime_type};base64,{image_base64}")
        }
    }))
}

fn read_file_image_content_part(image: &Value) -> Option<Value> {
    let image_base64 = image.get("base64").and_then(Value::as_str)?;
    if image_base64.is_empty() {
        return None;
    }
    let mime_type = image
        .get("mime_type")
        .and_then(Value::as_str)
        .filter(|mime| !mime.is_empty())
        .unwrap_or("image/png");
    Some(json!({
        "type": "image_url",
        "image_url": {
            "url": format!("data:{mime_type};base64,{image_base64}")
        }
    }))
}

fn inline_image_markdown(raw_result: &Value, tool_name: &str) -> String {
    if tool_name == "read_file" {
        return read_file_image_markdown(raw_result);
    }
    screenshot_image_markdown(raw_result)
}

fn screenshot_image_markdown(raw_result: &Value) -> String {
    if let Some(primary) = image_markdown(raw_result, "Screenshot") {
        return primary;
    }
    raw_result
        .get("images")
        .and_then(Value::as_array)
        .map(|images| {
            images
                .iter()
                .enumerate()
                .filter_map(|(index, image)| {
                    let label = image
                        .get("view")
                        .and_then(Value::as_str)
                        .filter(|view| !view.is_empty())
                        .map(|view| format!("Screenshot {view}"))
                        .unwrap_or_else(|| format!("Screenshot {}", index + 1));
                    image_markdown(image, &label)
                })
                .collect::<Vec<_>>()
                .join("\n\n")
        })
        .unwrap_or_default()
}

fn read_file_image_markdown(raw_result: &Value) -> String {
    raw_result
        .get("files")
        .and_then(Value::as_array)
        .map(|files| {
            files
                .iter()
                .filter_map(|file| {
                    let image = file.get("image")?;
                    let label = file.get("path").and_then(Value::as_str).unwrap_or("Image");
                    read_file_image_markdown_part(image, label)
                })
                .collect::<Vec<_>>()
                .join("\n\n")
        })
        .unwrap_or_default()
}

fn image_markdown(image: &Value, label: &str) -> Option<String> {
    let image_base64 = image.get("image_base64").and_then(Value::as_str)?;
    if image_base64.is_empty() {
        return None;
    }
    let mime_type = image
        .get("mime_type")
        .and_then(Value::as_str)
        .filter(|mime| !mime.is_empty())
        .unwrap_or("image/png");
    Some(format!(
        "![{label}](data:{mime_type};base64,{image_base64})"
    ))
}

fn read_file_image_markdown_part(image: &Value, label: &str) -> Option<String> {
    let image_base64 = image.get("base64").and_then(Value::as_str)?;
    if image_base64.is_empty() {
        return None;
    }
    let mime_type = image
        .get("mime_type")
        .and_then(Value::as_str)
        .filter(|mime| !mime.is_empty())
        .unwrap_or("image/png");
    Some(format!(
        "![{label}](data:{mime_type};base64,{image_base64})"
    ))
}

fn normalize_res_path(path: &str) -> String {
    if path.starts_with("res://") || path.is_empty() {
        path.to_string()
    } else {
        format!("res://{}", path.trim_start_matches('/'))
    }
}
