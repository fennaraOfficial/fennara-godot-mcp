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
```

The `version` input must match `VERSION`.

The workflow publishes:

- `v<version>`
- `latest` when `promote_latest` is true

## Release Assets

Each release should contain three package types per platform.

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

Package roles:

- `fennara-cli-*`: install script payload; contains only the `fennara` CLI.
- `fennara-local-*`: local MCP and daemon launchers plus versioned runtime binaries.
- `fennara-addon-*`: Godot addon payload copied into projects.

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
