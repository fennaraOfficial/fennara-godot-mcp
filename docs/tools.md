# Tools

Fennara tools are built around Godot feedback.

The MCP client should still use its normal repository search, read, and diff
tools for broad code navigation. Fennara is for the parts that need Godot
itself: scenes, resources, diagnostics, runtime output, screenshots, editor
state, and Godot-aware project settings.

When installed into a project, Fennara writes `.fennara/ai/guidelines.md` and a
generated Fennara block in `AGENTS.md`. Agents should read that project guidance
before doing Godot-specific work. The live MCP tool schemas remain the exact
source of truth for arguments, limits, and result fields.

## External MCP Tools

| Tool | Use It For |
| --- | --- |
| `fennara_status` | Check the active Godot project, daemon bridge, app-data paths, local runtime state, chat/webview support, and available MCP tools. |
| `read_file` | Read project-scoped text files and selected images through Godot path normalization and project boundaries. |
| `get_scene_tree` | Inspect real scene node structure, node types, scripts, and instanced subscenes before using node paths. |
| `get_node_properties` | Read properties changed from defaults for up to 5 scene nodes, including recursive SubResource summaries. |
| `get_class_info` | Look up real Godot class methods, properties, signals, enums, constants, inheritance, and docs before writing Godot API code. |
| `write_or_update_file` | Create, rewrite, or exact-replace project files. `.gd`, `.cs`, and `.gdshader` edits automatically run diagnostics. |
| `run_scene_edit_script` | Run one editor-time Godot worker script against exactly one scene/resource graph and save through Godot serialization. |
| `save_custom_resource` | Create `.tres` resources, including resources backed by custom Resource scripts. |
| `project_settings` | Read or change `project.godot` settings, autoloads, and input actions through structured operations. |
| `script_diagnostics` | Check `.gd`, `.cs`, and `.gdshader` files using Godot diagnostics, Godot scene-load checks, and `csharp-ls` for C#. |
| `validate_scene` | Validate scene structure and, when structural checks pass, run a brief headless startup pass. |
| `screenshot_scene` | Capture a scene image for layout, camera, rendering, material, and UI feedback. |
| `runtime_session` | Start, inspect, or stop one daemon-managed running Godot scene with live logs and runtime artifacts. |
| `runtime_script` | Run a small GDScript probe or input-driver inside an active managed runtime session. |
| `scrape_editor` | Read a narrow Godot editor debugger snapshot when the user manually ran a scene in the editor. |

These are the tools exposed to external MCP clients through the local
`fennara-mcp` process.

## Built-In Chat Tool Use

The in-editor Fennara chat uses the same local daemon and can call the same
Godot-side tools, including `read_file`.

It does not use the external MCP app's model account. Claude Code, Codex,
Cursor, Gemini, and other MCP clients use their own model setup when they call
Fennara over MCP. The built-in dock uses the provider configured in Fennara chat
settings.

The dock also has UI slash commands: `/provider` opens provider setup and
`/model` opens model selection. These are not MCP tools.

The built-in chat system prompt includes a compact runtime context from the
connected Godot editor: current date, platform, project root/name, Godot
version, Fennara plugin version, approval mode, available tools, and the Godot
editor executable path when the bridge can report it. The prompt also advertises
the built-in chat `exec_command` capability with the detected default shell and
the active project root used as the default cwd.

The built-in chat has two tool approval modes. `Ask for approval` allows
read-only inspection tools immediately and pauses project mutation or runtime
execution tools until the user approves or denies the specific tool call.
`Full access` lets those mutating/execution tools run without prompting, while
the existing hard tool safety blocks still apply. The `Approve for me` mode is
not implemented.

The built-in chat also has a daemon-owned `exec_command` tool. It does not route
through the Godot bridge. Phase one runs one non-interactive shell command with
captured stdout/stderr, timeout enforcement, output caps, and project-root cwd
restriction. Omitted cwd defaults to the active project root, relative cwd values
resolve under that root, and absolute cwd values outside the root are rejected.
Shell commands use real filesystem paths; Godot tools continue to use `res://`
and `user://`. This is approval and cwd restriction, not OS sandboxing. Phase one
does not support PTY sessions, background `session_id`, `write_stdin`, custom
environment variables, or shell-specific network controls.

