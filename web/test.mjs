// Tests for the read-only edition: validates web/example/plan.json against docs/export-schema.json and checks
// that rendering each view gives the expected elements, in English and Danish.
// No dependencies: run with `node web/test.mjs` (optionally `node web/test.mjs --update` regenerates
// web/index.html and web/index.da.html from the example). Also checks that the pages pull in no external
// resources, and that the static markup matches renderPage() exactly.

'use strict';

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import app from './app.js'; // CommonJS (module.exports): the default import gives the exports object

const WEB = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.join(WEB, '..');

let failed = 0;
let passed = 0;

function check(condition, message) {
  if (condition) {
    passed++;
    console.log('  ok   ' + message);
  } else {
    failed++;
    console.error('  FAIL ' + message);
  }
}

function section(name) {
  console.log('\n== ' + name + ' ==');
}

// ---------- minimal JSON Schema validation (the subset the schema uses) ----------

function typeOK(value, type) {
  const types = Array.isArray(type) ? type : [type];
  return types.some((x) => {
    if (x === 'string') return typeof value === 'string';
    if (x === 'integer') return typeof value === 'number' && Number.isInteger(value);
    if (x === 'number') return typeof value === 'number';
    if (x === 'object') return value !== null && typeof value === 'object' && !Array.isArray(value);
    if (x === 'array') return Array.isArray(value);
    if (x === 'null') return value === null;
    if (x === 'boolean') return typeof value === 'boolean';
    return false;
  });
}

function validate(node, schema, at, errors) {
  if (schema.const !== undefined && node !== schema.const) errors.push(at + ': expected const ' + JSON.stringify(schema.const) + ', got ' + JSON.stringify(node));
  if (schema.enum && !schema.enum.includes(node)) errors.push(at + ': ' + JSON.stringify(node) + ' not in enum ' + JSON.stringify(schema.enum));
  if (schema.type && !typeOK(node, schema.type)) errors.push(at + ': wrong type, expected ' + JSON.stringify(schema.type) + ', got ' + typeof node);
  if (typeof node === 'string') {
    if (schema.minLength !== undefined && node.length < schema.minLength) errors.push(at + ': shorter than minLength ' + schema.minLength);
    if (schema.maxLength !== undefined && node.length > schema.maxLength) errors.push(at + ': longer than maxLength ' + schema.maxLength);
    if (schema.pattern && !new RegExp(schema.pattern).test(node)) errors.push(at + ': does not match pattern ' + schema.pattern + ' (' + JSON.stringify(node) + ')');
  }
  if (typeof node === 'number') {
    if (schema.minimum !== undefined && node < schema.minimum) errors.push(at + ': below minimum ' + schema.minimum);
    if (schema.maximum !== undefined && node > schema.maximum) errors.push(at + ': above maximum ' + schema.maximum);
  }
  if (node !== null && typeof node === 'object' && !Array.isArray(node)) {
    for (const req of schema.required || []) {
      if (!(req in node)) errors.push(at + ': missing required key "' + req + '"');
    }
    if (schema.additionalProperties === false) {
      for (const key of Object.keys(node)) {
        if (!schema.properties || !(key in schema.properties)) errors.push(at + ': unknown key "' + key + '"');
      }
    } else if (schema.additionalProperties && typeof schema.additionalProperties === 'object') {
      for (const key of Object.keys(node)) {
        if (!schema.properties || !(key in schema.properties)) validate(node[key], schema.additionalProperties, at + '.' + key, errors);
      }
    }
    for (const [key, sub] of Object.entries(schema.properties || {})) {
      if (key in node) validate(node[key], sub, at + '.' + key, errors);
    }
  }
  if (Array.isArray(node) && schema.items) {
    node.forEach((element, i) => validate(element, schema.items, at + '[' + i + ']', errors));
  }
}

// ---------- read files ----------

