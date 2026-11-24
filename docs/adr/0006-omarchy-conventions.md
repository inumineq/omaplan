# 0006 – Built and translated the way Omarchy's own apps are

Status: decided 2026-11-24. Supersedes the translation format of [0005](0005-english-and-qt-translations.md).

Omaplan is meant to be packaged in omarchy-pkgs next to Omarchy's own apps, so it follows the `omarchy-make-app`
skill where it used to go its own way. The files the skill marks as shared are kept byte for byte, so a fix to one
of them can be copied in without a merge.

- **CMake instead of qmake.** `src/` is a library the app and the tests link, tests run through `ctest` offscreen,
  and `bin/build`, `bin/test` and `.github/workflows/test.yml` are the shared ones. The package recipe is no longer
  in this repository; it lives in omarchy-pkgs, and the CI workflow reads the dependency list from there.
- **JSON catalogs instead of Qt Linguist files.** `src/i18n/<code>.json`, keyed by the English text, read by the
  shared `Translator` with a per-string fallback to English. 0005 chose `.ts` files because they are Qt's own
  format and needed no code; the catalogs win because they are what every other Omarchy app uses, a translator
  edits one flat file without Qt tools, and `qt6-tools` stops being a build dependency. A language picked in
  Omarchy now counts even without a catalog: the strings stay English, while dates, numbers and the layout
  direction follow the language.
- **Text size** follows `[font] base-size` in Omarchy's `shell.toml`, the value `omarchy display text size` writes,
  watched like the theme. Every `font.pixelSize` is multiplied by `theme.textScale`. This needs no portal or D-Bus
  dependency.
- **Corner rounding** follows Hyprland's `configreloaded` event on its event socket, not a timer.
- **AI defaults follow Omarchy.** With no `engine.default`, the engine is the one `omarchy default agent` names
  when Omaplan has it (`claude`, `local`), else `vibe`. The `local` engine uses the model Omarchy runs when one is
  set up and no endpoint is configured: the endpoint comes from Omarchy's status file, and Omarchy's API key is
  read from its file for each request, sent only to a loopback address and never stored. This keeps "no API keys
  in the app" true.

Kept on purpose:

- Commands other than the window use `QCoreApplication`, not a `QGuiApplication` on the offscreen platform. They
  never need a display at all, which is the outcome the skill's rule is after.
- `--lang` and `language` in `config.toml` still override Omarchy's language, for scripts and for a machine
  without Omarchy.
- English formats dates the British way (day before month, 24-hour clock) unless `[dates]` says otherwise, as
  before.
