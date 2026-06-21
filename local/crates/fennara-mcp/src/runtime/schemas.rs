use serde_json::{Value, json};

const TOOL_GUIDANCE: &str = "Follow the generated Fennara MCP guidelines for Godot workflow rules, and treat this tool schema as the exact contract for this tool.";

const EMBEDDED_TOOL_DEFINITIONS: &[&str] = &[
    include_str!("../../../../schemas/tools/write_or_update_file.json"),
    include_str!("../../../../schemas/tools/run_scene_edit_script.json"),
    include_str!("../../../../schemas/tools/get_scene_tree.json"),
    include_str!("../../../../schemas/tools/save_custom_resource.json"),
    include_str!("../../../../schemas/tools/script_diagnostics.json"),
    include_str!("../../../../schemas/tools/screenshot_scene.json"),
    include_str!("../../../../schemas/tools/get_node_properties.json"),
    include_str!("../../../../schemas/tools/get_class_info.json"),
    include_str!("../../../../schemas/tools/validate_scene.json"),
    include_str!("../../../../schemas/tools/project_settings.json"),
    include_str!("../../../../schemas/tools/runtime_session.json"),
    include_str!("../../../../schemas/tools/runtime_script.json"),
    include_str!("../../../../schemas/tools/scrape_editor.json"),
];

pub(crate) const FORWARDED_TOOLS: &[&str] = &[
    "write_or_update_file",
    "run_scene_edit_script",
    "get_scene_tree",
    "save_custom_resource",
    "script_diagnostics",
    "screenshot_scene",
    "get_node_properties",
    "get_class_info",
    "validate_scene",
    "project_settings",
    "runtime_session",
    "runtime_script",
    "scrape_editor",
];

pub(crate) fn load_embedded_tool_schemas() -> Vec<Value> {
    let mut tools = Vec::new();

    for definition in EMBEDDED_TOOL_DEFINITIONS {
        match tool_from_embedded_definition(definition) {
            Ok(tool) => tools.push(tool),
            Err(error) => tools.push(json!({
                "name": "invalid_embedded_tool_definition",
                "description": format!("Failed to load embedded tool definition: {error}"),
                "inputSchema": {
                    "type": "object",
                    "properties": {},
                    "additionalProperties": false
                }
            })),
        }
    }

    tools
}

pub(crate) fn tool_from_embedded_definition(definition: &str) -> Result<Value, String> {
    let mut tool: Value = serde_json::from_str(definition).map_err(|error| error.to_string())?;
    let object = tool
        .as_object_mut()
        .ok_or_else(|| "tool definition is not a JSON object".to_string())?;
    if !object.contains_key("description") {
        if let Some(description_lines) = object.remove("description_lines") {
            let lines = description_lines
                .as_array()
                .ok_or_else(|| "description_lines must be an array of strings".to_string())?;
            let joined = lines
                .iter()
                .map(|line| {
                    line.as_str()
                        .ok_or_else(|| "description_lines must contain only strings".to_string())
                })
                .collect::<Result<Vec<_>, _>>()?
                .join("\n");
            object.insert("description".to_string(), Value::String(joined));
        }
    }
    if let Some(description) = object.get("description").and_then(Value::as_str) {
        object.insert(
            "description".to_string(),
            Value::String(format!("{description}\n\n{TOOL_GUIDANCE}")),
        );
    }
    let parameters = object
        .remove("parameters")
        .unwrap_or_else(|| json!({ "type": "object", "properties": {} }));
    object.insert("inputSchema".to_string(), parameters);
    Ok(tool)
}

pub(crate) fn is_forwarded_tool(name: &str) -> bool {
    FORWARDED_TOOLS.contains(&name)
}
