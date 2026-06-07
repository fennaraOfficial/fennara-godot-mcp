# FAQ

## Is Fennara only a code generator?

No. Fennara is a Godot-aware agent workflow. It can work with project files, scenes, diagnostics, runtime errors, screenshots, SemanticSearch, and Godot editor context.

## Is Fennara just another Godot MCP command server?

No. Fennara exposes Godot-aware tools, but the main product thesis is the feedback loop: Godot returns diagnostics, validation, runtime errors, screenshots, and structured tool results so agents can patch mistakes.

## Does Fennara replace Godot knowledge?

No. Fennara is not trying to make Godot optional. It is designed to make AI agents accountable to the real Godot engine.

## Does this repo include the full backend?

No. This public repo is for discoverability, docs, setup links, demo context, and public metadata. The installable plugin and MCP server are distributed through the Fennara installer. Some service-side components are not open source.

## How should I install Fennara?

Use the setup guide:

https://www.fennara.io/docs/get-started

The dashboard installer handles the local device identity, API key flow, plugin install, local MCP server install, and supported MCP app configuration.
