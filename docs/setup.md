# Setup

The recommended setup path is the Fennara installer from the dashboard.

## Normal Setup

1. Create a Fennara account.
2. Open the Fennara dashboard.
3. Generate an API key.
4. Copy the install command for your operating system.
5. Run the installer.
6. Choose your Godot project.
7. Open the project in Godot.
8. Paste your API key into the Fennara plugin settings.
9. If using an MCP app, let the installer configure it or follow the MCP docs.

Full guide:

https://www.fennara.io/docs/get-started

## MCP Setup

Fennara MCP runs as a local stdio MCP server installed by the Fennara installer.

Docs:

https://www.fennara.io/docs/mcp

After configuration, fully restart your MCP app and ask:

```text
Run fennara_status and tell me which Godot project is connected.
```

## Godot Project Targeting

If multiple Godot projects are open, use **Set as MCP Target** in the Fennara plugin so MCP calls route to the intended project.

Docs:

https://www.fennara.io/docs/godot-plugin/settings#set-as-mcp-target
