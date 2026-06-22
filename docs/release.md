# Release Process

Releases are manual. Do not publish from pull request workflows.

## Versioning

`VERSION` is the source of truth.

To bump the repo version:

```bash
node scripts/set-version.mjs 0.2.9
```

The script updates:

- `VERSION`
- `godot/addons/fennara/VERSION`
- plugin version constants
- Rust workspace package version under `local/`
- `local/Cargo.lock`

Check version sync:

```bash
node scripts/check-version.mjs
```

## 1. Prepare The Release Commit

1. Run the version script.
2. Review the diff.
3. Run local checks that match the changed surface.
4. Merge the release prep PR into `main`.

Common checks:

```bash
node scripts/check-version.mjs
cd local
cargo test --locked
```

For GDExtension changes, also build the addon locally when possible:

```bash
cd fennara-cpp
scons platform=windows target=editor
```

## 2. Run Package Preview

Use this before publishing when packaging changed or when you want a dry run.

GitHub:

```text
Actions > Package Preview > Run workflow
```

The workflow builds Windows, Linux, and macOS packages and uploads temporary artifacts. It does not create tags or GitHub releases.

Preview artifacts are useful for checking zip contents before publishing.

## 3. Run Release

Run the manual release workflow from `main`:

```text
Actions > Release > Run workflow
```

Inputs:

```text
version: 0.2.9
promote_latest: true
linux_cef_runtime_url: <empty unless local/webview-runtimes/linux-cef.json is enabled>
```

The `version` input must match `VERSION`.

The workflow publishes:

- `v<version>`
- `latest` when `promote_latest` is true

When `local/webview-runtimes/linux-cef.json` has `enabled: true`, the release
workflow also requires a prebuilt Linux CEF runtime zip. Pass its download URL
as `linux_cef_runtime_url`; the workflow downloads it, verifies the name and
SHA-256 from the manifest, and attaches it to both `v<version>` and `latest`.
Pull request workflows do not publish CEF. The Package Preview workflow creates
a separate test-only Linux CEF runtime artifact from the pinned official CEF 139
Linux minimal archive unless `linux_cef_runtime_url` points to another official
CEF tarball or a prebuilt Fennara CEF runtime zip.

## Release Assets

Each release should contain per-platform CLI/local runtime packages and one shared all-platform addon package.

Windows:

```text
fennara-cli-windows-x86_64-v<version>.zip
fennara-local-windows-x86_64-v<version>.zip
```

Linux:

```text
fennara-cli-linux-x86_64-v<version>.zip
fennara-local-linux-x86_64-v<version>.zip
```

macOS:

```text
fennara-cli-macos-arm64-v<version>.zip
fennara-local-macos-arm64-v<version>.zip
```

Shared addon:

```text
fennara-addon-v<version>.zip
fennara-addon-latest.zip
```

Package roles:

- `fennara-cli-*`: install script payload; contains only the `fennara` CLI for one platform.
- `fennara-local-*`: local MCP and daemon launchers plus versioned runtime binaries for one platform.
- `fennara-addon-v*`: versioned all-platform Godot addon payload copied into projects by the CLI.
- `fennara-addon-latest.zip`: stable all-platform addon URL for the Godot Asset Library and docs links.

The shared addon zip contains every built GDExtension binary referenced by `godot/addons/fennara/fennara.gdextension`. Godot loads the matching library for the user's OS and ignores the others.

Linux CEF webview runtime payloads are separate from the addon archive. The
runtime manifest lives at `local/webview-runtimes/linux-cef.json`; when it is
filled with a real archive name or URL and SHA-256, the CLI installs that CEF
payload once under the user's Fennara app-data directory:

```text
webview/cef/linux-x64/<cef-version>/
```

Do not place `libcef.so`, CEF helper executables, CEF resources, or locale packs
inside `fennara-addon-*`. Package Preview may build a separate CEF artifact for
testing, but release publishing still requires an explicit maintainer-selected
runtime asset and manifest checksum.

Package scripts fail if CEF runtime files are found inside the addon archive.
The runtime asset name must be:

```text
fennara-webview-cef-linux-x64-<cef-version>.zip
```

The zip must extract with required files at its root:

```text
libcef.so
fennara_cef_helper
icudtl.dat
resources.pak
chrome_100_percent.pak
chrome_200_percent.pak
v8_context_snapshot.bin
locales/en-US.pak
```

Optional CEF runtime files such as `chrome-sandbox`, `libEGL.so`,
`libGLESv2.so`, `libvk_swiftshader.so`, `libvulkan.so.1`,
`vk_swiftshader_icd.json`, `snapshot_blob.bin`, and additional `locales/*.pak`
should be included when present in the selected CEF distribution.

To assemble the runtime zip from a maintainer-selected CEF binary tree:

```bash
node scripts/prepare-linux-cef-runtime.mjs \
  --cef-root /path/to/cef_binary_<version>_linux64_minimal \
  --version <cef-version> \
  --out-dir dist/cef-runtime
```

On Linux, the script builds `fennara_cef_helper` from
`scripts/cef/linux/fennara_cef_helper.cpp` against the official CEF headers in
`fennara-cpp/vendor/cef/`. On another OS, build that helper on Linux first and
pass `--helper /path/to/fennara_cef_helper`. Use `--dry-run` to inspect the
selected files before writing the zip.

After the script prints the SHA-256, update
`local/webview-runtimes/linux-cef.json`:

```json
{
  "version": "<cef-version>",
  "enabled": true,
  "archive": {
    "format": "zip",
    "name": "fennara-webview-cef-linux-x64-<cef-version>.zip",
    "url": null,
    "sha256": "<sha256>"
  }
}
```

Do not enable the manifest until the exact zip bytes have been produced,
hashed, attached to the release, and smoke-tested on Linux. If the manifest is
enabled but the release asset is missing or the SHA-256 does not match, the
Release workflow and Linux `fennara install` / `fennara update` fail clearly.

The CLI must publish Linux CEF runtime updates atomically: extract and validate
in a staging directory, write the runtime marker only after required files are
present, then publish the version directory and update `current.json` with a
temp-file rename. Running editors keep using the runtime they already loaded.

The CLI embeds the generated project guidance templates from `local/templates/`.
When release packaging builds the CLI, those templates are compiled into the binary with the rest of the CLI code.

## What `latest` Means

`latest` is the moving release used by normal install and update flows.

- `install.ps1` and `install.sh` fetch the latest CLI asset by default.
- `fennara install` and `fennara update` fetch local/addon packages from `latest` by default.
- The Godot plugin update check compares against GitHub's latest release.

Use `promote_latest: false` only when publishing a version that should not become the default user install.

## Smoke Test After Release

On Windows:

```powershell
irm https://raw.githubusercontent.com/fennaraOfficial/fennara-godot-mcp/main/install.ps1 | iex
fennara --version
fennara doctor
```

In a Godot project:

```bash
cd path/to/your-godot-project
fennara install
fennara mcp-setup --claude
```

Check that the project received:

```text
AGENTS.md
.fennara/ai/guidelines.md
```

Open the project in Godot, then ask the MCP app:

```text
Use Fennara MCP to run fennara_status and tell me which Godot project is connected.
```

Update test:

```bash
cd path/to/your-godot-project
fennara update
```

## Rules

- Release workflow runs from `main` only.
- Release version input must match `VERSION`.
- Pull request workflows may build and upload test artifacts, but must not publish releases.
- Keep `latest` pointed at the newest release intended for normal users.
- Do not rewrite published release tags unless maintainers intentionally decide to replace a broken release.
