# Omaplan – architecture

Built after Omarchy's `omarchy-make-app` skill (C++ and Qt Quick, CMake, one binary, packaged from omarchy-pkgs).
The skill ships with Omarchy (`default/agents/skills/omarchy-make-app/`: `SKILL.md`, `templates.md`, `patterns.md`)
and is the binding source for project shape, theme, language and conventions. The files it marks as shared are kept
byte for byte: `bin/build`, `bin/test`, `.github/workflows/test.yml` and `src/translator.{h,cpp}`. This document only describes what is particular to Omaplan.

## Modules (flat `src/`)

| File | Responsibility |
|---|---|
| `main.cpp` | setup; chooses CLI or GUI (`open`); global `--db` and `--lang`; gives C++ objects to QML |
| `actions.{h,cpp}` | every action (create, edit, move, finish, delete, undo, list, summarize) in one place; CLI, MCP and QML only call through here; also the date parser (`parseDateTime`) |
| `cli.{h,cpp}` | the command line over `actions`, like Hype's `cli.cpp`; English flags, the Danish ones of 0.1 as hidden aliases |
| `mcp.{h,cpp}` | `omaplan mcp`: MCP over stdio (JSON-RPC), tools = `actions` |
| `skill.{h,cpp}` + `skill/SKILL.md` | bundled agent skill and `skill install/uninstall` |
| `config.{h,cpp}` | `~/.config/omaplan/config.toml`, parsed by hand (no TOML dependency); old Danish section names as aliases. `ConfigKeys` lists every setting with its allowed values; `ConfigFile` changes one line in place, keeping comments and unknown keys |
| `actions_config.cpp` | the settings as actions: `configList/Get/Set`, `engines` (is each tool installed, which model), and the panel's Test (`guiTestEngine`, one short request per engine) |
| `settings.{h,cpp}` | `config.toml` as the window sees it: watched (file and directory, debounced), exposed to QML (values, date convention, project colours), sets the process's `DateConvention`; opens the file in `$EDITOR` |
| `task.{h,cpp}` | the value type `Task` and its validation; date/time as `QDate`/`QDateTime` in local time |
| `store.{h,cpp}` | SQLite via Qt SQL (`QSQLITE`), schema version and migration, transactions; the only place that writes data |
| `planmodel.{h,cpp}` | `QAbstractListModel` over the tasks with roles; filtering/grouping via proxy models per view |
| `engine.{h,cpp}` | abstract `AiEngine` (asynchronous `propose(text, context)` → proposal or error), timeout and cancel |
| `engine_vibe.cpp` | Mistral via `vibe --prompt-file <private temp file> --output json --max-turns 1 --system-prompt-file …`, tools disabled (the Pro quota; no key in the app) |
| `engine_claude.cpp` | Claude via `claude -p --output-format json` with the prompt on stdin and no tools, MCP servers, settings files or slash commands |
| `engine_local.cpp` | a local model over HTTP: Ollama's `/api/chat` or an OpenAI-compatible `/v1/chat/completions`; with `api = "omarchy"` the model Omarchy runs, asked with Omarchy's key |
| `omarchy.{h,cpp}` | what is read from Omarchy about AI: the default agent (`~/.config/omarchy/defaults/agent`), the local models' status file and their key file; read at each use, never stored |
| `engine_command.cpp` | any command: prompt on stdin, JSON on stdout |
| `proposal.{h,cpp}` | parses and validates the engine's JSON against `docs/ai-schema.json`; marks guessed fields; also accepts the old Danish keys from a model and always emits English |
| `prompt.{h,cpp}` | builds the prompt (in English): today, time zone, interface language, existing tasks in the period, the schema; tells the model to keep titles and notes in the user's language |
| `dateannotate.{h,cpp}` | before the AI call, resolves date expressions in the text and writes them in brackets (`tomorrow at 14:30 [Tuesday 2026-11-24 14:30]`), so small models only copy the ISO date |
| `datewords.{h,cpp}` | the words the date parser and the annotation understand: one `DateWords` entry per language (Danish and English); every language is understood at once. Also `DateConvention` (day/month order, what 12.12 is, week start, clock) per language, overridden by `[dates]` |
| `snapshot.{h,cpp}` | writes `plan.json` atomically (QSaveFile) once after every change (`Store::committed`), from GUI, CLI and MCP |
| `render.{h,cpp}` | the read-only edition's HTML from `plan.json`; port of `renderPage` in `web/app.js`, byte-identical with `web/index.html` and `web/index.da.html` (test) |
| `server.{h,cpp}` | `serve`: small HTTP server over `QTcpServer`, GET/HEAD only, server-rendered HTML in the language of `Accept-Language`, plus `/plan.json`, `/style.css`, `/app.js`, `/i18n/<lang>.json` (Qt resources from `web/`) |
| `theme.{h,cpp}` | Omarchy's theme, followed live; from the skill's templates. Also the text size (`[font] base-size` in Omarchy's `shell.toml`, as `textScale`) and Hyprland's corner rounding, re-read on its `configreloaded` event. Also Omarchy's font: fontconfig's `monospace` resolved afresh (`FcInitLoadConfigAndFonts`), re-read when `~/.config/fontconfig/fonts.conf` changes (`omarchy-font-set`), as Folio does |
| `uilanguage.{h,cpp}` | which interface language applies, its locale and layout direction (see Language below) |
| `translator.{h,cpp}` | Omarchy's shared `Translator`: reads the JSON catalogs, per-string fallback, plural forms; kept byte for byte |
| `Main.qml` + components | window, keys, the four views, input, proposal, compare; `SettingsOverlay.qml` is the settings panel |

The engines run as `QProcess`/network asynchronously; the GUI never freezes. Timeout (default 90 s) and cancel (Esc)
are required. A missing tool (e.g. `vibe` not installed) is shown in the window with what is missing.

## Data

- `~/.local/share/omaplan/omaplan.db` (SQLite, WAL; `--db` or `OMAPLAN_DB` moves it). `~/.config/omaplan/config.toml`
  for language, engine choice and models (`OMAPLAN_CONFIG` moves it).
- `~/.local/share/omaplan/plan.json` for the read-only edition (next to the database; `[snapshot] path` in
  `config.toml` or `OMAPLAN_SNAPSHOT` moves it). Format: `docs/export.md`.
- Raw inputs are stored (the table `captures`) with engine and model, so a proposal can be traced.
- Undo through a change log in the database (the table `changes` with before/after and source gui/cli/mcp), so `undo`
  works across processes.
- The GUI notices changes from other processes through `PRAGMA data_version` (polled every 500 ms) and reloads the
  model.

## Time

Everything is stored as local time with a time zone id; shown through `QLocale` in the interface language. How dates
are read and shown is one `DateConvention` (`datewords.h`): the language's defaults (Danish and English alike: day/month,
12.12 a time, the week starts on Monday, 24-hour clock), each overridable in `[dates]` in `config.toml`.
`DateConvention::active()` is set once from the config (CLI, MCP) or by `Settings` whenever the file changes (GUI);
`parseDateTime`, the date annotation, `Fmt.qml` and the read-only edition (`clock` in `plan.json`) follow it. The prompt gets "today" and the weekday explicitly, and the date annotation has already resolved
expressions like "on Tuesday".

