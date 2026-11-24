# Omaplan

Planning for Omarchy: write or dictate your tasks in plain text, let an AI put them on dates, and see the plan as an
overview, calendar, list or timeline. The GUI and the command line share the same database, so changes from the CLI
or an agent show up in the window at once (and can be undone from there).

Build with `bin/build`, run the tests with `bin/test`, install as an Arch package with `bin/install`. See `SPEC.md`,
`docs/architecture.md` and, for what is planned, `docs/roadmap.md`.

## Keys (in the window)

Everything can be done with the keyboard. `?` (or the small `?` at the top right) shows every key in the window, and
hovering a button shows its key. `,` (or the gear beside it) opens the settings.

| Key | Action |
| --- | --- |
| `1` / `2` / `3` / `4` | Switch view: overview, calendar, list, timeline |
| `n` or Space | New tasks in plain text (AI proposal) |
| `a` | New task without AI |
| `j` / `k` or ↓ / ↑ | Select next / previous task |
| `x` | Done / not done |
| `e` or Enter | Edit the selected task |
| `Shift+←` / `Shift+→` | Move a day |
| `Shift+↑` / `Shift+↓` | Move a quarter hour |
| `Delete` or `d` | Delete |
| `h` / `l` or ← / → | Previous / next week or month |
| `t` | Go to today |
| `w` / `m` | Calendar: week / month |
| `g` / `s` / `f` | List: grouping / open-done-all / project filter |
| `+` / `-` | Timeline: zoom |
| `Ctrl+Enter` | Send input / approve proposal / save |
| `Alt+S` | Compare two engines (in the input) |
| `Ctrl+1` / `Ctrl+2` | Approve the left / right proposal when comparing |
| `Ctrl+N` / `Ctrl+Delete` | Add / remove a task in the proposal |
| `Tab` | Next field in the proposal |
| `Ctrl+R` / `Ctrl+E` | After an engine error: try again / create the task yourself |
| `Alt+←` / `Alt+→` | Compare: choose the left / right proposal |
| `Alt+1` / `Alt+2` / `Alt+3` | Editor: priority high / medium / normal |
| `Alt+X` | Editor: done / not done |
| `Ctrl+Delete` in the editor | Delete the task |
| `Esc` | Close / discard / cancel the engine (the draft is kept) |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo / redo (works across GUI, CLI and MCP) |
| `,` or `Ctrl+,` | Settings; there `Alt+1`/`2`/`3` picks the section, Tab and ← / → change a value, `Ctrl+E` opens `config.toml` |
| `q` | Quit |

With the mouse, tasks can be dragged to another day in the calendar, in the week view also to another time. In the
timeline, dragging the right edge of a bar changes the duration.

## Command line

Every command except `serve` and `mcp` takes `--json`, exits 0/1 and writes errors on stderr. Dates accept ISO
(`2026-12-01`, `2026-12-01 14:30`) and English or Danish expressions (`"tomorrow"`, `"friday 12"`,
`"next monday"`, optionally `"at 14:30"`; `"i morgen"`, `"kl. 14:30"`). An empty value clears a field:
`--project ""`.

```sh
omaplan add "write blog post friday at 14:30, 30 min, project Blog"   # AI proposal (shown first)
omaplan add "buy milk tomorrow" --yes --json                           # store directly
omaplan new --title "Buy milk" --start tomorrow --project Errands
omaplan list --from today --to "next friday" --json
omaplan show <id>  ·  omaplan edit <id> --deadline friday
omaplan move <id> "tuesday at 14:00"  ·  omaplan done <id>  ·  omaplan rm <id>
omaplan projects  ·  omaplan summary --week --json
omaplan undo --json
omaplan config list           # every setting, its value and the allowed values (also config get|set|unset|path)
omaplan config set engine.claude.model sonnet
omaplan engines --json        # which AI tools are installed, and the model each one uses
omaplan skill install         # bundled agent skill in ~/.claude/skills, ~/.codex/skills, ~/.vibe/skills, ...
omaplan skill install --force # also replace customized SKILL.md files
omaplan snapshot              # write plan.json for the read-only edition now
omaplan serve                 # the read-only edition at http://127.0.0.1:8787/
```

