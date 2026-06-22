# Scripts

This directory contains repository automation that is shared by local development, package preview, and release workflows.

Scripts should be small, deterministic, and safe to run from the repository root unless their help text says otherwise. They should not write user-specific state outside the repo.

## Version Scripts

- `set-version.mjs`: updates the repo `VERSION`, addon `VERSION`, local Rust workspace metadata, lockfile package versions, and the C++ plugin version constant.
- `check-version.mjs`: verifies those versioned files are still in sync.

Run `check-version.mjs` in CI and before release packaging. Use `set-version.mjs` only when intentionally changing the Fennara version.

## Packaging Scripts

- `package-preview.mjs`: assembles per-platform preview archives after the GDExtension and local Rust binaries have already been built.
- `package-addon-all.mjs`: combines platform addon parts into the final all-platform addon archive.

Both scripts use `.package-preview/` as temporary staging and write zip outputs under the repo-root `dist/` folder. Those outputs are ignored and should not be committed.

Packaging scripts must keep the addon payload small. In particular, Linux CEF runtime files such as `libcef.so` and `fennara_cef_helper` must not be bundled inside `fennara-addon-*`; CEF is installed once into the user's shared Fennara app-data directory.

## Linux CEF Scripts

- `prepare-linux-cef-sdk.mjs`: downloads/extracts the pinned official Linux x64 CEF SDK used to build the Linux CEF bridge.
- `prepare-linux-cef-runtime.mjs`: stages the separate Linux CEF runtime zip, validates required files, strips staged ELF binaries on Linux, and can write the generated `local/webview-runtimes/linux-cef.json` manifest for release packaging.
- `check-linux-cef-runtime-release.mjs`: validates that release assets contain the CEF runtime zip named by the enabled manifest and that its SHA-256 matches.
- `cef/linux/fennara_cef_helper.cpp`: tiny CEF helper process source used when building the runtime helper from the CEF SDK.

The CEF scripts operate on copied staging files only. They must not mutate the downloaded/source CEF SDK tree.

## UI Sync

- `sync-chat-ui.mjs`: copies `ui/chat/` into `godot/addons/fennara/dist/`.

`godot/addons/fennara/dist/` is intentionally committed because released addon zips must contain the built chat webview. Make changes in `ui/chat/`, run the sync script, and commit both source and generated addon assets together.

## Boundaries

- Scripts may create `.package-preview/` and root `dist/` outputs.
- Scripts may update committed generated payloads only when that is their explicit job, such as `sync-chat-ui.mjs` or `set-version.mjs`.
- Scripts must not write Godot editor cache, local app-data installs, downloaded release artifacts, or VM test output into tracked source folders.
