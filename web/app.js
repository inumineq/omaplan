// Omaplan read-only edition: renders plan.json to the same markup that omaplan serve writes server-side
// (src/render.cpp is a line-by-line port of this file). Pure string rendering, so both the browser and
// web/test.mjs (node) can use the functions. Page texts come from a dictionary, web/i18n/<language>.json.

'use strict';

const VIEWS = ['overview', 'list', 'list-project', 'timeline'];
const LANGUAGES = ['en', 'da'];

// ---------- small helpers ----------

function esc(s) {
  return String(s)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function capital(s) {
  return s.charAt(0).toUpperCase() + s.slice(1);
}

// "{d} {month}" with { d: 23, month: 'November' } -> "23 November"; unknown placeholders stay as written
function fill(template, vars) {
  return template.replace(/\{(\w+)\}/g, (m, key) => (Object.prototype.hasOwnProperty.call(vars, key) ? String(vars[key]) : m));
}

// The supported language for a BCP 47 tag ("da-DK" -> "da"), else 'en'.
function pickLanguage(tag) {
  const primary = String(tag || '').toLowerCase().split(/[-_]/)[0];
  return LANGUAGES.includes(primary) ? primary : 'en';
}

// "YYYY-MM-DD" or "YYYY-MM-DDTHH:MM" -> { date, time }; null/absent -> { date: null, time: null } (not scheduled)
function parseStart(s) {
  if (!s) return { date: null, time: null };
  const m = /^(\d{4}-\d{2}-\d{2})(?:T(\d{2}):(\d{2}))?$/.exec(s);
  if (!m) throw new Error('Invalid date: ' + s);
  return { date: m[1], time: m[2] ? m[2] + ':' + m[3] : null };
}

function dateObj(date) {
  const parts = date.split('-').map(Number);
  return new Date(parts[0], parts[1] - 1, parts[2]);
}

function dateStr(dt) {
  const p = (n) => String(n).padStart(2, '0');
  return dt.getFullYear() + '-' + p(dt.getMonth() + 1) + '-' + p(dt.getDate());
}

function todayStr() {
  return dateStr(new Date());
}

function addDays(date, n) {
  const dt = dateObj(date);
  dt.setDate(dt.getDate() + n);
  return dateStr(dt);
}

// The placeholders a date offers the dictionary's formats.
function dateVars(t, date) {
  const wd = (dateObj(date).getDay() + 6) % 7; // the week starts on Monday
  return {
    weekday: t.weekdays[wd],
    Weekday: capital(t.weekdays[wd]),
    wd: t.weekdaysShort[wd],
    Wd: capital(t.weekdaysShort[wd]),
    d: String(Number(date.slice(8, 10))),
    mm: date.slice(5, 7),
    month: t.months[Number(date.slice(5, 7)) - 1],
    yyyy: date.slice(0, 4),
  };
}

// "Monday 23 November 2026" / "Mandag 23. november 2026"
function formatDateLong(t, date) {
  return fill(t.dateLong, dateVars(t, date));
}

// "Mon 23/11" / "Man 23/11"
function formatDateShort(t, date) {
  return fill(t.dateShort, dateVars(t, date));
}

// "Friday 27 November 2026 at 12:00" / "fredag 27. november 2026 kl. 12:00" (running text)
function formatDeadline(t, deadline) {
  const p = parseStart(deadline);
  const date = fill(t.dateInText, dateVars(t, p.date));
  return p.time ? fill(t.atTime, { date, time: p.time }) : date;
}

// "23 November 2026 at 13:57": the wall time as written in the file (no time zone conversion)
function formatTimestamp(t, iso) {
  const p = parseStart(iso.slice(0, 16));
  return fill(t.timestamp, Object.assign(dateVars(t, p.date), { time: p.time }));
}

// "45 min", "2 h", "2 h 30 min"
function formatDuration(t, min) {
  if (min < 60) return fill(t.minutes, { m: min });
  const h = Math.floor(min / 60);
  const m = min % 60;
  return m ? fill(t.hoursMinutes, { h, m }) : fill(t.hours, { h });
}

function endTime(time, durationMin) {
  const h = Number(time.slice(0, 2));
  const m = Number(time.slice(3, 5));
  const end = h * 60 + m + durationMin;
  const p = (n) => String(n).padStart(2, '0');
  return p(Math.floor(end / 60) % 24) + ':' + p(end % 60);
}

function countTasks(t, n) {
  return n === 1 ? t.taskOne : fill(t.taskMany, { n });
}

// Numbers in style attributes without a decimal comma or needless zeros: 9, 1.5, 24
function num(x) {
  return String(Number(x.toFixed(2)));
}

// ---------- domain logic ----------

// The rule of src/task.h (effectiveDate, isOverdue), so the page and the app agree: a task counts on its start
// date, else its deadline date; overdue is open and past its deadline, or, with no deadline, past its start day.
function planDate(o) {
  if (o.start) return o.start.slice(0, 10);
  if (o.deadline) return o.deadline.slice(0, 10);
  return null;
}

function isOverdue(o, today) {
  if (o.status !== 'todo') return false;
  if (o.deadline) return o.deadline.slice(0, 10) < today;
  return !!o.start && o.start.slice(0, 10) < today;
}

// The overview day of an overdue task: the deadline it missed, else its start
function overdueDate(o) {
  return o.deadline ? o.deadline.slice(0, 10) : planDate(o);
}

// The overview day of a task that is not overdue: its plan date, or today for an open task that started earlier
// and is due later (in progress today)
function agendaDate(o, today) {
  const d = planDate(o);
  return d && d < today && o.status === 'todo' ? today : d;
}

// Dated first (by date, then timed before all-day, by time), then undated; then priority, then title
// (collated in the page's language)
function sortTasks(tasks, t) {
  return [...tasks].sort((a, b) => {
    const sa = parseStart(a.start);
    const sb = parseStart(b.start);
    if (!!sa.date !== !!sb.date) return sa.date ? -1 : 1;
    if (sa.date && sa.date !== sb.date) return sa.date < sb.date ? -1 : 1;
    if (sa.date && !!sa.time !== !!sb.time) return sa.time ? -1 : 1;
    if (sa.time && sb.time && sa.time !== sb.time) return sa.time < sb.time ? -1 : 1;
    if (a.priority !== b.priority) return a.priority - b.priority;
    return a.title.localeCompare(b.title, t.language);
  });
}

// ---------- markup ----------

function renderTask(o, t) {
  const s = parseStart(o.start);
  let timeText;
  let timeClass = 'time';
  if (!s.date) {
    timeText = t.timeNone;
    timeClass += ' time-none';
  } else if (s.time) {
    timeText = o.duration_min ? s.time + '–' + endTime(s.time, o.duration_min) : s.time;
  } else {
    timeText = t.allDay;
    timeClass += ' time-all-day';
  }
  const meta = [];
  if (o.project) meta.push('<span class="project-tag">' + esc(o.project) + '</span>');
  if (o.deadline) meta.push('<span class="deadline">' + esc(fill(t.deadline, { date: formatDeadline(t, o.deadline) })) + '</span>');
  if (o.duration_min) meta.push('<span class="duration">' + esc(formatDuration(t, o.duration_min)) + '</span>');
  if (o.guessed && o.guessed.length) {
    const fields = o.guessed.map((g) => t.guessNames[g] || g).join(', ');
    meta.push('<span class="guessed">' + esc(fill(t.guessed, { fields })) + '</span>');
  }
  meta.push('<span class="badge badge-p' + o.priority + '">P' + o.priority + '</span>');
  if (o.status === 'done') meta.push('<span class="badge badge-done">' + esc(t.done) + '</span>');
  const projectAttr = o.project ? ' data-project="' + esc(o.project) + '"' : '';
  const notes = o.notes ? '\n    <details class="notes"><summary>' + esc(t.notes) + '</summary><p>' + esc(o.notes) + '</p></details>' : '';
  return (
    '<li class="task status-' + esc(o.status) + '" data-id="' + esc(o.id) + '" data-priority="' + o.priority + '"' + projectAttr + '>\n' +
    '    <p class="task-row"><span class="' + timeClass + '">' + esc(timeText) + '</span><span class="task-title">' + esc(o.title) + '</span></p>\n' +
    '    <p class="task-meta">' + meta.join('') + '</p>' + notes + '\n' +
    '  </li>'
  );
}

function renderTaskList(tasks, t) {
  return '<ul class="tasks">\n' + tasks.map((o) => '    ' + renderTask(o, t).replace(/\n/g, '\n    ')).join('\n') + '\n  </ul>';
}

function renderDay(date, tasks, extraClass, badge, t) {
  const badgeHtml = badge ? '<span class="badge ' + badge.cls + '">' + esc(badge.text) + '</span>' : '';
  const body = tasks.length ? renderTaskList(tasks, t) : '    <p class="empty">' + esc(t.noTasks) + '</p>';
  return (
    '  <section class="day' + (extraClass ? ' ' + extraClass : '') + '" data-date="' + date + '">\n' +
    '    <h3><time datetime="' + date + '">' + esc(formatDateLong(t, date)) + '</time>' + badgeHtml + '</h3>\n' +
    body + '\n' +
    '  </section>'
  );
}

function viewClass(id, active) {
  return id === active ? 'view active' : 'view';
}

function renderOverview(plan, today, t, active) {
  const tasks = plan.tasks;
  const groups = new Map();
  for (const o of tasks.filter((o) => isOverdue(o, today))) {
    const d = overdueDate(o);
    if (!groups.has(d)) groups.set(d, []);
    groups.get(d).push(o);
  }
  const parts = [];
  for (const date of [...groups.keys()].sort()) {
    parts.push(renderDay(date, sortTasks(groups.get(date), t), 'overdue', { cls: 'badge-overdue', text: t.overdue }, t));
  }
  for (let k = 0; k < 8; k++) {
    const date = addDays(today, k);
    const onDay = tasks.filter((o) => !isOverdue(o, today) && agendaDate(o, today) === date); // undated tasks are not in the overview
    const badge = k === 0 ? { cls: 'badge-today', text: t.today } : null;
    parts.push(renderDay(date, sortTasks(onDay, t), k === 0 ? 'today' : '', badge, t));
  }
  return (
    '  <section id="overview" class="' + viewClass('overview', active) + '" aria-label="' + esc(t.overview) + '">\n' +
    '    <h2>' + esc(t.overview) + '</h2>\n' +
    '    <p class="description">' + esc(t.overviewDescription) + '</p>\n' +
    parts.join('\n') + '\n' +
    '  </section>'
  );
}

function renderGroup(dataGroup, heading, tasks, t) {
  return (
    '  <section class="group" data-group="' + dataGroup + '">\n' +
    '    <h3>' + heading + '<span class="count">' + esc(countTasks(t, tasks.length)) + '</span></h3>\n' +
    renderTaskList(tasks, t) + '\n' +
    '  </section>'
  );
}

function renderListByDate(plan, t, active) {
  const groups = new Map();
  const undated = [];
  for (const o of plan.tasks) {
    const d = planDate(o);
    if (!d) {
      undated.push(o); // the group "No date" comes last
      continue;
    }
    if (!groups.has(d)) groups.set(d, []);
    groups.get(d).push(o);
  }
  const parts = [];
  for (const date of [...groups.keys()].sort()) {
    parts.push(renderGroup('date:' + date, '<time datetime="' + date + '">' + esc(formatDateLong(t, date)) + '</time>', sortTasks(groups.get(date), t), t));
  }
  if (undated.length) {
    parts.push(renderGroup('no-date', '<span class="group-title">' + esc(t.noDate) + '</span>', sortTasks(undated, t), t));
  }
  return (
    '  <section id="list" class="' + viewClass('list', active) + '" aria-label="' + esc(t.listByDate) + '">\n' +
    '    <h2>' + esc(t.list) + '</h2>\n' +
    '    <p class="description">' + esc(t.listDescription) + ' <a href="#list-project">' + esc(t.groupByProject) + '</a></p>\n' +
    parts.join('\n') + '\n' +
    '  </section>'
  );
}

function renderListByProject(plan, t, active) {
  const none = t.noProject;
  const groups = new Map();
  for (const o of plan.tasks) {
    const p = o.project || none;
    if (!groups.has(p)) groups.set(p, []);
    groups.get(p).push(o);
  }
  const names = [...groups.keys()].sort((a, b) => (a === none ? 1 : b === none ? -1 : a.localeCompare(b, t.language)));
  const parts = [];
  for (const name of names) {
    parts.push(renderGroup(name === none ? 'no-project' : 'project:' + esc(name), '<span class="group-title">' + esc(name) + '</span>', sortTasks(groups.get(name), t), t));
  }
  return (
    '  <section id="list-project" class="' + viewClass('list-project', active) + '" aria-label="' + esc(t.listByProject) + '">\n' +
    '    <h2>' + esc(t.listByProject) + '</h2>\n' +
    '    <p class="description">' + esc(t.listProjectDescription) + ' <a href="#list">' + esc(t.groupByDate) + '</a></p>\n' +
    parts.join('\n') + '\n' +
    '  </section>'
  );
}

// Gantt: one row per day across the whole plan (start and deadline dates; undated tasks without a deadline
// are not in the timeline). The time axis spans a window taken from the data: earliest start to latest end,
// rounded to whole hours, at least 08-18. Bars go in tracks by overlap (first free track). The title sits in
// the bar when it fits (a conservative estimate, see below), otherwise beside it -- never clipped in a bar
// that is too short.
const CHARS_PER_DAY = 64; // characters that fit at most in an all-day bar at the column's minimum width (34rem)

function renderTimeline(plan, today, t, active) {
  const dated = plan.tasks.filter((o) => o.start);
  const dates = new Set();
  for (const o of dated) dates.add(o.start.slice(0, 10));
  for (const o of plan.tasks) if (o.deadline) dates.add(o.deadline.slice(0, 10));
  let sortedDates = [...dates].sort();
  if (sortedDates.length === 0) sortedDates = [today];
  const days = [];
  for (let d = sortedDates[0]; d <= sortedDates[sortedDates.length - 1]; d = addDays(d, 1)) days.push(d);

  // raw bars per day (all-day = no time)
  const raw = new Map(); // date -> [{ o, s, start, length, allDay }]
  for (const day of days) {
    raw.set(
      day,
      dated
        .filter((o) => o.start.slice(0, 10) === day)
        .map((o) => {
          const s = parseStart(o.start);
          if (!s.time) return { o, s, start: 0, length: 0, allDay: true };
          const start = Number(s.time.slice(0, 2)) + Number(s.time.slice(3, 5)) / 60;
          const length = o.duration_min ? o.duration_min / 60 : 0.5;
          return { o, s, start, length, allDay: false };
        })
    );
  }

  // window from the timed tasks: earliest start to latest end, rounded, at least 08-18
  let from = 8;
  let to = 18;
  const timed = [...raw.values()].flat().filter((b) => !b.allDay);
  if (timed.length) {
    const minStart = Math.min(...timed.map((b) => b.start));
    const maxEnd = Math.max(...timed.map((b) => b.start + b.length));
    from = Math.min(8, Math.floor(minStart));
    to = Math.max(18, Math.ceil(maxEnd));
  }

  // all-day bars span the whole window
  for (const bars of raw.values()) {
    for (const b of bars) if (b.allDay) {
      b.start = from;
      b.length = to - from;
    }
  }

  // hour labels on the axis
  const step = to - from > 12 ? 3 : to - from > 8 ? 2 : 1;
  const hours = [];
  for (let h = Math.ceil(from / step) * step; h <= to; h += step) hours.push(h);

  const dayParts = [];
  for (const day of days) {
    const bars = raw.get(day)
      .sort((a, b) => a.start - b.start || b.length - a.length || a.o.title.localeCompare(b.o.title, t.language));
    // tracks: the first free track where the bar does not overlap (start >= the track's end)
    const trackEnd = [];
    for (const b of bars) {
      let track = trackEnd.findIndex((end) => end <= b.start + 1e-9);
      if (track === -1) {
        trackEnd.push(0);
        track = trackEnd.length - 1;
      }
      trackEnd[track] = b.start + b.length;
      b.track = track;
    }
    const tracks = Math.max(1, trackEnd.length);
    const lines = [];
    for (const b of bars) {
      const title = b.o.title + ' – ' + formatDateShort(t, day) + (b.s.time ? ' ' + b.s.time + (b.o.duration_min ? '–' + endTime(b.s.time, b.o.duration_min) : '') : ' (' + t.allDay + ')');
      const pos = '--start:' + num(b.start) + ';--length:' + num(b.length) + ';--track:' + b.track;
      const bar =
        '        <div class="bar' + (b.allDay ? ' bar-all-day' : '') + (b.o.status === 'done' ? ' status-done' : '') + '" style="' + pos + '" data-id="' + esc(b.o.id) + '" title="' + esc(title) + '">';
      // The title goes beside the bar when it cannot fit inside it. A bar's pixel width shrinks with the
      // window (to - from); at the column's minimum width (34rem) about 64 characters fit across a whole-day
      // bar. Estimate conservatively: a title behind the estimate would be clipped by overflow:hidden.
      const fitsIn = b.o.title.length <= (b.length * CHARS_PER_DAY) / (to - from);
      if (fitsIn) {
        lines.push(bar + '<span class="bar-title">' + esc(b.o.title) + '</span></div>');
      } else {
        lines.push(bar + '</div>');
        lines.push('        <span class="bar-label" style="' + pos + '" data-id="' + esc(b.o.id) + '" title="' + esc(title) + '">' + esc(b.o.title) + '</span>');
      }
    }
    for (const o of plan.tasks) {
      if (!o.deadline || o.deadline.slice(0, 10) !== day) continue;
      const f = parseStart(o.deadline);
      const start = f.time ? Number(f.time.slice(0, 2)) + Number(f.time.slice(3, 5)) / 60 : to;
      const inWindow = !!f.time && start < to;
      const title = fill(t.deadlineTitle, { date: formatDeadline(t, o.deadline), title: o.title });
      lines.push('        <div class="deadline-mark' + (inWindow ? '' : ' deadline-mark-edge') + '" style="--start:' + num(Math.min(start, to)) + '" data-id="' + esc(o.id) + '" title="' + esc(title) + '"></div>');
    }
    dayParts.push(
      '    <section class="gantt-day' + (day === today ? ' today' : '') + '" data-date="' + day + '">\n' +
      '      <h3 class="gantt-date"><time datetime="' + day + '">' + esc(formatDateShort(t, day)) + '</time></h3>\n' +
      '      <div class="tracks" style="--tracks:' + tracks + '">\n' +
      lines.join('\n') + '\n' +
      '      </div>\n' +
      '    </section>'
    );
  }
  return (
    '  <section id="timeline" class="' + viewClass('timeline', active) + '" aria-label="' + esc(t.timeline) + '">\n' +
    '    <h2>' + esc(t.timeline) + '</h2>\n' +
    '    <p class="description">' + esc(fill(t.timelineDescription, { from, to })) + '</p>\n' +
    '    <div class="gantt" style="--from:' + from + ';--to:' + to + '">\n' +
    '      <div class="gantt-head" aria-hidden="true">\n' +
    '        <div class="gantt-date gantt-date-empty"></div>\n' +
    '        <div class="gantt-hours">' + hours.map((h) => '<span style="--hour:' + h + '">' + h + '</span>').join('') + '</div>\n' +
    '      </div>\n' +
    '      <div class="gantt-rows">\n' +
    dayParts.join('\n') + '\n' +
    '      </div>\n' +
    '    </div>\n' +
    '  </section>'
  );
}

// `active` (one of VIEWS, or empty) gets the class "active"; serve sets it from ?view=.
function renderMain(plan, today, t, active) {
  const view = VIEWS.includes(active) ? active : '';
  return [
    renderOverview(plan, today, t, view),
    renderListByDate(plan, t, view),
    renderListByProject(plan, t, view),
    renderTimeline(plan, today, t, view),
  ].join('\n');
}

function renderNav(t) {
  return (
    '  <nav class="views" aria-label="' + esc(t.views) + '">\n' +
    '    <a href="#overview">' + esc(t.overview) + '</a>\n' +
    '    <a href="#list">' + esc(t.list) + '</a>\n' +
    '    <a href="#timeline">' + esc(t.timeline) + '</a>\n' +
    '  </nav>'
  );
}

function renderMeta(plan, t) {
  return (
    esc(t.exported) + ' <time datetime="' + esc(plan.exported) + '">' + esc(formatTimestamp(t, plan.exported)) + '</time>' +
    ' <span class="dot">·</span> <span class="timezone">' + esc(plan.timezone) + '</span>'
  );
}

function renderFooter(t) {
  return '  <p>' + esc(t.footer) + ' · <a href="plan.json">plan.json</a></p>';
}

function renderPage(plan, today, t, active) {
  return (
    '<!doctype html>\n' +
    '<html lang="' + esc(t.language) + '">\n' +
    '<head>\n' +
    '<meta charset="utf-8">\n' +
    '<meta name="viewport" content="width=device-width, initial-scale=1">\n' +
    '<title>' + esc(t.title) + '</title>\n' +
    '<link rel="stylesheet" href="style.css">\n' +
    '</head>\n' +
    '<body>\n' +
    '<header class="top">\n' +
    '  <h1>Omaplan</h1>\n' +
    renderNav(t) + '\n' +
    '  <p class="meta">' + renderMeta(plan, t) + '</p>\n' +
    '</header>\n' +
    '<main id="plan" class="plan" data-today="' + today + '">\n' +
    renderMain(plan, today, t, active) + '\n' +
    '</main>\n' +
    '<footer class="footer">\n' +
    renderFooter(t) + '\n' +
    '</footer>\n' +
    '<script src="app.js"></script>\n' +
    '</body>\n' +
    '</html>\n'
  );
}

// ---------- theme (optional, from plan.json) ----------

const THEME_TO_CSS = {
  accent: '--accent',
  accent_fg: '--accent-fg',
  bg: '--bg',
  bg_raised: '--bg-raised',
  fg: '--fg',
  muted: '--muted',
  border: '--border',
  danger: '--danger',
  ok: '--ok',
};

function applyTheme(theme, root) {
  if (!theme) return;
  for (const [key, variable] of Object.entries(THEME_TO_CSS)) {
    if (theme[key]) root.style.setProperty(variable, theme[key]);
  }
}

// ---------- browser bootstrap (optional; the page works without JS) ----------

// After JS has replaced main.innerHTML some browsers lose :target on the new element, so also set the
// class .active (CSS without JS relies on :target only).
function setActiveView() {
  // The anchor first; otherwise ?view=..., which serve understands without JS too.
  const id = window.location.hash.replace(/^#/, '') || new URLSearchParams(window.location.search).get('view') || '';
  const target = id ? document.getElementById(id) : null;
  for (const v of document.querySelectorAll('#plan .view')) {
    v.classList.toggle('active', v === target);
  }
}

async function fetchJson(url) {
  const res = await fetch(url, { cache: 'no-store' });
  if (!res.ok) throw new Error(url + ': ' + res.status);
  return res.json();
}

async function boot() {
  let t;
  try {
    t = await fetchJson('i18n/' + pickLanguage(navigator.language) + '.json');
  } catch (e) {
    return; // no dictionary: keep the server-rendered markup
  }
  // plan.json from the same origin first; the example folder as a fallback when the page is opened statically.
  for (const url of ['plan.json', 'example/plan.json']) {
    let plan;
    try {
      plan = await fetchJson(url);
    } catch (e) {
      continue; // try the next url; on file:// or a failed fetch the server-rendered markup stays
    }
    if (!plan || plan.format !== 'omaplan-plan') continue;
    const root = document.documentElement;
    applyTheme(plan.theme, root);
    root.lang = t.language;
    document.title = t.title;
    const today = todayStr();
    const main = document.getElementById('plan');
    main.setAttribute('data-today', today);
    main.innerHTML = renderMain(plan, today, t, '');
    setActiveView();
    const nav = document.querySelector('header .views');
    if (nav) nav.outerHTML = renderNav(t).trim();
    const meta = document.querySelector('header .meta');
    if (meta) meta.innerHTML = renderMeta(plan, t);
    const footer = document.querySelector('footer.footer');
    if (footer) footer.innerHTML = '\n' + renderFooter(t) + '\n';
    main.dataset.js = 'yes'; // marker for tests: JS has rendered the page
    return;
  }
}

if (typeof document !== 'undefined') {
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
  else boot();
  window.addEventListener('hashchange', setActiveView);
}

// Exports for tests in node (no DOM).
if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    VIEWS, LANGUAGES, fill, pickLanguage, parseStart, todayStr, addDays, formatDateLong, formatDateShort,
    formatDeadline, formatDuration, formatTimestamp, endTime, planDate, isOverdue, overdueDate, agendaDate, sortTasks, renderTask, renderOverview,
    renderListByDate, renderListByProject, renderTimeline, renderMain, renderNav, renderMeta, renderFooter,
    renderPage, applyTheme,
  };
}
