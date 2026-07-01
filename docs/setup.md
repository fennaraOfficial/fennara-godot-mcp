# Setup

This guide walks through a normal Fennara setup from a clean machine to a connected MCP app.

## Requirements

- Godot 4.5+ project
- An MCP client that can run local stdio MCP servers
- Windows, macOS, or Linux
- For C# projects: .NET SDK installed and available as `dotnet`
- Optional for built-in chat: a configured chat provider, such as a cloud API key or a local Ollama/LM Studio server

## 1. Install The Fennara CLI

Windows:

```powershell
irm https://raw.githubusercontent.com/fennaraOfficial/fennara-godot-ai/main/install.ps1 | iex
```

macOS / Linux:

```bash
curl -fsSL https://raw.githubusercontent.com/fennaraOfficial/fennara-godot-ai/main/install.sh | sh
```

Check that the CLI is available:

```bash
fennara doctor
```

If `fennara` is not found, open a new terminal. The installer may have updated your shell PATH for future sessions.

Fennara installs the CLI here by default:

```text
Windows: %LOCALAPPDATA%\Fennara\bin
macOS: ~/Library/Application Support/Fennara/bin
Linux: ~/.local/share/fennara/bin
```

The install script installs the small outer CLI. In normal releases,
`fennara update` can update that CLI itself before refreshing project assets.
Rerun the install script only when CLI self-update is not available for the
selected release or install location.

## 2. Install Fennara In A Godot Project

Run this inside the Godot project folder:

```bash
cd path/to/your-godot-project
fennara install
```

For a C# Godot project:

```bash
cd path/to/your-godot-project
fennara install --csharp
```

`--csharp` installs Fennara's managed `csharp-ls` language server support. The
addon uses it for `.cs` results from `script_diagnostics` and for runtime
preflight checks before managed scenes are launched.

`fennara install` copies the Godot addon into:

```text
addons/fennara
```

It also reads the release manifest, downloads and verifies the local Fennara
runtime package into your user app-data folder, and writes project guidance for
AI coding agents:

```text
AGENTS.md
addons/fennara/ai/guidelines.md
```

If `AGENTS.md` already exists, Fennara only updates the generated block between its own markers.

### Built-In Chat Webview Prerequisites

Fennara MCP tools work without the built-in Godot chat dock. The chat dock needs
the platform webview:

```text
Windows: Microsoft Edge WebView2 Runtime
macOS: WKWebView from the system WebKit.framework
Linux: Fennara-managed shared CEF runtime
```

`fennara install`, `fennara update`, and `fennara doctor` check the current
platform. On Windows, missing WebView2 prints the official Microsoft WebView2
Runtime link. On macOS, WebKit is normally part of the OS, so Fennara only
reports if the framework cannot be found. On Linux, the CEF runtime is selected
from the release manifest and installed under Fennara app data.

On Linux, Fennara also uses a shared browser runtime location for the embedded
chat CEF runtime:

```text
~/.local/share/fennara/webview/cef/linux-x64/<cef-version>
```

The CEF browser payload is installed once per user and shared across Godot
projects/editors. It is not copied into `addons/fennara`. The Linux chat dock
renders through that shared runtime when it is present. Release manifests point
at the matching CEF runtime asset; `fennara install`, `fennara update`, and
`fennara doctor --repair` validate or repair the shared runtime layout.

The shared CEF runtime directory is read-only during normal browser use. Each
open Godot editor gets its own writable CEF profile/cache/log directories under
the Fennara app-data `cache/webview/profiles/cef/` and `logs/webview/cef/`
roots, so multiple editors can keep embedded chat open at the same time.

The built-in chat has its own provider settings. It can use OpenAI, Anthropic,
OpenRouter, Ollama Cloud, DeepSeek, Z.AI, Moonshot AI, Kimi For Coding, MiniMax,
local Ollama, or LM Studio. These settings are separate
from Claude Code, Codex, Cursor, Gemini, or any other external MCP app.
Provider keys and local base URLs are stored locally outside the Godot project.

Chat Settings also has **Open chat in my system browser next time**. Leave it
off to use the embedded Godot dock webview. Turn it on to have the dock show an
**Open chat** button that launches the same built-in chat in your system browser
through the local daemon. Restart Godot after changing this display setting.

Inside the dock, use `/provider` to connect or switch providers and `/model` to
choose a model. See [Built-In Chat Providers](providers.md) and [Built-In Chat Slash Commands](slash-commands.md).

To add focused code context to a built-in chat request, select code in Godot's
script editor, open the script editor context menu, and choose **Add to Chat**.
The selected script range is attached to the next chat message as removable code
context.

## 3. Configure Your MCP App

Claude Code and Claude Desktop:

```bash
fennara mcp-setup --claude
```

Codex:

```bash
fennara mcp-setup --codex
```

Cursor:

```bash
fennara mcp-setup --cursor
```

Gemini and Antigravity:

```bash
fennara mcp-setup --gemini
```

Other supported targets:

```bash
fennara mcp-setup --help
```

Restart the MCP app after running `mcp-setup`.

`mcp-setup` only connects that external app to Fennara's MCP tools. For example,
`fennara mcp-setup --claude` lets Claude call Fennara tools, but it does not make
the built-in Fennara dock use Claude or your Claude subscription. The dock uses
the provider configured in Fennara chat settings. See [MCP Apps And Built-In Chat](chat-vs-mcp.md).

## 4. Verify The Connection

Open the Godot project, then ask your MCP app:

```text
Use Fennara MCP to run fennara_status and tell me which Godot project is connected.
```

The result should show the project path you expect.

If the wrong project is shown, use the Fennara dock in Godot to set the current project as the MCP target.

## 5. Update Fennara

Run this inside the Godot project folder:

```bash
cd path/to/your-godot-project
fennara update
```

This reads the release manifest, updates the installed CLI when needed, and
then updates the project addon, the local runtime package, any shared runtime
assets needed by your platform, and the generated Fennara guidance files.

If an MCP app is currently running a Fennara launcher, `fennara update` may keep that launcher and continue. That is okay. The versioned runtime package is still updated.

## Troubleshooting

### `fennara` Is Not Found

Open a new terminal and try again:

```bash
fennara doctor
```

If it still fails, add the Fennara `bin` directory to PATH manually.

Default paths:

```text
Windows: %LOCALAPPDATA%\Fennara\bin
macOS: ~/Library/Application Support/Fennara/bin
Linux: ~/.local/share/fennara/bin
```

### Release Requires A Newer CLI

If `fennara install` or `fennara update` says the release requires a newer
Fennara CLI and self-update could not run, rerun the install script from step 1,
then run the command again. This should be rare; normal package, runtime, and
CLI changes are handled by the release manifest.

### The Addon Is Not Visible In Godot

Check that the project contains:

```text
addons/fennara/fennara.gdextension
```

Then reopen the project or refresh the plugin list in Godot.

### `fennara_status` Shows The Wrong Project

Open the intended Godot project and use the Fennara dock to set it as the MCP target.

### C# Diagnostics Are Missing

Install C# support. This installs the managed `csharp-ls` language server used
by Fennara's C# diagnostics:

```bash
cd path/to/your-godot-project
fennara install --csharp
```

Make sure `dotnet` works from your terminal:

```bash
dotnet --version
```
