# Fennara Local Tools

This folder contains local-native Fennara components.

## Daemon

`crates/fennara-daemon` runs the local Fennara daemon on:

```text
http://127.0.0.1:41287
```

Endpoints:

- `GET /health`: daemon health.
- `GET /status`: daemon status plus connected Godot plugin metadata.
- `POST /tools/call`: forwards a tool call to the connected Godot plugin and waits for a tool result.
- `WS /godot/ws`: local Godot plugin bridge. The plugin sends a `hello` message after connecting.

Development binary:

```text
local/target/debug/fennara-daemon.exe
```

## MCP Server

`crates/fennara-mcp` is the first local MCP server. It speaks JSON-RPC over stdio so MCP clients can launch it as a local process.

`fennara-mcp` embeds the MCP-facing tool schemas from `local/schemas/tools/`
at build time and forwards tool calls to the local daemon. It does not need an
external schema service at runtime.

`fennara install` also writes generated project guidance from `local/templates/`
into the Godot project:

```text
AGENTS.md
addons/fennara/ai/guidelines.md
```

Build:

```powershell
cd local
cargo build
```

On Windows, if a terminal has not refreshed the Rust PATH yet:

```powershell
cd local
& "$env:USERPROFILE\.cargo\bin\cargo.exe" build
```

Development binary:

```text
local/target/debug/fennara-mcp.exe
```

Current tools:

- `fennara_status`: verifies that the MCP server is installed and reachable, then reports daemon and Godot bridge status when the daemon is running.
- Godot project tools such as `write_or_update_file`, `run_scene_edit_script`,
  `get_scene_tree`, `script_diagnostics`, and `screenshot_scene` are forwarded
  to the daemon, which forwards them to the connected Godot plugin.

Later installed user path on Windows:

```text
%LOCALAPPDATA%\Fennara\bin\fennara-mcp.exe
```
