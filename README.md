# Omaplan

Planlægning til Omarchy: skriv eller diktér dine opgaver i almindelig tekst, lad en AI lægge dem på datoer, og se
planen som overblik, kalender, liste eller tidslinje. GUI og kommandolinje deler samme database, så ændringer fra
CLI eller en agent dukker op i vinduet med det samme (og kan fortrydes derfra).

Byg med `bin/build`, kør tests med `bin/test`, installér som Arch-pakke med `bin/install`. Se `SPEC.md` og
`docs/arkitektur.md`.

## Taster (i vinduet)

Alt kan gøres med tastaturet. `?` viser alle taster i vinduet.

| Tast | Handling |
| --- | --- |
| `1` / `2` / `3` / `4` | Skift visning: overblik, kalender, liste, tidslinje |
| `n` eller Space | Nye opgaver i fri tekst (AI-forslag) |
| `a` | Ny opgave uden AI |
| `j` / `k` eller ↓ / ↑ | Vælg næste / forrige opgave |
| `x` | Færdig / ikke færdig |
| `e` eller Enter | Ret den valgte opgave |
| `Shift+←` / `Shift+→` | Flyt en dag |
| `Shift+↑` / `Shift+↓` | Flyt et kvarter |
| `Delete` eller `d` | Slet |
| `h` / `l` eller ← / → | Forrige / næste uge eller måned |
| `t` | Gå til i dag |
| `w` / `m` | Kalender: uge / måned |
| `g` / `s` / `f` | Liste: gruppering / åbne-færdige-alle / projektfilter |
| `+` / `-` | Tidslinje: zoom |
| `Ctrl+Enter` | Send indtastning / godkend forslag / gem |
| `Alt+S` | Sammenlign to motorer (i indtastningen) |
| `Ctrl+1` / `Ctrl+2` | Godkend venstre / højre forslag ved sammenlign |
| `Ctrl+N` / `Ctrl+Delete` | Tilføj / fjern en opgave i forslaget |
| `Esc` | Luk / kassér / afbryd motoren (kladden bevares) |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Fortryd / gendan (virker på tværs af GUI, CLI og MCP) |
| `q` | Luk |

Med musen kan opgaver trækkes til en anden dag i kalenderen, i ugevisningen også til et andet klokkeslæt. I
tidslinjen ændrer et træk i en bjælkes højre kant varigheden.

## Kommandolinje

Alle kommandoer tager `--json`, giver exit 0/1 og skriver fejl på stderr. Datoer accepterer ISO
(`2026-12-01`, `2026-12-01 14:30`) og danske/engelske udtryk (`"i morgen"`, `"fredag 12"`, `"tomorrow"`,
`"next monday"`, valgfrit `"kl. 14:30"`).

```sh
omaplan add "skriv blogindlæg fredag kl. 14:30, 30 min, projekt Blog"   # AI-forslag (vis først)
omaplan add "køb mælk i morgen" --ja --json                             # gem direkte
omaplan new --titel "Køb mælk" --start "i morgen" --projekt Indkøb
omaplan list --fra "i dag" --til "næste fredag" --json
omaplan show <id>  ·  omaplan edit <id> --frist "fredag"
omaplan move <id> "tirsdag kl. 14:00"  ·  omaplan done <id>  ·  omaplan rm <id>
omaplan projects  ·  omaplan summary --uge --json
omaplan undo --json
omaplan skill install        # bundtet agent-skill i ~/.claude/skills, ~/.codex/skills, ~/.vibe/skills, ...
omaplan snapshot             # skriv plan.json til læseudgaven nu
omaplan serve                # læseudgaven på http://127.0.0.1:8787/
```

## Live

GUI'en poller `PRAGMA data_version` hvert 500 ms og genindlæser ved ændringer fra andre processer, med en kort
besked om kilden ("Ændret fra CLI"). Samtidige skrivninger er sikre (WAL + busy-timeout).

## MCP

`omaplan mcp` er en MCP-server over stdio (JSON-RPC 2.0) med de samme handlinger som kommandolinjen som værktøjer:
`list`, `show`, `new`, `edit`, `move`, `done`, `undone`, `rm`, `undo`, `redo`, `projects`, `summary`, `propose`,
`approve` og `add`. Felterne hedder som CLI'ens flag (`titel`, `start`, `varighed`, `frist`, `projekt`, ...), og
datoer accepterer de samme udtryk. Ændringer får kilden `mcp`, vises live i vinduet og kan fortrydes derfra.

Tilføj den til Claude Code:

