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

## Do I need a chat provider API key?

Only if you want to use a cloud provider in the built-in Fennara chat dock. External MCP clients use their own model/app configuration and can use Fennara MCP tools without putting any provider key into Fennara chat.

The built-in chat can also use local providers such as Ollama or LM Studio, which do not require a cloud API key.

Supported built-in chat providers are OpenRouter, Ollama Cloud, DeepSeek, Z.AI, local Ollama, and LM Studio. See [Built-In Chat Providers](providers.md).

## Why does the dock ask for a provider if I already ran `mcp-setup --claude`?

`fennara mcp-setup --claude` connects Claude to Fennara's Godot MCP tools. It does not connect the built-in Fennara dock to Claude, and it does not share your Claude subscription with Fennara chat.

Use Claude Code or Claude Desktop for the external MCP flow. Configure a separate provider only if you want to chat inside Godot's Fennara dock. See [MCP Apps And Built-In Chat](chat-vs-mcp.md).

## What are `/provider` and `/model`?

They are slash commands in the built-in Fennara chat dock. `/provider` opens the provider picker. `/model` opens the model picker. They are UI shortcuts, not external MCP tools and not text sent to the model. See [Built-In Chat Slash Commands](slash-commands.md).

## Does Fennara send my Godot project to a Fennara server?

No. In the normal OSS path, the MCP client, daemon, and Godot addon run locally.
The built-in chat sends model requests only to the provider you configure, such
as OpenRouter, Ollama Cloud, DeepSeek, Z.AI, or a local Ollama/LM Studio server.

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

## What if the built-in chat webview cannot start?

Fennara MCP tools still work. Only the optional in-editor chat dock needs the
platform webview. On Windows, install Microsoft Edge WebView2 Runtime if
`fennara doctor` reports it missing. On macOS, WKWebView comes from the system
WebKit.framework. On Linux, run `fennara update` so the release-managed CEF
runtime can be installed or repaired.

You can also use the Chat Settings option **Open chat in my system browser next
time**. That keeps the same built-in Fennara chat and provider settings, but
opens the UI through the local daemon in your system browser instead of the
embedded Godot webview. Restart Godot after changing the setting.

## Does opening chat in my browser use Claude or my MCP app?

No. Browser display is only a UI/runtime choice for the built-in Fennara chat.
It still uses the provider selected in Fennara chat settings. `fennara
mcp-setup --claude` and similar commands configure external MCP apps; they do
not configure the built-in chat model.

## Does `fennara update` rewrite MCP app config?

No. `fennara update` refreshes the installed CLI when needed, the project
addon, local runtime package, generated project guidance, and platform-managed
runtime assets. Run `fennara mcp-setup` again only when setting up or repairing
an MCP app config.

## Where does chat history live?

Chat history is stored locally by the daemon and scoped to the current Godot
project. Provider keys and local provider URLs are also stored locally by the
daemon, outside the Godot project.

## What should agents use Fennara tools for?

Use Fennara for Godot-aware feedback: scene trees, changed node/resource
properties, diagnostics, validation, runtime sessions, screenshots, and editor
debugger state. MCP clients should still use their own ordinary file
read/search tools unless a Fennara-specific tool is needed.
