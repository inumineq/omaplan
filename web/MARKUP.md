# Markup contract for the read-only edition

This document describes exactly the HTML that `omaplan serve` writes (`src/render.cpp`) and that `web/app.js`
renders in the browser. `web/index.html` (English) and `web/index.da.html` (Danish) in this repo are generated
examples of it: `node web/test.mjs --update` renders them from `web/example/plan.json` with `web/app.js`, and the
C++ tests check that `Render::page()` gives the same bytes. See `docs/export.md` for the `plan.json` format.

Rules that apply throughout:

- The page works **without JavaScript**. `web/app.js` is optional: it fetches `plan.json` and the dictionary
  from the same origin and renders the same markup again. On `file://` or a failed fetch the server-rendered
  markup stays unchanged.
- **No external resources**: only `style.css` and `app.js` (relative paths), and `plan.json` and
  `i18n/<language>.json` from the same origin. No CDNs, no web fonts, no other network requests.
- All task text (`title`, `notes`, `project`) and every dictionary text is HTML-escaped: `&` `<` `>` `"` `'`.
- The week starts on Monday (the page shows days, not weeks, so `dates.week_start` does not change it). `data-*` dates are always ISO `YYYY-MM-DD` / `YYYY-MM-DDTHH:MM`.

## Languages

Every visible text comes from one dictionary per language, `web/i18n/en.json` (the source) and
`web/i18n/da.json`. Both sides read the same files: `app.js` fetches `i18n/<language>.json`, and the server embeds
them through `src/web.qrc` and also serves them at `/i18n/en.json` and `/i18n/da.json`. `<html lang>` is the
rendered language.

- **Server**: the language of the request's `Accept-Language` header, the first supported one by q-value
  (`da*` → `da`, `en*` → `en`, `q=0` excluded); otherwise `PlanServer::Options::language`; otherwise `en`.
- **Browser**: `navigator.language` (`da*` → `da`, anything else `en`).

Adding a language means adding `web/i18n/<language>.json` with the same keys, listing it in `src/web.qrc`,
`Render::kLanguages` and `LANGUAGES` in `app.js`, and generating a reference page for the tests.

Dates use fixed formats from the dictionary, not the locale, so C++ and JS write the same bytes. A format is a
template with placeholders: `{weekday}`/`{Weekday}` (the weekday as written in `weekdays` / capitalised), `{wd}`/
`{Wd}` (from `weekdaysShort`), `{d}` (day without leading zero), `{mm}` (two-digit month), `{month}` (from
`months`), `{yyyy}`, `{time}` (`HH:MM`). Unknown placeholders stay as written.

| Format (key) | English | Danish |
|---|---|---|
| long date, headings (`dateLong`) | `Monday 23 November 2026` | `Mandag 23. november 2026` |
| short date, timeline (`dateShort`) | `Mon 23/11` | `Man 23/11` |
| deadline in running text (`deadline`, `dateInText`, `atTime`) | `deadline Friday 27 November 2026 at 12:00` | `frist fredag 27. november 2026 kl. 12:00` |
| export time (`exported`, `timestamp`) | `Exported 23 November 2026 at 13:57` | `Eksporteret 23. november 2026 kl. 13:57` |
| duration (`minutes`, `hours`, `hoursMinutes`) | `45 min`, `2 h`, `2 h 30 min` | `45 min`, `2 t.`, `2 t. 30 min` |
| count (`taskOne`, `taskMany`) | `1 task`, `3 tasks` | `1 opgave`, `3 opgaver` |

Times are `09:00` (24-hour, colon) in both; an interval is `09:00–10:30` (U+2013 en dash). With `"clock": "12h"`
in `plan.json` every time on the page (task times, deadlines, the export time, timeline titles) is written
`9:00 AM` / `2:30 PM` instead, in both languages; the timeline's hour axis stays numbers. The export time is the
wall time as written in `exported` (no time zone conversion).

The examples below are the English rendering.

## Page skeleton

