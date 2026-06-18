# Release Process

Releases are manual.

This repository is being prepared for source-based releases. Release automation should stay conservative while the public packaging flow is established.

## Expected Flow

1. Update the release version with `node scripts/set-version.mjs <x.y.z>`.
2. Run the manual Package Preview workflow from the release commit.
3. Verify artifacts locally.
4. Create a GitHub release tag such as `v0.1.0`.
5. Upload local tool archives and the Godot plugin package.
6. Update release notes with the build commit and verification notes.

## Versioning

`VERSION` is the source of truth for the repository version. The version script syncs:

- `VERSION`
- the Rust workspace package version under `local/`
- `local/Cargo.lock`
- the C++ plugin version reported by the Godot dock

Pull requests that touch versioned files run `node scripts/check-version.mjs` to catch drift before merge.

## Package Preview

The manual Package Preview workflow builds the Godot addon and local MCP tools on GitHub-hosted Windows, Linux, and macOS runners. It uploads temporary workflow artifacts only. It does not create tags, publish GitHub releases, or publish installer downloads.

## Rules

- Do not publish releases from pull request workflows.
- Do not add production deploy behavior to pull request workflows.
- Keep release workflows manual unless maintainers decide otherwise.
- Prefer small release workflow changes with clear review.
