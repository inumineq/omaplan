# Eksportformat: `plan.json`

`plan.json` er øjebliksbilledet af planen, som `omaplan` skriver **atomisk** (skriv ved siden af, omdøb) til
`~/.local/share/omaplan/plan.json` efter hver ændring. `omaplan serve` læser kun denne fil – aldrig databasen –
og serverer den som læseudgave sammen med `/plan.json` (se SPEC.md "Læseudgave på tailnettet" og ADR 0003).

Maskinelt skema: `docs/eksport-skema.json` (JSON Schema draft 2020-12, samme stil som `docs/ai-skema.json`).
Et opfundet eksempel ligger i `web/eksempel/plan.json`; `web/test.mjs` validerer det mod skemaet.

## Topniveau

| Nøgle | Type | Note |
|---|---|---|
| `format` | `"omaplan-plan"` | konstant, så filen kan genkendes |
| `version` | heltal ≥ 1 | formatversion; denne udgave er `1` |
| `eksporteret` | ISO 8601 **med offset** | tidspunktet for eksporten, fx `2026-11-23T13:57:00+01:00` |
| `tidszone` | IANA-navn | lokal tidszone, fx `Europe/Copenhagen` |
| `sprog` | BCP 47 | sprog for brugerfladen, fx `da` |
| `tema` | objekt, valgfri | temafarver `#RRGGBB`: `accent`, `accent_fg`, `bg`, `bg_raised`, `fg`, `muted`, `border`, `danger`, `ok` |
| `opgaver` | array | alle opgaver, se nedenfor |

## Opgaver

Felterne følger opgavemodellen i SPEC.md. JSON-nøglerne er ASCII-versioner af spec-navnene (som i
`docs/ai-skema.json`), så maskiner slipper for æ/ø/å i nøgler:

| JSON-nøgle | Spec-felt | Type | Note |
|---|---|---|---|
| `id` | id | UUID | |
| `titel` | titel | tekst, 1–200 tegn | |
| `noter` | noter | tekst, valgfri | |
| `projekt` | projekt | tekst, valgfri | fx `"Blog"`, `"Kunde A"` |
| `prioritet` | prioritet | 1–3 | 1 = høj |
| `status` | status | `"todo"` / `"done"` | |
| `start` | start | `"YYYY-MM-DD"` eller `"YYYY-MM-DDTHH:MM"` | lokal tid, **uden offset** |
| `varighed_min` | varighed | minutter, valgfri | heldagsopgave hvis tom og start uden tid |
| `frist` | frist | dato eller dato+tid, valgfri | lokal tid uden offset, adskilt fra start |
| `gaet` | gæt | array af `start`, `varighed_min`, `frist`, `projekt`, `prioritet` | hvilke felter AI'en har gættet |
| `kilde` | kilde | UUID eller `null` | id på indtastningen i `captures` |
| `oprettet` | oprettet | ISO 8601 med offset | |
| `aendret` | ændret | ISO 8601 med offset | |

### Datoer og tid

- `start` og `frist` er **lokal tid uden offset** (`2026-11-27` eller `2026-11-27T12:00`); offsettet ligger på
  topniveau i `tidszone`, så `2026-11-27T12:00` betyder kl. 12 lokal tid den dag.
- `eksporteret`, `oprettet` og `aendret` er tidspunkter og står **med offset** (`2026-11-23T13:57:00+01:00`).
- Tidszone forskydes aldrig ved eksport; sommer/vintertid ligger i offsettet.

### Semantik som visningerne bruger

- **Forfalden**: `status == "todo"` og (`start`-dato eller `frist`-dato, hvad der kommer først) ligger før i dag.
- **Heldagsopgave**: `start` uden tid og uden `varighed_min`.
- **Overblik** viser i dag + de næste 7 dage (8 datoer) plus forfaldne opgaver øverst.
- **Tidslinje** dækker alle datoer fra tidligste `start`/`frist` til seneste.
- Sortering inden for en dag: opgaver med tid først (efter tidspunkt), så heldagsopgaver, så prioritet, så titel.

## Markup-kontrakten (hvordan `serve` skriver HTML)

`omaplan serve` skal server-rendere præcis samme markup som `web/index.html` i dette repo – `web/index.html` er
referenceimplementeringen, genereret fra `web/eksempel/plan.json` af `web/app.js`. Den fulde kontrakt (elementer,
klasser, data-attributter, tekstformater, Gantt-algoritmen, CSS-variabler) står i **`web/MARKUP.md`**.

Det vigtigste i kort form:

- Siden virker **uden JavaScript**: de fire visninger ligger som `<section class="view">` med id'erne `overblik`,
  `liste`, `liste-projekt` og `tidslinje` i `<main id="plan" data-idag="YYYY-MM-DD">`; visningen vælges med ankre
  (`#liste` osv.) og CSS `:target` (overblik vises som standard).
- Farver ligger som CSS-variabler på `:root` (`--bg`, `--accent`, …), så C++ kan sætte temaet live fra Omarchy;
  `plan.json`'s valgfri `tema` svarer 1:1 til variablerne.
- `web/app.js` henter `plan.json` fra samme oprindelse og renderer samme markup igen (kun når siden åbnes statisk
  eller opdateres); ved `file://` eller fetch-fejl beholdes den server-renderede markup.
- Ingen eksterne CDN'er, fonte eller netværkskald ud over `plan.json` fra samme oprindelse.
- Al tekst HTML-escapes (`&`, `<`, `>`, `"`).
- Brugervendt tekst er dansk; ugen starter mandag; datoer formateres som dokumenteret i `web/MARKUP.md`
  (fx `Mandag 23. november 2026`, `09:00–10:30`, `2 t. 30 min`).