```html
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Omaplan – read-only edition</title>
<link rel="stylesheet" href="style.css">
</head>
<body>
<header class="top">
  <h1>Omaplan</h1>
  <nav class="views" aria-label="Views">
    <a href="#overview">Overview</a>
    <a href="#list">List</a>
    <a href="#timeline">Timeline</a>
  </nav>
  <p class="meta">Exported <time datetime="2026-11-23T13:57:00+01:00">23 November 2026 at 13:57</time> <span class="dot">·</span> <span class="timezone">Europe/Copenhagen</span></p>
</header>
<main id="plan" class="plan" data-today="2026-11-23">
  <!-- four views, see below -->
</main>
<footer class="footer">
  <p>Read-only edition · <a href="plan.json">plan.json</a></p>
</footer>
<script src="app.js"></script>
</body>
</html>
```

`data-today` on `<main>` is the local date the overview is computed from (`YYYY-MM-DD`). The server sets the
theme inline on `<html>` (`<html lang="en" style="--bg:#…;…">`, see below).

## Choosing a view without JS

The four views are sibling `<section class="view">` elements inside `<main>` with the ids `overview`, `list`,
`list-project` and `timeline`. The nav anchors point at them, and CSS switches:

```css
.view { display: none; }
#overview { display: block; }              /* the default when no anchor is chosen */
.view:target, .view.active { display: block; }
.plan:has(.view:target) #overview:not(:target),
.plan:has(.view.active) #overview:not(.active) { display: none; }
```

Without JS it all works through `:target`. When `app.js` has re-rendered `<main>` after a fetch, it also sets
the class `active` on the chosen view (and listens for `hashchange`), because some browsers do not find
`:target` again on the new elements – so the anchors also work with JS on.

**Only one tab is active at a time** (also without JS): the tab "Overview" is only marked by default when there
is neither a `:target` nor a `.view.active`:

```css
body:has(#timeline:target) .views a[href="#timeline"],
body:has(#timeline.active) .views a[href="#timeline"], … {
  color: var(--accent-fg); background: var(--accent);
}
:root:not(:has(:target)):not(:has(.view.active)) .views a[href="#overview"] {
  color: var(--accent-fg); background: var(--accent);
}
```

(`#list-project` counts as the tab "List".)

`serve` also understands `?view=list` (and the other ids): the chosen view gets the class `active` in the
server-rendered markup, so a link can point at a view entirely without JS and without an anchor. `app.js` reads
the same parameter when there is no anchor. An unknown id is ignored.

`list` and `list-project` are the same view with different grouping; each has a link to the other in its
description (`<a href="#list-project">Group by project</a>` / `<a href="#list">Group by date</a>`).

## Common task markup

Every view shows tasks with the same `<li>`:

```html
<li class="task status-todo" data-id="…uuid…" data-priority="1" data-project="Blog">
  <p class="task-row"><span class="time">09:00–10:30</span><span class="task-title">Meeting with Client A</span></p>
  <p class="task-meta"><span class="project-tag">Blog</span><span class="deadline">deadline Friday 27 November 2026</span><span class="duration">1 h 30 min</span><span class="guessed">guessed: start, duration</span><span class="badge badge-p1">P1</span><span class="badge badge-done">done</span></p>
  <details class="notes"><summary>Notes</summary><p>…</p></details>
</li>
```

- `class="task status-todo|status-done"`; `data-priority` 1–3; `data-project` only when there is a project;
  `data-color` (after `data-project`) only when `project_colors` gives the project a palette slot (`blue`,
  `bright_red`, …; any other value is left out). The CSS maps it to `--project` (`var(--blue)` …), which colours the
  project tag; a timeline `.bar` of the task carries the same `data-color` (before `title`) and is filled with it.
- `.time` = `HH:MM–HH:MM` (or `HH:MM` without a duration); all-day tasks have `.time.time-all-day` with the text
  `all day`; tasks without a date have `.time.time-none` with the text `no date`.
