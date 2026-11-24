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

// The page's settings from plan.json on top of the dictionary: the clock ("clock": "12h") and the
// project colours ("project_colors": { "Blog": "color4" }).
function pageTexts(plan, t) {
  return Object.assign({}, t, { clock12: plan.clock === '12h', projectColors: plan.project_colors || {} });
}

// "14:30", or "2:30 PM" when the plan's clock is 12h
function clockTime(t, time) {
  if (!t.clock12) return time;
  const h = Number(time.slice(0, 2));
  return ((h + 11) % 12 + 1) + ':' + time.slice(3, 5) + (h < 12 ? ' AM' : ' PM');
}

// A project colour is a slot of the theme palette, as Omarchy's colors.toml names them (CSS: --red,
// --bright-blue, ...).
const PALETTE_SLOTS = ['red', 'orange', 'yellow', 'green', 'cyan', 'blue', 'magenta', 'brown',
  'bright_red', 'bright_yellow', 'bright_green', 'bright_cyan', 'bright_blue', 'bright_magenta'];
const PALETTE_SLOT = /^(red|orange|yellow|green|cyan|blue|magenta|brown|bright_(red|yellow|green|cyan|blue|magenta))$/;

// ' data-color="blue"' when the task's project has a colour, else ''
function colorAttr(t, o) {
  const colors = t.projectColors || {};
  const slot = o.project && Object.prototype.hasOwnProperty.call(colors, o.project) ? colors[o.project] : '';
  return typeof slot === 'string' && PALETTE_SLOT.test(slot) ? ' data-color="' + slot + '"' : '';
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
  return p.time ? fill(t.atTime, { date, time: clockTime(t, p.time) }) : date;
}

