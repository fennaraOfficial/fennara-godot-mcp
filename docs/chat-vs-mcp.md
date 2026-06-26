# MCP Apps And Built-In Chat

Fennara has two related but separate ways to use AI with Godot.

| Path | Where You Chat | Who Provides The Model | What Fennara Provides |
| --- | --- | --- | --- |
| External MCP app | Claude Code, Claude Desktop, Codex, Cursor, Gemini, Antigravity, or another MCP client | That app's own model account, subscription, or API setup | Godot-aware MCP tools |
| Built-in Fennara chat | The Fennara dock inside Godot | The provider configured in Fennara chat settings | A local in-editor chat UI plus the same Godot-aware tools |

The built-in chat can be displayed inside the Godot dock or opened in your system browser, depending on the Chat Settings display option. Both display modes are still the same Fennara chat path and use the provider configured in Fennara settings.

## What `mcp-setup` Does

`fennara mcp-setup --claude` edits Claude's MCP configuration so Claude can start the local Fennara MCP server and call Fennara tools.

It does not make the built-in Fennara chat use Claude. It also does not share your Claude subscription, Claude Code login, or Claude Desktop account with the Fennara dock.

The same applies to:

```bash
fennara mcp-setup --codex
fennara mcp-setup --cursor
fennara mcp-setup --gemini
```

Those commands connect external apps to Fennara's Godot tools. They do not configure the in-editor chat model.

## Do I Need An API Key?

You do not need a Fennara chat provider key to use Fennara through an external MCP app. If you use Claude Code with `fennara mcp-setup --claude`, Claude uses its own model setup and Fennara supplies Godot tools.

You only need a chat provider in Fennara settings if you want to use the built-in chat dock inside Godot.

Built-in chat can use:

- cloud providers with your own API key, such as OpenRouter, Ollama Cloud, DeepSeek, or Z.AI
- local providers, such as Ollama or LM Studio

Provider keys and local provider URLs are stored locally by the daemon outside the Godot project.

## Common Examples

If you ask Claude Code to edit your Godot project, run:

```bash
fennara mcp-setup --claude
```

Then ask Claude:

```text
Use Fennara MCP to run fennara_status and tell me which Godot project is connected.
```

No Fennara chat API key is required for that flow.

If you want to chat inside Godot's Fennara dock, open the dock settings and choose a chat provider. Use an API key for a cloud provider, or run a local server and select a local model such as:

```text
ollama/llama3.1:8b
lmstudio/openai/gpt-oss-20b
```

For provider setup details, see [Built-In Chat Providers](providers.md). For dock shortcuts, see [Built-In Chat Slash Commands](slash-commands.md).

## Shared Godot Target

External MCP apps and the built-in chat both talk to the local Fennara daemon, so they can use the same Godot feedback loop. The model account is the part that differs.

When multiple Godot editors are open, use the Fennara dock's MCP target control to choose which project receives external MCP tool calls. Built-in chat sessions stay bound to the Godot editor that opened that chat.