The difference is presentation, not tool identity. External MCP clients receive
compact markdown tool results over MCP. The built-in chat may add UI-specific
handling around the same raw results, such as showing image previews from
`read_file`, collapsing large output, or attaching screenshots/image context to
the model request.

### Selected Script Context

The built-in chat can attach a selected script range from the Godot editor:

1. Open a script in Godot's script editor.
2. Select the code you want the model to see.
3. Open the script editor context menu on that selection.
4. Choose **Add to Chat**.

The selected range appears in the chat composer as a removable code context
chip. Click the chip to preview the attached code, or remove it before sending.
When you send the next message, Fennara includes the selected `res://` path,
1-based line range, and selected text as project context for that model request.

This is a built-in chat convenience, not an MCP tool. It requires the local
daemon connection and only appears when the current script editor has non-empty
selected text. Fennara accepts up to 8 code context snippets per message and
caps each snippet at 64,000 characters.

This approval flow is currently for the built-in chat tool loop. External MCP
clients continue to use their own permission model when deciding whether to call
Fennara MCP tools.

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
It reports the active project, Godot/addon bridge reachability, app-data paths,
daemon/runtime state, local chat/webview support, and available Fennara tools.

Example prompt:

```text
Use Fennara MCP to run fennara_status and tell me which Godot project is connected.
```

## Inspection

### `read_file`

Use this when Godot-side path normalization or image handling matters. It is
project-scoped and accepts Godot-style paths such as `res://scripts/player.gd`.
For broad source reading, use the MCP client's normal file reader.

### `get_scene_tree`

Use this before guessing node paths in a `.tscn` file. It returns nodes, types,
attached scripts, and instanced scenes. Node paths are relative to the scene
root when passed to other tools.

Example prompt:

```text
Use get_scene_tree on res://scenes/player.tscn and summarize the important nodes.
```

### `get_node_properties`

Use this after `get_scene_tree` when you need to understand existing node or
resource configuration. It shows properties changed from defaults and expands
embedded SubResources recursively.

Large or opaque resources are summarized into readable API-oriented output,
including TileSet, TileMapLayer data, GridMap data, MeshLibrary resources,
Theme resources, AnimationTree graphs, AnimationLibrary, Animation, and
SpriteFrames. A call can inspect up to 5 targets.

Example prompt:

```text
Use get_node_properties for the Player node in res://scenes/player.tscn before editing movement.
```

### `get_class_info`

Use this before `run_scene_edit_script` when touching Godot nodes/resources. It
helps avoid invented method names, wrong properties, wrong enum usage, and wrong
constructor assumptions by returning the real Godot API surface for a class.

Example prompt:

```text
Use get_class_info for CharacterBody2D and AnimationPlayer before writing the scene edit script.
```

## Editing

### `write_or_update_file`

Use this for normal project text files when a Fennara-aware edit is appropriate.
It supports two modes:

- `write`: create or completely rewrite a file.
- `update`: replace an exact `old_string` with `new_string`; include enough
  surrounding text to make the match unique.

For `.gd`, `.cs`, and `.gdshader` files, Fennara automatically runs diagnostics
after the edit and returns syntax/type/reference or shader parser feedback.
C# diagnostics use the `csharp-ls` language server installed by
`fennara install --csharp`.

For `.gdshader` edits, Fennara also scans referencing `.tscn` and `.tres` owners
and reserializes them through Godot when possible so stale embedded
`ShaderMaterial` data can be rewritten. Check `reserialized_resources`,
`reserialize_warnings`, and `reserialize_skipped` in the result.

Blocked paths include `res://project.godot`, `res://.godot/`, `res://.git/`,
`res://addons/fennara/`, and any `plugin.cfg`. Do not use this tool or generic
file edits for `.tscn`, `.tres`, or `.res` surgery; use Godot-aware structured
tools instead.

Example prompt:

```text
Use write_or_update_file to patch res://scripts/player.gd, then report any diagnostics.
```

### `run_scene_edit_script`