```sh
claude mcp add omaplan -- omaplan mcp
```

`propose` gemmer intet: vis forslaget for brugeren, og gem det med `approve` (med `capture_id` fra svaret). `add`
gemmer med det samme og er kun til, når brugeren har bedt om det.

## Læseudgave (telefon og andre maskiner)

Efter hver ændring (fra vinduet, CLI'en eller MCP) skriver Omaplan et øjebliksbillede, `plan.json`, atomisk ved
siden af databasen: `~/.local/share/omaplan/plan.json`. Formatet står i `docs/eksport.md`. Vil du have det et andet
sted, så sæt det i `config.toml` eller med `OMAPLAN_SNAPSHOT`:

```toml
[snapshot]
path = "~/Sync/omaplan/plan.json"
```

`omaplan snapshot [sti]` skriver det med det samme (fx første gang, eller til en anden sti).

`omaplan serve [--port N] [--bind ADR] [--snapshot STI]` viser øjebliksbilledet som en lille, server-renderet side
med overblik, liste og tidslinje, plus `/plan.json`. Den læser kun `plan.json` (aldrig databasen), kan ikke skrive,
og siden virker uden JavaScript. Den binder til `127.0.0.1:8787`; del den på dit tailnet med `tailscale serve`, så
adgang til siden er lig med adgang til tailnettet:

```sh
omaplan serve &                  # http://127.0.0.1:8787/
tailscale serve --bg 8787        # https://<maskinen>.<dit-tailnet>.ts.net/ på tailnettet
```

Kører `serve` på en anden maskine (her kaldet `server`), så flyt øjebliksbilledet derhen, fx med rsync over
Tailscale (eller Syncthing, hvis du allerede bruger det):

```sh
# på maskinen med Omaplan: send plan.json, hver gang den ændres
while inotifywait -qq -e close_write,moved_to ~/.local/share/omaplan/; do
  rsync -az ~/.local/share/omaplan/plan.json server:~/omaplan/plan.json
done

# på server
omaplan serve --snapshot ~/omaplan/plan.json
tailscale serve --bg 8787
```

Siden vælger visning med ankre (`#liste`, `#tidslinje`) eller et link som `/?visning=tidslinje`. Farverne følger
Omarchy-temaet på maskinen, der kører `serve`, og ellers temaet, som det var, da øjebliksbilledet blev skrevet.

## Agent-skill

`omaplan skill install` lægger den bundtede skill (`src/skill/SKILL.md`) i de agent-mapper, der findes
(`~/.claude/skills`, `~/.codex/skills`, `~/.vibe/skills`, `~/.config/opencode/skills`), så agenter ved, hvornår de
skal bruge Omaplan. `omaplan skill uninstall` fjerner den igen.

## AI-motorer

`omaplan add "<tekst>"` (eller `n`/Space i vinduet) spørger en motor om et forslag. Motorer: `vibe` (Mistral Vibe
CLI, standard), `claude` (Claude CLI), `lokal` (en hvilken som helst lokal model via Ollama eller et
OpenAI-kompatibelt endpoint) og `command` (vilkårlig kommando, mest til test).

Konfigurationen ligger i `~/.config/omaplan/config.toml`:

```toml
[motor]
engine = "vibe"            # standardmotor: vibe | claude | lokal | command
compare = "vibe, claude"   # de to motorer, Sammenlign (Alt+S) sender teksten til

[motor.lokal]
api = "ollama"             # ollama | openai (OpenAI-kompatibelt endpoint)
endpoint = "http://127.0.0.1:11434"
model = "dit-valg"         # frit valg -- intet er hårdkodet eller indbygget
```

`--motor`, `--model` og `--endpoint` på kommandolinjen vinder over config. Er der hverken model eller endpoint
for `lokal`, får du en fejl med et eksempel i stedet for et gæt.

**Modeller er kun eksempler** -- vælg selv, fx `gemma4:e2b` eller `qwen3.5:2b` (til Ollama), eller hvad din
llama-server/LM Studio stiller til rådighed. Omaplan henter eller bygger ingen modeller selv.

**Sammenlign:** Tryk `Alt+S` i indtastningen, og teksten går til begge `compare`-motorer på én gang. De to forslag
vises side om side, og du godkender det ene med `Ctrl+1` eller `Ctrl+2`. Begge svar gemmes i `captures`, så de
kan sammenlignes bagefter.

`bin/smoke-ai [motor]` kalder en rigtig motor manuelt med en kort dansk tekst og skriver svar og tid. Den køres
ikke af `bin/test`.
