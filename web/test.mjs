// Test for læseudgaven: validerer web/eksempel/plan.json mod docs/eksport-skema.json
// og tjekker at renderingen af hver visning giver de forventede elementer.
// Ingen afhængigheder: kør med `node web/test.mjs` (valgfrit: `node web/test.mjs --opdater`
// regenererer web/index.html fra eksemplet). Tjekker også at index.html ikke trækker
// eksterne ressourcer, og at statisk markup matcher renderMain() nøjagtigt.

'use strict';

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import app from './app.js'; // CommonJS (module.exports) – default-import giver exports-objektet

const WEB = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.join(WEB, '..');

let fejler = 0;
let ok = 0;

function tjek(betingelse, besked) {
  if (betingelse) {
    ok++;
    console.log('  ok  ' + besked);
  } else {
    fejler++;
    console.error('  FEJL ' + besked);
  }
}

function sektion(navn) {
  console.log('\n== ' + navn + ' ==');
}

// ---------- minimal JSON Schema-validering (det undersæt skemaet bruger) ----------

function typeOK(værdi, type) {
  const t = Array.isArray(type) ? type : [type];
  return t.some((x) => {
    if (x === 'string') return typeof værdi === 'string';
    if (x === 'integer') return typeof værdi === 'number' && Number.isInteger(værdi);
    if (x === 'number') return typeof værdi === 'number';
    if (x === 'object') return værdi !== null && typeof værdi === 'object' && !Array.isArray(værdi);
    if (x === 'array') return Array.isArray(værdi);
    if (x === 'null') return værdi === null;
    if (x === 'boolean') return typeof værdi === 'boolean';
    return false;
  });
}

function valider(node, skema, sti, fejl) {
  if (skema.const !== undefined && node !== skema.const) fejl.push(sti + ': forventede const ' + JSON.stringify(skema.const) + ', fik ' + JSON.stringify(node));
  if (skema.enum && !skema.enum.includes(node)) fejl.push(sti + ': ' + JSON.stringify(node) + ' ikke i enum ' + JSON.stringify(skema.enum));
  if (skema.type && !typeOK(node, skema.type)) fejl.push(sti + ': forkert type, forventede ' + JSON.stringify(skema.type) + ', fik ' + typeof node);
  if (typeof node === 'string') {
    if (skema.minLength !== undefined && node.length < skema.minLength) fejl.push(sti + ': kortere end minLength ' + skema.minLength);
    if (skema.maxLength !== undefined && node.length > skema.maxLength) fejl.push(sti + ': længere end maxLength ' + skema.maxLength);
    if (skema.pattern && !new RegExp(skema.pattern).test(node)) fejl.push(sti + ': matcher ikke pattern ' + skema.pattern + ' (' + JSON.stringify(node) + ')');
  }
  if (typeof node === 'number') {
    if (skema.minimum !== undefined && node < skema.minimum) fejl.push(sti + ': under minimum ' + skema.minimum);
    if (skema.maximum !== undefined && node > skema.maximum) fejl.push(sti + ': over maximum ' + skema.maximum);
  }
  if (node !== null && typeof node === 'object' && !Array.isArray(node)) {
    for (const req of skema.required || []) {
      if (!(req in node)) fejl.push(sti + ': mangler required nøgle "' + req + '"');
    }
    if (skema.additionalProperties === false) {
      for (const nøgle of Object.keys(node)) {
        if (!skema.properties || !(nøgle in skema.properties)) fejl.push(sti + ': ukendt nøgle "' + nøgle + '"');
      }
    }
    for (const [nøgle, underskema] of Object.entries(skema.properties || {})) {
      if (nøgle in node) valider(node[nøgle], underskema, sti + '.' + nøgle, fejl);
    }
  }
  if (Array.isArray(node) && skema.items) {
    node.forEach((element, i) => valider(element, skema.items, sti + '[' + i + ']', fejl));
  }
}

// ---------- læs filer ----------

const plan = JSON.parse(fs.readFileSync(path.join(WEB, 'eksempel', 'plan.json'), 'utf8'));
const skema = JSON.parse(fs.readFileSync(path.join(ROOT, 'docs', 'eksport-skema.json'), 'utf8'));
const indexHtml = fs.readFileSync(path.join(WEB, 'index.html'), 'utf8');
const appJs = fs.readFileSync(path.join(WEB, 'app.js'), 'utf8');
const styleCss = fs.readFileSync(path.join(WEB, 'style.css'), 'utf8');

