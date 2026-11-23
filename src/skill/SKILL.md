---
name: omaplan
description: >
  Omaplan is the user's plan on this machine: tasks with dates, projects and
  priorities, shared live between the GUI window and the command line. Use it
  when the user says "læg det i min plan", "tilføj ... til min plan", "hvad har
  jeg i denne uge", "hvornår skal jeg ...", or asks you to plan, remind or
  remember something. Triggers: "put it in my plan", "add to my plan", "what's
  on my plan this week", "remind me to", "plan ...".
---

# Omaplan

Omaplan keeps the user's tasks in one local plan (SQLite) that the GUI window,
this CLI and any agent share. Changes from the CLI appear live in the open
window, and `omaplan undo` reverts the latest change no matter where it came
from.

## When to use it

- "Læg det i min plan" / "put it in my plan" → `omaplan add "<text>"`.
- "Hvad har jeg i denne uge?" / "what's on my plan this week?" →
  `omaplan summary --uge --json` or `omaplan list --fra "i dag" --json`.
- "Påmind mig om ..." / "remind me to ..." → `omaplan add "<text>"` (dates are
  parsed from the text, e.g. "i morgen", "fredag 12", "næste uge").

## Commands

- `omaplan add "skriv blogindlæg fredag kl. 14:30, 30 min, projekt Blog"` —
  an AI engine proposes tasks from free text. **Show the proposal first; only
  add `--ja` when the user has explicitly asked you to save directly.**
- `omaplan new --titel "Køb mælk" --start "i morgen"` — create directly.
- `omaplan list [--fra D] [--til D] [--projekt P] [--status todo|done] --json`
- `omaplan show <id>`, `omaplan edit <id> --frist "fredag"`,
  `omaplan move <id> "tirsdag kl. 14:00"`, `omaplan done <id>`, `omaplan rm <id>`
- `omaplan projects`, `omaplan summary [--uge|--maaned] --json`
- `omaplan undo` — reverts the newest change from GUI, CLI or agent.

Dates accept ISO (`2026-12-01`, `2026-12-01 14:30`) and Danish/English
expressions ("i morgen", "fredag 12", "tomorrow", "next monday", optional
"kl. 14:30"). Every command takes `--json`, exits 0/1 and prints errors on
stderr, so it is safe to script. Examples use invented names ("Blog",
"Kunde A") — use the user's own words in real calls.
