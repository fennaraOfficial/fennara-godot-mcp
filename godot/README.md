# Godot Payload

This directory is the source tree for the Godot-facing addon payload that is copied into user projects and packaged into release archives.

```text
godot/
  addons/
    fennara/
```

`godot/addons/fennara/` must stay installable as a normal Godot addon directory. Anything committed here should be something a user project can receive directly under `res://addons/fennara/`.

## What Belongs Here

- `addons/fennara/fennara.gdextension` and `.uid` files that Godot loads.
- `addons/fennara/bin/` editor GDExtension binaries produced by platform builds.
- `addons/fennara/dist/` generated web chat assets used by the native chat webview.
- `addons/fennara/runtime/` small Godot-side helper scripts that belong with the addon.
- `addons/fennara/VERSION`, matching the repo `VERSION` during packaging.

## What Does Not Belong Here

- Local Godot user state such as `.godot/`, `.import/`, logs, temp files, or editor caches.
- Root package outputs from workflows. Those belong under ignored build folders such as `dist/` or `.package-preview/`.
- Shared local runtime payloads such as the Fennara daemon/MCP executables or Linux CEF runtime. Those are installed under the user's Fennara app-data directory by the CLI, not copied into every Godot project addon.

## Generated Files

The chat UI source lives under `ui/chat/`. After changing it, run:

```powershell
node scripts\sync-chat-ui.mjs
```

That syncs the built webview files into `godot/addons/fennara/dist/`, which is intentionally committed because addon users should not need Node.js or a frontend build step.