// i dag står i den genererede index.html (så testen er deterministisk, også når den kører en anden dag)
const iDagMatch = /<main id="plan" class="plan" data-idag="(\d{4}-\d{2}-\d{2})">/.exec(indexHtml);
const iDag = iDagMatch ? iDagMatch[1] : app.iDagStr();

// ---------- 1. plan.json mod skemaet ----------

sektion('plan.json mod docs/eksport-skema.json');
const skemaFejl = [];
valider(plan, skema, 'plan', skemaFejl);
tjek(skemaFejl.length === 0, 'skema-validering uden fejl' + (skemaFejl.length ? ':\n    ' + skemaFejl.join('\n    ') : ''));
tjek(plan.opgaver.length >= 15 && plan.opgaver.length <= 30, 'eksemplet har 15-30 opgaver (' + plan.opgaver.length + ')');

// ---------- 2. semantik i eksemplet ----------

sektion('eksemplet dækker kravene');
const ider = new Set(plan.opgaver.map((o) => o.id));
tjek(ider.size === plan.opgaver.length, 'alle id-er unikke');
tjek(plan.opgaver.some((o) => app.erForfalden(o, iDag)), 'der er forfaldne opgaver (todo før i dag)');
tjek(plan.opgaver.some((o) => !o.start.includes('T') && !o.varighed_min), 'der er heldagsopgaver');
tjek(plan.opgaver.some((o) => o.start.includes('T') && o.varighed_min), 'der er opgaver med tid og varighed');
tjek(plan.opgaver.some((o) => o.gaet && o.gaet.length > 0), 'der er gættede felter');
tjek(plan.opgaver.some((o) => o.status === 'done'), 'der er færdige opgaver');
tjek(new Set(plan.opgaver.map((o) => o.projekt).filter(Boolean)).size >= 3, 'der er flere projekter');
const udaterede = plan.opgaver.filter((o) => !o.start);
tjek(udaterede.length >= 2 && udaterede.length <= 3, 'der er 2-3 udaterede opgaver (Uden dato) (' + udaterede.length + ')');
// overlap: to opgaver samme dag der overlapper i tid
let overlap = false;
for (const o of plan.opgaver) {
  for (const p of plan.opgaver) {
    if (o === p || !o.start || !p.start || !o.start.includes('T') || !p.start.includes('T') || o.start.slice(0, 10) !== p.start.slice(0, 10)) continue;
    const a = app.parseStart(o.start), b = app.parseStart(p.start);
    if (!o.varighed_min || !p.varighed_min) continue;
    const oStart = +a.tid.slice(0, 2) + +a.tid.slice(3, 5) / 60, oSlut = oStart + o.varighed_min / 60;
    const pStart = +b.tid.slice(0, 2) + +b.tid.slice(3, 5) / 60, pSlut = pStart + p.varighed_min / 60;
    if (oStart < pSlut && pStart < oSlut) overlap = true;
  }
}
tjek(overlap, 'der er overlappende opgaver samme dag');

// ---------- 3. rendering af visningerne ----------

