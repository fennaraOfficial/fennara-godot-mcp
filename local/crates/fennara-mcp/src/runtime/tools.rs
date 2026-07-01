use super::{
    daemon_client::{daemon_status, daemon_tool_call},
    protocol::{SERVER_NAME, SERVER_VERSION, error_response, success_response},
    schemas::{is_forwarded_tool, load_embedded_tool_schemas},
};
use serde_json::{Value, json};

pub(crate) fn tools_list_result() -> Value {
    let mut tools = vec![json!({
        "name": "fennara_status",
        "description": "Return local Fennara MCP status. This verifies the MCP server is installed and reachable.",
        "inputSchema": {
            "type": "object",
            "properties": {},
            "additionalProperties": false
        }
    })];

    tools.extend(load_embedded_tool_schemas());

    json!({
        "tools": tools
    })
}

pub(crate) fn handle_tool_call(id: Value, params: Option<&Value>) -> Value {
    let tool_name = params
        .and_then(|params| params.get("name"))
        .and_then(Value::as_str);

    match tool_name {
        Some("fennara_status") => success_response(id, tool_result(status_payload())),
        Some(name) if is_forwarded_tool(name) => {
            let args = params
                .and_then(|params| params.get("arguments"))
                .cloned()
                .unwrap_or_else(|| json!({}));
            let result = match daemon_tool_call(name, args) {
                Ok(payload) => payload,
                Err(error) => json!({
                    "ok": false,
                    "error": error
                }),
            };
            let is_error = result.get("ok").and_then(Value::as_bool) == Some(false);
            success_response(id, forwarded_tool_result(name, &result, is_error))
        }
        Some(name) => error_response(id, -32602, format!("Unknown tool: {name}")),
        None => error_response(id, -32602, "Missing tool name".to_string()),
    }
}

fn status_payload() -> Value {
    match daemon_status() {
        Ok(status) => json!({
            "ok": true,
            "server": SERVER_NAME,
            "version": SERVER_VERSION,
            "daemon_connected": true,
            "daemon": status
        }),
        Err(error) => json!({
            "ok": true,
            "server": SERVER_NAME,
            "version": SERVER_VERSION,
            "daemon_connected": false,
            "godot_plugin_connected": false,
            "message": format!("Open a Godot project with Fennara enabled. The local daemon is not reachable yet: {error}")
        }),
    }
}

fn tool_result(payload: Value) -> Value {
    json_tool_result_with_error(payload, false)
}

fn json_tool_result_with_error(payload: Value, is_error: bool) -> Value {
    json!({
        "content": [
            {
                "type": "text",
                "text": payload.to_string()
            }
        ],
        "structuredContent": payload,
        "isError": is_error
    })
}

fn forwarded_tool_result(tool_name: &str, response: &Value, is_error: bool) -> Value {
    json!({
        "content": [
            {
                "type": "text",
                "text": text_from_plugin_result(tool_name, response)
            }
        ],
        "isError": is_error
    })
}

fn text_from_plugin_result(tool_name: &str, response: &Value) -> String {
    if let Some(result) = response.get("result") {
        if let Some(text) = result.as_str() {
            return text.to_string();
        }
        if !result.is_null() {
            return result.to_string();
        }
    }

    if let Some(error) = response.get("error").and_then(Value::as_str) {
        return format!("Tool: {tool_name}\nStatus: failed\nError: {error}");
    }

    format!("Tool: {tool_name}\nStatus: failed\nError: Tool returned an unsupported result shape.")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn forwarded_tool_result_sends_only_plugin_result() {
        let response = json!({
            "ok": true,
            "result": "Tool: validate_scene\nStatus: success",
            "formatted_result": {
                "content": "wrong layer",
                "metadata": {
                    "tool_name": "validate_scene"
                }
            },
            "raw_result": {
                "scenes": [
                    { "scene_path": "res://huge.tscn", "issues": [{ "message": "raw detail" }] }
                ]
            },
            "request_id": "local-tool-1",
            "type": "tool_result"
        });

        let result = forwarded_tool_result("validate_scene", &response, false);

        assert_eq!(
            result["content"][0]["text"],
            "Tool: validate_scene\nStatus: success"
        );
        assert!(result.get("structuredContent").is_none());
        assert!(!result.to_string().contains("wrong layer"));
        assert!(!result.to_string().contains("raw detail"));
        assert!(!result.to_string().contains("raw_result"));
    }

    #[test]
    fn forwarded_tool_result_reports_bridge_error_when_plugin_result_is_missing() {
        let response = json!({
            "ok": false,
            "error": "Godot plugin disconnected before returning a tool result."
        });

        let result = forwarded_tool_result("project_settings", &response, true);

        assert_eq!(
            result["content"][0]["text"],
            "Tool: project_settings\nStatus: failed\nError: Godot plugin disconnected before returning a tool result."
        );
        assert_eq!(result["isError"], true);
    }
}
