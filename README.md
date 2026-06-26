# Fennara Godot AI

[![Discord](https://img.shields.io/badge/Discord-Join%20Fennara-5865F2?logo=discord&logoColor=white)](https://discord.com/invite/3fF4ft9PTk)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)

Fennara gives MCP clients a live connection to Godot. Agents can inspect scenes, check scripts, capture runtime errors, and validate changes inside the editor instead of guessing from project files alone.

It is built for Godot projects where file edits are not enough. Node paths, exported variables, scene resources, runtime logs, screenshots, and editor diagnostics all matter.

Fennara also includes an optional in-editor chat dock backed by the same local daemon. The chat can use project-aware tools, show the active MCP target, store provider settings locally, and attach image context when supported by the selected model.

External MCP apps and the built-in chat are separate model paths. `fennara mcp-setup --claude` lets Claude use Fennara's Godot tools; it does not make the built-in dock use Claude or your Claude subscription. The dock uses the provider configured in Fennara chat settings, such as OpenRouter, Ollama Cloud, DeepSeek, Z.AI, local Ollama, or LM Studio. See [MCP Apps And Built-In Chat](docs/chat-vs-mcp.md) and [Built-In Chat Providers](docs/providers.md).

## Requirements

- Godot 4.5 or newer.
- A supported desktop OS: Windows x86_64, Linux x86_64, or macOS arm64.
- An MCP-capable coding app such as Claude, Codex, Cursor, Gemini, or Antigravity.
- A chat provider only if you want to use the built-in Fennara chat dock. External MCP apps use their own model setup.

For the full install walkthrough, see [Setup](docs/setup.md).

## What Fennara Installs

- a small `fennara` CLI
- a local MCP server used by AI coding apps
- a local daemon that bridges MCP/chat requests to the open Godot editor
- a Godot addon copied into `res://addons/fennara/`
- generated project guidance for AI agents

The built-in chat dock uses the platform webview: Microsoft Edge WebView2 on Windows, WKWebView/WebKit on macOS, and a Fennara-managed shared CEF runtime on Linux. MCP tools still work if the optional chat dock cannot start.

## Quick Start

Follow these steps in order.

### 1. Install The CLI

Windows:

```powershell
irm https://raw.githubusercontent.com/fennaraOfficial/fennara-godot-ai/main/install.ps1 | iex
```

macOS / Linux:

```bash
curl -fsSL https://raw.githubusercontent.com/fennaraOfficial/fennara-godot-ai/main/install.sh | sh
```

Check the install:

```bash
fennara doctor
```

### 2. Add Fennara To A Godot Project

Run this from the Godot project folder:

```bash
cd path/to/your-godot-project
fennara install
```

For a C# project:

```bash
fennara install --csharp
```

`--csharp` installs Fennara's managed `csharp-ls` language server support so
`script_diagnostics`, runtime preflight checks, and C# feedback can report real
C# parser/type issues.

Then open the project in Godot.

`fennara install` also writes project guidance for AI coding agents:

```text
AGENTS.md
.fennara/ai/guidelines.md
```

### 3. Configure Your MCP App

Claude Code and Claude Desktop:

```bash
fennara mcp-setup --claude
```

Gemini and Antigravity:

```bash
fennara mcp-setup --gemini
```

Cursor:

```bash
fennara mcp-setup --cursor
```

Codex:

```bash
fennara mcp-setup --codex
```

More targets:

```bash
fennara mcp-setup --help
```

Restart the MCP app after setup so it reloads the Fennara server.

This step only configures the external MCP app. It does not configure the built-in Fennara chat model. See [MCP Apps And Built-In Chat](docs/chat-vs-mcp.md) if you are wondering why the dock asks for a provider even after `mcp-setup --claude`.

### 4. Verify It Works

With the Godot project open, ask your MCP app:

```text
Use Fennara MCP to run fennara_status and tell me which Godot project is connected.
```

If the project path is correct, the MCP server and Godot plugin are talking.

If more than one Godot project is open, use the Fennara dock's MCP target control to choose which project receives external MCP tool calls.

### 5. Update Later

Run this from the Godot project folder:

```bash
cd path/to/your-godot-project
fennara update
```

`fennara update` reads the release manifest, updates the installed CLI when a newer release requires it, then refreshes the project addon, local runtime package, generated Fennara guidance files, and any release-managed shared webview runtime needed by the current platform. On Windows/macOS it also checks the platform webview prerequisite and warns if the built-in chat dock may not start. Rerun the install script only if CLI self-update is not available for the selected release or install location.

## Tools

Fennara exposes a small set of Godot-aware tools:

- write or update project files and return diagnostics
- run one-off scene edit scripts
- inspect scene trees, nodes, resources, and Godot classes
- validate scenes
- capture screenshots
- start runtime sessions and read runtime logs
- run small runtime scripts against a live scene

The goal is not to replace an agent's normal file tools. Fennara gives the missing Godot feedback loop.

## Built-In Chat

The Fennara dock includes a native web chat surface inside Godot. It talks to the local daemon, not a hosted Fennara backend.

- Bring your own cloud provider key, or run a local provider such as Ollama or LM Studio.
- Supported chat providers include OpenRouter, Ollama Cloud, DeepSeek, Z.AI, local Ollama, and LM Studio.
- Provider API keys and local base URL settings are saved locally by the daemon, outside the Godot project.
- Chat display can stay embedded in Godot, or use the system browser next time if you enable **Open chat in my system browser next time** in Chat Settings.
- Use `/provider` to connect or switch providers and `/model` to choose a model in the dock.
- Chat history is stored locally and scoped to the current project.
- Image attachments can be pasted or selected from the composer and sent as model context when the selected provider model supports vision. Ollama image input is not enabled yet.
- The dock shows whether the current project is the MCP target for external MCP clients.

More detail: [Built-In Chat Providers](docs/providers.md), [Built-In Chat Slash Commands](docs/slash-commands.md).

## Demos

Start here:

[![I Gave Codex an AI Game Image and It Built This in Godot](https://i.ytimg.com/vi/ztbH6zBhxMc/hqdefault.jpg)](https://www.youtube.com/watch?v=ztbH6zBhxMc)

More videos:

- [Fennara MCP Builds a Katamari-Style Godot Game](https://www.youtube.com/watch?v=8y2Ub8pgNSs)
- [Godot MCPs Ranked: The Best AI Tool for Godot](https://www.youtube.com/watch?v=2vSYP7GyA5U)
- [This Godot Plugin Transforms AI Game Development Forever](https://www.youtube.com/watch?v=wKln8248y2M)
- [This Godot Plugin Revolutionizes AI Game Development Forever](https://www.youtube.com/watch?v=pijlHyiOnz4)

See [Demos](docs/demos.md) for more videos from the Fennara channel.

## Star History

<a href="https://www.star-history.com/#fennaraOfficial/fennara-godot-ai&Date">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/svg?repos=fennaraOfficial/fennara-godot-ai&type=Date&theme=dark" />
    <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/svg?repos=fennaraOfficial/fennara-godot-ai&type=Date" />
    <img alt="Fennara Godot AI Star History" src="https://api.star-history.com/svg?repos=fennaraOfficial/fennara-godot-ai&type=Date" />
  </picture>
</a>

## Repository

Useful starting points:

- [Setup](docs/setup.md)
- [Repo map](docs/repo-map.md)
- [Architecture](docs/architecture.md)
- [Tools](docs/tools.md)
- [FAQ](docs/faq.md)
- [Demos](docs/demos.md)
- [Manual install notes](docs/manual-install.md)
- [Release process](docs/release.md)
- [Contributing](CONTRIBUTING.md)
- [Security](SECURITY.md)

## Community

Questions, setup help, and early feedback are welcome on Discord:

https://discord.com/invite/3fF4ft9PTk

## License

See [LICENSE.md](LICENSE.md).