const read = (...parts) => fs.readFileSync(path.join(...parts), 'utf8');
const plan = JSON.parse(read(WEB, 'example', 'plan.json'));
const schema = JSON.parse(read(ROOT, 'docs', 'export-schema.json'));
const en = JSON.parse(read(WEB, 'i18n', 'en.json'));
const da = JSON.parse(read(WEB, 'i18n', 'da.json'));
const pages = { en: 'index.html', da: 'index.da.html' };
const dictionaries = { en, da };
const indexHtml = read(WEB, pages.en);
const appJs = read(WEB, 'app.js');
const styleCss = read(WEB, 'style.css');

// today is written in the generated index.html (so the test is deterministic on any day)
const todayMatch = /<main id="plan" class="plan" data-today="(\d{4}-\d{2}-\d{2})">/.exec(indexHtml);
const today = todayMatch ? todayMatch[1] : app.todayStr();

// ---------- 1. plan.json against the schema ----------

section('plan.json against docs/export-schema.json');
const schemaErrors = [];
validate(plan, schema, 'plan', schemaErrors);
check(schemaErrors.length === 0, 'schema validation without errors' + (schemaErrors.length ? ':\n    ' + schemaErrors.join('\n    ') : ''));
check(plan.version === 3, 'format version 3');
check(plan.tasks.length >= 15 && plan.tasks.length <= 30, 'the example has 15-30 tasks (' + plan.tasks.length + ')');

// ---------- 2. semantics of the example ----------

section('the example covers the requirements');
const ids = new Set(plan.tasks.map((o) => o.id));
check(ids.size === plan.tasks.length, 'all ids unique');
check(plan.tasks.some((o) => app.isOverdue(o, today)), 'there are overdue tasks (todo before today)');
check(plan.tasks.some((o) => o.start && !o.start.includes('T') && !o.duration_min), 'there are all-day tasks');
check(plan.tasks.some((o) => o.start && o.start.includes('T') && o.duration_min), 'there are tasks with time and duration');
check(plan.tasks.some((o) => o.guessed && o.guessed.length > 0), 'there are guessed fields');
check(plan.tasks.some((o) => o.status === 'done'), 'there are done tasks');
check(new Set(plan.tasks.map((o) => o.project).filter(Boolean)).size >= 3, 'there are several projects');
const undated = plan.tasks.filter((o) => !app.planDate(o));
check(undated.length >= 2 && undated.length <= 3, 'there are 2-3 undated tasks (No date) (' + undated.length + ')');
check(plan.tasks.some((o) => !o.start && o.deadline), 'there is a task with only a deadline');
// overlap: two tasks on the same day that overlap in time
let overlap = false;
for (const o of plan.tasks) {
  for (const p of plan.tasks) {
    if (o === p || !o.start || !p.start || !o.start.includes('T') || !p.start.includes('T') || o.start.slice(0, 10) !== p.start.slice(0, 10)) continue;
    if (!o.duration_min || !p.duration_min) continue;
    const a = app.parseStart(o.start), b = app.parseStart(p.start);
    const oStart = +a.time.slice(0, 2) + +a.time.slice(3, 5) / 60, oEnd = oStart + o.duration_min / 60;
    const pStart = +b.time.slice(0, 2) + +b.time.slice(3, 5) / 60, pEnd = pStart + p.duration_min / 60;
    if (oStart < pEnd && pStart < oEnd) overlap = true;
  }
}
check(overlap, 'there are overlapping tasks on the same day');
check(Object.keys(plan.project_colors || {}).length >= 1 && plan.tasks.some((o) => o.project && !(o.project in plan.project_colors)),
  'some projects have a colour and some do not');

section('project colours and the 12-hour clock');
{
  const ov = app.renderOverview(plan, today, en, '');
  check(/data-project="Podcast" data-color="magenta"/.test(ov), 'a task of a coloured project carries data-color');
  check(!/data-project="Blog" data-color/.test(ov), 'a project without a colour carries none');
  check(/class="bar[^"]*"[^>]*data-color="green"/.test(app.renderTimeline(plan, today, en, '')), 'timeline bars carry the colour too');
  const bad = Object.assign({}, plan, { project_colors: { Podcast: 'url(x)', Blog: 'color4' } });
  check(!/data-color/.test(app.renderOverview(bad, today, en, '')), 'only palette slot names get through');
  const twelve = Object.assign({}, plan, { clock: '12h' });
  const list = app.renderListByDate(twelve, en, '');
  check(list.includes('>2:00 PM–') && list.includes('>10:00 AM'), '12h writes 2:00 PM and 10:00 AM');
  check(!app.renderListByDate(plan, en, '').includes(' PM'), '24h is the default');
  check(app.renderMeta(twelve, en).includes('1:57 PM'), 'the export time follows the clock');
}

