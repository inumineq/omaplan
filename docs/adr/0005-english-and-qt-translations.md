# 0005 – English in the repository, Qt's own tools for translations

Status: decided 2026-11-23. The choice of Qt Linguist files is superseded by
[0006](0006-omarchy-conventions.md); the rest stands.

The repository is public, so everything in it is in English: code, identifiers, comments, documentation, commit
messages, CLI flags and help, JSON keys, MCP tools and the agent skill. Danish was the original language; it is now
one translation among others.

- UI strings are English source strings in `qsTr()`/`tr()`. Translations are Qt Linguist files
  `i18n/omaplan_<lang>.ts`, compiled by `lrelease` during the build and embedded in the binary, so there is no
  runtime dependency and a new language is a data file and a pull request, no code change. `bin/i18n-check` (run
  by `bin/test`) fails on a missing translation.
- The interface language is `--lang` > `language` in `config.toml` > Omarchy's language file > the system locale >
  English. The GUI, the CLI's human-readable output, error messages and date formats are translated; `--json`,
  JSON field names and MCP tools are always English, so agents and scripts see one stable interface.
- The date parser understands Danish and English input at once, whatever the interface language. The AI prompt is
  English and tells the model to keep the user's own words in the user's language.
- `plan.json` became format version 2 with English keys. The read-only web page has its own small dictionaries,
  `web/i18n/<lang>.json`, and follows the browser's language.

Backward compatibility: the Danish CLI flags, config section names (`[motor]`, `[motor.lokal]`), the engine id
`lokal` and the Danish keys in a model's answer are still read as aliases. `plan.json` version 1 is not.
