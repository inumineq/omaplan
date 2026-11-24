# Roadmap

Planned work, roughly in order. Nothing here is promised for a date; open an issue before starting on an item, so
the approach can be agreed first.

1. **Build with CMake**, like Omarchy's own apps. `qmake6` and `bin/build` stay until the switch is done in one go
   (app, tests, `bin/test`, PKGBUILD and CI).
2. **A PKGBUILD that builds from a tagged source** (`source=` with a checksum instead of building in the checkout),
   ready for the AUR or omarchy-pkgs.
3. **Smaller fixes from the audit:**
   - retention for the `captures` and `changes` tables, which today grow without limit;
   - timestamps (`created`, `modified`, change log) with a UTC offset;
   - the read-only page's "today" in the plan's time zone rather than the server's or the browser's;
   - the layout of the fields when comparing two engines (Alt+S).

Done: **date notation per language** – the order of day and month, whether `12.12` is a time or a date, the first
day of the week and the clock follow the language's convention (`DateConvention` in `src/datewords.h`), with
`[dates]` in `config.toml` and the settings panel as overrides.