// "23 November 2026 at 13:57": the wall time as written in the file (no time zone conversion)
function formatTimestamp(t, iso) {
  const p = parseStart(iso.slice(0, 16));
  return fill(t.timestamp, Object.assign(dateVars(t, p.date), { time: clockTime(t, p.time) }));
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
    timeText = o.duration_min ? clockTime(t, s.time) + '–' + clockTime(t, endTime(s.time, o.duration_min)) : clockTime(t, s.time);
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
    '<li class="task status-' + esc(o.status) + '" data-id="' + esc(o.id) + '" data-priority="' + o.priority + '"' + projectAttr + colorAttr(t, o) + '>\n' +
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
  t = pageTexts(plan, t);
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
  t = pageTexts(plan, t);
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
  t = pageTexts(plan, t);
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

// ---------- timeline (Gantt) ----------

// A moment as written in plan.json: { date: 'YYYY-MM-DD', min: minutes into the day (0-1439) }, wall time.
function minutesOf(time) {
  return Number(time.slice(0, 2)) * 60 + Number(time.slice(3, 5));
}

function addMinutes(m, n) {
  const total = m.min + n;
  return { date: addDays(m.date, Math.floor(total / 1440)), min: total % 1440 };
}

function compareMoments(a, b) {
  return a.date < b.date ? -1 : a.date > b.date ? 1 : a.min - b.min;
}

function daysBetween(a, b) {
  return Math.round((dateObj(b) - dateObj(a)) / 86400000);
}

function mondayOf(date) {
  return addDays(date, -((dateObj(date).getDay() + 6) % 7));
}

function timeOf(min) {
  const p = (n) => String(n).padStart(2, '0');
  return p(Math.floor(min / 60)) + ':' + p(min % 60);
}

// A task's extent on the timeline, the rule of taskSpan in src/task.h: the duration is the work (whole days for a
// start without a time); without one, a deadline on a later day is the end; otherwise the whole day, or 30
// minutes from a start with a time. A deadline after the end, or a task's only date, is a mark. The end is
// exclusive: a whole day ends at midnight after it.
function taskSpan(o) {
  const s = parseStart(o.start);
  const d = parseStart(o.deadline);
  // the moment a deadline falls due: its time, or the end of its day
  const due = !d.date ? null : d.time ? { date: d.date, min: minutesOf(d.time) } : { date: addDays(d.date, 1), min: 0 };
  const span = { start: null, end: null, allDay: false, toDeadline: false, mark: null, markAllDay: false };
  if (s.date) {
    span.allDay = !s.time;
    span.start = { date: s.date, min: s.time ? minutesOf(s.time) : 0 };
    if (o.duration_min > 0) {
      span.end = span.allDay ? { date: addDays(s.date, Math.max(1, Math.ceil(o.duration_min / 1440))), min: 0 } : addMinutes(span.start, o.duration_min);
    } else if (due && d.date > s.date) {
      // whole days run to the end of the deadline's day, whatever its time
      span.end = span.allDay ? { date: addDays(d.date, 1), min: 0 } : due;
      span.toDeadline = true;
    } else {
      span.end = span.allDay ? { date: addDays(s.date, 1), min: 0 } : addMinutes(span.start, 30);
    }
  }
  if (due && (!span.end || compareMoments(due, span.end) > 0)) {
    span.mark = { date: d.date, min: d.time ? minutesOf(d.time) : 0 };
    span.markAllDay = !d.time;
  }
  return span;
}

// Where the titles go and which lane each task takes (Timeline::layout in src/timeline.cpp, kept in step): a title
// inside its bar when it fits, else after the bar (up to its mark), else before the bar, else after the mark, else
// shortened on the side with the most room; then each task takes the first lane where all it occupies (bar, title,
// mark) is `spacing` clear of the last task there. items: { barStart, barEnd, hasMark, markStart, markEnd,
// labelWidth }; g: { total, gap, pad, spacing }.
function placeTitle(item, g) {
  const w = item.labelWidth;
  const markEnd = item.hasMark ? item.markEnd : item.barEnd;
  const afterLimit = item.hasMark ? item.markStart - g.gap : g.total;
  const p = { lane: 0, side: 'inside', labelStart: 0, labelWidth: w, shortened: false, start: 0, end: 0 };
  if (w + 2 * g.pad <= item.barEnd - item.barStart) {
    p.side = 'inside';
    p.labelStart = item.barStart + g.pad;
  } else if (item.barEnd + g.gap + w <= afterLimit) {
    p.side = 'afterBar';
    p.labelStart = item.barEnd + g.gap;
  } else if (item.barStart - g.gap - w >= 0) {
    p.side = 'before';
    p.labelStart = item.barStart - g.gap - w;
  } else if (item.hasMark && item.markEnd + g.gap + w <= g.total) {
    p.side = 'afterMark';
    p.labelStart = item.markEnd + g.gap;
  } else {
    const rooms = [
      { side: 'inside', start: item.barStart + g.pad, width: item.barEnd - item.barStart - 2 * g.pad },
      { side: 'afterBar', start: item.barEnd + g.gap, width: afterLimit - item.barEnd - g.gap },
      { side: 'afterMark', start: item.markEnd + g.gap, width: item.hasMark ? g.total - item.markEnd - g.gap : -1 },
      { side: 'before', start: 0, width: item.barStart - g.gap },
    ];
    let best = rooms[0];
    for (const room of rooms) if (room.width > best.width) best = room;
    p.side = best.side;
    p.labelWidth = Math.max(0, best.width);
    p.labelStart = best.side === 'before' ? best.start + best.width - p.labelWidth : best.start;
    p.shortened = true;
  }
  p.start = Math.min(item.barStart, p.side === 'before' ? p.labelStart : item.barStart);
  p.end = Math.max(markEnd, p.side === 'inside' || p.side === 'before' ? item.barEnd : p.labelStart + p.labelWidth);
  return p;
}

function layoutTimeline(items, g) {
  const placed = items.map((item) => placeTitle(item, g));
  const order = placed.map((p, i) => i).sort((a, b) => placed[a].start - placed[b].start || placed[b].end - placed[a].end);
  const laneEnd = [];
  for (const i of order) {
    let lane = 0;
    while (lane < laneEnd.length && laneEnd[lane] + g.spacing > placed[i].start + 1e-9) lane++;
    if (lane === laneEnd.length) laneEnd.push(0);
    laneEnd[lane] = placed[i].end;
    placed[i].lane = lane;
  }
  return placed;
}

// The page does not know its width, so a week is laid out in rem at its minimum width (CSS: .gantt-week) with a
// title's width estimated from its length. Positions are written as percentages of the week and widths in rem:
// on a wider page the gaps grow and the titles keep their width, so nothing that is apart at the minimum width
// can meet, and a title that fits in its bar keeps fitting.
const WEEK_REM = 48;
const CHAR_REM = 0.47; // a character of a title at 0.75rem in a monospace font (0.6em), rounded up
const BAR_MIN_REM = 0.35; // CSS: .bar min-width
const MARK_REM = 0.6; // across a deadline mark (CSS: .deadline-mark, 0.42rem turned 45°)
const TIMELINE_GEOMETRY = { total: WEEK_REM, gap: 0.3, pad: 0.3, spacing: 0.9 };

// "Mon 23/11 14:00–15:30", "Mon 23/11 (all day)", "Mon 23/11 – Fri 27/11", "Mon 30/11 08:00 – Wed 2/12 10:00"
function spanText(t, s) {
  if (s.allDay) {
    const last = addDays(s.end.date, -1);
    return last === s.start.date ? formatDateShort(t, s.start.date) + ' (' + t.allDay + ')' : formatDateShort(t, s.start.date) + ' – ' + formatDateShort(t, last);
  }
  const at = (m) => formatDateShort(t, m.date) + ' ' + clockTime(t, timeOf(m.min));
  const sameDay = s.end.date === s.start.date || (s.end.min === 0 && s.end.date === addDays(s.start.date, 1));
  return sameDay ? at(s.start) + '–' + clockTime(t, timeOf(s.end.min)) : at(s.start) + ' – ' + at(s.end);
}

// Gantt: a week per block, seven day columns, from the first to the last week with a task. A task is a bar over
// its days (taskSpan); within a day the hours run from `from` to `to` (taken from the tasks with a time, at least
// 08-18). A deadline after the bar is a mark at the end of a line; a task with only a deadline is just the mark.
// Tasks without a start or a deadline are not in the timeline.
function renderTimeline(plan, today, t, active) {
  t = pageTexts(plan, t);
  const tasks = plan.tasks
    .map((o) => ({ o, span: taskSpan(o) }))
    .filter((x) => x.span.start || x.span.mark)
    .sort((a, b) => compareMoments(a.span.start || a.span.mark, b.span.start || b.span.mark) || a.o.title.localeCompare(b.o.title, t.language));

  // the hours of a day: the tasks with a time, from their start to their end on the same day, at least 08-18
  let from = 8;
  let to = 18;
  for (const { span: s } of tasks) {
    if (!s.start || s.allDay) continue;
    from = Math.min(from, Math.floor(s.start.min / 60));
    const end = s.end.date === s.start.date ? s.end.min : s.start.min + 30;
    to = Math.max(to, Math.min(24, Math.ceil(end / 60)));
  }

  const dates = [];
  for (const { span: s } of tasks) {
    if (s.start) dates.push(s.start.date, s.end.min === 0 ? addDays(s.end.date, -1) : s.end.date);
    if (s.mark) dates.push(s.mark.date);
  }
  dates.sort();
  const first = mondayOf(dates.length ? dates[0] : today);
  const last = mondayOf(dates.length ? dates[dates.length - 1] : today);

  const pct = (rem) => num((rem / WEEK_REM) * 100);
  const weeks = [];
  for (let week = first; week <= last; week = addDays(week, 7)) {
    const weekStart = { date: week, min: 0 };
    const weekEnd = { date: addDays(week, 7), min: 0 };
    // rem from the week's start; `edge`: a whole-day boundary
    const at = (m, edge) => {
      const day = daysBetween(week, m.date);
      if (day < 0) return 0;
      if (day >= 7) return WEEK_REM;
      const within = edge ? 0 : Math.max(0, Math.min(1, (m.min / 60 - from) / (to - from)));
      return ((day + within) / 7) * WEEK_REM;
    };
    const shown = [];
    const items = [];
    for (const x of tasks) {
      const s = x.span;
      const b = { x, hasBar: false, hasMark: false, cutStart: false, cutEnd: false, barStart: 0, barEnd: 0, markX: 0 };
      if (s.start && compareMoments(s.start, weekEnd) < 0 && compareMoments(s.end, weekStart) > 0) {
        b.hasBar = true;
        b.cutStart = compareMoments(s.start, weekStart) < 0;
        b.cutEnd = compareMoments(s.end, weekEnd) > 0;
        b.barStart = b.cutStart ? 0 : at(s.start, s.allDay);
        b.barEnd = b.cutEnd ? WEEK_REM : at(s.end, s.allDay);
        b.barEnd = Math.max(b.barEnd, Math.min(WEEK_REM, b.barStart + BAR_MIN_REM));
      }
      if (s.mark && s.mark.date >= week && s.mark.date < weekEnd.date) {
        b.hasMark = true;
        let c = s.markAllDay ? at({ date: addDays(s.mark.date, 1), min: 0 }, true) - 0.4 : at(s.mark, false);
        c = Math.max(MARK_REM / 2, Math.min(WEEK_REM - MARK_REM / 2, c));
        if (b.hasBar) c = Math.max(c, b.barEnd + MARK_REM / 2);
        b.markX = c;
      }
      if (!b.hasBar && !b.hasMark) continue;
      if (!b.hasBar) b.barStart = b.barEnd = b.markX - MARK_REM / 2;
      shown.push(b);
      items.push({
        barStart: b.barStart, barEnd: b.barEnd, hasMark: b.hasMark, markStart: b.markX - MARK_REM / 2,
        markEnd: b.markX + MARK_REM / 2, labelWidth: x.o.title.length * CHAR_REM,
      });
    }
    const placed = layoutTimeline(items, TIMELINE_GEOMETRY);
    const lines = [];
    let lanes = 1;
    shown.forEach((b, i) => {
      const p = placed[i];
      const o = b.x.o;
      const s = b.x.span;
      lanes = Math.max(lanes, p.lane + 1);
      const lane = ';--lane:' + p.lane;
      const done = o.status === 'done' ? ' status-done' : '';
      const markTitle = s.mark ? fill(t.deadlineTitle, { date: formatDeadline(t, o.deadline), title: o.title }) : '';
      const title = s.start ? o.title + ' – ' + spanText(t, s) : markTitle;
      if (b.hasBar) {
        lines.push(
          '          <div class="bar' + (s.allDay ? ' bar-all-day' : '') + done + (b.cutStart ? ' bar-cut-start' : '') + (b.cutEnd ? ' bar-cut-end' : '') +
          '" style="--x:' + pct(b.barStart) + ';--w:' + pct(b.barEnd - b.barStart) + lane + '" data-id="' + esc(o.id) + '"' + colorAttr(t, o) +
          ' title="' + esc(title) + '">' + (p.side === 'inside' ? '<span class="bar-title">' + esc(o.title) + '</span>' : '') + '</div>'
        );
      }
      if (b.hasBar && b.hasMark) {
        lines.push('          <div class="deadline-line" style="--x:' + pct(b.barEnd) + ';--w:' + pct(b.markX - b.barEnd) + lane + '"></div>');
      }
      if (b.hasMark) {
        lines.push('          <div class="deadline-mark' + done + '" style="--x:' + pct(b.markX) + lane + '" data-id="' + esc(o.id) + '" title="' + esc(markTitle) + '"></div>');
      }
      // A title before its bar is anchored by its right edge, so it stays at the bar on a wider page.
      if (p.side !== 'inside' && p.labelWidth > 0) {
        const before = p.side === 'before';
        lines.push(
          '          <span class="bar-label' + (before ? ' bar-label-before' : '') + done + '" style="--x:' + pct(before ? p.labelStart + p.labelWidth : p.labelStart) + lane + (p.shortened ? ';--max:' + num(p.labelWidth) : '') +
          '" data-id="' + esc(o.id) + '" title="' + esc(title) + '">' + esc(o.title) + '</span>'
        );
      }
    });
    const days = [];
    for (let i = 0; i < 7; i++) {
      const day = addDays(week, i);
      days.push('          <h3 class="gantt-date' + (day === today ? ' today' : '') + '"><time datetime="' + day + '">' + esc(formatDateShort(t, day)) + '</time></h3>');
    }
    weeks.push(
      '      <section class="gantt-week" data-week="' + week + '">\n' +
      '        <div class="gantt-days">\n' +
      days.join('\n') + '\n' +
      '        </div>\n' +
      '        <div class="lanes" style="--lanes:' + lanes + '">\n' +
      (lines.length ? lines.join('\n') + '\n' : '') +
      '        </div>\n' +
      '      </section>'
    );
  }
  return (
    '  <section id="timeline" class="' + viewClass('timeline', active) + '" aria-label="' + esc(t.timeline) + '">\n' +
    '    <h2>' + esc(t.timeline) + '</h2>\n' +
    '    <p class="description">' + esc(fill(t.timelineDescription, { from, to })) + '</p>\n' +
    '    <div class="gantt">\n' +
    weeks.join('\n') + '\n' +
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
  t = pageTexts(plan, t);
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
  accent_text: '--accent-text',
  bg: '--bg',
  bg_raised: '--bg-raised',
  fg: '--fg',
  muted: '--muted',
  border: '--border',
  danger: '--danger',
  ok: '--ok',
};
// The palette slots project colours name, lifted to read as text: blue -> --blue, bright_red -> --bright-red.
for (const slot of PALETTE_SLOTS) THEME_TO_CSS[slot] = '--' + slot.replace('_', '-');

// The same rule as isFontFamily() in src/snapshot.cpp: safe to quote in CSS.
const FONT_FAMILY = /^[A-Za-z0-9 ._-]{1,64}$/;

function applyTheme(theme, root) {
  if (!theme) return;
  for (const [key, variable] of Object.entries(THEME_TO_CSS)) {
    if (theme[key]) root.style.setProperty(variable, theme[key]);
  }
  // Omarchy's font; style.css falls back to the device's monospace.
  if (typeof theme.font === 'string' && FONT_FAMILY.test(theme.font)) root.style.setProperty('--font', "'" + theme.font + "'");
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
    // serve has already set this machine's live theme on <html>; plan.json's
    // is only for the page opened statically.
    if (!root.getAttribute('style')) applyTheme(plan.theme, root);
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
    renderListByDate, renderListByProject, taskSpan, layoutTimeline, TIMELINE_GEOMETRY, WEEK_REM, CHAR_REM,
    renderTimeline, renderMain, renderNav, renderMeta, renderFooter,
    renderPage, applyTheme,
  };
}