Use this for scene/resource edits that are safer through Godot APIs than raw
text editing. The tool runs one editor-time worker script against exactly one
target scene. It gives the script an instantiated scene object graph, not a full
running gameplay scene.

Good uses:

- add, rename, remove, or reparent nodes
- assign resources and scene-owned SubResources
- update exported properties
- create a new scene
- edit Control layout, cameras, collision shapes, materials, AnimationPlayer
  setup, signals, and groups
- inspect scene-owned resources and log a concise summary

Before using it, inspect relevant classes with `get_class_info`.

The worker script must be `@tool`, extend `RefCounted`, and define
`func run(ctx) -> void`. Inline `code` is saved under
`res://.fennara/tmp/editor_scripts/`; every result includes the effective
`script_path`. If a long or failed script needs a retry, patch that returned
`script_path` with `write_or_update_file` and rerun with `scene_path` plus
`script_path` instead of pasting a fresh full script.

Useful context methods include `ctx.get_scene_root()`, `ctx.set_scene_root()`,
`ctx.own()`, `ctx.instance_scene()`, `ctx.get_node_or_null()`,
`ctx.find_nodes_by_name()`, `ctx.remove_node()`, `ctx.clear_children()`,
`ctx.mark_modified()`, `ctx.log()`, and `ctx.error()`.

New raw nodes must be given meaningful names and owned with `ctx.own(node)` so
Godot serializes them. PackedScene instances should be added with
`ctx.instance_scene(parent, "res://path/to/scene.tscn", "DesiredName")`; do not
recursively own instance internals.

For inherited scenes, the tool tries to preserve inherited roots and save only
valid overrides/additions. If Godot would flatten the inherited root, the tool
restores the original scene and returns a failure instead of silently saving a
bad scene. In that case, patch the returned script or choose a narrower edit.

Read-only inspection scripts should only log. They should not call
`ctx.mark_modified()` or mutating helpers. Unmodified inspection runs return
logs without rewriting the `.tscn`.

`run_scene_edit_script` automatically runs script diagnostics and scene
validation after edits. Treat those results as part of the edit result.

Example prompt:

```text
Use get_class_info first, then run_scene_edit_script to add a named HealthBar node to res://scenes/player.tscn.
```

### `save_custom_resource`

Use this to create `.tres` files through Godot's resource system. For custom
resource scripts, pass the full script path such as `res://data/UpgradeData.gd`,
not only the class name.

Example prompt:

```text
Use save_custom_resource to create res://data/upgrades/double_income.tres from res://data/UpgradeData.gd.
```

### `project_settings`

Use this for Godot project settings, autoloads, display/window settings,
rendering settings, physics settings, application metadata, addon configuration,
and input actions. For input actions, use structured events instead of editing
`project.godot` manually.

`project_settings` with `action: "list"` returns settings saved in
`project.godot` with compact values in the markdown result. Input actions include
deadzone, event count, and readable event summaries, so agents can inspect
controls without immediately falling back to raw `project.godot` reads.

Example prompt:

```text
Use project_settings to inspect existing input actions, then add a jump action bound to Space.
```

## Checks

### `script_diagnostics`

Use this after editing `.gd`, `.cs`, or `.gdshader` files, especially when the
edit did not go through `write_or_update_file`.

Targeted calls support at most 5 files. `scan_project: true` scans all `.gd`,
`.cs`, and `.gdshader` files under `res://`.

The diagnostic sources are language-specific:

- `.gd`: Godot's GDScript diagnostics.
- `.cs`: the managed `csharp-ls` language server installed by
  `fennara install --csharp`, with C# project support checked by the addon.
- `.gdshader`: Godot shader parser diagnostics.

For requested `.gd` and `.cs` files, Fennara also loads and instantiates project
`.tscn` scenes in memory with Godot logging captured. Script-related scene-load
errors are attached back to the matching script with `source="scene_load"` and
`scene_path`, so agents can see which scene triggered the script error. This is
a diagnostic pass only; it does not open the editor UI, run gameplay, or
validate arbitrary scene/resource state.

Example prompt:

```text
Use script_diagnostics on res://scripts/player.gd and res://scripts/enemy.gd.
```

