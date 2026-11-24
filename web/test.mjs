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
check(plan.version === 2, 'format version 2');
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

// ---------- 3. rendering the views (English) ----------

section('rendering: overview');
const overview = app.renderOverview(plan, today, en, '');
const overviewDays = [...overview.matchAll(/<section class="day[^"]*" data-date="([^"]+)"/g)].map((m) => m[1]);
const overdueDays = [...overview.matchAll(/<section class="day overdue" data-date="([^"]+)"/g)].map((m) => m[1]);
check(overdueDays.length === 3, 'three overdue days at the top (' + overdueDays.join(', ') + ')');
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
  check(!app.isOverdue(inProgress, today), 'started earlier and due later is not overdue');
  check(app.isOverdue(late, today) && app.isOverdue(pastStart, today), 'past the deadline, or past the start without one, is overdue');
  check(!app.isOverdue(task('done', yesterday, null, 'done'), today), 'a done task is never overdue');
  const mini = { tasks: [inProgress, late, deadlineOnly, pastStart] };
  const ov = app.renderOverview(mini, today, en, '');
  const daySection = (date) => ov.slice(ov.indexOf('data-date="' + date + '"'), ov.indexOf('</section>', ov.indexOf('data-date="' + date + '"')));
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
const timeline = app.renderTimeline(plan, today, en, '');
const ganttDays = [...timeline.matchAll(/<section class="gantt-day[^"]*" data-date="([^"]+)"/g)].map((m) => m[1]);
const allDates = new Set();
for (const o of plan.tasks) {
  if (o.start) allDates.add(o.start.slice(0, 10));
  if (o.deadline) allDates.add(o.deadline.slice(0, 10));
}
const all = [...allDates].sort();
const min = all[0];
const max = all[all.length - 1];
check(ganttDays[0] === min && ganttDays[ganttDays.length - 1] === max, 'the timeline spans ' + min + '..' + max + ' (' + ganttDays.length + ' days)');
const barCount = [...timeline.matchAll(/<div class="bar[ "]/g)].length;
const started = plan.tasks.filter((o) => o.start);
check(barCount === started.length, 'one bar per task with a start (' + barCount + ' of ' + plan.tasks.length + ')');
check(plan.tasks.filter((o) => !o.start).every((o) => !new RegExp('<div class="bar[^"]*"[^>]*data-id="' + o.id + '"').test(timeline)), 'tasks without a start have no bar in the timeline');
const day23 = timeline.split('data-date="2026-11-23"')[1].split('</section>')[0];
check(day23.includes('--tracks:3'), 'the overlap day 2026-11-23 has 3 tracks');
check((day23.match(/--track:2/g) || []).length >= 1, 'the overlapping task is in track 2');
const deadlineMarks = [...timeline.matchAll(/<div class="deadline-mark/g)].length;
const expectedDeadlines = plan.tasks.filter((o) => o.deadline).length;
check(deadlineMarks === expectedDeadlines, 'deadline marks for every task with a deadline (' + deadlineMarks + ')');
check(timeline.includes('bar-all-day'), 'all-day bars have bar-all-day');

// time window from the data: earliest start to latest end rounded to whole hours, at least 08-18
let from = 8, to = 18;
const timed = started.filter((o) => o.start.includes('T')).map((o) => {
  const t = app.parseStart(o.start).time;
  const start = Number(t.slice(0, 2)) + Number(t.slice(3, 5)) / 60;
  return { start, end: start + (o.duration_min ? o.duration_min / 60 : 0.5) };
});
if (timed.length) {
  from = Math.min(8, Math.floor(Math.min(...timed.map((b) => b.start))));
  to = Math.max(18, Math.ceil(Math.max(...timed.map((b) => b.end))));
}
check(timeline.includes('<div class="gantt" style="--from:' + from + ';--to:' + to + '">'), 'the time window is set on .gantt (--from:' + from + ';--to:' + to + ')');
const step = to - from > 12 ? 3 : to - from > 8 ? 2 : 1;
let expectedHours = 0;
for (let h = Math.ceil(from / step) * step; h <= to; h += step) expectedHours++;
const hourCount = (timeline.match(/<div class="gantt-hours">.*?<\/div>/s) || [''])[0].match(/<span style="--hour:/g)?.length || 0;
check(hourCount === expectedHours, 'the hour axis has ' + hourCount + ' labels (step ' + step + ' h)');
check(timeline.includes('bar-all-day" style="--start:' + from + ';--length:' + (to - from)), 'an all-day bar spans the whole window');
// the title beside the bar when it cannot fit (Meeting with Client A: 21 characters in 0.75 h of the window 8-18)
check(/<span class="bar-label"[^>]*title="[^"]*"[^>]*>Meeting with Client A<\/span>/.test(timeline), 'a short bar gets its title beside it (bar-label with title)');
check(/<div class="bar bar-all-day"[^>]*><span class="bar-title">/.test(timeline), 'a long bar has its title inside');
// no title is clipped in a bar that is too short: bar-title only appears when the estimate says it fits
{
  const CHARS_PER_DAY = 64;
  const tooShort = [];
  for (const m of timeline.matchAll(/<div class="bar[^"]*" style="--start:([^;]+);--length:([^;]+);--track:\d+"[^>]*><span class="bar-title">([^<]*)</g)) {
    if (m[3].length > (Number(m[2]) * CHARS_PER_DAY) / (to - from)) tooShort.push(m[3]);
  }
  check(tooShort.length === 0, 'no title inside a bar where it cannot fit (' + tooShort.join(', ') + ')');
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

// ---------- 6. --update ----------

if (process.argv.includes('--update')) {
  for (const [lang, file] of Object.entries(pages)) fs.writeFileSync(path.join(WEB, file), app.renderPage(plan, today, dictionaries[lang]));
  console.log('\nindex.html and index.da.html regenerated from example/plan.json (today = ' + today + ')');
}

// ---------- result ----------

console.log('\n' + passed + ' checks passed, ' + failed + ' failed');
process.exit(failed === 0 ? 0 : 1);
