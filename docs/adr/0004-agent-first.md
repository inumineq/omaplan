# 0004 – Agents are first-class users

Status: decided 2026-11-23

Omaplan must work seamlessly with every AI interface on an Omarchy machine. Therefore:

- The command line covers everything the GUI can do (create, edit, move, finish, delete, undo, list, summarize),
  with `--json` and stable ids. The GUI is one client among several on top of the same store.
- `omaplan mcp` exposes the same actions as MCP tools over stdio, for agents that prefer MCP.
- `omaplan skill install` puts a bundled skill in the agent directories that exist, so agents know when and how to
  use Omaplan.
- Changes from agents are seen live in the GUI and can be undone from there.

All logic lives in C++ under CLI, MCP and QML alike, so the three can never behave differently.
