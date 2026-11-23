# Omaplan

Planlægning til Omarchy: skriv eller diktér dine opgaver i almindelig tekst, lad en AI lægge dem på datoer, og se
planen som overblik, kalender, liste eller tidslinje. GUI og kommandolinje deler samme database, så ændringer fra
CLI eller en agent dukker op i vinduet med det samme (og kan fortrydes derfra).

Byg med `bin/build`, kør tests med `bin/test`, installér som Arch-pakke med `bin/install`. Se `SPEC.md` og
`docs/arkitektur.md`.

## Taster (i vinduet)

| Tast | Handling |
| --- | --- |
| `1` / `2` / `3` / `4` | Skift visning: overblik, kalender, liste, tidslinje |
| `n` eller Space | Ny opgave i almindelig tekst (AI-forslag) |
| `Ctrl+Enter` | Send indtastning / godkend forslag |
| `Esc` | Luk indtastning / kassér forslag |
| `w` / `m` | Kalender: uge / måned |
| `g` / `f` | Liste: skift gruppering / projektfilter |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Fortryd / gendan (virker på tværs af GUI og CLI) |
| `?` | Genvejsoversigt |
| `q` | Luk |

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

[motor.lokal]
api = "ollama"             # ollama | openai (OpenAI-kompatibelt endpoint)
endpoint = "http://127.0.0.1:11434"
model = "dit-valg"         # frit valg -- intet er hårdkodet eller indbygget
```

`--motor`, `--model` og `--endpoint` på kommandolinjen vinder over config. Er der hverken model eller endpoint
for `lokal`, får du en fejl med et eksempel i stedet for et gæt.

**Modeller er kun eksempler** -- vælg selv, fx `gemma4:e2b` eller `qwen3.5:2b` (til Ollama), eller hvad din
llama-server/LM Studio stiller til rådighed. Omaplan henter eller bygger ingen modeller selv.

`bin/smoke-ai [motor]` kalder en rigtig motor manuelt med en kort dansk tekst og skriver svar og tid. Den køres
ikke af `bin/test`.
