# Fennara C++ Plugin Source

This directory is reserved for the C++ Godot GDExtension source.

Planned responsibilities:

- expose Godot-aware inspection and editing tools
- connect the Godot editor/project to the local Fennara daemon
- collect diagnostics, validation results, screenshots, runtime feedback, and editor state
- format concise tool results for AI agents

Fennara tools should stay primitive and game-agnostic. Do not add helpers that assume a specific game's controls, objectives, movement, combat, inventory, pathing, economy, quests, or UI flow.
