# Roadmap

Planned work, roughly in order. Nothing here is promised for a date; open an issue before starting on an item, so
the approach can be agreed first.

1. **Build with CMake**, like Omarchy's own apps. `qmake6` and `bin/build` stay until the switch is done in one go
   (app, tests, `bin/test`, PKGBUILD and CI).
2. **Date notation per language.** Today `12/12` is always a date (day/month) and `12.12` a time. The order of day
   and month and the meaning of `12.12` should follow the language's convention, set per language in `DateWords`
   (`src/datewords.h`), with an optional override in `config.toml`.
3. **A PKGBUILD that builds from a tagged source** (`source=` with a checksum instead of building in the checkout),
   ready for the AUR or omarchy-pkgs.
4. **Smaller fixes from the audit:**
   - retention for the `captures` and `changes` tables, which today grow without limit;
   - timestamps (`created`, `modified`, change log) with a UTC offset;
   - the read-only page's "today" in the plan's time zone rather than the browser's;
   - the layout of the fields when comparing two engines (Alt+S).
