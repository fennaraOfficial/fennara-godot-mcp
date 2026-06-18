# Fennara vs Traditional Godot MCP

Most Godot MCP servers expose editor commands to AI clients.

Examples:

- create node
- set property
- open scene
- save scene
- read logs
- take screenshot
- run project
- connect signal
- edit input map
- manage materials
- run tests

That is useful. It turns Godot into an API surface.

But for real AI game development, the hard part is not whether an AI can call `set_property`.

The hard part is whether the AI can tell when the project is broken.

## Traditional MCP Pattern

```text
AI calls editor command.
Editor returns result.
AI guesses next step.
```

This works well for small direct edits.

Example:

```text
Rename Camera3D to MainCamera.
```

But it is weaker for larger project tasks where the agent must inspect architecture, edit scripts/resources/scenes, see failures, and recover.

## Fennara Pattern

```text
AI changes project.
Godot feedback comes back.
AI patches and reruns until it works.
```

Fennara focuses on feedback:

- GDScript diagnostics
- scene validation
- runtime errors
- scene tree inspection
- node properties
- class/API inspection
- screenshots
- generated project guidance
- patch-and-rerun workflows

## The Difference

Traditional Godot MCP asks:

```text
What editor commands should we expose?
```

Fennara asks:

```text
What feedback does the model need to successfully build inside Godot?
```

Commands are table stakes.

Feedback is the moat.
