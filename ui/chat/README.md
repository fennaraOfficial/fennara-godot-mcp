# Fennara Chat UI

This folder contains the source for the optional in-editor chat surface.

The first version is buildless on purpose: plain HTML, CSS, and JavaScript.
That keeps the OSS repo easy to inspect and avoids adding a frontend toolchain
before the webview host and daemon chat bridge are settled.

The packaged copy lives in `godot/addons/fennara/dist/`.

After editing this folder, run:

```bash
node scripts/sync-chat-ui.mjs
```

## Design Notes

- Match Godot editor surfaces: compact controls, quiet contrast, small radii,
  clear focus states, and no marketing-style hero treatment.
- Use only local Fennara daemon/chat APIs; do not require hosted services.
- OpenRouter support should use a user-provided key stored locally outside the
  Godot project.
- Keep the UI useful without a model connection: status, settings, transcript,
  and composer states should still be visible.
