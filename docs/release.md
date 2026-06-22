# Release Process

Releases are manual. Do not publish from pull request workflows.

## Versioning

`VERSION` is the source of truth.

To bump the repo version:

```bash
node scripts/set-version.mjs 0.3.0
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
version: 0.3.0
promote_latest: true
```

The `version` input must match `VERSION`.

The workflow publishes:

- `v<version>`
- `latest` when `promote_latest` is true

The release workflow prepares the Linux CEF runtime before platform packaging.
It downloads the pinned official CEF 139 Linux minimal SDK, assembles the
separate `fennara-webview-cef-linux-x64-<cef-version>.zip`, strips staged ELF
binaries, writes a generated enabled `local/webview-runtimes/linux-cef.json`
manifest, and feeds that manifest into the CLI packages. The publish job then
validates that the release assets include the exact CEF zip named by the
generated manifest and that its SHA-256 matches. It also writes
`fennara-release-manifest-v<version>.json`, validates every referenced asset and
hash, and uploads that manifest with the release.

Pull request workflows do not publish releases. The Package Preview workflow
creates test artifacts for Linux CEF so maintainers can smoke-test embedded chat
before merging, but Package Preview is not the user-facing release channel.

## Release Assets

Each release should contain per-platform CLI/local runtime packages and one shared all-platform addon package.

Windows:

```text
fennara-cli-windows-x86_64-v<version>.zip
fennara-release-local-windows-x86_64-v<version>.zip
```

Linux:

```text
fennara-cli-linux-x86_64-v<version>.zip
fennara-release-local-linux-x86_64-v<version>.zip
```

macOS:

```text
fennara-cli-macos-arm64-v<version>.zip
fennara-release-local-macos-arm64-v<version>.zip
```

Shared addon:

```text
fennara-release-addon-v<version>.zip
fennara-addon-latest.zip
```

Linux webview runtime:

```text
fennara-webview-cef-linux-x64-<cef-version>.zip
```

Release manifest:

```text
fennara-release-manifest-v<version>.json
```

Package roles:

- `fennara-cli-*`: install script payload; contains only the `fennara` CLI for one platform.
- `fennara-release-local-*`: local MCP and daemon launchers plus versioned runtime binaries for one platform. Release uses the `fennara-release-local-*` prefix so older CLIs cannot silently bypass the manifest.
- `fennara-release-addon-v*`: versioned all-platform Godot addon payload copied into projects by the CLI through the release manifest.
- `fennara-addon-latest.zip`: stable all-platform addon URL for the Godot Asset Library and docs links.
- `fennara-webview-cef-linux-x64-*`: Linux-only shared CEF runtime installed once into Fennara app data by the CLI.
- `fennara-release-manifest-v*`: schema-versioned install/update plan used by the CLI to resolve assets, verify SHA-256 hashes, and install shared runtimes.

## Release Manifest

Starting in 0.3.0, `fennara install` and `fennara update` prefer the release
manifest whenever the release publishes one. The manifest records:

- `schema_version`
- `version`
- `minimum_cli_version`
- supported install primitives
- per-platform CLI and local runtime assets with SHA-256 hashes
- the shared addon asset with SHA-256
- platform-specific shared runtime assets, currently Linux CEF

The 0.3.x manifest uses `minimum_cli_version: 0.3.0` by default. Future normal
package layout or asset name changes should be handled by manifest data, not by
changing the outer CLI. Raise `minimum_cli_version` only when a release needs a
new manifest schema or install primitive that older CLIs truly cannot perform.

When the CLI is too old, it should fail before installing packages and print a
clear instruction to rerun `install.sh` or `install.ps1`.

The shared addon zip contains every built GDExtension binary referenced by `godot/addons/fennara/fennara.gdextension`. Godot loads the matching library for the user's OS and ignores the others.

Linux CEF webview runtime payloads are separate from the addon archive. Release
packaging generates the enabled runtime manifest and embeds that data into
`fennara-release-manifest-v<version>.json`. The CLI installs the matching CEF
payload once under the user's Fennara app-data directory:

```text
webview/cef/linux-x64/<cef-version>/
```

Do not place `libcef.so`, CEF helper executables, CEF resources, or locale packs
inside `fennara-addon-*`. Package Preview may build a separate CEF artifact for
testing, but release publishing owns the generated runtime asset and manifest
checksum.

Linux GDExtension builds also need the official CEF SDK wrapper source, but not
the CEF runtime files in the addon. CI runs:

```bash
node scripts/prepare-linux-cef-sdk.mjs
```

and passes the extracted directory as `FENNARA_CEF_ROOT` to SCons. SCons uses
`FENNARA_CEF_ROOT/libcef_dll/` to build the small
`libfennara_linux_cef_bridge.so` addon library against the pinned CEF 139 C++
wrapper. The SDK download is version- and hash-checked because the generated
wrapper source must match the runtime CEF ABI. The bridge is packaged with the
addon; `libcef.so`, resources, locale packs, and `fennara_cef_helper` remain in
the separate shared CEF runtime.

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

To assemble the runtime zip manually from a maintainer-selected CEF binary tree:

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

For normal releases, the workflow writes the Linux CEF runtime manifest
automatically with `--write-manifest`, then `scripts/write-release-manifest.mjs`
copies the runtime fields into `fennara-release-manifest-v<version>.json`. Do
not hand-enable the checked-in placeholder manifest unless intentionally
debugging a manual runtime asset path or legacy fallback behavior. If generated
manifest data points at an asset that is missing or whose SHA-256 does not
match, the Release workflow and Linux `fennara install` / `fennara update` fail
clearly.

The CLI must publish Linux CEF runtime updates atomically: extract and validate
in a staging directory, write the runtime marker only after required files are
present, then publish the version directory and update `current.json` with a
temp-file rename. Running editors keep using the runtime they already loaded.

The CLI embeds the generated project guidance templates from `local/templates/`.
When release packaging builds the CLI, those templates are compiled into the binary with the rest of the CLI code.

## What `latest` Means

`latest` is the moving release used by normal install and update flows.

- `install.ps1` and `install.sh` fetch the latest CLI asset by default.
- `fennara install` and `fennara update` fetch the release manifest from `latest` by default, then resolve local/addon/shared runtime assets from it.
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
