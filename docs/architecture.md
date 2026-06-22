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
| CLI | `local/crates/fennara-cli/` | Installs the addon into a Godot project, updates local packages, writes project guidance, and configures MCP apps through `fennara mcp-setup`. |
| MCP launcher | `local/crates/fennara-mcp/` | Stable executable that MCP apps call. It finds the active version and starts the runtime. |
| MCP runtime | `local/crates/fennara-mcp/` | Speaks MCP over stdio and forwards tool calls to the local bridge. |
| Daemon launcher | `local/crates/fennara-daemon/` | Stable executable used to start the active daemon runtime. |
| Daemon runtime | `local/crates/fennara-daemon/` | Keeps local state, coordinates with Godot, and serves the MCP runtime. |
| Godot addon | `godot/addons/fennara/` | The addon payload copied into user projects. |
| GDExtension | `fennara-cpp/` | Godot-facing tools, dock UI, diagnostics, validation, runtime capture, and editor integration. |
| Tool schemas | `local/schemas/tools/` | MCP tool descriptions exposed to clients. |

## In-Editor Chat Webview

The optional chat dock is hosted by the GDExtension UI layer. The shared host
contract separates two browser surface styles:

| Platform Path | Behavior |
| --- | --- |
| Windows | Native WebView2 child/overlay attached to the Godot editor window. |
| macOS | Native WKWebView attached to the Godot editor window. |
| Linux | CEF off-screen rendering into an internal Godot `TextureRect`, using a shared CEF runtime from Fennara app data. |

The Linux path renders browser pixels inside a Godot `Control` and routes the
CEF message loop through the dock process hook. The GDExtension discovers the
shared CEF runtime, validates its `fennara-cef-runtime.json` marker and
required files, dynamically opens `libcef.so`, then dlopens the small
`libfennara_linux_cef_bridge.so` addon library through a focused bridge loader.
That bridge is built from the
pinned official CEF 139 `libcef_dll_wrapper` source and owns the C++ CEF
objects (`CefClient`, `CefRenderHandler`, `CefRefPtr`) used to initialize CEF in
windowless mode, create the browser for the packaged chat URL, and copy paint
buffers into a Godot texture. Full IME, clipboard, and cursor handling are
separate follow-up work. The CEF runtime is intentionally separate from the
Godot addon zip: Linux installs use a shared app-data runtime location and the
CLI installs the release-managed CEF asset there once per user.

Multiple Godot editors may be open at the same time. Each embedded chat
websocket is accepted with the owning editor's `chat_token` and remains bound to
that Godot session for chat storage scope, snapshots, tool execution, cancel,
and revert. External MCP clients still route through the daemon's active target.
OpenRouter settings are global for now, while chats remain project-scoped.

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
  webview/
    cef/
      linux-x64/
        <cef-version>/
```

On Windows, executables use `.exe`.

The `webview/cef/...` directory is for read-only browser engine payloads shared
by every Godot project/editor using that Fennara install. Per-process writable
CEF profile, cache, and log data must stay outside that shared runtime payload
under `cache/webview/profiles/cef/godot-<pid>-<timestamp>-<nonce>/` and
`logs/webview/cef/godot-<pid>-<timestamp>-<nonce>/`.

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
- generated project guidance in `AGENTS.md` and `.fennara/ai/guidelines.md`
- shared webview runtime assets needed by the current platform, such as Linux CEF

It does not rewrite MCP app config. Run `fennara mcp-setup` again only when
adding a new MCP client, repairing that client's config, or changing the MCP
target app integration itself.

If an MCP app is currently running a launcher, the update may keep that launcher
and continue. The versioned runtime package is still updated, and future starts
use the version from `current.json`.

The daemon currently allows one managed `runtime_session` scene globally across
all connected Godot editors. A start request runs in the selected or
chat-bound Godot project, but another running managed scene must be stopped
before starting a new one.

## Release Assets

Each public release publishes separate assets so installs can stay modular:

| Asset | Purpose |
| --- | --- |
| `fennara-cli-<platform>-<arch>-v<version>.zip` | CLI and stable launchers. |
| `fennara-local-<platform>-<arch>-v<version>.zip` | Versioned MCP and daemon runtimes. |
| `fennara-addon-v<version>.zip` / `fennara-addon-latest.zip` | All-platform Godot addon payload with every built GDExtension binary referenced by `fennara.gdextension`. |
| `fennara-webview-cef-linux-x64-<cef-version>.zip` | Linux-only shared CEF runtime installed once into Fennara app data. |

The moving `latest` release is what normal users should install from. Versioned
releases such as `v0.2.8` stay available for pinning and debugging.

Linux CEF runtime payloads are not part of `fennara-addon-*`. They are selected
by the generated release copy of `local/webview-runtimes/linux-cef.json` and
installed once into the shared app-data
`webview/cef/linux-x64/<cef-version>/` directory.

CEF runtime installs stage into a temporary sibling directory, validate required
files and the runtime marker, then publish the completed version directory and
atomically update `current.json`. Existing editor processes keep using the
already-loaded runtime.

## Design Rules

- Keep tools primitive and game-agnostic.
- Let agents inspect the project before making assumptions.
- Prefer Godot API feedback over file-only guesses.
- Return concise markdown results that an MCP client can use directly.
- Keep launchers stable and move changing code into versioned runtimes.
- Keep the external MCP path local. The optional built-in chat dock uses the user's own OpenRouter API key and stores it locally through the daemon.