Global options: `--db <path>` uses another database, `--lang da|en` picks the interface language (see
[Language](#language)). The Danish flags of 0.1 (`--titel`, `--frist`, `--ja`, ...) still work as hidden
aliases for backward compatibility.

## Live

The GUI polls `PRAGMA data_version` every 500 ms and reloads on changes from other processes, with a short notice
about the source ("Changed from the command line"). Concurrent writes are safe (WAL + busy timeout).

## MCP

`omaplan mcp` is an MCP server over stdio (JSON-RPC 2.0) with the same actions as the command line as tools:
`list`, `show`, `new`, `edit`, `move`, `done`, `undone`, `rm`, `undo`, `redo`, `projects`, `summary`, `propose`,
`approve` and `add`, plus `config_list`, `config_get`, `config_set` and `engines` for the settings. An agent may
set the language, `dates.*`, `projects.*`, the model names, the timeout, the default engine and compare, but not
what runs or where data goes (`engine.command`, `engine.local.endpoint`, `engine.local.api`, `snapshot.path`);
those take `omaplan config set` or the settings panel. It can pick the `command` engine only once you have set its
command line yourself. The arguments are named like the CLI flags (`title`, `start`, `duration`, `deadline`,
`project`, `from`, `to`, ...; `when` for `move`, `period` = `week`|`month` for `summary`, `text` and `engine` for
`propose`/`add`), and dates accept the same expressions. Changes get the source `mcp`, show up live in the window
and can be undone from there.

Add it to Claude Code:

```sh
claude mcp add omaplan -- omaplan mcp
```

`propose` stores no tasks (only the text and the answer, as a capture): show the proposal to the user, and store it
with `approve` (with `capture_id` from the answer). `add` stores at once and is only for when the user has asked for that. Tasks in `--json` and MCP answers
list the fields the AI guessed under `guessed`, the same key as in proposals and `plan.json`.

## Read-only edition (phone and other machines)

After every change (from the window, the CLI or MCP) Omaplan writes a snapshot, `plan.json`, atomically next to the
database: `~/.local/share/omaplan/plan.json`. The format is in `docs/export.md`. If you want it somewhere else, set
it in `config.toml` or with `OMAPLAN_SNAPSHOT`:

```toml
[snapshot]
path = "~/Sync/omaplan/plan.json"
```

The configured path belongs to the standard database; with `--db` or `OMAPLAN_DB` the snapshot goes next to that
database instead, unless `OMAPLAN_SNAPSHOT` is set.

`omaplan snapshot [path]` writes it at once (e.g. the first time, or to another path).

`plan.json` is private (0600): only the user that writes it can read it, so a `serve` running as another user or
service needs a copy it owns, e.g. with rsync as below.

`omaplan serve [--port N] [--bind ADDR] [--host NAME,...] [--snapshot PATH]` shows the snapshot as a small,
server-rendered page with overview, list and timeline, plus `/plan.json`. It only reads `plan.json` (never the
database), cannot write, and the page works without JavaScript. It binds to `127.0.0.1:8787`; share it on your
tailnet with `tailscale serve`, so access to the page equals access to the tailnet. It answers only to `localhost`,
IP addresses, the machine's own name, `<machine>.local` and `<machine>.<tailnet>.ts.net`; add other names with
`--host`, so a web page cannot read the plan by pointing its own name at `127.0.0.1`:

```sh
omaplan serve &                  # http://127.0.0.1:8787/
tailscale serve --bg 8787        # https://<machine>.<your-tailnet>.ts.net/ on the tailnet
```

If `serve` runs on another machine (here called `server`), move the snapshot there, e.g. with rsync over Tailscale
(or Syncthing, if you already use it):

```sh
# on the machine with Omaplan: send plan.json whenever it changes
while inotifywait -qq -e close_write,moved_to ~/.local/share/omaplan/; do
  rsync -az ~/.local/share/omaplan/plan.json server:~/omaplan/plan.json
done

# on server
omaplan serve --snapshot ~/omaplan/plan.json
tailscale serve --bg 8787
```

The page picks a view with anchors (`#overview`, `#list`, `#list-project`, `#timeline`) or a link like
`/?view=timeline`. It is in English or Danish, following the browser's language (`Accept-Language` on the server,
`navigator.language` in `web/app.js`). The colours and the font follow the Omarchy theme and font on the machine
running `serve`, and otherwise the theme as it was when the snapshot was written; a device without that font
shows its own monospace. The markup is described in `web/MARKUP.md`.

## Agent skill

`omaplan skill install` puts the bundled skill (`src/skill/SKILL.md`) in the agent directories that exist
(`~/.claude/skills`, `~/.codex/skills`, `~/.vibe/skills`, `~/.config/opencode/skills`), so agents know when to use
Omaplan. An existing customized `SKILL.md` is kept with a conflict message; use `omaplan skill install --force` to
replace it. `omaplan skill uninstall` removes it again.

## AI engines

`omaplan add "<text>"` (or `n`/Space in the window) asks an engine for a proposal. Engines: `vibe` (Mistral Vibe
CLI, default), `claude` (Claude CLI), `local` (any local model via Ollama or an OpenAI-compatible endpoint) and
`command` (any command, mostly for tests).

Before the text goes to the engine, Omaplan resolves date expressions itself and writes them into the text, e.g.
"tomorrow at 14:30" becomes "tomorrow at 14:30 [Tuesday 2026-11-24 14:30]", so small models only have to copy the
date. The prompt is in English and tells the model to keep titles and notes in the language you wrote them in.

## Settings

The settings live in `~/.config/omaplan/config.toml` (or `$OMAPLAN_CONFIG`), and nowhere else. There are three
ways to change them, and all three write the same file, keeping its comments and any keys Omaplan does not know:

- the settings panel in the window (`,`), which saves every change at once;
- `omaplan config set <key> <value>` (or the MCP tool `config_set`); `omaplan config list` shows every key;
- your editor (`Ctrl+E` in the panel opens it with `$EDITOR`).

The open window follows the file live, whichever way it changed.

```toml
language = "da"            # interface language: da | en (default: see Language)

[engine]
default = "vibe"           # default engine: vibe | claude | local | command
command = "..."            # the command for default = "command"
compare = "vibe, claude"   # the two engines Compare (Alt+S) sends the text to
timeout = 90               # seconds an engine may take (10-600)

[engine.vibe]
model = ""                 # passed to vibe --model; empty uses vibe's own

[engine.claude]
model = "sonnet"           # passed to claude --model; empty uses claude's default

[engine.local]
api = "ollama"             # ollama | openai (OpenAI-compatible endpoint)
endpoint = "http://127.0.0.1:11434"
model = "your-choice"      # free choice -- nothing is hardcoded or built in

[dates]                    # each one: leave it out to follow the language
order = "dmy"              # how 12/11 is read: dmy (12 November) | mdy (11 December)
dotted = "time"            # what 12.12 is: time (12:12) | date (12 December; "14.30", "kl. 9.05"
                           # and "2.30 pm" are still times, as they cannot be dates, and so is
                           # a last one after a date: "12.12 10.05", "friday 9.05")
week_start = "monday"      # monday | sunday
clock = "24h"              # 24h | 12h (2:30 PM)

[projects]                 # a colour per project: a slot in the theme's palette
Blog = "blue"
"Client A" = "green"
```

Project colours are slots in the Omarchy theme's palette (`red`, `orange`, `yellow`, `green`, `cyan`, `blue`,
`magenta`, `brown` and their `bright_` variants; `color1`..`color14` are read as the same slots), so they follow a
theme switch and are lifted until they read on the background. A project's colour shows on its tag and on its blocks
and bars in the calendar and timeline, in the window and in the read-only edition; the priority edge stays as it is.

