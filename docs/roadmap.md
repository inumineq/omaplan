# Roadmap

Planned work, roughly in order. Nothing here is promised for a date; open an issue before starting on an item, so
the approach can be agreed first.

1. **A package in omarchy-pkgs.** The recipe builds from a tagged source with CMake and lives there, not here. Once
   it is published, the shared CI workflow can install the dependencies and run `bin/test` on every pull request.
2. **More interface languages.** English and Danish are complete. A new language is one JSON catalog
   (`docs/translating.md`); regional catalogs such as `pt-BR` need `UiLanguage` to keep the region.
3. **Paired machines' local models.** The `local` engine asks the model Omarchy runs on this machine. A model
   pinned on a paired machine (`host/recipe`) is passed over for now.
4. **Smaller fixes from the audit:**
   - retention for the `captures` and `changes` tables, which today grow without limit;
   - timestamps (`created`, `modified`, change log) with a UTC offset;
   - the read-only page's "today" in the plan's time zone rather than the server's or the browser's;
   - the layout of the fields when comparing two engines (Alt+S).

Done in 0.3.0: **built like Omarchy's own apps** – CMake and `ctest` with the shared `bin/build`, `bin/test` and CI
workflow; Omarchy's JSON catalogs and shared `Translator`; text size and corner rounding followed live;
`omarchy agent look app` support; the default engine follows `omarchy default agent`, and the `local` engine can
ask the model Omarchy runs. See `docs/adr/0006-omarchy-conventions.md`.

Done earlier: **date notation per language** – the order of day and month, whether `12.12` is a time or a date, the first
day of the week and the clock follow the language's convention (`DateConvention` in `src/datewords.h`), with
`[dates]` in `config.toml` and the settings panel as overrides.
