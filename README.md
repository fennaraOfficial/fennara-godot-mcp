# Fennara Godot MCP

[![Discord](https://img.shields.io/badge/Discord-Join%20Fennara-5865F2?logo=discord&logoColor=white)](https://discord.com/invite/3fF4ft9PTk)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)

Fennara gives MCP clients a live connection to Godot. Agents can inspect scenes, check scripts, capture runtime errors, and validate changes inside the editor instead of guessing from project files alone.

It is built for Godot projects where file edits are not enough. Node paths, exported variables, scene resources, runtime logs, screenshots, and editor diagnostics all matter.

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

### 5. Update Later

Run this from the Godot project folder:

```bash
cd path/to/your-godot-project
fennara update
```

`fennara update` refreshes the project addon, local runtime package, and generated Fennara guidance files for the current release.

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

## Demos

Start here:

[![I Gave Codex an AI Game Image and It Built This in Godot](https://i.ytimg.com/vi/ztbH6zBhxMc/hqdefault.jpg)](https://www.youtube.com/watch?v=ztbH6zBhxMc)

More videos:

- [Fennara MCP Builds a Katamari-Style Godot Game](https://www.youtube.com/watch?v=8y2Ub8pgNSs)
- [Godot MCPs Ranked: The Best AI Tool for Godot](https://www.youtube.com/watch?v=2vSYP7GyA5U)
- [This Godot Plugin Transforms AI Game Development Forever](https://www.youtube.com/watch?v=wKln8248y2M)
- [This Godot Plugin Revolutionizes AI Game Development Forever](https://www.youtube.com/watch?v=pijlHyiOnz4)

See [Demos](docs/demos.md) for more videos from the Fennara channel.

## Repository

Useful starting points:

- [Repo map](docs/repo-map.md)
- [Architecture](docs/architecture.md)
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
