# Markup-kontrakt for læseudgaven

Dette dokument beskriver præcis den HTML, `omaplan serve` skal skrive, og som `web/index.html` i dette repo er
genereret eksempel på (genereret fra `web/example/plan.json` af `web/app.js`, så JS og server-rendering altid
matcher). Se `docs/export.md` for selve `plan.json`-formatet.

Regler der gælder hele vejen igennem:

- Siden virker **uden JavaScript**. `web/app.js` er valgfri: den henter `plan.json` fra samme oprindelse og
  renderer samme markup igen. Ved `file://` eller fetch-fejl beholdes den server-renderede markup uændret.
- **Ingen eksterne ressourcer**: kun `style.css` og `app.js` (relative stier) og `plan.json` fra samme
  oprindelse. Ingen CDN'er, ingen webfonte, ingen netværkskald ud over `plan.json`.
- Al opgavetekst (`titel`, `noter`, `projekt`, frist-tekster) HTML-escapes: `&` `<` `>` `"` `'`.
- Sprog: dansk (`lang="da"` fra `plan.sprog`). Ugen starter mandag.
- Datoformater (faste, ikke locale-afhængige, så C++/Qt og JS skriver det samme):
  - Lang dato (overskrifter): `Mandag 23. november 2026` – stort begyndelsesbogstav, måned små bogstaver.
  - Kort dato (tidslinje): `Man 23/11`.
  - Klokkeslæt: `09:00` (24-timer, kolon). Interval: `09:00–10:30` (U+2013 en dash).
  - Varighed: `45 min`, `2 t.`, `2 t. 30 min`.
  - Frist i løbende tekst: `frist fredag 27. november 2026` eller `... kl. 12:00` (små bogstaver).
  - Metadata øverst: `Eksporteret 23. november 2026 kl. 13:57` – væggtiden som skrevet i `eksporteret`
    (ingen tidszone-konvertering).
  - `data-*`-datoer er altid ISO `YYYY-MM-DD` / `YYYY-MM-DDTHH:MM`.

## Sidens skelet

```html
<!doctype html>
<html lang="da">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Omaplan – læseudgave</title>
  <link rel="stylesheet" href="style.css">
</head>
<body>
  <header class="top">
    <h1>Omaplan</h1>
    <nav class="visninger" aria-label="Visninger">
      <a href="#overblik">Overblik</a>
      <a href="#liste">Liste</a>
      <a href="#tidslinje">Tidslinje</a>
    </nav>
    <p class="meta">Eksporteret <time datetime="2026-11-23T13:57:00+01:00">23. november 2026 kl. 13:57</time>
      <span class="prik">·</span> <span class="tidszone">Europe/Copenhagen</span>
      <span class="prik">·</span> <span class="sprog">da</span></p>
  </header>
  <main id="plan" class="plan" data-idag="2026-11-23">
    <!-- fire visninger, se nedenfor -->
  </main>
  <footer class="sidefod">
    <p>Læseudgave (kun læsning) · <a href="plan.json">plan.json</a></p>
  </footer>
  <script src="app.js"></script>
</body>
</html>
```

`data-idag` på `<main>` er den lokale dato, overblikket er beregnet fra (`YYYY-MM-DD`).

## Valg af visning uden JS

De fire visninger er søskende-`<section class="view">` inde i `<main>` med id'erne `overblik`, `liste`,
`liste-projekt`, `tidslinje`. Nav-ankrene peger på dem, og CSS skifter:

```css
.view { display: none; }
#overblik { display: block; }              /* standard når intet anker er valgt */
.view:target, .view.aktiv { display: block; }
.plan:has(.view:target) #overblik:not(:target),
.plan:has(.view.aktiv) #overblik:not(.aktiv) { display: none; }
```

Uden JS virker det hele gennem `:target`. Når `app.js` har re-renderet `<main>` efter et fetch, sætter den
også klassen `aktiv` på den valgte visning (og lytter på `hashchange`), fordi nogle browsere ikke genfinder
`:target` på de nye elementer – så virker ankrene også med JS slået til.

**Kun én fane er aktiv ad gangen** (også uden JS): fane-knappen "Overblik" er kun markeret som standard når
hverken et `:target` eller en `.view.aktiv` findes:

```css
body:has(#tidslinje:target) .visninger a[href="#tidslinje"],
body:has(#tidslinje.aktiv) .visninger a[href="#tidslinje"], … {
  color: var(--accent-fg); background: var(--accent);
}
:root:not(:has(:target)):not(:has(.view.aktiv)) .visninger a[href="#overblik"] {
  color: var(--accent-fg); background: var(--accent);
}
```

(`#liste-projekt` tæller som fanen "Liste".)

