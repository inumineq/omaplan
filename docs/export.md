# Export format: `plan.json`

`plan.json` is the snapshot of the plan that `omaplan` writes **atomically** (write beside, rename) to
`~/.local/share/omaplan/plan.json` after every change. `omaplan serve` reads only this file, never the database,
and serves it as the read-only edition together with `/plan.json` (see SPEC.md and ADR 0003).

Machine-readable schema: `docs/export-schema.json` (JSON Schema draft 2020-12, same style as `docs/ai-schema.json`).
An invented example lives in `web/example/plan.json`; `web/test.mjs` validates it against the schema.

## Top level

| Key | Type | Note |
|---|---|---|
| `format` | `"omaplan-plan"` | constant, so the file can be recognised |
| `version` | integer | format version; this edition is `2` |
| `exported` | ISO 8601 **with offset** | time of the export, e.g. `2026-11-23T13:57:00+01:00` |
| `timezone` | IANA name | local time zone, e.g. `Europe/Copenhagen` |
| `language` | BCP 47 | the app's user interface language, e.g. `en` or `da` |
| `theme` | object, optional | theme colours `#RRGGBB`: `accent`, `accent_fg`, `bg`, `bg_raised`, `fg`, `muted`, `border`, `danger`, `ok` |
| `tasks` | array | every task, see below |

The read-only edition does not use `language` to pick its own language: `omaplan serve` follows the browser's
`Accept-Language` header and `web/app.js` follows `navigator.language` (see `web/MARKUP.md`).

## Tasks

The fields follow the task model in SPEC.md:

| Key | Type | Note |
|---|---|---|
| `id` | UUID | |
| `title` | text, 1–200 characters | |
| `notes` | text, optional | |
| `project` | text, optional | e.g. `"Blog"`, `"Client A"` |
| `priority` | 1–3 | 1 = high |
| `status` | `"todo"` / `"done"` | |
| `start` | optional: `"YYYY-MM-DD"`, `"YYYY-MM-DDTHH:MM"` or `null` | local time, **without offset**. `null`/absent = not scheduled ("No date") |
| `duration_min` | minutes, optional | an all-day task when absent and start has no time |
| `deadline` | date or date and time, optional | local time without offset, separate from start |
| `guessed` | array of `start`, `duration_min`, `deadline`, `project`, `priority` | which fields the AI guessed |
| `source` | UUID or `null` | id of the capture in `captures` |
| `created` | ISO 8601 with offset | |
| `modified` | ISO 8601 with offset | |

### Dates and times

- `start` and `deadline` are **local time without offset** (`2026-11-27` or `2026-11-27T12:00`); the zone is
  given once at the top level in `timezone`, so `2026-11-27T12:00` means 12:00 local time that day.
- `exported`, `created` and `modified` are instants and carry an **offset** (`2026-11-23T13:57:00+01:00`).
- Nothing is shifted between zones on export; daylight saving time lives in the offset.

### Semantics the views use

- **Not scheduled**: `start` is `null` or absent. Such a task goes in the group **No date** (last) in the list by
  date, and it is **not** shown in the overview or the timeline (a deadline still shows as a mark in the timeline).
- **Overdue**: `status == "todo"`, the task has a date, and the earlier of `start`/`deadline` is before today.
- **All-day task**: `start` without a time and without `duration_min`.
- The **overview** shows today and the next 7 days (8 dates) plus overdue tasks at the top.
- The **timeline** covers every date from the earliest `start`/`deadline` to the latest. Its time axis spans a
  window taken from the data: the earliest start to the latest end (of tasks with a time), rounded to whole hours,
  at least 08–18.
- Order within a day: tasks with a time first (by time), then all-day tasks, then priority, then title.

## Version history

- **2**: English keys (`exported`, `timezone`, `language`, `theme`, `tasks`; per task `title`, `notes`,
  `project`, `priority`, `duration_min`, `deadline`, `guessed`, `source`, `created`, `modified`).
- **1**: the same content with Danish keys. Not read any more.

## The markup contract (how `serve` writes HTML)

`omaplan serve` renders exactly the same markup server-side as `web/app.js` renders in the browser.
`web/index.html` (English) and `web/index.da.html` (Danish) are the reference renderings of
`web/example/plan.json`. The full contract (elements, classes, data attributes, text formats, the Gantt
algorithm, CSS variables, languages) is in **`web/MARKUP.md`**.

In short:

- The page works **without JavaScript**: the four views are `<section class="view">` elements with the ids
  `overview`, `list`, `list-project` and `timeline` inside `<main id="plan" data-today="YYYY-MM-DD">`; a view is
  chosen with anchors (`#list` etc.) and CSS `:target` (the overview shows by default), or with `?view=list`.
- Colours are CSS variables on `:root` (`--bg`, `--accent`, …), so the server can set the theme live from
  Omarchy; the optional `theme` in `plan.json` maps 1:1 to the variables.
- `web/app.js` fetches `plan.json` from the same origin and renders the same markup again; on `file://` or a
  failed fetch the server-rendered markup stays.
- No external CDNs, fonts or network requests beyond `plan.json` and `i18n/<language>.json` from the same origin.
- All text is HTML-escaped (`&`, `<`, `>`, `"`, `'`).
- Page texts come from `web/i18n/en.json` and `web/i18n/da.json`; the week starts on Monday.
