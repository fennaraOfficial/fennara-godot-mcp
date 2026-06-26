# Examples

## Check Connection

```text
Use Fennara MCP to run fennara_status and tell me which Godot project is connected.
```

## Inspect a Project Before Editing

```text
Use Fennara MCP to inspect this Godot project. Look at the scene tree, relevant files, diagnostics, and project structure before suggesting changes.
```

## Make a Small Architecture-Aware Change

```text
Work inside this existing Godot project like a careful contributor. Inspect how the relevant system is organized, make the smallest useful change, and explain what files/resources changed and how I can test it.
```

## Debug a Runtime Error

```text
Use Fennara MCP to inspect the latest Godot runtime errors, find the likely source, patch the issue, and explain the fix.
```

## Visual Feedback

```text
Use Fennara MCP to capture a screenshot of the current scene, inspect the UI layout, and suggest or make a small fix if something is visibly wrong.
```

## Built-In Chat Provider Setup

In the Fennara dock inside Godot:

```text
/provider
```

Connect a cloud provider or local provider.

Then:

```text
/model
```

Pick the model the dock should use.

## Existing Project Demo Prompt

This is the kind of prompt used for the Open RPG demo:

```text
I want you to work inside this existing Godot RPG project like a careful project contributor. Before making changes, understand how the relevant systems are organized. Reuse the existing architecture and naming style wherever possible. Add the requested feature in the smallest clean way, then tell me what changed and how to try it in-game.
```