// ---------- 3. rendering the views (English) ----------

section('rendering: overview');
const overview = app.renderOverview(plan, today, en, '');
const overviewDays = [...overview.matchAll(/<section class="day[^"]*" data-date="([^"]+)"/g)].map((m) => m[1]);
const overdueDays = [...overview.matchAll(/<section class="day overdue" data-date="([^"]+)"/g)].map((m) => m[1]);
check(overdueDays.join() === '2026-11-20,2026-11-21', 'two overdue days at the top, by deadline (' + overdueDays.join(', ') + ')');
check(overview.indexOf('day overdue') < overview.indexOf('data-date="' + today + '"'), 'overdue days come before today');
const expectedDays = [];
for (let k = 0; k < 8; k++) expectedDays.push(app.addDays(today, k));
check(expectedDays.every((d) => overviewDays.includes(d)), 'today + 7 days are all there (' + expectedDays[0] + '..' + expectedDays[7] + ')');
check(overview.includes('badge-today'), 'today is marked');
const overviewTasks = [...overview.matchAll(/<li class="task /g)];
const dated = plan.tasks.filter((o) => app.planDate(o));
const expectedInOverview = dated.filter((o) => {
  const d = app.agendaDate(o, today);
  return (d >= today && d <= app.addDays(today, 7)) || app.isOverdue(o, today);
});
check(overviewTasks.length === expectedInOverview.length, 'number of tasks in the overview (' + overviewTasks.length + ')');
check(undated.every((o) => !overview.includes(o.id)), 'undated tasks are not in the overview');
check(overview.includes('09:00–09:45'), 'a time interval shows (09:00–09:45)');
check(overview.includes('<span class="time time-all-day">all day</span>'), 'all day shows');
check(overview.includes('guessed: start, duration'), 'guessed fields show with their English names');

section('overdue and deadline-only tasks follow src/task.h');
{
  const task = (id, start, deadline, status = 'todo') => ({ id, title: id, priority: 2, status, start, deadline, guessed: [] });
  const yesterday = app.addDays(today, -1), friday = app.addDays(today, 4);
  const inProgress = task('in-progress', yesterday, friday); // started yesterday, due Friday: not overdue
  const late = task('late', app.addDays(today, -3), yesterday); // past its deadline
  const deadlineOnly = task('deadline-only', null, friday);
  const pastStart = task('past-start', yesterday, null);
  const lateStart = task('late-start', app.addDays(today, 2), app.addDays(today, -3)); // due before it was to start
  check(!app.isOverdue(inProgress, today), 'started earlier and due later is not overdue');
  check(app.isOverdue(late, today) && app.isOverdue(pastStart, today), 'past the deadline, or past the start without one, is overdue');
  check(!app.isOverdue(task('done', yesterday, null, 'done'), today), 'a done task is never overdue');
  const mini = { tasks: [inProgress, late, deadlineOnly, pastStart, lateStart] };
  const ov = app.renderOverview(mini, today, en, '');
  const daySection = (date) => ov.slice(ov.indexOf('data-date="' + date + '"'), ov.indexOf('</section>', ov.indexOf('data-date="' + date + '"')));
  const overdueSection = (date) => {
    const at = ov.indexOf('class="day overdue" data-date="' + date + '"');
    return at < 0 ? '' : ov.slice(at, ov.indexOf('</section>', at));
  };
  check(overdueSection(yesterday).includes('data-id="late"'), 'an overdue task is grouped under its deadline');
  check(overdueSection(app.addDays(today, -3)).includes('data-id="late-start"'), 'an overdue task that starts later is grouped under its deadline, not in the future');
  check(overdueSection(yesterday).includes('data-id="past-start"'), 'an overdue task without a deadline is grouped under its start');
  check(!/class="day overdue"[^]*in-progress/.test(ov.slice(0, ov.indexOf('class="day today"'))), 'the task in progress is not under overdue');
  check(daySection(today).includes('data-id="in-progress"'), 'the task in progress shows today');
  check(daySection(friday).includes('data-id="deadline-only"'), 'a deadline-only task shows on its deadline day');
  const ls = app.renderListByDate(mini, en, '');
  check(ls.includes('data-group="date:' + friday + '"') && !ls.includes('data-group="no-date"'), 'a deadline-only task is listed under its deadline date, not No date');
}

section('rendering: list (by date and by project)');
const list = app.renderListByDate(plan, en, '');
const listProject = app.renderListByProject(plan, en, '');
const dateGroups = [...list.matchAll(/data-group="date:([^"]+)"/g)].map((m) => m[1]);
const projectGroups = [...listProject.matchAll(/data-group="(?:project:)?([^"]+)"/g)].map((m) => m[1]);
check(dateGroups.length === new Set(dated.map(app.planDate)).size, 'list by date: one group per start (else deadline) date (' + dateGroups.length + ')');
check(['Blog', 'Podcast', 'Client A', 'no-project'].every((p) => projectGroups.includes(p)), 'list by project: Blog, Podcast, Client A, no-project');
check(projectGroups[projectGroups.length - 1] === 'no-project', 'the group no-project comes last');
check(listProject.includes('<section class="group" data-group="no-project">\n    <h3><span class="group-title">No project</span>'), 'the group no-project is titled No project');
const listLi = [...list.matchAll(/<li class="task /g)].length;
const listProjectLi = [...listProject.matchAll(/<li class="task /g)].length;
check(listLi === plan.tasks.length && listProjectLi === plan.tasks.length, 'every task is in both lists (' + listLi + ')');
check(/<span class="count">(1 task|\d+ tasks)<\/span>/.test(list), 'the count shows per group');
check(list.includes('data-group="no-date"'), 'the list by date has the group No date');
check(list.lastIndexOf('data-group="date:') < list.indexOf('data-group="no-date"'), 'No date comes last in the list by date');
check(list.includes('>No date</span>') && list.includes('>no date</span>'), 'No date shows with its title and "no date"');

section('rendering: timeline');
const unescape = (s) => s.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&amp;/g, '&');
// What a rendered week draws, read back from the markup, in rem at the week's minimum width.
function timelineWeeks(html) {
  const W = app.WEEK_REM;
  const g = app.TIMELINE_GEOMETRY;
  return html.split('<section class="gantt-week" data-week="').slice(1).map((chunk) => {
    const week = chunk.slice(0, 10);
    const parts = [];
    for (const m of chunk.matchAll(/<(div|span) class="([^"]+)" style="([^"]+)"(?: data-id="([^"]+)")?[^>]*>(?:<span class="bar-title">([^<]*)<\/span>)?([^<]*)/g)) {
      const cls = m[2].split(' ');
      const style = Object.fromEntries(m[3].split(';').map((kv) => kv.split(':')).map(([k, v]) => [k, Number(v)]));
      const x = (style['--x'] / 100) * W;
      const part = { kind: cls[0], cls, lane: style['--lane'], id: m[4], inside: m[5], text: m[6] };
      if (part.kind === 'bar') Object.assign(part, { start: x, end: Math.max(x + (style['--w'] / 100) * W, x + 0.35) });
      else if (part.kind === 'deadline-mark') Object.assign(part, { start: x - 0.3, end: x + 0.3 });
      else if (part.kind === 'bar-label') {
        const width = '--max' in style ? style['--max'] : unescape(part.text).length * app.CHAR_REM;
        // a title before its bar is anchored by its right edge
        Object.assign(part, cls.includes('bar-label-before') ? { start: x - width, end: x } : { start: x, end: x + width }, { max: style['--max'] });
      }
      else continue; // the line to a mark is drawn under the title
      parts.push(part);
    }
    return { week, parts, lanes: Number((/--lanes:(\d+)/.exec(chunk) || [])[1]), g };
  });
}
// Every week of a rendered timeline keeps its titles readable: no bar, mark or title meets another task's in its lane
// (at the minimum width, so at any width), a title is inside its bar exactly when it fits, a title outside is not
// on its own bar or mark, nothing leaves the week, and a title is shortened only when no side has room.
function checkTimelineReadable(html, what) {
  const problems = [];
  const eps = 0.02; // two decimals of a percentage of 48rem
  for (const { week, parts, g } of timelineWeeks(html)) {
    for (const p of parts) {
      if (p.kind === 'bar-label' && (p.start < -eps || p.end > app.WEEK_REM + eps)) problems.push(week + ': title off the week: ' + p.text);
      if (p.kind === 'bar') {
        const title = p.inside !== undefined ? unescape(p.inside) : null;
        const label = parts.find((q) => q.kind === 'bar-label' && q.id === p.id);
        const width = (title ?? unescape(label ? label.text : '')).length * app.CHAR_REM;
        const fits = width + 2 * g.pad <= p.end - p.start + eps;
        if ((title !== null) !== fits && !(label && label.max !== undefined)) problems.push(week + ': inside iff it fits: ' + (title ?? label?.text));
      }
    }
    for (let a = 0; a < parts.length; a++) {
      for (let b = a + 1; b < parts.length; b++) {
        const [p, q] = [parts[a], parts[b]];
        if (p.lane !== q.lane) continue;
        const apart = p.end <= q.start + eps || q.end <= p.start + eps;
        if (p.id !== q.id) {
          if (!(p.end + g.spacing <= q.start + eps || q.end + g.spacing <= p.start + eps)) problems.push(week + ': ' + p.kind + ' and ' + q.kind + ' of two tasks meet in lane ' + p.lane + ' (' + (p.text || p.inside || '') + ' / ' + (q.text || q.inside || '') + ')');
        } else if ((p.kind === 'bar-label' || q.kind === 'bar-label') && !apart) {
          problems.push(week + ': a title on its own ' + (p.kind === 'bar-label' ? q.kind : p.kind) + ': ' + (p.text || q.text));
        }
      }
    }
  }
  check(problems.length === 0, what + (problems.length ? ' (' + problems.slice(0, 4).join('; ') + ')' : ''));
}

const timeline = app.renderTimeline(plan, today, en, '');
const weeks = timelineWeeks(timeline);
const allDates = [];
for (const o of plan.tasks) {
  const s = app.taskSpan(o);
  if (s.start) allDates.push(s.start.date, s.end.min === 0 ? app.addDays(s.end.date, -1) : s.end.date);
  if (s.mark) allDates.push(s.mark.date);
}
allDates.sort();
const mondayOf = (d) => app.addDays(d, -((new Date(d + 'T12:00').getDay() + 6) % 7));
check(weeks[0].week === mondayOf(allDates[0]) && weeks[weeks.length - 1].week === mondayOf(allDates[allDates.length - 1]),
  'the timeline runs from the week of ' + allDates[0] + ' to the week of ' + allDates[allDates.length - 1] + ' (' + weeks.length + ' weeks)');
check(weeks.every((w, i) => i === 0 || w.week === app.addDays(weeks[i - 1].week, 7)), 'every week in between is there, empty ones too');
check(weeks.every((w) => (timeline.split('data-week="' + w.week + '"')[1].match(/<h3 class="gantt-date/g) || []).length >= 7), 'each week has seven day headings');
check(timeline.includes('<h3 class="gantt-date today"><time datetime="' + today + '">'), 'today\'s heading has the class today');
const started = plan.tasks.filter((o) => o.start);
check(started.every((o) => timeline.includes('class="bar') && new RegExp('<div class="bar[^"]*"[^>]*data-id="' + o.id + '"').test(timeline)), 'every task with a start has a bar');
check(plan.tasks.filter((o) => !o.start).every((o) => !new RegExp('<div class="bar[^"]*"[^>]*data-id="' + o.id + '"').test(timeline)), 'tasks without a start have no bar in the timeline');
check(plan.tasks.filter((o) => !o.start && o.deadline).every((o) => new RegExp('<div class="deadline-mark[^"]*"[^>]*data-id="' + o.id + '"').test(timeline)), 'a task with only a deadline is a mark');
check(plan.tasks.filter((o) => !o.start && !o.deadline).every((o) => !timeline.includes('data-id="' + o.id + '"')), 'undated tasks are not in the timeline');
// "Draft the newsletter": 24/11 10:00 for an hour, deadline 26/11 17:00 -- a bar, a line and a mark in one lane
{
  const o = plan.tasks.find((x) => x.title === 'Draft the newsletter');
  const mine = weeks.flatMap((w) => w.parts).filter((p) => p.id === o.id);
  const bar = mine.find((p) => p.kind === 'bar');
  const mark = mine.find((p) => p.kind === 'deadline-mark');
  check(bar && mark && bar.lane === mark.lane && mark.start > bar.end && timeline.includes('<div class="deadline-line"'), 'a deadline after the bar is a mark at the end of a line, in the bar\'s lane');
}
check(timeline.includes('bar-all-day'), 'all-day bars have bar-all-day');
check(/<span class="bar-label"[^>]*>Meeting with Client A<\/span>/.test(timeline), 'a short bar gets its title beside it');
check(/<div class="bar[^"]*"[^>]*><span class="bar-title">/.test(timeline), 'a long bar has its title inside');
checkTimelineReadable(timeline, 'the example\'s titles are readable (no overlap, inside iff it fits)');
check(timeline.includes('a day runs from 8 to 18'), 'the description names the hours of a day');

// spans: a whole working week, a span over a weekend into the next week, a timed span over two days
{
  const mini = {
    tasks: [
      { id: 'week', title: 'Write the long feature article about the city budget', status: 'todo', priority: 2, start: '2026-11-23', deadline: '2026-11-27' },
      { id: 'over', title: 'Trip', status: 'todo', priority: 2, start: '2026-11-28', duration_min: 4 * 1440 },
      { id: 'timed', title: 'Conference', status: 'todo', priority: 2, start: '2026-11-24T08:00', duration_min: 3000 },
    ],
  };
  const html = app.renderTimeline(mini, '2026-11-23', en, '');
  const parts = timelineWeeks(html);
  const bars = (id) => parts.flatMap((w) => w.parts.filter((p) => p.kind === 'bar' && p.id === id).map((p) => Object.assign({ week: w.week }, p)));
  const week = bars('week');
  check(week.length === 1 && Math.abs(week[0].start) < 0.02 && Math.abs(week[0].end - (5 / 7) * app.WEEK_REM) < 0.02, 'Monday to a Friday deadline: one bar over five days');
  check(week[0].inside === 'Write the long feature article about the city budget', 'a long title sits inside a long span');
  const over = bars('over');
  check(over.length === 2 && over[0].cls.includes('bar-cut-end') && over[1].cls.includes('bar-cut-start'), 'a span into the next week is a bar in each week, cut where it goes on');
  check(Math.abs(over[1].end - (2 / 7) * app.WEEK_REM) < 0.02, 'Saturday for four days ends with Tuesday');
  const timed = bars('timed');
  // 08:00 on Tuesday to 10:00 on Thursday, in the window 8-18
  check(timed.length === 1 && Math.abs(timed[0].start - (1 / 7) * app.WEEK_REM) < 0.02 && Math.abs(timed[0].end - ((3 + 0.2) / 7) * app.WEEK_REM) < 0.02, 'a timed span runs from its start to its end two days later');
  check(!/--x:-|--w:-/.test(html), 'no negative positions');
}

// generated plans of 1, 10 and 40 tasks: always readable
{
  const words = ['Draft the newsletter', 'Review the pull request', 'Book train tickets', 'Update the website', 'Pay the invoice',
    'Write the long feature article about the city budget', 'Call Client A about the draft', 'Record video', 'Team lunch', 'Read the contract'];
  let seed = 7;
  const rand = (n) => ((seed = (seed * 1103515245 + 12345) % 2147483648) % n);
  for (const count of [1, 10, 40]) {
    const tasks = [];
    for (let i = 0; i < count; i++) {
      const day = app.addDays('2026-11-23', rand(14));
      const hour = String(8 + rand(9)).padStart(2, '0') + ':' + ['00', '15', '30', '45'][rand(4)];
      const o = { id: 'g' + i, title: words[rand(words.length)], status: rand(4) ? 'todo' : 'done', priority: 2 };
      switch (i % 6) {
        case 0: Object.assign(o, { start: day, deadline: app.addDays(day, 1 + rand(5)) }); break;
        case 1: Object.assign(o, { deadline: day }); break;
        case 2: Object.assign(o, { start: day, duration_min: rand(2) ? 2880 : undefined }); break;
        case 3: Object.assign(o, { start: day + 'T' + hour, duration_min: 60, deadline: app.addDays(day, 2) }); break;
        default: Object.assign(o, { start: day + 'T' + hour, duration_min: 15 + 15 * rand(8) });
      }
      tasks.push(o);
    }
    checkTimelineReadable(app.renderTimeline({ tasks }, '2026-11-23', en, ''), count + ' generated tasks: titles readable');
  }
}

// ---------- 4. languages ----------

section('languages');
check(app.pickLanguage('da-DK') === 'da' && app.pickLanguage('da') === 'da', 'navigator.language da-DK / da picks Danish');
check(app.pickLanguage('en-US') === 'en' && app.pickLanguage('fr') === 'en' && app.pickLanguage(undefined) === 'en', 'en-US, fr and no language pick English');
const shape = (o) => Object.keys(o).sort().map((k) => k + ':' + (Array.isArray(o[k]) ? 'array' + o[k].length : typeof o[k])).join(',');
check(shape(en) === shape(da) && shape(en.guessNames) === shape(da.guessNames), 'en.json and da.json have the same keys');
check(en.language === 'en' && da.language === 'da', 'each dictionary names its language');
const pageEn = app.renderPage(plan, today, en);
const pageDa = app.renderPage(plan, today, da);
const texts = {
  en: ['<html lang="en">', 'Monday 23 November 2026', 'Mon 23/11', '1 h 30 min', 'deadline Friday 27 November 2026 at 12:00',
    'Exported <time datetime="2026-11-23T13:57:00+01:00">23 November 2026 at 13:57</time>', '>all day<', '>no date<', 'guessed: start, duration',
    '<span class="badge badge-done">done</span>', '<summary>Notes</summary>', 'Read-only edition · <a href="plan.json">plan.json</a>', '<span class="count">1 task</span>',
    'aria-label="Views"', '<a href="#overview">Overview</a>', '<title>Omaplan – read-only edition</title>'],
  da: ['<html lang="da">', 'Mandag 23. november 2026', 'Man 23/11', '1 t. 30 min', 'frist fredag 27. november 2026 kl. 12:00',
    'Eksporteret <time datetime="2026-11-23T13:57:00+01:00">23. november 2026 kl. 13:57</time>', '>heldag<', '>ingen dato<', 'gæt: start, varighed',
    '<span class="badge badge-done">færdig</span>', '<summary>Noter</summary>', 'Læseudgave (kun læsning) · <a href="plan.json">plan.json</a>', '<span class="count">1 opgave</span>',
    'aria-label="Visninger"', '<a href="#overview">Overblik</a>', '<title>Omaplan – læseudgave</title>'],
};
for (const [lang, page] of [['en', pageEn], ['da', pageDa]]) {
  const missing = texts[lang].filter((s) => !page.includes(s));
  check(missing.length === 0, lang + ': page texts and date formats' + (missing.length ? ' (missing: ' + missing.join(' | ') + ')' : ''));
}
check(!/\{\w+\}/.test(pageEn + pageDa), 'no unfilled {placeholder} in either page');
check(pageDa.replace(/>[^<]*</g, '><').replace(/ (title|aria-label)="[^"]*"/g, '') === pageEn.replace(/>[^<]*</g, '><').replace(/ (title|aria-label)="[^"]*"/g, '').replace('lang="en"', 'lang="da"'),
  'the Danish page has the same markup as the English one, only the texts differ');
check(app.renderMain(plan, today, en, 'timeline').includes('<section id="timeline" class="view active"'), 'an active view gets the class active');
check(!app.renderMain(plan, today, en, '<x>').includes('active"'), 'an unknown view id is ignored');

// ---------- 5. the static pages: markup = renderPage, no external resources ----------

for (const [lang, file] of Object.entries(pages)) {
  section(file + ' (static, server-rendered reference, ' + lang + ')');
  const html = fs.existsSync(path.join(WEB, file)) ? read(WEB, file) : '';
  check(html === app.renderPage(plan, today, dictionaries[lang]), file + ' equals renderPage(example, data-today, ' + lang + ') exactly – run `node web/test.mjs --update` if app.js or the dictionaries changed');
  for (const id of app.VIEWS) check(html.includes('<section id="' + id + '"'), 'section id="' + id + '" exists');
  check((html.match(/href="#[a-z-]+"/g) || []).length >= 5, 'views are chosen through anchors (href="#…")');
  check(!/<script(?![^>]*src="app\.js")[^>]*src=/.test(html), 'only app.js as an external script');
  check(html.includes('<link rel="stylesheet" href="style.css">'), 'only style.css as an external stylesheet');
}
check(!/src="https?:|href="https?:|@import|url\(/i.test(indexHtml + appJs + styleCss.replace(/\/\*.*?\*\//gs, '')),
  'no external URLs, @import or url() in index.html/app.js/style.css');
check(/:root:not\(:has\(:target\)\):not\(:has\(\.view\.active\)\)/.test(styleCss), 'only one active tab at a time (the default tab turns off when another is chosen)');
{
  // every class the pages use has a rule in style.css (or is a deliberate hook without one)
  const hooks = new Set(['view', 'status-todo', 'badge-p2', 'badge-p3', 'group-title', 'deadline', 'duration', 'gantt-date-empty', 'top', 'timezone', 'gantt-rows']);
  const used = new Set();
  for (const m of (pageEn + app.renderMain(plan, today, en, 'list')).matchAll(/class="([^"]+)"/g)) for (const c of m[1].split(' ')) used.add(c);
  const missing = [...used].filter((c) => !hooks.has(c) && !new RegExp('\\.' + c.replace(/-/g, '\\-') + '(?![\\w-])').test(styleCss));
  check(missing.length === 0, 'every class in the markup is styled' + (missing.length ? ' (unstyled: ' + missing.join(', ') + ')' : ''));
}

section('theme: colours and Omarchy font as CSS variables');
{
  // The same variables as Snapshot::themeStyle() in src/snapshot.cpp.
  const set = {};
  const root = { style: { setProperty: (name, value) => { set[name] = value; } } };
  app.applyTheme(plan.theme, root);
  check(set['--accent'] === plan.theme.accent && set['--accent-text'] === plan.theme.accent_text, 'colours become CSS variables');
  check(set['--font'] === "'" + plan.theme.font + "'", 'the font family becomes a quoted --font');
  for (const bad of ["X';}</style>", 'A"B', '', 'Fira;Code', 42]) {
    const out = {};
    app.applyTheme({ font: bad }, { style: { setProperty: (n, v) => { out[n] = v; } } });
    check(out['--font'] === undefined, 'an unsafe font family is left out: ' + JSON.stringify(bad));
  }
  check(/font-family: var\(--font\), monospace;/.test(styleCss) && /--font: monospace;/.test(styleCss), 'style.css falls back to monospace');
}

// ---------- 6. --update ----------

if (process.argv.includes('--update')) {
  for (const [lang, file] of Object.entries(pages)) fs.writeFileSync(path.join(WEB, file), app.renderPage(plan, today, dictionaries[lang]));
  console.log('\nindex.html and index.da.html regenerated from example/plan.json (today = ' + today + ')');
}

// ---------- result ----------

console.log('\n' + passed + ' checks passed, ' + failed + ' failed');
process.exit(failed === 0 ? 0 : 1);
