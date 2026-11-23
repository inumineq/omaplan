// Omaplan læseudgave – renderer plan.json til samme markup som omaplan serve skriver server-side.
// Ren string-rendering, så både browseren og web/test.mjs (node) kan bruge funktionerne.
// Brugervendt tekst er dansk; kode og kommentarer er engelsk.

'use strict';

const UGE_DAGE = ['mandag', 'tirsdag', 'onsdag', 'torsdag', 'fredag', 'lørdag', 'søndag']; // mandag først
const UGE_DAGE_KORT = ['man', 'tir', 'ons', 'tor', 'fre', 'lør', 'søn'];
const MAANEDER = ['januar', 'februar', 'marts', 'april', 'maj', 'juni', 'juli', 'august', 'september', 'oktober', 'november', 'december'];
const GAET_NAVNE = { start: 'start', varighed_min: 'varighed', frist: 'frist', projekt: 'projekt', prioritet: 'prioritet' };

// ---------- små hjælpere ----------

function esc(s) {
  return String(s)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function stort(s) {
  return s.charAt(0).toUpperCase() + s.slice(1);
}

// "YYYY-MM-DD" eller "YYYY-MM-DDTHH:MM" -> { dato, tid }
function parseStart(s) {
  const m = /^(\d{4}-\d{2}-\d{2})(?:T(\d{2}):(\d{2}))?$/.exec(s);
  if (!m) throw new Error('Ugyldig dato: ' + s);
  return { dato: m[1], tid: m[2] ? m[2] + ':' + m[3] : null };
}

function datoObj(dato) {
  const dele = dato.split('-').map(Number);
  return new Date(dele[0], dele[1] - 1, dele[2]);
}

function datoStr(dt) {
  const p = (n) => String(n).padStart(2, '0');
  return dt.getFullYear() + '-' + p(dt.getMonth() + 1) + '-' + p(dt.getDate());
}

function iDagStr() {
  return datoStr(new Date());
}

function tilfoejDage(dato, n) {
  const dt = datoObj(dato);
  dt.setDate(dt.getDate() + n);
  return datoStr(dt);
}

function ugeDag(dato) {
  return UGE_DAGE[(datoObj(dato).getDay() + 6) % 7]; // ugen starter mandag
}

function ugeDagKort(dato) {
  return UGE_DAGE_KORT[(datoObj(dato).getDay() + 6) % 7];
}

function maaned(dato) {
  return MAANEDER[Number(dato.slice(5, 7)) - 1];
}

function dagNr(dato) {
  return String(Number(dato.slice(8, 10)));
}

// "Mandag 23. november 2026"
function formaterDatoLang(dato) {
  return stort(ugeDag(dato)) + ' ' + dagNr(dato) + '. ' + maaned(dato) + ' ' + dato.slice(0, 4);
}

// "Man 23/11"
function formaterDatoKort(dato) {
  return stort(ugeDagKort(dato)) + ' ' + dagNr(dato) + '/' + dato.slice(5, 7);
}

// "fredag 27. november 2026 kl. 12:00" (løbende tekst, små bogstaver)
function formaterFrist(frist) {
  const p = parseStart(frist);
  return ugeDag(p.dato) + ' ' + dagNr(p.dato) + '. ' + maaned(p.dato) + ' ' + p.dato.slice(0, 4) + (p.tid ? ' kl. ' + p.tid : '');
}

// "23. november 2026 kl. 13:57" – væggtiden som skrevet i filen (ingen tidszone-konvertering)
function formaterTidsstempel(iso) {
  const p = parseStart(iso.slice(0, 16));
  return dagNr(p.dato) + '. ' + maaned(p.dato) + ' ' + p.dato.slice(0, 4) + ' kl. ' + p.tid;
}

// "45 min", "2 t.", "2 t. 30 min"
function formaterVarighed(min) {
  if (min < 60) return min + ' min';
  const t = Math.floor(min / 60);
  const m = min % 60;
  return m ? t + ' t. ' + m + ' min' : t + ' t.';
}

function slutTid(tid, varighedMin) {
  const h = Number(tid.slice(0, 2));
  const m = Number(tid.slice(3, 5));
  const slut = h * 60 + m + varighedMin;
  const p = (n) => String(n).padStart(2, '0');
  return p(Math.floor(slut / 60) % 24) + ':' + p(slut % 60);
}

function antal(n, ental) {
  return n === 1 ? '1 ' + ental : n + ' ' + ental + 'r';
}

// Tal i style-attributter uden komma og uden unødige nuller: 9, 1.5, 24
function num(x) {
  return String(Number(x.toFixed(2)));
}

// ---------- domænelogik ----------

function tidligsteDato(o) {
  const datoer = [o.start.slice(0, 10)];
  if (o.frist) datoer.push(o.frist.slice(0, 10));
  return datoer.sort()[0];
}

function erForfalden(o, iDag) {
  return o.status === 'todo' && tidligsteDato(o) < iDag;
}

// Inden for en dag: opgaver med tid først (efter tidspunkt), så heldag, så prioritet, så titel
function sorterOpgaver(opgaver) {
  return [...opgaver].sort((a, b) => {
    const sa = parseStart(a.start);
    const sb = parseStart(b.start);
    if (!!sa.tid !== !!sb.tid) return sa.tid ? -1 : 1;
    if (sa.tid && sb.tid && sa.tid !== sb.tid) return sa.tid < sb.tid ? -1 : 1;
    if (a.prioritet !== b.prioritet) return a.prioritet - b.prioritet;
    return a.titel.localeCompare(b.titel, 'da');
  });
}

// ---------- markup ----------

function renderOpgave(o) {
  const s = parseStart(o.start);
  let tidTekst;
  if (s.tid) {
    tidTekst = o.varighed_min ? s.tid + '–' + slutTid(s.tid, o.varighed_min) : s.tid;
  } else {
    tidTekst = 'heldag';
  }
  const meta = [];
  if (o.projekt) meta.push('<span class="projekt-tag">' + esc(o.projekt) + '</span>');
  if (o.frist) meta.push('<span class="frist">frist ' + esc(formaterFrist(o.frist)) + '</span>');
  if (o.varighed_min) meta.push('<span class="varighed">' + esc(formaterVarighed(o.varighed_min)) + '</span>');
  if (o.gaet && o.gaet.length) meta.push('<span class="gaet">gæt: ' + esc(o.gaet.map((g) => GAET_NAVNE[g] || g).join(', ')) + '</span>');
  meta.push('<span class="badge badge-p' + o.prioritet + '">P' + o.prioritet + '</span>');
  if (o.status === 'done') meta.push('<span class="badge badge-done">færdig</span>');
  const projektAttr = o.projekt ? ' data-projekt="' + esc(o.projekt) + '"' : '';
  const noter = o.noter ? '\n    <details class="noter"><summary>Noter</summary><p>' + esc(o.noter) + '</p></details>' : '';
  return (
    '<li class="opgave status-' + o.status + '" data-id="' + esc(o.id) + '" data-prioritet="' + o.prioritet + '"' + projektAttr + '>\n' +
    '    <p class="opgave-rad"><span class="tid' + (s.tid ? '' : ' tid-heldag') + '">' + esc(tidTekst) + '</span><span class="titel">' + esc(o.titel) + '</span></p>\n' +
    '    <p class="opgave-meta">' + meta.join('') + '</p>' + noter + '\n' +
    '  </li>'
  );
}

function renderOpgaveListe(opgaver) {
  return '<ul class="opgaver">\n' + opgaver.map((o) => '    ' + renderOpgave(o).replace(/\n/g, '\n    ')).join('\n') + '\n  </ul>';
}

function renderDagSektion(dato, opgaver, ekstraKlasse, badge) {
  const badgeHtml = badge ? '<span class="badge ' + badge.klasse + '">' + badge.tekst + '</span>' : '';
  const indhold = opgaver.length
    ? renderOpgaveListe(opgaver)
    : '    <p class="tom">Ingen opgaver.</p>';
  return (
    '  <section class="dag' + (ekstraKlasse ? ' ' + ekstraKlasse : '') + '" data-dato="' + dato + '">\n' +
    '    <h3><time datetime="' + dato + '">' + esc(formaterDatoLang(dato)) + '</time>' + badgeHtml + '</h3>\n' +
    indhold + '\n' +
    '  </section>'
  );
}

function renderOverblik(plan, iDag) {
  const opgaver = plan.opgaver;
  const forfaldne = opgaver.filter((o) => erForfalden(o, iDag));
  const grupper = new Map();
  for (const o of forfaldne) {
    const d = tidligsteDato(o);
    if (!grupper.has(d)) grupper.set(d, []);
    grupper.get(d).push(o);
  }
  const dele = [];
  for (const dato of [...grupper.keys()].sort()) {
    dele.push(renderDagSektion(dato, sorterOpgaver(grupper.get(dato)), 'forfalden', { klasse: 'badge-forfalden', tekst: 'forfalden' }));
  }
  for (let k = 0; k < 8; k++) {
    const dato = tilfoejDage(iDag, k);
    const paaDag = opgaver.filter((o) => o.start.slice(0, 10) === dato);
    const badge = k === 0 ? { klasse: 'badge-i-dag', tekst: 'i dag' } : null;
    dele.push(renderDagSektion(dato, sorterOpgaver(paaDag), k === 0 ? 'i-dag' : '', badge));
  }
  return (
    '  <section id="overblik" class="view" aria-label="Overblik">\n' +
    '    <h2>Overblik</h2>\n' +
    '    <p class="beskrivelse">I dag og de næste 7 dage – forfaldne opgaver øverst.</p>\n' +
    dele.join('\n') + '\n' +
    '  </section>'
  );
}

function renderListeDato(plan) {
  const grupper = new Map();
  for (const o of plan.opgaver) {
    const d = o.start.slice(0, 10);
    if (!grupper.has(d)) grupper.set(d, []);
    grupper.get(d).push(o);
  }
  const dele = [];
  for (const dato of [...grupper.keys()].sort()) {
    const opgaver = sorterOpgaver(grupper.get(dato));
    dele.push(
      '  <section class="gruppe" data-gruppe="dato:' + dato + '">\n' +
      '    <h3><time datetime="' + dato + '">' + esc(formaterDatoLang(dato)) + '</time><span class="antal">' + antal(opgaver.length, 'opgave') + '</span></h3>\n' +
      renderOpgaveListe(opgaver) + '\n' +
      '  </section>'
    );
  }
  return (
    '  <section id="liste" class="view" aria-label="Liste efter dato">\n' +
    '    <h2>Liste</h2>\n' +
    '    <p class="beskrivelse">Alle opgaver grupperet efter dato. <a href="#liste-projekt">Gruppér efter projekt</a></p>\n' +
    dele.join('\n') + '\n' +
    '  </section>'
  );
}

function renderListeProjekt(plan) {
  const INGEN = 'Ingen projekt';
  const grupper = new Map();
  for (const o of plan.opgaver) {
    const p = o.projekt || INGEN;
    if (!grupper.has(p)) grupper.set(p, []);
    grupper.get(p).push(o);
  }
  const navne = [...grupper.keys()].sort((a, b) => (a === INGEN ? 1 : b === INGEN ? -1 : a.localeCompare(b, 'da')));
  const dele = [];
  for (const navn of navne) {
    const opgaver = sorterOpgaver(grupper.get(navn));
    dele.push(
      '  <section class="gruppe" data-gruppe="projekt:' + esc(navn) + '">\n' +
      '    <h3><span class="gruppe-titel">' + esc(navn) + '</span><span class="antal">' + antal(opgaver.length, 'opgave') + '</span></h3>\n' +
      renderOpgaveListe(opgaver) + '\n' +
      '  </section>'
    );
  }
  return (
    '  <section id="liste-projekt" class="view" aria-label="Liste efter projekt">\n' +
    '    <h2>Liste efter projekt</h2>\n' +
    '    <p class="beskrivelse">Alle opgaver grupperet efter projekt. <a href="#liste">Gruppér efter dato</a></p>\n' +
    dele.join('\n') + '\n' +
    '  </section>'
  );
}

// Gantt: én række pr. dag i hela planens spænd (start- og frist-datoer).
// Algoritme for spor (lanes): sorter bjælker efter start, så længst først; læg hver bjælke i første spor
// hvor den ikke overlapper sporets seneste bjælke (overlap hvis start < sporets slut).
function renderTidslinje(plan, iDag) {
  const datoer = new Set();
  for (const o of plan.opgaver) {
    datoer.add(o.start.slice(0, 10));
    if (o.frist) datoer.add(o.frist.slice(0, 10));
  }
  let dage = [...datoer].sort();
  if (dage.length === 0) dage = [iDag];
  const foerste = dage[0];
  const sidste = dage[dage.length - 1];
  const raekke = [];
  for (let d = foerste; d <= sidste; d = tilfoejDage(d, 1)) raekke.push(d);

  const dagDele = [];
  for (const dag of raekke) {
    const bjælker = plan.opgaver
      .filter((o) => o.start.slice(0, 10) === dag)
      .map((o) => {
        const s = parseStart(o.start);
        const start = s.tid ? Number(s.tid.slice(0, 2)) + Number(s.tid.slice(3, 5)) / 60 : 0;
        const laengde = s.tid ? (o.varighed_min ? o.varighed_min / 60 : 0.5) : 24;
        return { o, s, start, laengde, heldag: !s.tid };
      })
      .sort((a, b) => a.start - b.start || b.laengde - a.laengde || a.o.titel.localeCompare(b.o.titel, 'da'));
    const sporSlut = [];
    for (const b of bjælker) {
      let spor = sporSlut.findIndex((slut) => slut <= b.start + 1e-9);
      if (spor === -1) {
        sporSlut.push(0);
        spor = sporSlut.length - 1;
      }
      sporSlut[spor] = b.start + b.laengde;
      b.spor = spor;
    }
    const sporAntal = Math.max(1, sporSlut.length);
    const stangDele = bjælker.map((b) => {
      const titel = b.o.titel + ' – ' + formaterDatoKort(dag) + (b.s.tid ? ' ' + b.s.tid + (b.o.varighed_min ? '–' + slutTid(b.s.tid, b.o.varighed_min) : '') : ' (heldag)');
      return (
        '        <div class="stang' + (b.heldag ? ' stang-heldag' : '') + (b.o.status === 'done' ? ' status-done' : '') + '" style="--start:' + num(b.start) + ';--laengde:' + num(b.laengde) + ';--spor-index:' + b.spor + '" data-id="' + esc(b.o.id) + '" title="' + esc(titel) + '"><span class="stang-titel">' + esc(b.o.titel) + '</span></div>'
      );
    });
    const fristDele = plan.opgaver
      .filter((o) => o.frist && o.frist.slice(0, 10) === dag)
      .map((o) => {
        const f = parseStart(o.frist);
        const start = f.tid ? Number(f.tid.slice(0, 2)) + Number(f.tid.slice(3, 5)) / 60 : 24;
        return '        <div class="frist-maerke' + (f.tid ? '' : ' frist-maerke-heldag') + '" style="--start:' + num(start) + '" data-id="' + esc(o.id) + '" title="frist: ' + esc(formaterFrist(o.frist)) + ' – ' + esc(o.titel) + '"></div>';
      });
    dagDele.push(
      '    <section class="gantt-dag' + (dag === iDag ? ' i-dag' : '') + '" data-dato="' + dag + '">\n' +
      '      <h3 class="gantt-dato"><time datetime="' + dag + '">' + esc(formaterDatoKort(dag)) + '</time></h3>\n' +
      '      <div class="spor" style="--spor:' + sporAntal + '">\n' +
      [...stangDele, ...fristDele].join('\n') + '\n' +
      '      </div>\n' +
      '    </section>'
    );
  }
  return (
    '  <section id="tidslinje" class="view" aria-label="Tidslinje">\n' +
    '    <h2>Tidslinje</h2>\n' +
    '    <p class="beskrivelse">Gantt over alle datoer med opgaver eller frister – overlap ligger i hver sit spor.</p>\n' +
    '    <div class="gantt">\n' +
    '      <div class="gantt-hoved" aria-hidden="true">\n' +
    '        <div class="gantt-dato gantt-dato-tom"></div>\n' +
    '        <div class="gantt-timer"><span style="--t:0">0</span><span style="--t:6">6</span><span style="--t:12">12</span><span style="--t:18">18</span><span style="--t:24">24</span></div>\n' +
    '      </div>\n' +
    '      <div class="gantt-raekker">\n' +
    dagDele.join('\n') + '\n' +
    '      </div>\n' +
    '    </div>\n' +
    '  </section>'
  );
}

function renderMain(plan, iDag) {
  return [renderOverblik(plan, iDag), renderListeDato(plan), renderListeProjekt(plan), renderTidslinje(plan, iDag)].join('\n');
}

function renderMeta(plan) {
  return (
    'Eksporteret <time datetime="' + esc(plan.eksporteret) + '">' + esc(formaterTidsstempel(plan.eksporteret)) + '</time>' +
    ' <span class="prik">·</span> <span class="tidszone">' + esc(plan.tidszone) + '</span>' +
    ' <span class="prik">·</span> <span class="sprog">' + esc(plan.sprog) + '</span>'
  );
}

function renderSide(plan, iDag) {
  return (
    '<!doctype html>\n' +
    '<html lang="' + esc(plan.sprog || 'da') + '">\n' +
    '<head>\n' +
    '<meta charset="utf-8">\n' +
    '<meta name="viewport" content="width=device-width, initial-scale=1">\n' +
    '<title>Omaplan – læseudgave</title>\n' +
    '<link rel="stylesheet" href="style.css">\n' +
    '</head>\n' +
    '<body>\n' +
    '<header class="top">\n' +
    '  <h1>Omaplan</h1>\n' +
    '  <nav class="visninger" aria-label="Visninger">\n' +
    '    <a href="#overblik">Overblik</a>\n' +
    '    <a href="#liste">Liste</a>\n' +
    '    <a href="#tidslinje">Tidslinje</a>\n' +
    '  </nav>\n' +
    '  <p class="meta">' + renderMeta(plan) + '</p>\n' +
    '</header>\n' +
    '<main id="plan" class="plan" data-idag="' + iDag + '">\n' +
    renderMain(plan, iDag) + '\n' +
    '</main>\n' +
    '<footer class="sidefod">\n' +
    '  <p>Læseudgave (kun læsning) · <a href="plan.json">plan.json</a></p>\n' +
    '</footer>\n' +
    '<script src="app.js"></script>\n' +
    '</body>\n' +
    '</html>\n'
  );
}

// ---------- tema (valgfrit fra plan.json) ----------

const TEMA_TIL_CSS = {
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

function anvendTema(tema, root) {
  if (!tema) return;
  for (const [noegle, variabel] of Object.entries(TEMA_TIL_CSS)) {
    if (tema[noegle]) root.style.setProperty(variabel, tema[noegle]);
  }
}

// ---------- browser-bootstrap (valgfri; siden virker uden JS) ----------

// Efter JS har erstattet main.innerHTML mister nogle browsere :target på det nye element,
// så sæt også klassen .aktiv (CSS uden JS bruger kun :target).
function sætAktivVisning() {
  const målId = window.location.hash.replace(/^#/, '');
  const mål = målId ? document.getElementById(målId) : null;
  for (const v of document.querySelectorAll('#plan .view')) {
    v.classList.toggle('aktiv', v === mål);
  }
}

async function boot() {
  const main = document.getElementById('plan');
  // plan.json fra samme oprindelse først; eksempel-mappen som fallback når siden åbnes statisk.
  for (const url of ['plan.json', 'eksempel/plan.json']) {
    try {
      const res = await fetch(url, { cache: 'no-store' });
      if (!res.ok) continue;
      const plan = await res.json();
      if (!plan || plan.format !== 'omaplan-plan') continue;
      anvendTema(plan.tema, document.documentElement);
      document.documentElement.lang = plan.sprog || 'da';
      const iDag = iDagStr();
      main.setAttribute('data-idag', iDag);
      main.innerHTML = renderMain(plan, iDag);
      sætAktivVisning();
      const meta = document.querySelector('header .meta');
      if (meta) meta.innerHTML = renderMeta(plan);
      main.dataset.js = 'ja'; // markør til test: JS har renderet siden
      return;
    } catch (e) {
      // prøv næste url; ved file:// eller fetch-fejl beholdes server-renderet markup
    }
  }
}

if (typeof document !== 'undefined') {
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
  else boot();
  window.addEventListener('hashchange', sætAktivVisning);
}

// Eksport til test i node (uden DOM).
if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    parseStart, iDagStr, tilfoejDage, formaterDatoLang, formaterDatoKort, formaterFrist,
    formaterVarighed, formaterTidsstempel, slutTid, erForfalden, sorterOpgaver,
    renderOpgave, renderOverblik, renderListeDato, renderListeProjekt, renderTidslinje,
    renderMain, renderMeta, renderSide, anvendTema,
  };
}
