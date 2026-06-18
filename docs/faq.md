# FAQ

## Is Fennara only a code generator?

No. Fennara is a Godot-aware agent workflow. It can work with project files, scenes, diagnostics, runtime errors, screenshots, and Godot editor context.

## Is Fennara just another Godot MCP command server?

No. Fennara exposes Godot-aware tools, but the main product thesis is the feedback loop: Godot returns diagnostics, validation, runtime errors, screenshots, and structured tool results so agents can patch mistakes.

## Does Fennara replace Godot knowledge?

No. Fennara is not trying to make Godot optional. It is designed to make AI agents accountable to the real Godot engine.

## How should I install Fennara?

Use the setup guide in this repository:

```bash
fennara install
fennara mcp-setup --help
```

The setup flow installs the Godot addon, downloads the local MCP runtime package, writes generated project guidance, and configures supported MCP apps.
