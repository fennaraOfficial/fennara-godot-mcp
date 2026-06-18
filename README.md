# Fennara Godot MCP

Fennara connects AI coding agents to real Godot feedback so they can inspect, fix, and rerun work instead of guessing from files alone.

Game development breaks in places normal coding agents cannot see: script errors, node warnings, runtime errors, diagnostics, missing resources, nested subresources, scene state, and screenshots. Fennara gives agents that Godot context through the Godot plugin and local MCP server.

## Start Here

Install the Fennara CLI:

```powershell
irm https://raw.githubusercontent.com/fennaraOfficial/fennara-godot-mcp/main/install.ps1 | iex
```

```bash
curl -fsSL https://raw.githubusercontent.com/fennaraOfficial/fennara-godot-mcp/main/install.sh | sh
```

Then check the local install:

```bash
fennara doctor
```

Then install the Godot addon from inside your Godot project:

```bash
cd path/to/your-godot-project
fennara install
```

When a new release is available, update the local Fennara runtime and addon package:

```bash
fennara update
fennara install
```

## Setup Steps

1. Install the Fennara CLI.
2. Run `fennara doctor` to verify the local runtime layout.
3. Open a terminal in your Godot project folder.
4. Run `fennara install`.
5. Open the Godot project and enable the Fennara addon.
6. Configure your MCP app to run the local `fennara-mcp` command.
7. Restart your MCP app so it reloads the Fennara MCP server.

Other MCP apps can work when they support local stdio MCP servers and are configured manually.

## How to Confirm It Works

After setup, open your MCP app and ask:

```text
Use Fennara MCP to run fennara_status and tell me which Godot project is connected.
```

If Fennara reports the expected Godot project, the MCP server and plugin are connected.

If it does not appear, restart the MCP app and check:

- the Fennara installer finished successfully
- your MCP app is configured to run `fennara-mcp`
- your Godot project is open

## What Fennara Tools Do

Fennara exposes Godot-aware tools for agent workflows:

- file and script writes with diagnostics
- one-off scene edit scripts
- scene tree inspection
- node property inspection
- Godot class/API inspection
- runtime error capture
- scene screenshots
- scene validation

For the full tools reference, see:

https://www.fennara.io/docs/tools

## Demos

Start with the real-project test: Fennara MCP on GDQuest's open-source Godot 4 Open RPG project.

[![Fennara MCP tested on a real Godot RPG project](https://img.youtube.com/vi/0Egu3S-9MM0/maxresdefault.jpg)](https://www.youtube.com/watch?v=0Egu3S-9MM0)

In the demo, an AI coding agent works on an existing RPG codebase instead of an empty project. The first script breaks, Fennara returns Godot feedback, and the agent patches the implementation.

More demos:

- [Open RPG demo breakdown](docs/open-rpg-demo.md)
- [More Fennara demos](docs/demos.md)
- [Fennara vs traditional Godot MCP](docs/fennara-vs-traditional-godot-mcp.md)

## Useful Links

- Setup guide: https://www.fennara.io/docs/get-started
- MCP setup: https://www.fennara.io/docs/mcp
- Godot tools docs: https://www.fennara.io/docs/tools
- Godot MCP overview: https://www.fennara.io/godot-mcp
- Godot AI plugin overview: https://www.fennara.io/godot-ai-plugin
- Website: https://www.fennara.io

## Repository Status

This repository is being prepared as the public source home for Fennara's Godot MCP tooling. Documentation, build instructions, and release workflows are being added incrementally.

Start with:

- [Repo map](docs/repo-map.md)
- [Architecture](docs/architecture.md)
- [Contributing](CONTRIBUTING.md)
- [Security](SECURITY.md)

## License

See [LICENSE.md](LICENSE.md).
