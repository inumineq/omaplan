# Contributing to Omaplan

Thanks for helping. This file covers how to build Omaplan, how the code is laid out and what a good pull request
looks like.

## What Omaplan is

Omaplan is a planning app for [Omarchy](https://omarchy.org). You write or dictate tasks in plain text, an AI engine
of your choice turns them into dated tasks, and you see the plan as an overview, calendar, list or timeline. The
window, the command line, the MCP server and the read-only web page all share one SQLite database.

It follows the Omarchy spirit:

- **Small, fast, light.** One small C++17 / Qt Quick binary, built with qmake6. It uses only the Qt modules Omarchy
  already ships. A new dependency needs an ADR in `docs/adr/` first.
- **Free choice of AI engine.** Vibe, Claude, any local model via Ollama or an OpenAI-compatible endpoint, or any
  command that reads a prompt and writes JSON. No API keys live in the app, and nothing calls a model you did not
  configure.
- **No bundled model.** Omaplan never downloads or ships a model. Model names in the docs are examples only.
- **Keyboard first, agent first.** Everything the window can do, the CLI (with `--json`) and `omaplan mcp` can do
  too. All three go through `src/actions.cpp`, so they cannot drift apart.

Read `SPEC.md`, `docs/architecture.md` and the ADRs in `docs/adr/` before larger changes. Planned work is in
[`docs/roadmap.md`](docs/roadmap.md); open an issue before starting on an item.

## Build and run from source

You need `qt6-base`, `qt6-declarative`, `qt6-wayland`, `qt6-tools` (for `lupdate`/`lrelease` at build time only)
and a C++17 compiler (`gcc`, `make`).

```sh
bin/build                    # qmake6 + make into build/
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

`bin/test` builds the app, checks that every UI string is translated (`bin/i18n-check`), runs the Qt Test suites
offscreen and, when Node is installed, the read-only page's JavaScript tests (`node web/test.mjs`). Node is not a
build dependency, but please run those tests before a pull request that touches `web/`, `src/render.*` or the
export format. CI (`.github/workflows/test.yml`) runs `bin/test`, Node included, in an Arch container on every
pull request.

No test calls a real AI. Engines are replaced by fakes or by the `command` engine with a fixture. To try a real
engine by hand, run `bin/smoke-ai [engine]` with a temporary `OMAPLAN_DB`. It is never part of `bin/test`.

## Build and install the package locally

```sh
bin/install                  # bin/build, then makepkg -fsi in pkgbuild/
```

This installs `/usr/bin/omaplan` plus the launcher entry and icon, so Omaplan shows up in the app launcher.

## Code style

Follow the code around you. In short:

- **English everywhere:** code, identifiers, comments, docs, commit messages, CLI help, MCP descriptions and the
  bundled skill (`src/skill/SKILL.md`). User input may be in any language.
- **Every UI string goes through `qsTr()` (QML) or `tr()` (C++)**, including the CLI's human-readable output and
  error messages. The English text is the source string. After adding or changing one, run `bin/i18n-update` and
  translate the new entries, or `bin/test` fails. Do not translate `--json` output, JSON field names or MCP tool
  names and descriptions; they are a stable, English contract.
- **Flat `src/`.** One module per concern (`store`, `actions`, `cli`, `mcp`, `engine_*`, ...); see the table in
  `docs/architecture.md`. Only the store writes data. CLI, MCP and QML call `actions`, never the store directly.
- **C++:** C++17, `#pragma once`, four-space indent, `m_` prefix for members, `QStringLiteral` for literals.
  Engines and network calls are asynchronous; the GUI never blocks.
- **QML:** one component per `PascalCase.qml` file in `src/`. Colours come from the Omarchy theme, never hard-coded,
  and every `Text` binds `font.family: theme.fontFamily` (a test checks both).
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
   then run `bin/i18n-update` and translate the changed strings.
3. If the tool wraps its answer in an envelope, teach `parseProposal()` in `src/proposal.cpp` to unwrap it.
4. Add the source file to `omaplan.pro`, `tests/omaplan_tests.pro` and `tests/unit/unit.pro`.
5. Add `tests/ai-fixtures/envelopes/<name>-valid.json` and `<name>-invalid.json` and list them in
   `tests/unit/tst_ai.cpp` (`envelopeFixturesParse`, `invalidEnvelopeFixturesAreRejectedWithRaw`). Test the engine
   with a fake, never a real model.
6. Document it under "AI engines" in `README.md`, including any `[engine.<name>]` section in `config.toml`.

No API keys in the app, the config example or the repository. If the engine needs a login, rely on the tool's own.

### Add a UI language

For the window and the CLI, a new language is a data-only pull request: `bin/i18n-update <lang>` creates
`i18n/omaplan_<lang>.ts`, you translate it in Qt Linguist or by editing the XML, and `bin/test` checks it. The build
picks up every `i18n/omaplan_*.ts` by itself. The read-only web page has its own dictionaries in `web/i18n/`, which
need a few small code changes as well. [`docs/translating.md`](docs/translating.md) walks through both.

### Add a Danish or English date rule

The date parser (`Actions::parseDateTime()` in `src/actions.cpp`) and the date annotation for the AI
(`annotateDates()` in `src/dateannotate.cpp`) read their words from `src/datewords.cpp`: one `DateWords` entry per
language with phrases for today, tomorrow, next week, weekdays, months, "in N days" and clock times. Every language
is understood at once, whatever the interface language.

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
