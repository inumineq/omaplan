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
`9:00 AM` / `2:30 PM` instead, in both languages; the hours in the timeline's description stay numbers. The export time is the
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
  <p class="description">Gantt chart, a week per block: each task spans its days, and a day runs from 8 to 18. A title sits in its bar when it fits, otherwise beside it; ◆ is a deadline.</p>
  <div class="gantt">
    <section class="gantt-week" data-week="2026-11-23">
      <div class="gantt-days">
        <h3 class="gantt-date today"><time datetime="2026-11-23">Mon 23/11</time></h3>
        <h3 class="gantt-date"><time datetime="2026-11-24">Tue 24/11</time></h3>
        …
      </div>
      <div class="lanes" style="--lanes:3">
        <div class="bar bar-all-day" style="--x:0;--w:71.43;--lane:0" data-id="…" title="…"><span class="bar-title">Write the article</span></div>
        <div class="bar" style="--x:15.71;--w:1.43;--lane:1" data-id="…" data-color="green" title="…"></div>
        <div class="deadline-line" style="--x:17.14;--w:30.2;--lane:1"></div>
        <div class="deadline-mark" style="--x:47.34;--lane:1" data-id="…" title="deadline: …"></div>
        <span class="bar-label" style="--x:48.6;--lane:1" data-id="…" title="…">Draft the newsletter</span>
        <span class="bar-label bar-label-before" style="--x:84.3;--lane:2" data-id="…" title="…">Send out newsletter</span>
        …
      </div>
    </section>
    …
  </div>
</section>
```

- **Weeks**: one `.gantt-week` per week (Monday to Sunday, `data-week` = the Monday) from the week of the earliest
  date to the week of the latest, empty weeks too. The dates are each task's first and last day and its deadline
  mark's day. With no dated tasks, the current week. `.gantt-days` has seven `h3.gantt-date` headings; today's has
  the class `today`.
- **Span** (`taskSpan`, the same rule as the window's `taskSpan` in `src/task.h`), in wall time:
  - with `duration_min`: from `start` for that long; a start without a time covers whole days,
    `ceil(duration / 1440)` of them, at least one;
  - without a duration but with a deadline on a later day: from `start` to the deadline (the end of its day if
    the start has no time);
  - otherwise the start's day (no time) or 30 minutes.
  A deadline after the span's end, or a task with only a deadline, gets a mark. Tasks with neither `start` nor
  `deadline` are not in the timeline.
- **Positions**: a day runs from `from` to `to` (whole hours: the earliest start and the latest same-day end among
  tasks **with a time**, at least `8`–`18`, at most 24); a whole-day span starts and ends on day boundaries.
  `--x` and `--w` are percentages of the week (two decimals), `--lane` the row inside the week. A span is drawn as
  a bar in every week it touches; `bar-cut-start` / `bar-cut-end` mark the side where it goes on into the
  week before or after. A bar is at least `0.35rem` wide (`BAR_MIN_REM`).
- **Deadlines**: a `deadline-mark` (a diamond) at the deadline's time, or just before the end of its day when it
  has no time, never closer than half a mark to its own bar. When the task has a bar in that week, a
  `deadline-line` runs from the bar to the mark.
- **Layout** (`layoutTimeline`, the same algorithm as `Timeline::layout` in `src/timeline.cpp`), computed in rem
  for the week's minimum width `WEEK_REM = 48` with a title's width estimated as `0.47rem` per character
  (`CHAR_REM`); `gap 0.3`, `pad 0.3`, `spacing 0.9`. The title goes, in this order of preference:
  1. inside its bar (`<span class="bar-title">`), when its width plus padding on both sides fits;
  2. after the bar, before its mark (`<span class="bar-label">`);
  3. before the bar (`bar-label bar-label-before`; `--x` is then the title's **right** edge, so it stays at the
     bar on a wider page);
  4. after the mark;
  5. otherwise shortened on the side with the most room, with `--max` (rem) as its width and an ellipsis.
  Then each task takes the first lane where all it occupies (bar, title, mark) is `spacing` clear of the last task
  there; tasks are taken by start, longest first. `--lanes` is the number of lanes (at least 1). Since positions
  are percentages and titles keep their width in rem, a wider page only spreads things further apart; below the
  minimum width the week scrolls sideways.
- Done tasks: `status-done` on the bar, the label and the mark.
- `title` on bars and labels: `Title – Mon 23/11 14:00–15:30`, `Title – Mon 23/11 (all day)`,
  `Title – Mon 23/11 – Fri 27/11` or `Title – Mon 23/11 14:00 – Wed 25/11 10:00`; on marks
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
