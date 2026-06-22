# Tools

Fennara tools are built around Godot feedback.

The MCP client should still use its normal file search and file reading tools.
Fennara is for the parts that need Godot itself: scenes, resources, diagnostics,
runtime output, screenshots, and editor state.

When installed into a project, Fennara writes `.fennara/ai/guidelines.md` and a
generated Fennara block in `AGENTS.md`. Agents should read that project guidance
before doing Godot-specific work.

## External MCP Tools

| Tool | Use It For |
| --- | --- |
| `fennara_status` | Check which Godot project is connected and whether the local bridge is reachable. |
| `read_file` | Read project-scoped files and selected image files through Godot-side path normalization. |
| `file_ops` | List, glob, search with bundled ripgrep, and perform scoped copy/move/delete/create-dir file operations. |
| `get_scene_tree` | Inspect scene node structure before using node paths. |
| `get_node_properties` | Read changed node/resource properties in a scene. |
| `get_class_info` | Look up real Godot class APIs before writing scene edit scripts. |
| `write_or_update_file` | Create or patch project files, with diagnostics for scripts and shaders. |
| `run_scene_edit_script` | Make procedural scene/resource edits through Godot APIs. |
| `save_custom_resource` | Create `.tres` resources, including resources backed by custom scripts. |
| `project_settings` | Read or change `project.godot` settings and input actions. |
| `script_diagnostics` | Check `.gd`, `.cs`, and `.gdshader` files after edits. |
| `validate_scene` | Validate scene structure and run a brief headless startup check. |
| `screenshot_scene` | Capture a scene image for visual feedback. |
| `runtime_session` | Start, inspect, or stop a managed running scene. |
| `runtime_script` | Run a small probe or input-driver script inside a managed runtime session. |
| `scrape_editor` | Read the Godot editor debugger when the user manually ran a scene. |

These are the tools exposed to external MCP clients through the local
`fennara-mcp` process.

## Built-In Chat Tool Use

The in-editor Fennara chat uses the same local daemon and can call the same
Godot-side tools, including `read_file` and `file_ops`.

The difference is presentation, not tool identity. External MCP clients receive
compact markdown tool results over MCP. The built-in chat may add UI-specific
handling around the same raw results, such as showing image previews from
`read_file`, collapsing large output, or attaching screenshots/image context to
the model request.

External MCP clients should still prefer their own normal file tools for broad
repository reading/search when Godot feedback is not needed.

## Normal Workflow

For most tasks, use tools in this order:

```text
fennara_status
inspect scene/project state
make the smallest useful edit
run diagnostics or validation
use runtime tools or screenshots when behavior/visuals matter
```

## Connection

### `fennara_status`

Use this first when the MCP client is not sure which Godot project is connected.

Example prompt:

```text
Use Fennara MCP to run fennara_status and tell me which Godot project is connected.
```

## Inspection

### `get_scene_tree`

Use this before guessing node paths in a `.tscn` file. It returns nodes, types,
attached scripts, and instanced scenes.

Example prompt:

```text
Use get_scene_tree on res://scenes/player.tscn and summarize the important nodes.
```

### `get_node_properties`

Use this after `get_scene_tree` when you need to understand existing node or
resource configuration. It shows properties changed from defaults and summarizes
important embedded resources.

Example prompt:

```text
Use get_node_properties for the Player node in res://scenes/player.tscn before editing movement.
```

### `get_class_info`

Use this before `run_scene_edit_script` when touching Godot nodes/resources. It
helps avoid invented method names, wrong properties, and wrong enum usage.

Example prompt:

```text
Use get_class_info for CharacterBody2D and AnimationPlayer before writing the scene edit script.
```

## Editing

### `write_or_update_file`

Use this for normal project files. It can write a full file or replace an exact
section. For `.gd`, `.cs`, and `.gdshader` files, Fennara runs diagnostics after
the edit.

It must not write Fennara's own addon files or `project.godot`.

Example prompt:

```text
Use write_or_update_file to patch res://scripts/player.gd, then report any diagnostics.
```

### `run_scene_edit_script`

Use this for scene/resource edits that are safer through Godot APIs than raw
text editing. The worker script runs once against a target scene.

Good uses:

- add or rename nodes
- assign resources
- update exported properties
- create a new scene
- inspect scene-owned resources and log a concise summary

Before using it, inspect relevant classes with `get_class_info`.

Example prompt:

```text
Use get_class_info first, then run_scene_edit_script to add a named HealthBar node to res://scenes/player.tscn.
```

### `save_custom_resource`

Use this to create `.tres` files. For custom resource scripts, pass the full
script path such as `res://data/UpgradeData.gd`, not only the class name.

Example prompt:

```text
Use save_custom_resource to create res://data/upgrades/double_income.tres from res://data/UpgradeData.gd.
```

### `project_settings`

Use this for Godot project settings, autoloads, and input actions. For input
actions, use structured events instead of editing `project.godot` manually.

Example prompt:

```text
Use project_settings to inspect existing input actions, then add a jump action bound to Space.
```

## Checks

### `script_diagnostics`

Use this after editing `.gd`, `.cs`, or `.gdshader` files, especially when the
edit did not go through `write_or_update_file`.

Example prompt:

```text
Use script_diagnostics on res://scripts/player.gd and res://scripts/enemy.gd.
```

### `validate_scene`

Use this after scene edits. It checks common scene issues such as missing
resources, broken script references, invalid NodePaths, duplicate names, and
cyclic dependencies. If the structure is clean, it also runs a short headless
startup check.

Example prompt:

```text
Use validate_scene on res://scenes/main.tscn and explain any errors or warnings.
```

## Visual And Runtime Feedback

### `screenshot_scene`

Use this when layout, framing, camera view, or visual correctness matters.

Example prompt:

```text
Use screenshot_scene on res://scenes/main_menu.tscn and check whether the buttons are visible.
```

### `runtime_session`

Use this to start, check, or stop a managed running scene. Start returns a
`session_id` and a `runtime_session.log` path. Treat that log as the source of
truth for startup output and runtime errors.

The daemon currently allows one managed runtime session globally across all
connected Godot editors. If another managed scene is running, stop or inspect
that session before starting a new one.

Example prompt:

```text
Use runtime_session to start res://scenes/main.tscn, then inspect the session log.
```

### `runtime_script`

Use this inside an active `runtime_session` to inspect live state, press input
actions, click UI, capture frames, or run small bounded probes.

Do not guess a game's controls. Inspect `project_settings`, scripts, scene tree,
and runtime state first.

Example prompt:

```text
Use runtime_script in the active session to press the jump action once, wait, capture a screenshot, and release all actions.
```

### `scrape_editor`

Use this only when the user manually ran a scene in Godot and wants to know what
the editor debugger currently shows. For scenes started with `runtime_session`,
read `runtime_session.log` instead.

Example prompt:

```text
Use scrape_editor with target debugger and summarize the current Godot debugger errors.
```

## What Fennara Does Not Replace

Use the MCP client's own tools for:

- reading ordinary files
- searching code
- listing folders
- editing files that do not need Godot feedback
- running non-Godot shell commands

Use Fennara when the next step depends on Godot understanding the project.
