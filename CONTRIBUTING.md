# Contributing to Omaplan

Thanks for helping. This file covers how to build Omaplan, how the code is laid out and what a good pull request
looks like.

## What Omaplan is

Omaplan is a planning app for [Omarchy](https://omarchy.org). You write or dictate tasks in plain text, an AI engine
of your choice turns them into dated tasks, and you see the plan as an overview, calendar, list or timeline. The
window, the command line, the MCP server and the read-only web page all share one SQLite database.

It follows the Omarchy spirit:

- **Small, fast, light.** One small C++17 / Qt Quick binary, built with CMake like Omarchy's own apps. It uses only the
  Qt modules Omarchy already ships. A new dependency needs an ADR in `docs/adr/` first.
- **Free choice of AI engine.** Vibe, Claude, a local model (the one Omarchy runs, Ollama, or any OpenAI-compatible
  endpoint), or any command that reads a prompt and writes JSON. No API keys live in the app, and nothing calls a
  model you did not configure. Until you choose, the engine follows `omarchy default agent`.
- **No bundled model.** Omaplan never downloads or ships a model. Model names in the docs are examples only.
- **Keyboard first, agent first.** Everything the window can do, the CLI (with `--json`) and `omaplan mcp` can do
  too. All three go through `src/actions.cpp`, so they cannot drift apart.

Read `SPEC.md`, `docs/architecture.md` and the ADRs in `docs/adr/` before larger changes. Planned work is in
[`docs/roadmap.md`](docs/roadmap.md); open an issue before starting on an item.

## Build and run from source

You need `cmake`, `qt6-base`, `qt6-declarative`, `qt6-svg`, `qt6-wayland`, `fontconfig` and a C++17 compiler
(`gcc`, `make`).

```sh
bin/build                    # cmake into build/
./build/omaplan              # the window
./build/omaplan list --json  # the CLI
```

Use a throwaway database while you develop, so you never touch your own plan:

```sh
export OMAPLAN_DB=/tmp/omaplan-dev.db
```

## Run the tests

```sh
bin/test
```

`bin/test` builds the app and runs every test offscreen with `ctest`: the Qt Test suites (one of them checks that
every UI string is in the catalogs and translated to Danish, another opens the real window and fails on any QML
warning), `qmllint` over `src/*.qml`, and the read-only page's JavaScript tests (`node web/test.mjs`, so Node is
needed for the tests but not for the build). `bin/test -V` shows each test's output. `bin/build`, `bin/test` and
the CI workflow (`.github/workflows/test.yml`) are the files every Omarchy app shares, kept unchanged. The workflow
runs `bin/test` in an Arch container and reads the dependencies from Omaplan's recipe in
[omarchy-pkgs](https://github.com/omacom/omarchy-pkgs), so until that recipe is published, `bin/test` on your own
machine is the check.

No test calls a real AI. Engines are replaced by fakes or by the `command` engine with a fixture. To try a real
engine by hand, run `bin/smoke-ai [engine]` with a temporary `OMAPLAN_DB`. It is never part of `bin/test`.

## Install a local build

```sh
cmake --install build --prefix ~/.local
```

This installs `~/.local/bin/omaplan` plus the launcher entry and icon (`share/`), so Omaplan shows up in the app
launcher. The package recipe is not in this repository: like every Omarchy app's, it lives in omarchy-pkgs.

## Code style

Follow the code around you. In short:

- **English everywhere:** code, identifiers, comments, docs, commit messages, CLI help, MCP descriptions and the
  bundled skill (`src/skill/SKILL.md`). User input may be in any language.
- **Every UI string goes through `qsTr()` (QML) or `tr()` (C++)**, including the CLI's human-readable output and
  error messages. The English text is the source string. After adding or changing one, add it to
  `src/i18n/en.json` and translate it in `src/i18n/da.json`, or `bin/test` fails. Do not translate `--json` output, JSON field names or MCP tool
  names and descriptions; they are a stable, English contract.
- **Flat `src/`.** One module per concern (`store`, `actions`, `cli`, `mcp`, `engine_*`, ...); see the table in
  `docs/architecture.md`. Only the store writes data. CLI, MCP and QML call `actions`, never the store directly.
- **C++:** C++17, `#pragma once`, four-space indent, `m_` prefix for members, `QStringLiteral` for literals.
  Engines and network calls are asynchronous; the GUI never blocks.
- **QML:** one component per `PascalCase.qml` file in `src/`. Colours come from the Omarchy theme, never hard-coded,
  every `Text` binds `font.family: theme.fontFamily`, and every `font.pixelSize` is multiplied by
  `theme.textScale`, so the window follows Omarchy's text size (a test checks all three). `qmllint` must stay clean. Words in the accent colour use
  `theme.accentText`; `muted`, `danger` and `success` are already lifted to 4.5:1, so light themes read too.
- **Comments say why**, not what.
- **Data contracts** (`docs/ai-schema.json`, `docs/export.md`, `docs/export-schema.json`) change in one commit
  together with their parser, tests and docs.

## Common contributions

### Add an AI engine

1. Create `src/engine_<name>.cpp` with a subclass of `AiEngine` (`src/engine.h`). Implement `id()`,
   `displayName()` and `start()`. For a CLI tool, call `setRequiredTool()` in the constructor and use `launch()`
   with `useIsolatedWorkingDirectory()`, so the tool does not read the repository's `AGENTS.md` as instructions.
   Timeout, cancel and error handling come from the base class. `src/engine_claude.cpp` is a short example.
2. Add a `create<Name>Engine()` factory to `src/engine.h` and register the id in `createEngine()` in
   `src/engine.cpp`. Add the id to the lists of valid engines there and in the `add` usage text in `src/cli.cpp`,
   then update the changed strings in `src/i18n/en.json` and `src/i18n/da.json`.
3. If the tool wraps its answer in an envelope, teach `parseProposal()` in `src/proposal.cpp` to unwrap it.
4. The build picks up every `src/*.cpp` by itself; run `bin/build` once so CMake sees the new file.
5. Add `tests/ai-fixtures/envelopes/<name>-valid.json` and `<name>-invalid.json` and list them in
   `tests/unit/tst_ai.cpp` (`envelopeFixturesParse`, `invalidEnvelopeFixturesAreRejectedWithRaw`). Test the engine
   with a fake, never a real model.
6. Document it under "AI engines" in `README.md`, including any `[engine.<name>]` section in `config.toml`.
7. For the settings: add its keys to `ConfigKeys::all()` in `src/config.cpp` (name, section, allowed values,
   description), and to the engine list of `Actions::engines()` in `src/actions_config.cpp`, so `omaplan config`,
   `omaplan engines` and the settings panel know it.

### Add a setting

Every setting is a line in `config.toml`, and the file is the only source. Add the key to `ConfigKeys::all()` in
`src/config.cpp` (with its allowed values, so `ConfigKeys::validate()` and `omaplan config list` know them), read it
in `AppConfig::load()`, and test the round trip next to `configWriteKeepsCommentsUnknownKeysAndOldNames`. To show it
in the panel, add a `ConfigField` or `ConfigChoice` with that key in `src/SettingsOverlay.qml`: it writes through
`actions.guiConfigSet` and reads `settings.values`, so the CLI, MCP and the panel stay in step. Every panel control
must be reachable with Tab.

No API keys in the app, the config example or the repository. If the engine needs a login, rely on the tool's own.

### Add a UI language

For the window and the CLI, a new language is a data-only pull request: copy `src/i18n/en.json` to
`src/i18n/<code>.json`, translate the values, list the file in `src/resources.qrc`, and `bin/test` checks it. The
read-only web page has its own dictionaries in `web/i18n/`, which
need a few small code changes as well. [`docs/translating.md`](docs/translating.md) walks through both.

### Add a Danish or English date rule

The date parser (`Actions::parseDateTime()` in `src/actions.cpp`) and the date annotation for the AI
(`annotateDates()` in `src/dateannotate.cpp`) read their words from `src/datewords.cpp`: one `DateWords` entry per
language with phrases for today, tomorrow, next week, weekdays, months, "in N days" and clock times. Every language
is understood at once, whatever the interface language. How numbers are read (12/11 as day/month or month/day,
12.12 as a time or a date) is the language's `DateConvention`, which `[dates]` in `config.toml` overrides; a test
that depends on it should set it explicitly (see `ConventionGuard` in `tests/omaplan_tests.cpp`).

Most rules are one more phrase in a list. To accept the Danish "i næste uge" next to "næste uge":

```cpp
// src/datewords.cpp, in danish()
w.nextWeek = {QStringLiteral("næste uge"), QStringLiteral("i næste uge")};
```

Then add a test to `tests/omaplan_tests.cpp`, next to `dateParsing()` and `dateAnnotation()`, with a fixed `today`:

```cpp
void dateRuleNextWeekWithI()
{
    const QDate today(2026, 11, 23); // a Monday
    QDateTime out;
    bool hasTime = false;
    QVERIFY(Actions::parseDateTime(QStringLiteral("i næste uge"), today, &out, &hasTime));
    QCOMPARE(out.date(), QDate(2026, 11, 30));
    QVERIFY(!hasTime);
    QCOMPARE(annotateDates(QStringLiteral("skriv nyhedsbrev i næste uge"), today),
             QStringLiteral("skriv nyhedsbrev i næste uge [Monday 2026-11-30]"));
}
```

Check that the test fails without your rule and passes with it. Also test what must *not* match: a bare number such
as "3 timer" is a duration, not a date, and stays unannotated. A new input language is a new `DateWords` entry in
`dateLanguages()` plus tests like these.

## Privacy: this is a public repository

Nothing personal goes into the repository: not in code, tests, fixtures, docs, screenshots, issues, commit messages,
branch names or pull requests.

- Use invented examples only: "Blog", "Client A", "Buy milk", "Write newsletter".
- No real names of people, organisations or workplaces, and no email addresses.
- No host names, IP addresses, tailnet names or home directories with a user name (write `~/…`).
- No real tasks, calendar data or AI answers produced from real data. Build fixtures by hand.
- Screenshots only with invented data and the default accent colour.

When you paste a log or a bug report, strip it first. If in doubt, leave it out.

## Issues and pull requests

- **Issues:** say what you did, what you expected and what happened, with the Omaplan version (package version or
  commit) and the engine. Reproduce it with invented example text.
- **One topic per pull request**, and keep it small. Keep refactoring separate from behaviour changes.
- **Describe what and why** in the PR description. Mention user-visible changes to the CLI, `--json`, MCP or the
  export format.
- **Tests green:** `bin/test` passes (with Node installed, so `web/test.mjs` runs too), and new behaviour comes with
  a test.
- **Docs follow code:** update `README.md`, `SPEC.md` or `docs/` in the same PR when behaviour changes.

## AI-assisted contributions

Contributions written with an AI assistant are welcome. You are the author: you have read and run the code, and you
vouch for it as if you had typed it yourself. Agents working in this repository follow `AGENTS.md`, the same privacy
rules included.

## License

Omaplan is released under the [MIT License](LICENSE), copyright inumineq. By contributing you agree that your
contribution is released under the same license. Code copied from elsewhere keeps its original notice; see
`LICENSE`.
