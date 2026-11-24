# Omaplan

Planning for Omarchy: write or dictate your tasks in plain text, let an AI put them on dates, and see the plan as an
overview, calendar, list or timeline. The GUI and the command line share the same database, so changes from the CLI
or an agent show up in the window at once (and can be undone from there).

Build with `bin/build`, run the tests with `bin/test`, install as an Arch package with `bin/install`. See `SPEC.md`
and `docs/architecture.md`.

## Keys (in the window)

Everything can be done with the keyboard. `?` shows every key in the window.

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
| `q` | Quit |

With the mouse, tasks can be dragged to another day in the calendar, in the week view also to another time. In the
timeline, dragging the right edge of a bar changes the duration.

## Command line

Every command takes `--json`, exits 0/1 and writes errors on stderr. Dates accept ISO
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
`approve` and `add`. The arguments are named like the CLI flags (`title`, `start`, `duration`, `deadline`,
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

`omaplan serve [--port N] [--bind ADDR] [--host NAME,...] [--snapshot PATH]` shows the snapshot as a small,
server-rendered page with overview, list and timeline, plus `/plan.json`. It only reads `plan.json` (never the
database), cannot write, and the page works without JavaScript. It binds to `127.0.0.1:8787`; share it on your
tailnet with `tailscale serve`, so access to the page equals access to the tailnet. It answers only to `localhost`,
IP addresses and the machine's own name (also as the first part of its tailnet name); add other names with
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
`navigator.language` in `web/app.js`). The colours follow the Omarchy theme on the machine running `serve`, and
otherwise the theme as it was when the snapshot was written. The markup is described in `web/MARKUP.md`.

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

The configuration is in `~/.config/omaplan/config.toml`:

```toml
language = "da"            # interface language: da | en (default: see Language)

[engine]
default = "vibe"           # default engine: vibe | claude | local | command
command = "..."            # the command for default = "command"
compare = "vibe, claude"   # the two engines Compare (Alt+S) sends the text to

[engine.local]
api = "ollama"             # ollama | openai (OpenAI-compatible endpoint)
endpoint = "http://127.0.0.1:11434"
model = "your-choice"      # free choice -- nothing is hardcoded or built in
```

The section and key names of 0.1 (`[motor]`, `engine = ...`, `[motor.lokal]`, the engine id `lokal`) are
still read.

`--engine`, `--model` and `--endpoint` on the command line win over the config. If there is neither a model nor an
endpoint for `local`, you get an error with an example instead of a guess.

**Models are only examples** -- choose for yourself, e.g. `gemma4:e2b` or `qwen3.5:2b` (for Ollama), or whatever
your llama-server/LM Studio provides. Omaplan does not download or build any models itself.

**Compare:** Press `Alt+S` in the input, and the text goes to both `compare` engines at once. The two proposals are
shown side by side, and you approve one with `Ctrl+1` or `Ctrl+2`. Both answers are stored in `captures`, so they can
be compared afterwards.

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
