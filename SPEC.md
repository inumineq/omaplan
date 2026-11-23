# Omaplan – spec

Omaplan er en planlægningsapp til Omarchy-skrivebordet. Du skriver eller dikterer dine opgaver i almindelig tekst,
en AI rydder op og lægger dem på datoer og tidspunkter, og du ser planen som overblik, kalender, liste eller tidslinje.

Primær brug: indholdsproduktion og opgaver over en uge, en måned eller en periode.

## Den ene ting appen gør

Fra rodet tekst til en plan, du kan se og flytte rundt på.

## Brugsforløb

1. Appen åbner på **Overblik**: i dag og de kommende dage som en agenda.
2. `n` (eller Space) åbner **indtastning**: et stort tekstfelt. Man skriver frit eller dikterer med Omarchys
   diktering (Superwhisper skriver i det felt, der har fokus; appen bygger ikke sin egen).
   Eksempel: "podcast-klip til linkedin tirsdag, artikel om overenskomst skal være klar fredag 12, research 3 timer
   onsdag formiddag, nyhedsbrev hver torsdag i november".
3. `Ctrl+Enter` sender teksten til den valgte AI-motor.
4. **Forslag**: AI'ens opgaver vises som en redigerbar liste, før noget gemmes. Felter, AI'en har gættet
   (fx "engang næste uge"), er markeret tydeligt som gæt. Man kan rette, slette og tilføje.
   `Ctrl+Enter` godkender, Esc kasserer.
5. Godkendte opgaver ligger i planen og kan ses i alle visninger.

Første version tilføjer kun nyt. "Planlæg min uge om" er næste version.

## Sammenlign-tilstand

I indtastningen kan man vælge **Sammenlign**: samme tekst sendes til to motorer samtidig (standard: Mistral og
Claude), og forslagene vises side om side. Man godkender det ene. Det er både nyttigt og en måde at se, hvad
motorerne kan.

## Visninger (taster 1–4)

1. **Overblik** (start): agenda for i dag og de næste 7 dage, forfaldne øverst.
2. **Kalender**: uge og måned (`w`/`m` skifter). Opgaver kan trækkes til ny dag/tid.
3. **Liste**: alle opgaver med afkrydsning; gruppér efter dato eller projekt; filtrér på projekt og status.
4. **Tidslinje**: Gantt-stil med varighed og overlap; opgaver kan trækkes og deres længde ændres.

Visningerne er forskellige blik på de samme data. Ingen visning har egen tilstand ud over zoom/filter.

## Opgavemodel

| Felt | Type | Note |
|---|---|---|
| id | UUID | |
| titel | tekst | kort, imperativ eller substantiv |
| noter | tekst | valgfri |
| projekt | tekst | valgfri, fx "Journalisten", "Jour" |
| prioritet | 1–3 | 1 = høj |
| status | `todo` / `done` | |
| start | dato eller dato+tid | lokal tid (Europe/Copenhagen) |
| varighed | minutter | valgfri; heldagsopgave hvis tom og ingen tid |
| frist | dato eller dato+tid | valgfri, adskilt fra start |
| gæt | liste af feltnavne | hvilke felter AI'en har gættet |
| kilde | id på indtastningen | sporbarhed tilbage til den rå tekst |
| oprettet, ændret | tidsstempler | |

Gentagelse ("hver torsdag i november") udfoldes af AI'en til enkeltopgaver i første version. Ægte gentagelsesregler
er næste version.

## AI-motorer

En fælles grænseflade med flere udbydere, valgt i indstillinger og pr. indtastning:

- **Mistral** (standard i første version)
- **Claude** via `claude -p` (brugerens abonnement)
- **Ollama** lokalt, hvis installeret
- **Kommando**: valgfri kommandolinje, der læser prompt på stdin og skriver JSON på stdout

Alle motorer får samme prompt og skal returnere JSON efter samme skema (`docs/ai-skema.json`).
Appen validerer svaret; ugyldigt svar vises som fejl i vinduet med motorens råsvar, aldrig som tavst tab.
Ingen API-nøgler i appen eller i repoet. Se ADR 0002.

## Kommandolinje (uden skærm, til agenter)

Som Hype: hver kommando tager `--json`, exit 0/1, fejl på stderr, `QT_QPA_PLATFORM=offscreen`.

- `omaplan open` – GUI (standard uden argumenter)
- `omaplan add "<tekst>" [--motor mistral|claude|ollama|kommando] [--ja]` – foreslå opgaver; `--ja` gemmer
  forslaget uden at spørge, ellers udskrives det kun
- `omaplan list [--fra DATO] [--til DATO] [--projekt P] [--json]`
- `omaplan done <id>`
- `omaplan snapshot <sti>` – skriv læseudgavens øjebliksbillede (se nedenfor)
- `omaplan serve [--snapshot STI] [--port 8787] [--bind 127.0.0.1]` – læseudgave (tilføjelse, fase 3)
- `omaplan skill install` – installer en kort agent-skill, der er bundtet i binæren

## Læseudgave på tailnettet (tilføjelse, fase 3)

Planen skal kunne læses fra telefon eller anden maskine på brugerens Tailscale-net, kun læse.

- Appen skriver efter hver ændring et øjebliksbillede, `plan.json`, atomisk (skriv ved siden af, omdøb).
- `omaplan serve` kører på en maskine, der kan være server, og viser øjebliksbilledet som en lille,
  server-renderet HTML-side (agenda og uge, mobilvenlig, Omarchy-mørk) plus `/plan.json`. Ingen skrivning,
  ingen JavaScript nødvendig.
- Binder til `127.0.0.1` som standard; deles på tailnettet med `tailscale serve`. Ingen egen login: adgang = tailnet.
- Hvordan øjebliksbilledet kommer til servermaskinen, er brugerens valg (Syncthing, rsync over Tailscale, eller
  serveren er samme maskine). README viser rsync-eksemplet.

## Omarchy-konventioner (fra omarchy-app-skill'en)

Følger temaet live, følger Omarchys sprog live (dansk og engelsk i første version, alle strenge via `qsTr`),
hjørner fra Hyprland, tastatur først (`?` viser genveje, `q` lukker, Space er hovedhandlingen, Ctrl+Z/Ctrl+Shift+Z),
atomisk gemning, fejl vises i vinduet. Installeres med `bin/install` (makepkg), så den findes i app-launcheren.

## Uden for første version

Planlæg om, ægte gentagelser, import fra/eksport til kalender (.ics), Google Kalender, synk mellem maskiner,
skrivning via læseudgaven, notifikationer.

## Faser

1. **Kerne** (Mistral bygger): datamodel og lager, AI-motorer med Mistral og Claude, indtastning og forslag,
   de fire visninger, kommandolinjen undtagen `serve`, tests.
2. **Andet pas** (Claude): gennemgang og forbedring af fase 1, sammenlign-tilstand, polering af visningerne,
   oversættelse til dansk.
3. **Læseudgave**: `snapshot` og `serve`.
4. **Uafhængig gennemgang** (Codex) før installation.
