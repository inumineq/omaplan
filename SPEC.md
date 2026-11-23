# Omaplan – spec

Omaplan is a planning app for the Omarchy desktop. You write or dictate your tasks in plain text, an AI tidies them
up and puts them on dates and times, and you see the plan as an overview, calendar, list or timeline.

Main use: content production and tasks over a week, a month or a period.

## The one thing the app does

From messy text to a plan you can see and move around.

## User flow

1. The app opens on **Overview**: today and the coming days as an agenda.
2. `n` (or Space) opens **input**: a large text field. You write freely or dictate with Omarchy's dictation
   (Superwhisper types into the focused field; the app does not build its own).
   Example: "podcast clip for linkedin tuesday, article on the budget must be ready friday 12, research 3 hours
   wednesday morning, newsletter every thursday in november".
3. `Ctrl+Enter` sends the text to the chosen AI engine.
4. **Proposal**: the AI's tasks are shown as an editable list before anything is stored. Fields the AI has guessed
   (e.g. "sometime next week") are clearly marked as guesses. You can correct, delete and add.
   `Ctrl+Enter` approves, Esc discards.
5. Approved tasks are in the plan and can be seen in every view.

The first version only adds. "Replan my week" is the next version.

## Compare mode

In the input you can choose **Compare**: the same text goes to two engines at once (default: Mistral and Claude),
and the proposals are shown side by side. You approve one. It is useful in itself and a way to see what the engines
can do.

## Views (keys 1–4)

1. **Overview** (start): agenda for today and the next 7 days, overdue first.
2. **Calendar**: week and month (`w`/`m` switches). Tasks can be dragged to a new day/time.
3. **List**: all tasks with checkboxes; group by date or project; filter by project and status.
4. **Timeline**: Gantt style with duration and overlap; tasks can be dragged and their length changed.

The views are different looks at the same data. No view has state of its own beyond zoom/filter.

## Task model

| Field | Type | Note |
|---|---|---|
| id | UUID | |
| title | text | short, imperative or noun |
| notes | text | optional |
| project | text | optional, e.g. "Blog", "Client A" |
| priority | 1–3 | 1 = high |
| status | `todo` / `done` | |
| start | date or date+time | local time (e.g. Europe/Copenhagen) |
| duration | minutes | optional; an all-day task if empty and no time |
| deadline | date or date+time | optional, separate from start |
| guessed | list of field names | which fields the AI has guessed |
| source | id of the capture | traceability back to the raw text |
| created, modified | timestamps | |

In JSON the guessed fields are `guessed` everywhere (AI proposals, `--json`, MCP, `plan.json`); `guesses` from
before 0.2 is still read. The duration is `duration_min` in AI proposals and `plan.json`, and `duration_minutes`
in `--json` and MCP answers, next to `start_has_time` and `deadline_has_time`.

Recurrence ("every thursday in november") is expanded by the AI into single tasks in the first version. Real
recurrence rules are the next version.

## AI engines

One shared interface with several providers, chosen in the settings and per input:

