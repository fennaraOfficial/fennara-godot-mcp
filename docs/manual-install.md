# Manual Install

Use this page if you do not want to run the install script.

For most users, the normal setup guide is easier:

```bash
fennara install
```

See [Setup](setup.md).

## 1. Download Release Files

Open the latest GitHub release:

https://github.com/fennaraOfficial/fennara-godot-mcp/releases/latest

Download the files for your platform.

Windows:

```text
fennara-cli-windows-x86_64-v<version>.zip
fennara-local-windows-x86_64-v<version>.zip
fennara-addon-windows-x86_64-v<version>.zip
```

Linux:

```text
fennara-cli-linux-x86_64-v<version>.zip
fennara-local-linux-x86_64-v<version>.zip
fennara-addon-linux-x86_64-v<version>.zip
```

macOS:

```text
fennara-cli-macos-arm64-v<version>.zip
fennara-local-macos-arm64-v<version>.zip
fennara-addon-macos-arm64-v<version>.zip
```

## 2. Install The CLI

Extract the `fennara-cli` zip.

Add its `bin` directory to PATH, or copy the `fennara` binary into one of your existing PATH folders.

Check it:

```bash
fennara --version
fennara doctor
```

## 3. Install The Godot Addon

Extract the `fennara-addon` zip.

Copy:

```text
addons/fennara
```

into your Godot project so the project contains:

```text
addons/fennara/fennara.gdextension
```

## 4. Install The Local Runtime Package

The CLI normally manages this for you. Manual runtime setup is only needed if you are avoiding `fennara install`.

Default Fennara data folders:

```text
Windows: %LOCALAPPDATA%\Fennara
macOS: ~/Library/Application Support/Fennara
Linux: ~/.local/share/fennara
```

The expected layout is:

```text
Fennara/
  bin/
    fennara-mcp
    fennara-daemon
  current.json
  versions/
    <version>/
      fennara-mcp-runtime
      fennara-daemon-runtime
      addon/
        addons/
          fennara/
```

On Windows, the binaries use `.exe`.

`current.json` points the launcher binaries to the active runtime version. The normal `fennara install` and `fennara update` commands create this file automatically.

## 5. Configure Your MCP App

After the local runtime package is installed, configure your MCP app:

```bash
fennara mcp-setup --claude
```

Other targets:

```bash
fennara mcp-setup --help
```

Restart the MCP app after setup.

## 6. Verify

Open the Godot project, then ask your MCP app:

```text
Use Fennara MCP to run fennara_status and tell me which Godot project is connected.
```

If the path is correct, the manual install is working.

## Recommended Shortcut

Even if you install the CLI manually, you can let it install the addon and local runtime package:

```bash
cd path/to/your-godot-project
fennara install
```

For C# projects:

```bash
cd path/to/your-godot-project
fennara install --csharp
```

The CLI also writes project guidance for AI coding agents:

```text
AGENTS.md
.fennara/ai/guidelines.md
```

If you copy files manually instead of running `fennara install`, those guidance files are not created automatically.
