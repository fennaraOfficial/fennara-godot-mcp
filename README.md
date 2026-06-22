# Fennara Godot MCP

[![Discord](https://img.shields.io/badge/Discord-Join%20Fennara-5865F2?logo=discord&logoColor=white)](https://discord.com/invite/3fF4ft9PTk)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)

Fennara gives MCP clients a live connection to Godot. Agents can inspect scenes, check scripts, capture runtime errors, and validate changes inside the editor instead of guessing from project files alone.

It is built for Godot projects where file edits are not enough. Node paths, exported variables, scene resources, runtime logs, screenshots, and editor diagnostics all matter.

Fennara also includes an in-editor chat dock backed by the same local daemon. The chat can use project-aware tools, show the active MCP target, store its OpenRouter key locally, and attach image context when supported by the selected model.

The built-in chat requires your own [OpenRouter](https://openrouter.ai/) API key. Create a key from [OpenRouter Keys](https://openrouter.ai/keys), then paste it into the Fennara chat settings inside Godot. Fennara stores that key locally through the daemon and does not put it in the Godot project. MCP clients can still use Fennara's tools through their own model/app setup.

## Requirements

- Godot 4.5 or newer.
- A supported desktop OS: Windows x86_64, Linux x86_64, or macOS arm64.
- An MCP-capable coding app such as Claude, Codex, Cursor, Gemini, or Antigravity.
- An [OpenRouter API key](https://openrouter.ai/keys) only if you want to use the built-in Fennara chat dock.

For the full install walkthrough, see [Setup](docs/setup.md).

## What Fennara Installs

- a small `fennara` CLI
- a local MCP server used by AI coding apps
- a local daemon that bridges MCP/chat requests to the open Godot editor
- a Godot addon copied into `res://addons/fennara/`
- generated project guidance for AI agents

On Linux, the embedded chat webview uses a shared CEF runtime installed once under the user's Fennara app-data directory. The CEF runtime is separate from the Godot addon so every project does not carry a browser engine copy.

## Quick Start

Follow these steps in order.

### 1. Install The CLI

Windows:

```powershell
irm https://raw.githubusercontent.com/fennaraOfficial/fennara-godot-mcp/main/install.ps1 | iex
```

macOS / Linux:

```bash
curl -fsSL https://raw.githubusercontent.com/fennaraOfficial/fennara-godot-mcp/main/install.sh | sh
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

`fennara update` reads the release manifest, then refreshes the project addon, local runtime package, generated Fennara guidance files, and any release-managed shared webview runtime needed by the current platform. Rerun the install script only when the CLI says a release requires a newer outer CLI.

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

- Bring your own [OpenRouter API key](https://openrouter.ai/keys) for built-in chat.
- OpenRouter API keys are saved locally by the daemon, outside the Godot project.
- Chat history is stored locally and scoped to the current project.
- Image attachments can be pasted or selected from the composer and sent as model context when the selected OpenRouter model supports vision.
- The dock shows whether the current project is the MCP target for external MCP clients.

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

<a href="https://www.star-history.com/#fennaraOfficial/fennara-godot-mcp&Date">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/svg?repos=fennaraOfficial/fennara-godot-mcp&type=Date&theme=dark" />
    <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/svg?repos=fennaraOfficial/fennara-godot-mcp&type=Date" />
    <img alt="Fennara Godot MCP Star History" src="https://api.star-history.com/svg?repos=fennaraOfficial/fennara-godot-mcp&type=Date" />
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
