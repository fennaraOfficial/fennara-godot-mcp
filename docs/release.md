# Release Process

Releases are manual.

This repository is being prepared for source-based releases. Release automation should stay conservative while the public packaging flow is established.

## Expected Flow

1. Update the release version with `node scripts/set-version.mjs <x.y.z>`.
2. Run the manual Package Preview workflow from the release commit.
3. Verify artifacts locally.
4. Run the manual Release workflow from `main` with the matching version input.
5. Confirm the GitHub release has all platform addon and local tool archives.
6. Update release notes if extra human context is needed.

## Versioning

`VERSION` is the source of truth for the repository version. The version script syncs:

- `VERSION`
- the Rust workspace package version under `local/`
- `local/Cargo.lock`
- the C++ plugin version reported by the Godot dock

Pull requests that touch versioned files run `node scripts/check-version.mjs` to catch drift before merge.

## Package Preview

The manual Package Preview workflow builds the Godot addon and local MCP tools on GitHub-hosted Windows, Linux, and macOS runners. It uploads temporary workflow artifacts only. It does not create tags, publish GitHub releases, or publish installer downloads.

## Publishing

The manual Release workflow publishes versioned GitHub releases such as `v0.2.8`. It must run from `main`, and the typed version must match `VERSION`.

The workflow can also promote the same assets to a moving `latest` release for installer scripts. `latest` is mutable and should point at the newest release intended for normal users.

This repository does not publish `stable`, beta, nightly, installer downloads, or release channels yet.

## Rules

- Do not publish releases from pull request workflows.
- Do not add production deploy behavior to pull request workflows.
- Keep release workflows manual unless maintainers decide otherwise.
- Prefer small release workflow changes with clear review.
