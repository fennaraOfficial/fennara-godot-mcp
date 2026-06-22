# Repo Map

This is the quick map for contributors and coding agents working in this repository.

## Top Level

| Path | Owns |
| --- | --- |
| `.github/` | Pull request template, issue templates, and GitHub Actions workflows. |
| `docs/` | Project docs, setup guides, architecture notes, examples, demos, and release notes. |
| `fennara-cpp/` | C++ Godot GDExtension source and SCons build entrypoint. |
| `godot/addons/fennara/` | Installable Godot addon payload copied into user projects. |
| `local/` | Rust CLI, MCP server, daemon, schemas, and local runtime code. |
| `media/` | Images and public media used by docs. |
| `scripts/` | Versioning, packaging, and release helper scripts. |
| `ui/chat/` | Source for the optional in-editor web chat UI. |
| `local/templates/` | Markdown templates written into Godot projects by `fennara install` and refreshed by `fennara update`. |
| `local/webview-runtimes/` | Manifest/config files for external webview runtimes installed into shared Fennara app data, such as the Linux CEF payload. |
| `install.ps1` / `install.sh` | Bootstrap scripts that install the Fennara CLI from GitHub releases. |
| `VERSION` | Version source of truth. |
| `README.md` | Short human-facing overview and quick start. |
| `CONTRIBUTING.md` | Contribution rules. |
| `SECURITY.md` | Security reporting policy. |
| `LICENSE.md` | Project license. |

## Local Rust Packages

| Path | Owns |
| --- | --- |
| `local/crates/fennara-cli/` | `fennara` command: install, update, doctor, C# support, MCP app setup, and generated project guidance. |
| `local/crates/fennara-mcp/` | Local stdio MCP server and tool schema forwarding. |
| `local/crates/fennara-daemon/` | Local daemon used for runtime sessions and Godot bridge work. |
| `local/schemas/tools/` | MCP tool JSON schemas embedded into the local MCP server. |
| `local/webview-runtimes/linux-cef.json` | Linux CEF runtime placeholder/generated manifest used for release manifest generation, doctor output, and legacy fallback. It records the shared app-data layout and archive metadata without placing CEF inside the addon zip. |
| `local/Cargo.toml` | Rust workspace config. |
| `local/Cargo.lock` | Locked Rust dependency graph. |

## GDExtension Source

| Path | Owns |
| --- | --- |
| `fennara-cpp/SConstruct` | GDExtension build entrypoint. |
| `fennara-cpp/include/` | Public C++ headers. |
| `fennara-cpp/src/` | C++ implementation. |
| `fennara-cpp/vendor/cef/` | Official CEF 139 header snapshot used by the Linux OSR bridge. Runtime binaries stay outside the addon. |
| `fennara-cpp/src/ui/webview_host*` | Native in-editor chat webview host and platform backends. |
| `fennara-cpp/src/ui/linux_cef_runtime.*` | Linux-only shared CEF runtime discovery, marker validation, and dynamic `libcef.so` loader foundation. |
| `fennara-cpp/src/ui/linux_cef_osr.*` / `linux_cef_input.*` / `linux_cef_bridge_loader.*` / `linux_cef_bridge_api.hpp` | Linux-only CEF off-screen rendering surface, Godot input forwarding, bridge ABI loading, and Godot texture updates for the internal chat webview. |
| `fennara-cpp/src/ui/linux_cef_bridge/` | Small Linux-only bridge library built from the pinned official CEF 139 `libcef_dll_wrapper` source and Fennara's CEF OSR adapter. The main GDExtension dlopens this after the external `libcef.so` runtime is loaded. |
| `fennara-cpp/src/tools/` | Godot-facing tool implementations. |
| `fennara-cpp/src/lsp/` | Script diagnostics and language-server helpers. |
| `fennara-cpp/src/runtime/` | Runtime capture/session support used by runtime tools. |
| `fennara-cpp/godot-cpp/` | Godot C++ bindings submodule. |

## Addon Payload