- **Mistral** via the Vibe CLI (default in the first version)
- **Claude** via `claude -p` (the user's subscription)
- **Local**: a local model via Ollama or an OpenAI-compatible endpoint
- **Command**: any command line that reads the prompt on stdin and writes JSON on stdout

All engines get the same prompt and must return JSON following the same schema (`docs/ai-schema.json`).
The app validates the answer; an invalid answer is shown as an error in the window with the engine's raw answer,
never as silent loss. No API keys in the app or in the repo. See ADR 0002.

## AI integration (core requirement)

Omaplan must be fully operable by any AI agent on an Omarchy machine (Claude Code, Codex, Mistral Vibe,
OpenCode and others), as well as by the user. Everything you can do in the GUI you can do from the command line.
See ADR 0004.

### Command line

Like Hype: every command takes `--json`, exit 0/1, errors on stderr, `QT_QPA_PLATFORM=offscreen`, stable ids,
and `--help` with examples. Dates accept ISO and expressions in English or Danish ("tomorrow", "friday 12",
"i morgen").

- `omaplan open` – GUI (default without arguments)
- `omaplan add "<free text>" [--engine …] [--yes]` – AI proposal; without `--yes` the proposal is printed, with it
  it is stored
- `omaplan new --title T [--start …] [--duration …] [--deadline …] [--project …] [--priority …]` – create directly,
  without AI
- `omaplan list [--from] [--to] [--project] [--status] [--json]`, `omaplan show <id>`
- `omaplan edit <id> [fields]`, `omaplan move <id> <when>`, `omaplan done <id>`, `omaplan undone <id>`, `omaplan rm <id>`
- `omaplan projects`, `omaplan summary [--week|--month]` – short text/JSON summary to give an agent context
- `omaplan undo` – undo the latest change, whether it came from the GUI or the CLI
- `omaplan snapshot <path>`, `omaplan serve …` (read-only edition, phase 3)
- `omaplan mcp` – MCP server over stdio with the same actions as tools
- `omaplan skill install` – install the bundled agent skill in every agent directory that exists
  (`~/.claude/skills`, `~/.codex`, `~/.vibe`, `~/.config/opencode` and others); `omaplan skill uninstall` removes it

Global options: `--db <path>` and `--lang da|en` (interface language, see below).

### Live

GUI and CLI share the database. Changes from an agent show up in an open window at once (the GUI watches the
database for changes), briefly marked with the source of the change ("from CLI"). Concurrent writes are safe
(SQLite transactions, WAL, busy timeout).

### Agent skill

A short skill bundled in the binary: what Omaplan is, when the agent should use it ("put it in my plan",
"what do I have this week"), the most important commands with examples, and the rule to show proposals before
`--yes`, unless the user has asked to store directly.

## Read-only edition on the tailnet (addition, phase 3)

The plan can be read from a phone or another machine on the user's Tailscale network, read-only.

- After every change the app writes a snapshot, `plan.json`, atomically (write beside, rename). Format:
  `docs/export.md`.
- `omaplan serve` runs on a machine that can act as server and shows the snapshot as a small, server-rendered
  HTML page (overview, list and timeline, mobile friendly, Omarchy dark) plus `/plan.json`. No writing, no
  JavaScript required. The page is in English or Danish, following the browser's language.
- Binds to `127.0.0.1` by default; shared on the tailnet with `tailscale serve`. No login of its own: access =
  tailnet.
- How the snapshot gets to the server machine is the user's choice (Syncthing, rsync over Tailscale, or the
  server is the same machine). The README shows the rsync example.

## Omarchy conventions (from the omarchy-app skill)

Follows the theme live, follows Omarchy's language live (all strings through `qsTr`/`tr`, translations in
`i18n/omaplan_<lang>.ts`; English and Danish in the first version, see `docs/translating.md`), corners from
Hyprland, keyboard first (`?` shows the keys, `q` quits, Space is the main action, Ctrl+Z/Ctrl+Shift+Z), atomic
saving, errors shown in the window. Installed with `bin/install` (makepkg), so it is in the app launcher.

## Interface language

The interface language is the first of: `--lang`, `language` in `config.toml`, Omarchy's language file, the system
locale, English. A language without a translation passes to the next source. The GUI, the CLI's human-readable
output, error messages and date formats are translated. `--json`, JSON field names, MCP tool names and
descriptions, code and documentation are always English. User input may be in any language.

## Outside the first version

Replanning, real recurrence, import from/export to calendar (.ics), Google Calendar, sync between machines,
writing through the read-only edition, notifications.

## Phases

1. **Core** (Mistral builds): data model and store, AI engines with Mistral and Claude, input and proposal,
   the four views, the whole command line except `serve`, live updates from the CLI, agent skill, tests.
2. **Second pass** (Claude): review and improvement of phase 1, `omaplan mcp`, compare mode,
   polishing the views, Danish translation.
3. **Read-only edition**: `snapshot` and `serve`.
4. **Independent review** (Codex) before installation.