- `.task-meta` holds only the fields that exist: `project-tag`, `deadline`, `duration`, `guessed` (the field
  names come from the dictionary's `guessNames`: `duration_min` → `duration`), `badge-p1|p2|p3`, and
  `badge-done` when done.
- `notes` only when the task has notes.

## 1. Overview — `<section id="overview" class="view">`

```html
<section id="overview" class="view" aria-label="Overview">
  <h2>Overview</h2>
  <p class="description">Today and the next 7 days – overdue tasks first.</p>
  <section class="day overdue" data-date="2026-11-20">
    <h3><time datetime="2026-11-20">Friday 20 November 2026</time><span class="badge badge-overdue">overdue</span></h3>
    <ul class="tasks">…</ul>
  </section>
  <section class="day today" data-date="2026-11-23">
    <h3><time datetime="2026-11-23">Monday 23 November 2026</time><span class="badge badge-today">today</span></h3>
    <ul class="tasks">…</ul>
  </section>
  …
</section>
```

- First every overdue day, ascending, each with `class="day overdue"` and the badge `overdue`. Overdue is the rule
  of `src/task.h`: status `todo` and the `deadline` before `data-today`, or, with no deadline, the `start` before
  it. An overdue task shows on its `deadline` date, else its `start` date.
- Then the days `data-today` … `data-today + 7` (8 days in all), each `class="day"` (today also `today`) with
  the badge `today` on the first. Days without tasks get `<p class="empty">No tasks.</p>` instead of `<ul>`.
- A task shows on its `start` date, else its `deadline` date; an open task that started earlier and is due later
  shows today (tasks without `start` and `deadline` are not in the overview). Order within a day: tasks
  with a time first (by time), then all-day tasks, then priority (1 before 3), then title (collated in the page's
  language).

## 2. List by date — `<section id="list" class="view">`

```html
<section id="list" class="view" aria-label="List by date">
  <h2>List</h2>
  <p class="description">All tasks grouped by date. <a href="#list-project">Group by project</a></p>
  <section class="group" data-group="date:2026-11-18">
    <h3><time datetime="2026-11-18">Wednesday 18 November 2026</time><span class="count">1 task</span></h3>
    <ul class="tasks">…</ul>
  </section>
  …
</section>
```

Every task, grouped by `start` date (else `deadline` date), dates ascending, the same order within a day as the
overview. **Tasks without `start` and `deadline` go in the group `No date` at the end**:

```html
<section class="group" data-group="no-date">
  <h3><span class="group-title">No date</span><span class="count">2 tasks</span></h3>
  <ul class="tasks">…</ul>
</section>
```

Undated tasks are not shown in the overview. Tasks without `start` have no bar in the timeline (only their deadline
mark, if they have a deadline).

## 3. List by project — `<section id="list-project" class="view">`

The same structure, but `data-group="project:Blog"` and the heading is
`<h3><span class="group-title">Blog</span><span class="count">5 tasks</span></h3>`.
Groups are sorted by project name (collated in the page's language). Tasks without a project go in the group
`data-group="no-project"`, titled `No project`, at the end.

## 4. Timeline — `<section id="timeline" class="view">`

```html
<section id="timeline" class="view" aria-label="Timeline">
  <h2>Timeline</h2>
  <p class="description">Gantt chart of every date with tasks or deadlines – window 8–18, overlapping tasks get a track each.</p>
  <div class="gantt" style="--from:8;--to:18">
    <div class="gantt-head" aria-hidden="true">
      <div class="gantt-date gantt-date-empty"></div>
      <div class="gantt-hours"><span style="--hour:8">8</span><span style="--hour:10">10</span>…<span style="--hour:18">18</span></div>
    </div>
    <div class="gantt-rows">
      <section class="gantt-day today" data-date="2026-11-23">
        <h3 class="gantt-date"><time datetime="2026-11-23">Mon 23/11</time></h3>
        <div class="tracks" style="--tracks:3">
          <div class="bar bar-all-day" style="--start:8;--length:10;--track:0" data-id="…" title="…"><span class="bar-title">…</span></div>
          <div class="bar" style="--start:14;--length:1.5;--track:1" data-id="…" title="…"></div>
          <span class="bar-label" style="--start:14;--length:1.5;--track:1" data-id="…" title="…">Edit podcast clip for social media</span>
          <div class="deadline-mark" style="--start:12" data-id="…" title="deadline: …"></div>
        </div>
      </section>
      …
    </div>
  </div>
</section>
```

- One row per day from the earliest to the latest date among all `start` and `deadline` values (every day in the
  span, empty ones too). Today has the class `today`. Tasks without `start` have no bar (only a deadline mark, if
  any).
- **Time window**: `--from`/`--to` on `.gantt` are set inline (whole hours) to the earliest start and the latest
  end among tasks **with a time**, rounded to whole hours (`floor`/`ceil`), at least `8`–`18`. CSS places bars with
  `left: calc((var(--start) - var(--from)) / (var(--to) - var(--from)) * 100%)` and
  `width: calc(var(--length) / (var(--to) - var(--from)) * 100%)` — `--start`/`--length` are absolute hours.
- The hour axis shows labels every 1/2/3 hours depending on the window's width as `<span style="--hour:8">8</span>`.
- All-day tasks (start without a time) span the whole window: `--start: from`, `--length: to - from`, class
  `bar-all-day`. A task with a time but no duration: a 30-minute bar (`--length:0.5`). Done tasks: `status-done`
  on the bar.
- **Title**: inside the bar (`<span class="bar-title">`) when it fits – a conservative estimate: title length ≤
  `--length` × 64 characters per all-day window (the column's minimum width is 34rem), divided by the window's
  length (`to - from`), so an hour is narrower in a wide window. Otherwise as `<span class="bar-label">` right
  after the bar (the same `--start`, `--length`, `--track` and `title` as the bar) – never clipped in the bar,
  not even when there is no room to the right.
- **Track algorithm** (overlap): sort the day's bars by start (ascending), at equal start longest first; put each
  bar in the first track whose latest end ≤ the bar's start, otherwise in a new track. `--tracks` = number of
  tracks (at least 1).
- Deadlines show as `deadline-mark` (a diamond) on the deadline day at the deadline time (`--start` = hours within
  the window), or at the right edge (class `deadline-mark-edge`) when the deadline is a date only or lies outside
  the window. They take no track.
- `title` on bars and marks: `Title – Mon 23/11 14:00–15:30` (all-day: `Title – Mon 23/11 (all day)`) /
  `deadline: Friday 27 November 2026 at 12:00 – Title`.

## Message pages (server only)

When `plan.json` does not exist yet, or cannot be read, `serve` answers with a page in the same frame: the meta
line says `No snapshot yet`, `<main>` has no `data-today`, and a single
`<section id="overview" class="view">` holds the heading and the explanation (`noPlanHeading`/`noPlanText` with
status 200, `unreadableHeading`/`unreadableText` with status 500).

## Theme as CSS variables

`web/style.css` reads every colour from variables on `:root`, so `serve` can set the theme live (accent from
Omarchy's `colors.toml`, fallback `#FFD60A`):

| CSS variable | `plan.json` key in `theme` | fallback |
|---|---|---|
| `--bg` | `bg` | `#0e0e10` |
| `--bg-raised` | `bg_raised` | `#1a1a21` |
| `--fg` | `fg` | `#e8e8ee` |
| `--muted` | `muted` | `#8b8b98` |
| `--border` | `border` | `#2a2a34` |
| `--accent` | `accent` | `#ffd60a` |
| `--accent-fg` | `accent_fg` | `#000000` |
| `--accent-text` | `accent_text` | `var(--accent)` |
| `--font` | `font` | `monospace` |
| `--danger` | `danger` | `#f7768e` |
| `--ok` | `ok` | `#9ece6a` |

Text colours (`muted`, `danger`, `ok`, `accent_text`) are the theme's own, lifted toward white or black where they
would read below 4.5:1 on `bg` (Omarchy's `muted` is a terminal's dim colour, about 2:1); `accent` itself stays exact
for fills and borders.

`--font` is Omarchy's font family (fontconfig's `monospace`, which `omarchy-font-set` writes), single-quoted:
`--font:'JetBrainsMono Nerd Font'`. Only letters, digits, space, `.`, `_` and `-` get through, so the name is safe
inside the style attribute. The body is `font-family: var(--font), monospace`: a device without that font (a phone)
shows its own monospace.

`serve` sets the variables inline on `<html>` from the theme of the machine it runs on (or else the `theme` in
`plan.json`); `app.js` does the same from `plan.json`'s `theme` when the page is opened statically. `--radius`
defaults to `0px`, the sharp corners of Omarchy's default; tags and bars round at most 3–4 px.