| Path | Owns |
| --- | --- |
| `godot/addons/fennara/fennara.gdextension` | Godot GDExtension registration file. |
| `godot/addons/fennara/VERSION` | Addon package version. |
| `godot/addons/fennara/bin/` | Built platform libraries. |
| `godot/addons/fennara/dist/` | Packaged web UI assets used by the in-editor chat webview. |
| `godot/addons/fennara/runtime/` | Runtime helper scripts packaged with the addon. |

## Scripts And Workflows

| Path | Owns |
| --- | --- |
| `scripts/set-version.mjs` | Updates versioned files across the repo. |
| `scripts/check-version.mjs` | Checks version sync. |
| `scripts/sync-chat-ui.mjs` | Copies the buildless chat UI source into the addon payload. |
| `scripts/package-preview.mjs` | Assembles addon, CLI, and local runtime preview/release zips after platform builds. |
| `scripts/prepare-linux-cef-runtime.mjs` | Stages the separate Linux x64 CEF runtime zip, strips staged ELF binaries, validates required files, and can write the generated release manifest. |
| `scripts/prepare-linux-cef-sdk.mjs` | Downloads and extracts the pinned official CEF 139 Linux minimal SDK for CI builds that need `libcef_dll/` wrapper source. |
| `scripts/check-linux-cef-runtime-release.mjs` | Validates the Linux CEF runtime release asset against the generated `local/webview-runtimes/linux-cef.json` manifest. |
| `scripts/write-release-manifest.mjs` | Writes and validates `fennara-release-manifest-v<version>.json` from release assets, including local package, addon, and shared runtime hashes. |
| `scripts/cef/linux/fennara_cef_helper.cpp` | Minimal Linux CEF subprocess helper source packaged inside the separate CEF runtime zip. |
| `.github/workflows/version-check.yml` | Version consistency check. |
| `.github/workflows/gdextension-build.yml` | GDExtension build check. |
| `.github/workflows/local-build.yml` | Rust local package build check. |
| `.github/workflows/package-preview.yml` | Manual package preview artifacts, including a test-only Linux CEF runtime artifact for Linux chat smoke tests. |
| `.github/workflows/release.yml` | Manual GitHub release publishing, including generated Linux CEF runtime packaging, release manifest generation, and final asset validation. |

## Where To Change Things

| Task | Start here |
| --- | --- |
| Add or change a Godot tool | `fennara-cpp/src/tools/` and `local/schemas/tools/` |
| Change MCP schema text | `local/schemas/tools/` |
| Change `fennara install` or `fennara update` | `local/crates/fennara-cli/src/` |
| Change generated project guidance | `local/templates/` and `local/crates/fennara-cli/src/project_guidance.rs` |
| Change MCP app setup | `local/crates/fennara-cli/src/mcp_setup.rs` |
| Change runtime session behavior | `fennara-cpp/src/tools/runtime_session/` and `local/crates/fennara-daemon/` |
| Change in-editor chat UI | `ui/chat/`, `godot/addons/fennara/dist/`, `fennara-cpp/src/ui/dock.cpp`, and `fennara-cpp/src/ui/webview_host*` |
| Change vendored chat UI libraries | `ui/chat/vendor/`, `godot/addons/fennara/dist/vendor/`, and `THIRD_PARTY_NOTICES.md` |
| Change C# support | `fennara-cpp/src/lsp/` and `local/crates/fennara-cli/src/csharp_support.rs` |
| Change release packages | `local/crates/fennara-cli/src/release_manifest.rs`, `local/crates/fennara-cli/src/release_package.rs`, `scripts/package-preview.mjs`, `scripts/write-release-manifest.mjs`, and `.github/workflows/release.yml` |
| Bump version | `node scripts/set-version.mjs <version>` |
| Update setup docs | `README.md`, `docs/setup.md`, and `docs/manual-install.md` |

## Notes

- Keep this file current when adding or moving major source areas.
- Keep release steps in [release.md](release.md).
- Keep setup steps in [setup.md](setup.md).
