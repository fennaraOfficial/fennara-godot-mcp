# Scripts

This directory contains small repository maintenance scripts.

Current scripts:

- `set-version.mjs`: updates `VERSION`, Rust package metadata, the lockfile, and the C++ plugin version.
- `check-version.mjs`: verifies versioned files are in sync.
- `package-preview.mjs`: assembles temporary addon and local tool zip archives after CI builds.

Planned responsibilities:

- release artifact checks
- local development helpers

Scripts should be small, documented, and safe to run from the repository root unless stated otherwise.
