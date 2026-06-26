# Built-In Chat Slash Commands

Slash commands are shortcuts in the Fennara chat dock inside Godot. They are UI commands, not MCP tools and not prompts sent to the model.

Type `/` in the composer to open the command palette.

| Command | Opens | Use It For |
| --- | --- | --- |
| `/provider` | Provider picker | Connect a cloud provider, configure a local provider URL, or switch provider. |
| `/model` | Model picker | Choose a model from the current or connected provider. |

## How They Behave

- Use arrow keys to move through command suggestions.
- Press Enter to run the selected command.
- Press Escape to close the command palette.
- The slash command text is removed from the composer before the chat message is sent.

## Common Flow

For the built-in chat dock:

```text
/provider
```

Connect OpenRouter, Ollama Cloud, DeepSeek, Z.AI, local Ollama, or LM Studio.

Then:

```text
/model
```

Pick the model you want the dock to use.

For external MCP apps, do not use these slash commands. Configure the app with `fennara mcp-setup`, then ask the app to use Fennara MCP tools.
