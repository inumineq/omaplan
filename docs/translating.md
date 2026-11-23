# Translating Omaplan

Omaplan's source language is English. The interface is translated with Qt's own tools; a translation is a data file
in `i18n/`, compiled into the binary at build time. No code change is needed for the app itself.

What is translated: the window, the CLI's human-readable output, error messages and date formats (through
`QLocale`). What is not: `--json` output, JSON field names, MCP tool names and descriptions, code and documentation;
those stay English.

## Files

- `i18n/omaplan_<lang>.ts` – one Qt Linguist file per language (`<lang>` is an ISO 639-1 code, e.g. `da`, `pt`).
  `omaplan_da.ts` is complete. `omaplan_en.ts` only carries the English plural forms ("%n tasks" → "1 task").
- `omaplan.pro` picks up every `i18n/omaplan_*.ts` (`TRANSLATIONS = $$files(i18n/omaplan_*.ts)`), and
  `CONFIG += lrelease embed_translations` compiles them to `.qm` and embeds them as `:/i18n/omaplan_<lang>.qm`.
  Building needs `lupdate`/`lrelease` from `qt6-tools`; running does not.
- `bin/i18n-update [lang…]` refreshes the `.ts` files from the sources with `lupdate` (every `qsTr`/`tr` string in
  `src/`), and creates the file for each new language given.
- `bin/i18n-check` fails when a string has no translation or a `.ts` file is out of date. `bin/test` runs it.

## Adding a language

1. `bin/i18n-update pt` creates `i18n/omaplan_pt.ts` with every string marked unfinished.
2. Translate it in Qt Linguist (`linguist6 i18n/omaplan_pt.ts`) or by editing the XML: fill in each
   `<translation>` and remove `type="unfinished"`. Keep placeholders (`%1`, `%n`) and fill in every plural form
   for `%n` strings.
3. `bin/test` builds the app with the new language embedded and runs `bin/i18n-check`.
4. Try it: `./build/omaplan --lang pt` (or `language = "pt"` in `config.toml`).
5. Open a pull request.

The language is picked from `--lang`, `language` in `config.toml`, Omarchy's language file, the system locale, then
English; a language without a translation passes to the next source, so a partial setup never breaks the app.

When you change or add a UI string in the code, run `bin/i18n-update` and translate the new entries in every
`.ts` file, or `bin/test` fails.

## Other places with language

These are separate from the Qt translations and do need small code or data changes:

- **Date input.** The date parser and the date annotation understand every language in `src/datewords.cpp` at
  once (today Danish and English), whatever the interface language. Adding a language there means adding one
  `DateWords` entry (words for today, tomorrow, weekdays, months, "next", "in N days", clock phrases) and a test in
  `tests/omaplan_tests.cpp`.
- **The read-only web page** (`omaplan serve`, `web/`) has its own dictionaries, `web/i18n/<lang>.json`, with the
  same keys as `web/i18n/en.json`. A new page language also has to be listed in `src/web.qrc`, in
  `Render::kLanguages` (`src/render.h`) and in `LANGUAGES` in `web/app.js`. Then add a reference page (`web/index.<lang>.html`
  via `node web/test.mjs --update`, the `pages` map in `web/test.mjs`, and a row in `renderMatchesWebReference_data`
  in `tests/omaplan_tests.cpp`). See `web/MARKUP.md`.
- **The AI prompt** is English and needs no translation: it tells the model to keep titles and notes in the
  language the user wrote them in.
