# Agent Instructions

Read this file before changing the repository.

## Core Rules

- Keep changes small, focused, and easy to review.
- Prefer simple code and clear ownership boundaries.
- Do not add game-specific MCP tools or guidance. Fennara should expose Godot feedback and primitive controls, not assumptions about a particular game's movement, combat, inventory, quests, UI flow, or objectives.
- Do not publish releases, create tags, or run release workflows unless a maintainer explicitly asks for that exact action.
- Do not change GitHub Actions release behavior casually. Explain any workflow change in the pull request.
- Keep platform-specific native code behind explicit platform files or small bridge boundaries. Windows, macOS, Linux, and unsupported fallback behavior should remain obvious from filenames and call sites.
- Do not bundle heavyweight browser runtimes into the Godot addon. Linux CEF is a shared local webview runtime installed under the user's Fennara app-data directory, not copied into every `res://addons/fennara/`.

## Source Of Truth

- `README.md` is the human-facing project overview.
- `llms.txt` is the short index for language models and coding agents.
- `CONTEXT.md` defines shared Fennara vocabulary.
- `docs/repo-map.md` explains repository layout.
- `docs/architecture.md` explains the high-level system.
- `docs/release.md` explains release expectations.
- `local/templates/` contains project guidance written by `fennara install` and refreshed by `fennara update`.
- `ui/chat/` contains the source web chat UI. `godot/addons/fennara/dist/` is the synced addon copy.

## Generated And Packaged Files

- After editing `ui/chat/`, run `node scripts/sync-chat-ui.mjs` and commit the matching `godot/addons/fennara/dist/` changes.
- Do not hand-edit generated addon webview files in `godot/addons/fennara/dist/` without also updating the source in `ui/chat/`.
- Root `dist/` and `.package-preview/` are build outputs and should stay untracked.
- `godot/addons/fennara/dist/` is intentionally tracked because release addon zips must contain the built chat UI.

## Documentation Updates

When changing tool behavior, setup behavior, or release behavior, update the relevant docs in the same pull request.

When adding source areas, update `docs/repo-map.md` so contributors and agents can find the right files quickly.

When changing Linux webview runtime installation, update the release docs and keep package-preview limitations explicit. Package Preview is for test artifacts; Release is the source of truth for user-facing install assets.

## Pull Requests

- Use Conventional Commit style for pull request titles.
- Keep descriptions short and specific.
- Explain how the change was verified.
- Avoid unrelated cleanup in feature or fix pull requests.
