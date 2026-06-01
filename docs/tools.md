# Tools

Fennara tools are built around Godot feedback.

The goal is not only to let an AI call editor commands. The goal is to let the AI see what Godot reports after a change.

## Representative Tools

| Tool | Purpose |
| --- | --- |
| `fennara_status` | Checks local connection, entitlement, and MCP target state. |
| `write_or_update_file` | Creates or updates project files and runs diagnostics for GDScript changes. |
| `run_scene_edit_script` | Runs one-off Godot scene/resource edits through Godot APIs. |
| `get_scene_tree` | Inspects scene structure so agents do not guess node paths. |
| `get_node_properties` | Reads important node/resource configuration before edits. |
| `get_class_info` | Looks up Godot class APIs so agents do not invent methods or properties. |
| `get_runtime_errors` | Captures runtime errors and debugger output from Godot. |
| `screenshot_scene` | Captures 2D or 3D scene screenshots for visual feedback. |
| `semantic_search` | Searches indexed project code for relevant scripts, signals, resources, and likely bug locations. |

## Why Feedback Matters

A command can succeed while the project is still broken.

Examples:

- a scene saves but a script does not parse
- a NodePath points to a missing node
- a resource path is invalid
- a runtime error appears only after playing the scene
- a generated UI technically exists but looks wrong

Fennara is built to return this feedback to the model so it can patch and rerun instead of silently handing back broken work.
