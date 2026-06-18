# Fennara Context

This file defines common terms used in Fennara documentation and issues.

## Terms

**Fennara MCP Server**

The local stdio MCP server launched by an AI coding app.

**Fennara Daemon**

The local service that connects MCP calls to the Godot plugin and manages local Godot-facing work.

**Godot Plugin**

The Godot addon installed in a user's project. It provides Godot-aware inspection, diagnostics, validation, screenshots, and editor/runtime feedback.

**MCP Target**

The Godot project currently selected to receive Fennara MCP calls.

**Tool Schema**

The model-facing description of a Fennara tool, including arguments, limits, and workflow notes.

**Tool Result Envelope**

The concise model-facing result returned after a tool call. Fennara results should explain status, important findings, and next useful context without dumping unnecessary raw data.

**Runtime Session**

A daemon-managed Godot runtime session used for runtime inspection, logs, validation, and screenshot workflows.

**Project Guidance**

Generated guidance files placed in a Godot project so AI coding agents know when and how to use Fennara.
