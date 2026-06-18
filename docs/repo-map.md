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
| `local/templates/` | Markdown templates written into Godot projects by `fennara install` and refreshed by `fennara update`. |
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
| `local/Cargo.toml` | Rust workspace config. |
| `local/Cargo.lock` | Locked Rust dependency graph. |

## GDExtension Source

| Path | Owns |
| --- | --- |
| `fennara-cpp/SConstruct` | GDExtension build entrypoint. |
| `fennara-cpp/include/` | Public C++ headers. |
| `fennara-cpp/src/` | C++ implementation. |
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
| `godot/addons/fennara/runtime/` | Runtime helper scripts packaged with the addon. |

## Scripts And Workflows

| Path | Owns |
| --- | --- |
| `scripts/set-version.mjs` | Updates versioned files across the repo. |
| `scripts/check-version.mjs` | Checks version sync. |
| `scripts/package-preview.mjs` | Assembles addon, CLI, and local runtime release zips. |
| `.github/workflows/version-check.yml` | Version consistency check. |
| `.github/workflows/gdextension-build.yml` | GDExtension build check. |
| `.github/workflows/local-build.yml` | Rust local package build check. |
| `.github/workflows/package-preview.yml` | Manual package preview artifacts. |
| `.github/workflows/release.yml` | Manual GitHub release publishing. |

## Where To Change Things

| Task | Start here |
| --- | --- |
| Add or change a Godot tool | `fennara-cpp/src/tools/` and `local/schemas/tools/` |
| Change MCP schema text | `local/schemas/tools/` |
| Change `fennara install` or `fennara update` | `local/crates/fennara-cli/src/` |
| Change generated project guidance | `local/templates/` and `local/crates/fennara-cli/src/project_guidance.rs` |
| Change MCP app setup | `local/crates/fennara-cli/src/mcp_setup.rs` |
| Change runtime session behavior | `fennara-cpp/src/tools/runtime_session/` and `local/crates/fennara-daemon/` |
| Change C# support | `fennara-cpp/src/lsp/` and `local/crates/fennara-cli/src/csharp_support.rs` |
| Change release packages | `scripts/package-preview.mjs` and `.github/workflows/release.yml` |
| Bump version | `node scripts/set-version.mjs <version>` |
| Update setup docs | `README.md`, `docs/setup.md`, and `docs/manual-install.md` |

## Notes

- Keep this file current when adding or moving major source areas.
- Keep release steps in [release.md](release.md).
- Keep setup steps in [setup.md](setup.md).
