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

The supported setup path is documented here:

https://www.fennara.io/docs/get-started

The setup flow handles:

- creating the local device identity
- installing the Godot plugin
- installing the local Fennara MCP server
- choosing the Godot project Fennara should connect to
- configuring supported MCP apps

## Setup Steps

1. Create a Fennara account.
2. Open your Fennara dashboard.
3. Copy the install command for your operating system.
4. Run the command in your terminal.
5. In the installer, choose the Godot project you want Fennara to use.
6. If your project uses C#, choose C# support in Step 2. If you only use GDScript, you can skip C# support.
7. In Step 3, choose the MCP app you want to connect, then click that app's config/update button.
8. Open the Godot project and paste your API key in the Fennara plugin settings.
9. Fully restart your MCP app so it reloads the Fennara MCP server.

If your MCP app is not shown in the installer, finish the installer first, then use the manual MCP setup guide:

https://www.fennara.io/docs/mcp#manual-setup

Other MCP apps can work when they support local stdio MCP servers and are configured manually.

## How to Confirm It Works

After setup, open your MCP app and ask:

```text
Use Fennara MCP to run fennara_status and tell me which Godot project is connected.
```

If Fennara reports the expected Godot project, the MCP server and plugin are connected.

If it does not appear, restart the MCP app and check:

- the Fennara installer finished successfully
- the MCP app was selected in installer Step 3
- the app's config/update button was clicked
- your Godot project is open
- your API key is saved in the Fennara plugin settings

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
- SemanticSearch for indexed project code
- shader search

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