Both languages read 12/11 as day/month and 12.12 as a time, start the week on Monday and use a 24-hour clock; the
`[dates]` keys change that. Omaplan never stores API keys: the `vibe` and `claude` tools keep their own login.

The section and key names of 0.1 (`[motor]`, `engine = ...`, `[motor.lokal]`, the engine id `lokal`) are
still read.

`--engine`, `--model` and `--endpoint` on the command line win over the config. If there is neither a model nor an
endpoint for `local`, you get an error with an example instead of a guess.

**Models are only examples** -- choose for yourself, e.g. `gemma4:e2b` or `qwen3.5:2b` (for Ollama), or whatever
your llama-server/LM Studio provides. Omaplan does not download or build any models itself.

**Compare:** Press `Alt+S` in the input, and the text goes to both `compare` engines at once. The two proposals are
shown side by side, and you approve one with `Ctrl+1` or `Ctrl+2`. Both answers are stored in `captures`, so they can
be compared afterwards.

In the settings panel, **Test** next to an engine sends one short request and shows the answer and the time, or why
the engine cannot run (for example that the tool is not on `PATH`). It runs only when pressed, and stores nothing.

`bin/smoke-ai [engine]` calls a real engine by hand with a short Danish text and prints the answer and the time. It
is not run by `bin/test`.

## Language

The interface is in English or Danish. The language is the first of these that has a translation: `--lang`,
`language` in `config.toml`, Omarchy's language (`~/.config/omarchy/language`), the system locale, English. The open
window switches live when the Omarchy language changes. The window, the CLI's human-readable output, error messages
and date formats are translated; `--json` output, JSON field names and the MCP tools are always English.

Translations are Qt Linguist files in `i18n/`, compiled into the binary. To add a language, see
`docs/translating.md`.

## License

Omaplan is released under the [MIT License](LICENSE).
