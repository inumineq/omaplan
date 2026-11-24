# Translating Omaplan

Omaplan's source language is English. The interface is translated the way Omarchy's own apps are: one JSON catalog
per language in `src/i18n/`, compiled into the binary, read by the `Translator` class every Omarchy app shares
(`src/translator.{h,cpp}`, copied unchanged from the `omarchy-make-app` skill). No code change is needed for a new
language.

What is translated: the window, the CLI's human-readable output, error messages and date formats (through
`QLocale`). What is not: `--json` output, JSON field names, MCP tool names and descriptions, code and documentation;
those stay English.

## Files

- `src/i18n/en.json` lists every string in the app, keyed by the English source text. It is the list a translation
  is checked against, and it carries the English plural forms.
- `src/i18n/<code>.json` is one language (`da`, `pt`, or a regional code such as `pt-BR`). The keys are the same
  English texts; the values are the translations. `da.json` is complete.
- `src/resources.qrc` lists every catalog, so it ends up in the binary as `:/i18n/<code>.json`.

A plain string is `"English text": "Translated text"`. A string that counts something (it contains `%n`) is an
object with the plural forms the language has, by their CLDR names (`one` and `other` for English and Danish; other
languages may also need `zero`, `two`, `few` or `many`):

```json
"%n tasks": { "one": "%n opgave", "other": "%n opgaver" }
```

Keep every placeholder (`%1`, `%2`, `%n`) and every line break (`\n`) of the source text.

## Which language is shown

Omaplan follows the language picked in Omarchy (Setup > Region > Language, stored in `~/.config/omarchy/language`),
and the open window switches when that file changes. `--lang` on the command line and `language` in `config.toml`
override it. Without any of the three, the system locale counts when it names a language with a catalog; otherwise
the interface is English.

Fallback is per string: a string a catalog does not have shows in English, so a partial translation never breaks
the app. A language without a catalog still gets its own date and number formats, and right-to-left languages
(Arabic, Hebrew, Persian, Urdu) mirror the layout.

## Adding a language

1. Copy `src/i18n/en.json` to `src/i18n/<code>.json` and translate the values. Leave the keys alone.
2. Add the file to `src/resources.qrc`, next to the others.
3. `bin/test` checks the catalog: every key must exist in `en.json`, placeholders must match, and counted strings
   must have their plural forms.
4. Try it: `./build/omaplan --lang <code>` (or `language = "<code>"` in `config.toml`).
5. Open a pull request.

When you change or add a UI string in the code, add it to `src/i18n/en.json` (the value is the same text, or the
plural forms) and translate it in `src/i18n/da.json`, or `bin/test` fails: one test finds every `qsTr()`, `tr()` and
`translate()` string in `src/` and compares it with `en.json`, and another requires the Danish catalog to be
complete. Other languages may lag behind; their missing strings show in English.

## Other places with language

These are separate from the catalogs and do need small code or data changes:

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
