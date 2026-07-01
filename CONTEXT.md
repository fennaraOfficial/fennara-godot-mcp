# Fennara Context

This file defines common terms used in Fennara documentation, issues, release notes, and agent-facing guidance.

## Product Terms

**Fennara**

The Godot-aware agent environment in this repository. Fennara connects AI tools to real Godot feedback such as diagnostics, scene validation, runtime errors, screenshots, and project guidance.

**Godot Addon**

The installable plugin copied into a user's Godot project under `res://addons/fennara/`. It owns the dock UI, Godot-facing inspection tools, native GDExtension library, packaged chat UI assets, runtime helper scripts, and project-local addon version.

**Fennara CLI**

The `fennara` command installed on the user's machine. It handles install, update, CLI self-update, doctor checks, MCP app setup, webview prerequisite warnings, C# setup checks, and generated project guidance.

**Local Package**

The release zip containing local Fennara executables such as the MCP server, daemon, runtime binaries, and launcher binaries for one platform/architecture.

**Project Guidance**

Generated guidance files placed in a Godot project, mainly `AGENTS.md` and `addons/fennara/ai/guidelines.md`, so AI coding agents know when and how to use Fennara.

## MCP Terms

**Fennara MCP Server**

The local stdio MCP server launched by an AI coding app such as Claude Code, Cursor, Cline, Gemini CLI, or another MCP client. It exposes Fennara tools to that external app.

**MCP App**

An external AI app configured by `fennara mcp-setup`. MCP app setup controls which external app can call Fennara tools; it does not select the model used by Fennara's built-in chat.

**MCP Target**

The Godot project currently selected to receive Fennara MCP calls.

**Tool Schema**

The model-facing description of a Fennara MCP tool, including arguments, limits, and workflow notes.

**Tool Result Envelope**

The concise model-facing result returned after a tool call. Fennara results should explain status, important findings, and next useful context without dumping unnecessary raw data.

## Built-In Chat Terms

**Built-In Chat**

Fennara's own chat surface inside the Godot addon or system browser. It is separate from external MCP apps. A user can configure Claude Code for MCP and still choose a different provider/model for built-in chat.

**Chat Surface**

The display mode for built-in chat. Embedded mode uses the native Godot dock webview. Browser mode serves the same UI from the local daemon and opens it in the system browser.

**Chat Provider**

A backend that can generate built-in chat responses, such as OpenRouter, Ollama Cloud, DeepSeek, Z.AI, Moonshot AI, Kimi For Coding, MiniMax, local Ollama, or LM Studio.

**Model Ref**

The provider-qualified model identifier selected in built-in chat. Slash commands such as `/provider` and `/model` help users connect providers and choose model refs.

**Provider Connection**

Daemon-managed local settings and auth state for a chat provider, including API keys or local base URLs. Provider secrets should stay in local daemon-managed storage, not inside the Godot project.

**Generation Trace**

Stored metadata for a built-in chat generation, tying assistant messages, tool calls, provider/model choice, usage, and cost logs back to the generation that produced them.

## Runtime And Webview Terms

**Fennara Daemon**

The local service that connects MCP calls and built-in chat requests to the Godot addon, stores local runtime state, and serves daemon-hosted chat routes such as `/chat/`.

**Runtime Session**

A daemon-managed Godot runtime session used for runtime inspection, logs, validation, screenshots, and future running-scene workflows.

**Godot Snapshot**

A reversible project state snapshot taken before a Fennara-assisted turn that may modify files. Snapshot setup should complete before persisting the user turn so failed setup does not leave dangling prompts.

**Webview Runtime**

Platform support needed to display built-in chat in or near Godot. Windows uses WebView2, macOS uses WebKit/WKWebView, and Linux uses a shared CEF runtime installed under Fennara app data.

**Shared Linux CEF Runtime**

The external Linux CEF runtime payload used by the Linux chat webview. It is installed once under the user's Fennara app-data directory and must not be bundled into every Godot addon zip.

## Release Terms

**Release Manifest**

The JSON asset named `fennara-release-manifest-v<version>.json`. It maps release assets to platforms, records SHA-256 hashes, declares shared runtime assets, and sets `minimum_cli_version`.

**Minimum CLI Version**

The lowest `fennara` CLI version allowed to consume a release manifest. If a release needs newer install/update logic, this value must be raised in the generated manifest and in the release workflow that writes it.

**Latest Release**

The moving GitHub release/tag used by installers and default updates. Updating source files after publishing does not change release assets; already-published manifest assets must be replaced explicitly.