## Language

`UiLanguage` picks the interface language: `--lang` > `language` in `config.toml` > Omarchy's language file
(`~/.config/omarchy/language`) > the system locale (only with a catalog) > English. A flag, config or Omarchy
choice counts even without a catalog: strings stay English, the locale and direction follow the language. The window switches live when the Omarchy language file changes.

Translated: the GUI, the CLI's human-readable output, error messages and date formats (`QLocale`). Not translated
(always English): `--json` output, JSON field names, MCP tool names and descriptions, code and documentation.

UI strings are English source strings in `qsTr()`/`tr()`. Translations are Omarchy's JSON catalogs,
`src/i18n/<code>.json`, keyed by the English text and embedded in the binary. The shared `Translator` reads them
with a per-string fallback to English, and `UiLanguage` (`src/uilanguage.cpp`) decides which language applies and
sets the locale and the layout direction. `en.json` lists every string and carries the English plural forms. The
read-only web page has its
own small dictionaries, `web/i18n/<lang>.json`. See `docs/translating.md`.

## Test

`bin/test` (Qt Test, offscreen): store and migration, `proposal` validation with good and bad answers (fixtures
from each engine), prompt building, date parsing and annotation, snapshot atomicity, CLI exit codes, the rendered
page against `web/index.html`/`web/index.da.html`, and that each engine can be replaced by a fake. `ctest` also runs the desktop tests
(theme, text size, Hyprland events, and the catalogs: every string listed, Danish complete), the window test, which
fails on any QML warning, and `qmllint`. No test calls a real AI. A separate `bin/smoke-ai` calls the
real engines by hand.

## Dependencies

Omarchy's standard Qt modules. `serve` uses `QTcpServer` from Qt Network, so there is no extra dependency. `qt6-svg`
draws the icon. Building needs `cmake`; `qt6-tools` is no longer needed. Nothing else without an ADR.