### `validate_scene`

Use this after scene edits or when checking scene/resource integrity. It accepts
1 to 10 scene paths.

Structural checks include missing scripts/resources, script extends mismatch,
unset exported variables, invalid NodePath properties, invalid script
`$Node`/`get_node()` references, duplicate sibling names, and cyclic scene
dependencies.

For scenes with zero structural errors, Fennara also runs each scene headlessly
for exactly 3 seconds through the local daemon using up to 3 memory-throttled
workers. The result includes compact runtime output inline and points to the
full result JSON and raw runtime logs. It does not open scenes in the editor or
scrape the editor Output panel.

Example prompt:

```text
Use validate_scene on res://scenes/main.tscn and explain any errors or warnings.
```

## Visual And Runtime Feedback

### `screenshot_scene`

Use this when layout, framing, camera view, rendering, material/shader output,
animation-visible state, or visual correctness matters.

Example prompt:

```text
Use screenshot_scene on res://scenes/main_menu.tscn and check whether the buttons are visible.
```

### `runtime_session`

Use this to start, check, or stop a managed running scene. A start request first
runs scene-execution gates:

- C# projects run `dotnet build`.
- The requested scene gets a structural `validate_scene` preflight without the
  headless runtime pass.
- Fennara checks autoload and scene-attached scripts with targeted diagnostics.

If any gate fails, Fennara does not open the scene. If the gates pass, Fennara
launches the scene in a separate windowed Godot process with the runtime helper
enabled.

The daemon currently allows one managed runtime session globally across all
connected Godot editors. If another managed scene is running, call
`runtime_session.status` to inspect it or `runtime_session.stop` to close it
before starting a new one.

Start returns a `session_id` and a `runtime_session.log` path. Treat that log as
the source of truth for startup output, raw Godot stdout/stderr, runtime errors,
`FENNARA_SCRIPT_*` markers, `ctx.log(...)` messages, captures, and completion
events.

Example prompt:

```text
Use runtime_session to start res://scenes/main.tscn, then inspect the session log.
```

### `runtime_script`

Use this inside an active `runtime_session` to inspect live state, press input
actions, click UI, capture frames, or run small bounded probes.

Runtime scripts are not editor tool scripts. Do not include `@tool`. The script
must extend `RefCounted`, define `func run(ctx: Variant) -> void`, and use
explicit local types for meaningful variables.

Runtime scripts can finish while the scene stays open. Use follow-up
`runtime_script` calls for incremental observe/experiment/verify loops, then
close the scene with `runtime_session.stop` or a final script that calls
`ctx.close_scene()`.

Await yielding helpers such as `ctx.wait(...)`, `ctx.capture(...)`,
`ctx.tap_action(...)`, `ctx.action(...)`, `ctx.action_sequence(...)`,
`ctx.click_at(...)`, and `ctx.click_button(...)`. Synchronous helpers such as
`ctx.log(...)`, `ctx.press_action(...)`, `ctx.release_action(...)`,
`ctx.set_mouse_position(...)`, `ctx.release_all_actions()`, and getters do not
need `await`.

Do not guess a game's controls or objectives. Inspect `project_settings`,
scripts, the scene tree, and runtime state first. Infer action effects from
measured state changes, not from action names, attempted input, elapsed time, or
proximity alone.

Example prompt:

```text
Use runtime_script in the active session to press the jump action once, wait, capture a screenshot, and release all actions.
```

### `scrape_editor`

Use this only when the user manually ran a scene in Godot and wants to know what
the editor debugger currently shows. For scenes started with `runtime_session`,
read `runtime_session.log` instead.

The result is intentionally narrow and compact: repeated issues are grouped,
detail lines are capped, and raw editor UI noise is omitted.

Example prompt:

```text
Use scrape_editor with target debugger and summarize the current Godot debugger errors.
```

## What Fennara Does Not Replace

Use the MCP client's own tools for:

- broad repository search and navigation
- ordinary text file reading
- diffs and version control inspection
- editing files that do not need Godot feedback
- running non-Godot shell commands

Use Fennara when the next step depends on Godot understanding the project.
