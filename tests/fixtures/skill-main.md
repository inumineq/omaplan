---
name: omaplan
description: >
  Omaplan is the user's plan on this machine: tasks with dates, projects and
  priorities, shared live between the GUI window and the command line. Use it
  when the user says "put it in my plan", "add ... to my plan", "what's on my
  plan this week", "when do I have to ...", "remind me to", or asks you to
  plan, remind or remember something. The user may say it in another
  language, e.g. Danish "læg det i min plan", "hvad har jeg i denne uge".
---

# Omaplan

Omaplan keeps the user's tasks in one local plan (SQLite) that the GUI window,
this CLI and any agent share. Changes from the CLI appear live in the open
window, and `omaplan undo` reverts the latest change no matter where it came
from.

## When to use it

- "Put it in my plan" → `omaplan add "<text>"`.
- "What's on my plan this week?" → `omaplan summary --week --json` or
  `omaplan list --from monday --to sunday --json` (filter dates mean this
  week's monday; tasks without a date are left out).
- "Remind me to ..." → `omaplan add "<text>"` (dates are parsed from the
  text, e.g. "tomorrow", "friday 12", "next week", or Danish "i morgen").

Pass the user's words as they are, in their language; titles keep that
language.

## Commands

- `omaplan add "write blog post friday at 14:30, 30 min, project Blog"` — an
  AI engine proposes tasks from free text. **Show the proposal first; only
  add `--yes` when the user has explicitly asked you to save directly.**
- `omaplan new --title "Buy milk" --start tomorrow` — create directly.
- `omaplan list [--from D] [--to D] [--project P] [--status todo|done] --json`
- `omaplan show <id>`, `omaplan edit <id> --deadline friday`,
  `omaplan move <id> "tuesday at 14:00"`, `omaplan done <id>`, `omaplan rm <id>`
- `omaplan projects`, `omaplan summary [--week|--month] --json`
- `omaplan undo` — reverts the newest change from GUI, CLI or agent;
  `omaplan redo` re-applies the newest undo.

Dates accept ISO (`2026-12-01`, `2026-12-01 14:30`) and English or Danish
expressions ("tomorrow", "friday 12", "next monday", "at 14:30", "i morgen",
"kl. 14:30"). Every command takes `--json`, exits 0/1 and prints errors on
stderr, so it is safe to script; `--json` output is always English. A task's
`guessed` lists the fields the AI guessed — confirm those with the user. Examples
use invented names ("Blog", "Client A") — use the user's own words in real
calls.

If your agent has MCP, `omaplan mcp` offers the same actions as tools
(`claude mcp add omaplan -- omaplan mcp`); `propose` + `approve` there is the
same "show first, then save" flow as `add` and `add --yes`.