sektion('rendering: overblik');
const overblik = app.renderOverblik(plan, iDag);
const overblikDage = [...overblik.matchAll(/<section class="dag[^"]*" data-dato="([^"]+)"/g)].map((m) => m[1]);
const forfaldneDage = [...overblik.matchAll(/<section class="dag forfalden" data-dato="([^"]+)"/g)].map((m) => m[1]);
tjek(forfaldneDage.length === 3, 'tre forfaldne dage øverst (' + forfaldneDage.join(', ') + ')');
tjek(overblik.indexOf('forfalden') < overblik.indexOf('data-dato="' + iDag + '"'), 'forfaldne står før i dag');
const forventedeDage = [];
for (let k = 0; k < 8; k++) forventedeDage.push(app.tilfoejDage(iDag, k));
tjek(forventedeDage.every((d) => overblikDage.includes(d)), 'i dag + 7 dage er alle med (' + forventedeDage[0] + '..' + forventedeDage[7] + ')');
tjek(overblik.includes('badge-i-dag'), 'i dag er markeret');
const overblikOpgaver = [...overblik.matchAll(/<li class="opgave /g)];
const daterede = plan.opgaver.filter((o) => o.start);
const forventedeIOverblik = daterede.filter((o) => {
  const d = o.start.slice(0, 10);
  return (d >= iDag && d <= app.tilfoejDage(iDag, 7)) || app.erForfalden(o, iDag);
});
tjek(overblikOpgaver.length === forventedeIOverblik.length, 'antal opgaver i overblik (' + overblikOpgaver.length + ')');
tjek(udaterede.every((o) => !overblik.includes(o.id)), 'udaterede opgaver er ikke i overblik');
tjek(overblik.includes('09:00–09:45'), 'tidsinterval vises (09:00–09:45)');
tjek(overblik.includes('heldag'), 'heldag vises');
tjek(overblik.includes('gæt: start, varighed'), 'gæt vises med danske navne');

sektion('rendering: liste (dato og projekt)');
const liste = app.renderListeDato(plan, iDag);
const listeProjekt = app.renderListeProjekt(plan, iDag);
const datoGrupper = [...liste.matchAll(/data-gruppe="dato:([^"]+)"/g)].map((m) => m[1]);
const projektGrupper = [...listeProjekt.matchAll(/data-gruppe="projekt:([^"]+)"/g)].map((m) => m[1]);
tjek(datoGrupper.length === new Set(daterede.map((o) => o.start.slice(0, 10))).size, 'liste efter dato: en gruppe pr. start-dato (' + datoGrupper.length + ')');
tjek(projektGrupper.includes('Blog') && projektGrupper.includes('Podcast') && projektGrupper.includes('Kunde A') && projektGrupper.includes('Ingen projekt'), 'liste efter projekt: Blog, Podcast, Kunde A, Ingen projekt');
tjek(projektGrupper[projektGrupper.length - 1] === 'Ingen projekt', 'Ingen projekt ligger til sidst');
const listeLi = [...liste.matchAll(/<li class="opgave /g)].length;
const listeProjektLi = [...listeProjekt.matchAll(/<li class="opgave /g)].length;
tjek(listeLi === plan.opgaver.length && listeProjektLi === plan.opgaver.length, 'alle opgaver med i begge lister (' + listeLi + ')');
tjek(liste.includes('1 opgave') || liste.includes(' opgaver'), 'antal vises pr. gruppe');
tjek(liste.includes('data-gruppe="uden-dato"'), 'liste efter dato har gruppen Uden dato');
tjek(liste.lastIndexOf('data-gruppe="dato:') < liste.indexOf('data-gruppe="uden-dato"'), 'Uden dato ligger sidst i liste efter dato');
tjek(liste.includes('>Uden dato</span>') && liste.includes('ingen dato'), 'Uden dato vises med titel og "ingen dato"');

sektion('rendering: tidslinje');
const tidslinje = app.renderTidslinje(plan, iDag);
const ganttDage = [...tidslinje.matchAll(/<section class="gantt-dag[^"]*" data-dato="([^"]+)"/g)].map((m) => m[1]);
const alleDatoer = new Set();
for (const o of plan.opgaver) {
  if (o.start) alleDatoer.add(o.start.slice(0, 10));
  if (o.frist) alleDatoer.add(o.frist.slice(0, 10));
}
const alle = [...alleDatoer].sort();
const min = alle[0];
const max = alle[alle.length - 1];
tjek(ganttDage[0] === min && ganttDage[ganttDage.length - 1] === max, 'tidslinje spænder ' + min + '..' + max + ' (' + ganttDage.length + ' dage)');
const stangCount = [...tidslinje.matchAll(/<div class="stang[ "]/g)].length;
tjek(stangCount === daterede.length, 'én bjælke pr. dateret opgave (' + stangCount + ' af ' + plan.opgaver.length + ')');
tjek(udaterede.every((o) => !new RegExp('<div class="stang[^"]*"[^>]*data-id="' + o.id + '"').test(tidslinje)), 'udaterede opgaver har ingen bjælke i tidslinjen');
const dag23 = tidslinje.split('data-dato="2026-11-23"')[1].split('</section>')[0];
tjek(dag23.includes('--spor:3'), 'overlap-dagen 2026-11-23 har 3 spor');
tjek((dag23.match(/--spor-index:2/g) || []).length >= 1, 'den overlappende opgave ligger i spor 2');
const fristMaerker = [...tidslinje.matchAll(/<div class="frist-maerke/g)].length;
const forventedeFrister = plan.opgaver.filter((o) => o.frist).length;
tjek(fristMaerker === forventedeFrister, 'frist-mærker for alle opgaver med frist (' + fristMaerker + ')');
tjek(tidslinje.includes('stang-heldag'), 'heldagsbjælker har stang-heldag');

// tidsvindue ud fra data: tidligste start til seneste slut afrundet til hele timer, mindst 08-18
let vs = 8, vu = 18;
const medTid = daterede.filter((o) => o.start.includes('T')).map((o) => {
  const t = app.parseStart(o.start).tid;
  const start = Number(t.slice(0, 2)) + Number(t.slice(3, 5)) / 60;
  return { start, slut: start + (o.varighed_min ? o.varighed_min / 60 : 0.5) };
});
if (medTid.length) {
  vs = Math.min(8, Math.floor(Math.min(...medTid.map((b) => b.start))));
  vu = Math.max(18, Math.ceil(Math.max(...medTid.map((b) => b.slut))));
}
tjek(tidslinje.includes('<div class="gantt" style="--vs:' + vs + ';--vu:' + vu + '">'), 'tidsvindue sættes på .gantt (--vs:' + vs + ';--vu:' + vu + ')');
const skridt = vu - vs > 12 ? 3 : vu - vs > 8 ? 2 : 1;
let forventedeTimer = 0;
for (let t = Math.ceil(vs / skridt) * skridt; t <= vu; t += skridt) forventedeTimer++;
const timerAntal = (tidslinje.match(/<div class="gantt-timer">.*?<\/div>/s) || [''])[0].match(/<span style="--t:/g)?.length || 0;
tjek(timerAntal === forventedeTimer, 'time-aksen har ' + timerAntal + ' labels (skridt ' + skridt + ' t)');
tjek(tidslinje.includes('stang-heldag" style="--start:' + vs + ';--laengde:' + (vu - vs)), 'heldagsbjælke spænder hele vinduet');
// titel ved siden af bjælken når den ikke kan være i den (Møde med Kunde A: 16 tegn i 0,75 t af vinduet 8–18)
tjek(/<span class="stang-label"[^>]*title="[^"]*"[^>]*>Møde med Kunde A<\/span>/.test(tidslinje), 'kort bjælke får titel ved siden af (stang-label med title');
tjek(/<div class="stang stang-heldag"[^>]*><span class="stang-titel">/.test(tidslinje), 'lang bjælke har titlen i sig');
// ingen titel klippes i en for kort bjælke: stang-titel findes kun, når estimatet siger den kan være der
{
  const TEGN_PR_DAG = 64;
  const forKorte = [];
  for (const m of tidslinje.matchAll(/<div class="stang[^"]*" style="--start:([^;]+);--laengde:([^;]+);--spor-index:\d+"[^>]*><span class="stang-titel">([^<]*)</g)) {
    if (m[3].length > (Number(m[2]) * TEGN_PR_DAG) / (vu - vs)) forKorte.push(m[3]);
  }
  tjek(forKorte.length === 0, 'ingen titel i en bjælke, hvor den ikke kan være der (' + forKorte.join(', ') + ')');
}

// ---------- 4. index.html: statisk markup = renderSide, ingen eksterne ressourcer ----------

sektion('index.html (statisk, server-renderet reference)');
const forventet = app.renderSide(plan, iDag);
tjek(indexHtml === forventet, 'index.html er nøjagtig lig renderSide(eksempel, data-idag) – kør `node web/test.mjs --opdater` hvis app.js er ændret');
for (const id of ['id="overblik"', 'id="liste"', 'id="liste-projekt"', 'id="tidslinje"']) {
  tjek(indexHtml.includes('<section ' + id), 'sektion ' + id + ' findes');
}
tjek((indexHtml.match(/href="#[a-z-]+"/g) || []).length >= 5, 'visninger vælges via ankre (href="#…")');
tjek(!/src="https?:|href="https?:|@import|url\(/i.test(indexHtml + appJs + styleCss.replace(/\/\*.*?\*\//gs, '')),
  'ingen eksterne URL\'er, @import eller url() i index.html/app.js/style.css');
tjek(!/<script(?![^>]*src="app\.js")[^>]*src=/.test(indexHtml), 'kun app.js som eksternt script');
tjek(indexHtml.includes('<link rel="stylesheet" href="style.css">'), 'kun style.css som eksternt stylesheet');
tjek(/:root:not\(:has\(:target\)\):not\(:has\(\.view\.aktiv\)\)/.test(styleCss), 'kun én aktiv fane ad gangen (default-fanen slås fra når en anden er valgt)');

// ---------- 5. --opdater ----------

if (process.argv.includes('--opdater')) {
  fs.writeFileSync(path.join(WEB, 'index.html'), app.renderSide(plan, app.iDagStr()));
  console.log('\nindex.html regenereret fra eksempel/plan.json (i dag = ' + app.iDagStr() + ')');
}

// ---------- resultat ----------

console.log('\n' + ok + ' tjek OK, ' + fejler + ' fejlede');
process.exit(fejler === 0 ? 0 : 1);
