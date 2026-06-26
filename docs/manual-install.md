# Manual Install

Use this page if you do not want to run the install script.

For most users, the normal setup guide is easier:

```bash
fennara install
```

See [Setup](setup.md).

## 1. Download Release Files

Open the latest GitHub release:

https://github.com/fennaraOfficial/fennara-godot-ai/releases/latest

Download the release manifest, the CLI and local runtime files for your
platform, plus the shared addon zip.

Release manifest:

```text
fennara-release-manifest-v<version>.json
```

Windows:

```text
fennara-cli-windows-x86_64-v<version>.zip
fennara-release-local-windows-x86_64-v<version>.zip
```

Linux:

```text
fennara-cli-linux-x86_64-v<version>.zip
fennara-release-local-linux-x86_64-v<version>.zip
fennara-webview-cef-linux-x64-<cef-version>.zip
```

macOS:

```text
fennara-cli-macos-arm64-v<version>.zip
fennara-release-local-macos-arm64-v<version>.zip
```

Shared addon:

```text
fennara-release-addon-v<version>.zip
```

For Godot Asset Library links, use the stable latest asset:

```text
fennara-addon-latest.zip
```

The manifest records the expected SHA-256 for the local runtime, addon, and
shared runtime assets. Use it as the source of truth when checking manual
downloads.

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
  webview/
    cef/
      linux-x64/
        <cef-version>/
```

On Windows, the binaries use `.exe`.

`current.json` points the launcher binaries to the active runtime version. The normal `fennara install` and `fennara update` commands create this file automatically.

Linux embedded chat uses the shared `webview/cef/linux-x64/<cef-version>/`
runtime location. Normal `fennara install` / `fennara update` runs install the
release-managed CEF runtime automatically from the release manifest and asset.
If you are installing everything by hand, extract
`fennara-webview-cef-linux-x64-<cef-version>.zip` into that shared runtime
location and write the matching `webview/cef/linux-x64/current.json` marker.
Keep that payload outside the Godot project addon; `addons/fennara` should not
contain `libcef.so` or other CEF runtime files.

This CEF payload is only for embedded Linux chat. Users can choose **Open chat
in my system browser next time** in Chat Settings to display the same built-in
chat through the local daemon in their system browser instead of the embedded
Godot webview.

The final Linux CEF layout should look like this:

```text
~/.local/share/fennara/
  webview/
    cef/
      linux-x64/
        current.json
        <cef-version>/
          fennara-cef-runtime.json
          libcef.so
          fennara_cef_helper
          icudtl.dat
          resources.pak
          locales/
            en-US.pak
```

`webview/cef/linux-x64/current.json` must be:

```json
{
  "runtime": "cef",
  "platform": "linux",
  "platform_arch": "linux-x64",
  "version": "<cef-version>",
  "dir": "<cef-version>"
}
```

`webview/cef/linux-x64/<cef-version>/fennara-cef-runtime.json` must be the
matching release manifest for the CEF asset, for example:

```json
{
  "schema_version": 1,
  "runtime": "cef",
  "platform": "linux",
  "arch": "x86_64",
  "platform_arch": "linux-x64",
  "version": "<cef-version>",
  "enabled": true,
  "layout": "webview/cef/linux-x64/<cef-version> with webview/cef/linux-x64/current.json pointing at the selected version",
  "required_files": [
    "libcef.so",
    "fennara_cef_helper",
    "icudtl.dat",
    "resources.pak",
    "chrome_100_percent.pak",
    "chrome_200_percent.pak",
    "v8_context_snapshot.bin",
    "locales/en-US.pak"
  ],
  "archive": {
    "format": "zip",
    "name": "fennara-webview-cef-linux-x64-<cef-version>.zip",
    "url": null,
    "sha256": "<sha256>"
  }
}
```

Do not put writable browser state inside the CEF version directory. Normal use
writes per-editor profiles and logs under the Fennara app-data cache/log roots,
while the runtime payload remains shared and read-only.

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

This only connects the external MCP app to Fennara's Godot tools. It does not
configure the built-in Fennara chat dock's model provider. Configure the dock
inside Godot if you want built-in chat, or see [MCP Apps And Built-In Chat](chat-vs-mcp.md).

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
