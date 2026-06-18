# Open RPG Demo Breakdown

Video:

https://www.youtube.com/watch?v=0Egu3S-9MM0

This demo tests Fennara MCP on GDQuest's open-source Godot 4 Open RPG project.

The point of the demo is not that an AI created a blank project from scratch. The point is that an AI agent worked inside an existing Godot RPG codebase, made mistakes, received feedback from Godot, patched the implementation, and continued.

## Project

GDQuest Godot 4 Open RPG:

https://github.com/gdquest-demos/godot-open-rpg

## Task

Add a progression feature where Baloo, the Bear player battler, unlocks a new combat ability called Tactical Guard after winning an existing encounter.

The ability needed to:

- target one enemy
- deal modest damage
- raise Baloo's Defense
- appear in Baloo's combat action menu after the unlock
- show a message like `Baloo learned Tactical Guard!` after the unlock

## What Happened

An AI coding agent connected to the live Godot project through Fennara MCP and inspected the project architecture.

It used Fennara tools for:

- scene tree inspection
- node property inspection
- GDScript diagnostics
- scene validation
- runtime error feedback
- project and scene inspection

The first implementation did not work perfectly. That was the useful part.

Fennara returned feedback from Godot, the agent patched the broken script, adjusted the implementation, and continued until the feature worked in-game.

## Why This Matters

Blank demos are easy. Existing projects are where AI agents usually break.

Fennara's thesis is that Godot AI agents need engine feedback:

- Did the script parse?
- Did the scene validate?
- Did the runtime emit an error?
- Did the agent inspect the real project structure?
- Can the agent patch the mistake instead of pretending the task is done?

Traditional MCP gives an AI commands.

Fennara gives the AI feedback from Godot.
