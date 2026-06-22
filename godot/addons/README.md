# Godot Addons

This directory mirrors the shape Godot expects inside a project:

```text
res://addons/
  fennara/
```

Keeping the repository payload under `godot/addons/` lets packaging and local test scripts copy the addon into a project without reshaping paths.

## Current Addon

`fennara/` is the installable Fennara Godot MCP addon. It contains:

- `fennara.gdextension`, the Godot entry point for the native extension.
- `bin/`, platform editor binaries built from `fennara-cpp/`.
- `dist/`, generated native chat webview assets synced from `ui/chat/`.
- `runtime/`, small Godot-side helper scripts shipped with the addon.
- `debugger/`, debugger-facing addon assets.
- `VERSION`, the packaged addon version marker.

## Rules

- Keep addon-relative paths stable. User projects receive this folder as `res://addons/fennara/`.
- Do not put package-preview zips, release zips, downloaded CEF archives, logs, or local test output here.
- Do not hand-edit generated webview files in `fennara/dist/` unless you are intentionally patching generated output and then syncing the source change too.
- Add new addon payloads here only if they are meant to be copied into Godot projects.
