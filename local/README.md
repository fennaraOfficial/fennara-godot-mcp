# Local Tools

This directory is reserved for Fennara's local MCP server, local daemon, and command-line setup/update tools.

Planned responsibilities:

- expose Fennara tools to MCP clients over stdio
- bridge MCP tool calls to the local Godot plugin
- manage local runtime helpers used by validation and runtime feedback workflows
- provide command-line setup, update, doctor, and repair commands

Keep Godot editor integration in the Godot plugin source area. Keep local process, protocol, and filesystem plumbing here.
