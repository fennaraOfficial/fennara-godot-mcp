# FAQ

## Is Fennara only a code generator?

No. Fennara is a Godot-aware agent workflow. It can work with project files, scenes, diagnostics, runtime errors, screenshots, and Godot editor context.

## Is Fennara just another Godot MCP command server?

No. Fennara exposes Godot-aware tools, but the main product thesis is the feedback loop: Godot returns diagnostics, validation, runtime errors, screenshots, and structured tool results so agents can patch mistakes.

## Does Fennara replace Godot knowledge?

No. Fennara is not trying to make Godot optional. It is designed to make AI agents accountable to the real Godot engine.

## How should I install Fennara?

Use [Setup](setup.md) for the normal install path:

```bash
fennara install
fennara mcp-setup --help
```

The setup flow installs the Godot addon, downloads the local MCP runtime package, writes generated project guidance, and configures supported MCP apps.

## Do I need an OpenRouter API key?

Only for the built-in Fennara chat dock. Create a key at [OpenRouter Keys](https://openrouter.ai/keys), then paste it into the Fennara chat settings inside Godot.

External MCP clients use their own model/app configuration. They can use Fennara MCP tools without putting an OpenRouter key into Fennara chat.

## Does Fennara send my Godot project to a Fennara server?

No. In the normal OSS path, the MCP client, daemon, and Godot addon run locally.
The built-in chat sends model requests to OpenRouter only when you use chat with
your own OpenRouter key.

## Which project receives MCP tool calls if multiple Godot editors are open?

The daemon routes external MCP calls to the active MCP target. Use the Fennara
dock's MCP target control in Godot to choose the project. Built-in chat sessions
stay bound to the Godot editor that opened that chat.

## Why does Linux install a separate CEF runtime?

Linux embedded chat uses CEF off-screen rendering. The CEF payload is large, so
Fennara installs it once under the user's Fennara app-data directory instead of
copying it into every Godot project addon.

## Is the addon supposed to contain `libcef.so`?

No. `libcef.so`, CEF resources, locale packs, and the CEF helper belong in the
shared Linux CEF runtime. The addon should only contain the Godot addon files,
GDExtension binaries, chat UI files, and small bundled helper binaries such as
ripgrep.

## Does `fennara update` rewrite MCP app config?

No. `fennara update` refreshes the project addon, local runtime package,
generated project guidance, and platform-managed runtime assets. Run
`fennara mcp-setup` again only when setting up or repairing an MCP app config.

## Where does chat history live?

Chat history is stored locally by the daemon and scoped to the current Godot
project. OpenRouter keys are also stored locally by the daemon, outside the
Godot project.

## What should agents use Fennara tools for?

Use Fennara for Godot-aware feedback: scene trees, changed node/resource
properties, diagnostics, validation, runtime sessions, screenshots, and editor
debugger state. MCP clients should still use their own ordinary file
read/search tools unless a Fennara-specific tool is needed.
