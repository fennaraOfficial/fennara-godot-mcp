# Setup

This guide walks through a normal Fennara setup from a clean machine to a connected MCP app.

## Requirements

- Godot 4 project
- An MCP client that can run local stdio MCP servers
- Windows, macOS, or Linux
- For C# projects: .NET SDK installed and available as `dotnet`

## 1. Install The Fennara CLI

Windows:

```powershell
irm https://raw.githubusercontent.com/fennaraOfficial/fennara-godot-mcp/main/install.ps1 | iex
```

macOS / Linux:

```bash
curl -fsSL https://raw.githubusercontent.com/fennaraOfficial/fennara-godot-mcp/main/install.sh | sh
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

`fennara install` copies the Godot addon into:

```text
addons/fennara
```

It also downloads the local Fennara runtime package into your user app-data folder and writes project guidance for AI coding agents:

```text
AGENTS.md
.fennara/ai/guidelines.md
```

If `AGENTS.md` already exists, Fennara only updates the generated block between its own markers.

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

This updates the project addon, the local runtime package, and the generated Fennara guidance files.

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

### The Addon Is Not Visible In Godot

Check that the project contains:

```text
addons/fennara/fennara.gdextension
```

Then reopen the project or refresh the plugin list in Godot.

### `fennara_status` Shows The Wrong Project

Open the intended Godot project and use the Fennara dock to set it as the MCP target.

### C# Diagnostics Are Missing

Install C# support:

```bash
cd path/to/your-godot-project
fennara install --csharp
```

Make sure `dotnet` works from your terminal:

```bash
dotnet --version
```
