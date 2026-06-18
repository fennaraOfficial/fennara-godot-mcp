# Architecture

Fennara is a local bridge between an MCP client and a Godot editor project.

There is no cloud service in the normal OSS path. The MCP client starts a local
process, that process talks to a local daemon, and the daemon talks to the
Godot addon running inside the open editor.

```text
MCP client
  -> fennara-mcp launcher
  -> versioned fennara-mcp-runtime
  -> fennara-daemon launcher
  -> versioned fennara-daemon-runtime
  -> Godot editor addon
  -> open Godot project
```

## Main Pieces

| Piece | Where It Lives | What It Does |
| --- | --- | --- |
| CLI | `local/crates/fennara-cli/` | Installs the addon into a Godot project, updates local packages, and writes MCP app config. |
| MCP launcher | `local/crates/fennara-mcp/` | Stable executable that MCP apps call. It finds the active version and starts the runtime. |
| MCP runtime | `local/crates/fennara-mcp/` | Speaks MCP over stdio and forwards tool calls to the local bridge. |
| Daemon launcher | `local/crates/fennara-daemon/` | Stable executable used to start the active daemon runtime. |
| Daemon runtime | `local/crates/fennara-daemon/` | Keeps local state, coordinates with Godot, and serves the MCP runtime. |
| Godot addon | `godot/addons/fennara/` | The addon payload copied into user projects. |
| GDExtension | `fennara-cpp/` | Godot-facing tools, dock UI, diagnostics, validation, runtime capture, and editor integration. |
| Tool schemas | `local/schemas/tools/` | MCP tool descriptions exposed to clients. |

## Install Layout

The install script only installs the CLI and adds it to `PATH`.

After that, `fennara install` or `fennara update` downloads release assets and
sets up the local package layout.

```text
Fennara/
  bin/
    fennara
    fennara-mcp
    fennara-daemon
  current.json
  versions/
    <version>/
      fennara-mcp-runtime
      fennara-daemon-runtime
      addon/
        addons/
          fennara/
```

On Windows, executables use `.exe`.

Default platform locations:

| OS | Base Directory |
| --- | --- |
| Windows | `%LOCALAPPDATA%\Fennara` |
| macOS | `~/Library/Application Support/Fennara` |
| Linux | `~/.local/share/fennara` |

## Project Layout

When a user runs this inside a Godot project:

```bash
fennara install
```

the CLI copies the release addon into:

```text
<godot-project>/
  AGENTS.md
  .fennara/
    ai/
      guidelines.md
  addons/
    fennara/
```

For C# projects:

```bash
fennara install --csharp
```

adds the same addon plus the C# support files needed by Fennara's diagnostics
path.

## MCP Setup

`fennara mcp-setup` edits MCP app config so the app can start the local
launcher.

Examples:

```bash
fennara mcp-setup --claude
fennara mcp-setup --codex
fennara mcp-setup --cursor
fennara mcp-setup --gemini
```

The config points at the stable `fennara-mcp` launcher in the Fennara `bin`
directory. The launcher reads `current.json`, then starts the matching
versioned runtime.

That keeps MCP app configs stable across updates.

## Tool Call Flow

```text
MCP client
  calls a Fennara tool
MCP runtime
  validates the request against local schemas
  forwards the call to the local daemon
Daemon runtime
  routes the request to the connected Godot project
Godot addon
  runs the Godot-aware tool through GDExtension
  returns a concise markdown result
MCP runtime
  sends the result back to the MCP client
```

The MCP client can read and write normal files by itself. Fennara tools focus on
Godot-specific feedback: scene structure, node properties, diagnostics,
validation, runtime state, screenshots, and editor-aware edits.

## Updates

`fennara update` is the normal project update command.

It can update:

- the local CLI/runtime package
- the project addon
- MCP app config written by `fennara mcp-setup`
- generated project guidance in `AGENTS.md` and `.fennara/ai/guidelines.md`

If an MCP app is currently running a launcher, the update may keep that launcher
and continue. The versioned runtime package is still updated, and future starts
use the version from `current.json`.

## Release Assets

Each public release publishes separate assets so installs can stay modular:

| Asset | Purpose |
| --- | --- |
| `fennara-cli-<platform>-<arch>.zip` | CLI and stable launchers. |
| `fennara-local-<platform>-<arch>.zip` | Versioned MCP and daemon runtimes. |
| `fennara-addon-<platform>-<arch>.zip` | Godot addon payload with the GDExtension binary. |

The moving `latest` release is what normal users should install from. Versioned
releases such as `v0.2.8` stay available for pinning and debugging.

## Design Rules

- Keep tools primitive and game-agnostic.
- Let agents inspect the project before making assumptions.
- Prefer Godot API feedback over file-only guesses.
- Return concise markdown results that an MCP client can use directly.
- Keep launchers stable and move changing code into versioned runtimes.
- Avoid cloud, account, and API-key requirements in the OSS path.