`serve` forstår også `?visning=liste` (og de andre id'er): den valgte visning får klassen `aktiv` i den
server-renderede markup, så et link kan pege på en visning helt uden JS og uden anker. `app.js` læser samme
parameter, når der ikke er et anker.

`liste` og `liste-projekt` er samme visning med forskellig gruppering; hver af dem har i sin beskrivelse et
link til den anden (`<a href="#liste-projekt">Gruppér efter projekt</a>` / `<a href="#liste">Gruppér efter
dato</a>`).

## Fælles opgave-markup

Alle visninger viser opgaver med samme `<li>`:

```html
<li class="opgave status-todo" data-id="…uuid…" data-prioritet="1" data-projekt="Blog">
  <p class="opgave-rad"><span class="tid">09:00–10:30</span><span class="titel">Møde med Kunde A</span></p>
  <p class="opgave-meta"><span class="projekt-tag">Blog</span><span class="frist">frist fredag 27. november 2026</span><span class="varighed">90 min</span><span class="gaet">gæt: start, varighed</span><span class="badge badge-p1">P1</span><span class="badge badge-done">færdig</span></p>
  <details class="noter"><summary>Noter</summary><p>…</p></details>
</li>
```

- `class="opgave status-todo|status-done"`; `data-prioritet` 1–3; `data-projekt` kun når projekt findes.
- `.tid` = `HH:MM–HH:MM` (eller `HH:MM` uden varighed); heldagsopgaver har `.tid.tid-heldag` med teksten
  `heldag`; opgaver uden dato har `.tid.tid-ingen` med teksten `ingen dato`.
- `.opgave-meta` indeholder kun de felter der findes: `projekt-tag`, `frist`, `varighed`, `gaet`
  (gæt-felter vises med danske navne: `varighed_min` → `varighed`), `badge-p1|p2|p3`, og `badge-done` hvis færdig.
- `noter` kun når opgaven har noter.

## 1. Overblik — `<section id="overblik" class="view">`

```html
<section id="overblik" class="view" aria-label="Overblik">
  <h2>Overblik</h2>
  <p class="beskrivelse">I dag og de næste 7 dage – forfaldne opgaver øverst.</p>
  <section class="dag forfalden" data-dato="2026-11-18">
    <h3><time datetime="2026-11-18">Onsdag 18. november 2026</time><span class="badge badge-forfalden">forfalden</span></h3>
    <ul class="opgaver">…</ul>
  </section>
  <section class="dag i-dag" data-dato="2026-11-23">
    <h3><time datetime="2026-11-23">Mandag 23. november 2026</time><span class="badge badge-i-dag">i dag</span></h3>
    <ul class="opgaver">…</ul>
  </section>
  …
</section>
```

- Først alle forfaldne dage (status `todo` og tidligste af `start`/`frist` før `data-idag`), sorteret stigende,
  hver med `class="dag forfalden"` og badge `forfalden`.
- Så dagene `data-idag` … `data-idag + 7` (8 dage i alt), hver `class="dag"` (i dag også `i-dag`) med badge
  `i dag` på den første. Dage uden opgaver får `<p class="tom">Ingen opgaver.</p>` i stedet for `<ul>`.
- En opgave vises på sin `start`-dato (opgaver uden `start` vises ikke i overblik). Sortering inden for en dag:
  opgaver med tid først (efter tidspunkt), så
  heldagsopgaver, så prioritet (1 før 3), så titel (dansk sortering).

## 2. Liste efter dato — `<section id="liste" class="view">`

```html
<section id="liste" class="view" aria-label="Liste efter dato">
  <h2>Liste</h2>
  <p class="beskrivelse">Alle opgaver grupperet efter dato. <a href="#liste-projekt">Gruppér efter projekt</a></p>
  <section class="gruppe" data-gruppe="dato:2026-11-18">
    <h3><time datetime="2026-11-18">Onsdag 18. november 2026</time><span class="antal">1 opgave</span></h3>
    <ul class="opgaver">…</ul>
  </section>
  …
</section>
```

Alle opgaver, grupperet efter `start`-dato, datoer stigende, samme sortering inden for dagen som overblik.
`antal` = `1 opgave` / `N opgaver`. **Opgaver uden `start` ligger i gruppen `Uden dato` til sidst**:

```html
<section class="gruppe" data-gruppe="uden-dato">
  <h3><span class="gruppe-titel">Uden dato</span><span class="antal">3 opgaver</span></h3>
  <ul class="opgaver">…</ul>
</section>
```

De udaterede opgaver vises ikke i overblik eller tidslinje (kun deres frist-mærke, hvis de har en frist).

## 3. Liste efter projekt — `<section id="liste-projekt" class="view">`

Samme struktur, men `data-gruppe="projekt:Blog"` og overskriften er
`<h3><span class="gruppe-titel">Blog</span><span class="antal">5 opgaver</span></h3>`.
Grupper sorteret efter projektnavn (dansk), `Ingen projekt` til sidst. Opgaver uden projekt ligger i
`Ingen projekt`.

## 4. Tidslinje — `<section id="tidslinje" class="view">`

```html
<section id="tidslinje" class="view" aria-label="Tidslinje">
  <h2>Tidslinje</h2>
  <p class="beskrivelse">Gantt over alle datoer med opgaver eller frister – vindue 8–18, overlap ligger i hver sit spor.</p>
  <div class="gantt" style="--vs:8;--vu:18">
    <div class="gantt-hoved" aria-hidden="true">
      <div class="gantt-dato gantt-dato-tom"></div>
      <div class="gantt-timer"><span style="--t:8">8</span><span style="--t:10">10</span>…<span style="--t:18">18</span></div>
    </div>
    <div class="gantt-raekker">
      <section class="gantt-dag i-dag" data-dato="2026-11-23">
        <h3 class="gantt-dato"><time datetime="2026-11-23">Man 23/11</time></h3>
        <div class="spor" style="--spor:3">
          <div class="stang stang-heldag" style="--start:8;--laengde:10;--spor-index:0" data-id="…" title="…"><span class="stang-titel">…</span></div>
          <div class="stang" style="--start:14;--laengde:1.5;--spor-index:1" data-id="…" title="…"></div>
          <span class="stang-label" style="--start:14;--laengde:1.5;--spor-index:1" data-id="…" title="…">Rediger podcast-klip</span>
          <div class="frist-maerke" style="--start:12" data-id="…" title="frist: …"></div>
        </div>
      </section>
      …
    </div>
  </div>
</section>
```

- Én række pr. dag fra tidligste til seneste dato blandt alle `start` og `frist` (alle dage i spændet, også tomme).
  i dag har klassen `i-dag`. Opgaver uden `start` har ingen bjælke (kun evt. frist-mærke).
- **Tidsvindue**: `--vs`/`--vu` på `.gantt` sættes inline (hele timer) til tidligste start og seneste slut blandt
  opgaver **med tidspunkt**, afrundet til hele timer (`floor`/`ceil`), mindst `8`–`18`. CSS placerer bjælker med
  `left: calc((var(--start) - var(--vs)) / (var(--vu) - var(--vs)) * 100%)` og
  `width: calc(var(--laengde) / (var(--vu) - var(--vs)) * 100%)` — `--start`/`--laengde` er absolutte timer.
- Time-aksen viser labels hver 1/2/3 time efter vinduets bredde som `<span style="--t:8">8</span>`.
- Heldagsopgaver (start uden tid) spænder hele vinduet: `--start: vs`, `--laengde: vu - vs`, klasse `stang-heldag`.
  Opgave med tid men uden varighed: 30 minutters bjælke (`--laengde:0.5`). Færdige opgaver: `status-done` på bjælken.
- **Titel**: står i bjælken (`<span class="stang-titel">`) når den kan være der – konservativt estimat:
  titel-længde ≤ `--laengde` × 64 tegn pr. heldagsvindue (kolonnens mindste bredde er 34rem), delt på
  vinduets længde (`vu - vs`), så en time er smallere i et bredt vindue. Ellers som
  `<span class="stang-label">` lige efter bjælken (samme `--start`, `--laengde`, `--spor-index`, samme
  `title` som bjælken) – aldrig klippet i bjælken, heller ikke når der ikke er plads til højre.
- **Spor-algoritmen** (overlap): sorter dagens bjælker efter start (stigende), ved start længst først; læg hver
  bjælke i det første spor hvis seneste slut ≤ bjælkens start, ellers i et nyt spor. `--spor` = antal spor (min. 1).
- Frister vises som `frist-maerke` (diamant) på frist-dagen ved frist-tidspunktet (`--start` = timer inden for
  vinduet), eller i højre kant (klasse `frist-maerke-heldag`) når fristen kun er en dato eller ligger uden for
  vinduet. De optager ikke spor.
- `title` på bjælker og mærker: `Titel – Man 23/11 14:00–15:30` / `frist: fredag 27. november 2026 kl. 12:00 – Titel`.

## Tema som CSS-variabler

`web/style.css` læser alle farver fra variabler på `:root`, så `serve` kan sætte temaet live (accent fra
Omarchys `colors.toml`, fallback `#FFD60A`):

| CSS-variabel | `plan.json`-nøgle i `tema` | fallback |
|---|---|---|
| `--bg` | `bg` | `#0e0e10` |
| `--bg-raised` | `bg_raised` | `#1a1a21` |
| `--fg` | `fg` | `#e8e8ee` |
| `--muted` | `muted` | `#8b8b98` |
| `--border` | `border` | `#2a2a34` |
| `--accent` | `accent` | `#ffd60a` |
| `--accent-fg` | `accent_fg` | `#000000` |
| `--danger` | `danger` | `#f7768e` |
| `--ok` | `ok` | `#9ece6a` |

`serve` sætter variablerne inline på `<html>` (eller i en lille `<style>`) ud fra temaet;
`app.js` gør det samme fra `plan.json`'s `tema` når siden åbnes statisk. `--radius` sættes fra Hyprlands
`decoration:rounding` (0 på Omarchy-standard).
