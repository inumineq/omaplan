# Omaplan – arkitektur

Bygget efter omarchy-app-skill'en (C++ og Qt Quick, qmake6, én binær, Arch-pakke). Skill'en ligger på
byggemaskinen i `~/.claude/skills/omarchy-app/` (`SKILL.md` og `templates.md`) og er den bindende kilde
til projektform, tema, sprog og konventioner. Dette dokument beskriver kun det, der er særligt for Omaplan.

## Moduler (flad `src/`)

| Fil | Ansvar |
|---|---|
| `main.cpp` | opsætning; vælger CLI eller GUI (`open`); giver C++-objekter til QML |
| `cli.{h,cpp}` | kommandolinjen (`add`, `list`, `done`, `snapshot`, `serve`, `skill install`), som Hypes `cli.cpp` |
| `task.{h,cpp}` | værditypen `Task` og dens validering; dato/tid som `QDate`/`QDateTime` i lokal tid |
| `store.{h,cpp}` | SQLite via Qt SQL (`QSQLITE`), skema-version og migrering, transaktioner; eneste sted der skriver data |
| `planmodel.{h,cpp}` | `QAbstractListModel` over opgaverne med roller; filtrering/gruppering via proxy-modeller pr. visning |
| `engine.{h,cpp}` | abstrakt `AiEngine` (asynkron `propose(text, kontekst)` → forslag eller fejl) |
| `engine_vibe.cpp` | Mistral via `vibe -p --output json --max-turns 1 --system-prompt-file …` (Pro-kvoten; ingen nøgle i appen) |
| `engine_claude.cpp` | Claude via `claude -p --output-format json` |
| `engine_ollama.cpp` | Ollama via HTTP på `127.0.0.1:11434`, `format: json` |
| `engine_command.cpp` | vilkårlig kommando: prompt på stdin, JSON på stdout |
| `proposal.{h,cpp}` | parser og validerer motorens JSON mod `docs/ai-skema.json`; markerer gættede felter |
| `prompt.{h,cpp}` | bygger prompten: i dag, tidszone, sprog, eksisterende opgaver i perioden, skemaet |
| `snapshot.{h,cpp}` | skriver `plan.json` atomisk efter hver ændring |
| `server.{h,cpp}` | `serve`: QHttpServer (`qt6-httpserver`), kun GET, server-renderet HTML + `/plan.json` |
| `theme.{h,cpp}`, `translator.{h,cpp}` | fra skill'ens skabeloner, uændret i ånden |
| `Main.qml` + komponenter | vindue, taster, de fire visninger, indtastning, forslag, sammenlign |

Motorerne kører som `QProcess`/netværk asynkront; GUI'en fryser aldrig. Timeout (standard 90 s) og afbryd (Esc)
er påkrævet. Manglende værktøj (fx `vibe` ikke installeret) vises i vinduet med hvad der mangler.

## Data

- `~/.local/share/omaplan/omaplan.db` (SQLite, WAL). `~/.config/omaplan/config.toml` til motorvalg og modeller.
- `~/.local/share/omaplan/plan.json` til læseudgaven.
- Rå indtastninger gemmes (tabellen `captures`) med motor og model, så et forslag kan spores.
- Fortryd/gentag via en kommandostak i C++ (`QUndoStack`), ikke i QML.

## Tid

Alt lagres som lokal tid med tidszone-id; visning via `Qt.locale()` og `translator.language`.
Ugen starter mandag for dansk. Prompten får "i dag" og ugedag eksplicit, så "på tirsdag" kan opløses.

## Test

`bin/test` (Qt Test, offscreen): lager og migrering, `proposal`-validering med gode og dårlige svar (fixtures
fra hver motor), prompt-bygning, snapshot-atomicitet, CLI-exitkoder, og at hver motor kan erstattes af en falsk.
Ingen test kalder en rigtig AI. En separat `bin/smoke-ai` kalder de rigtige motorer manuelt.

## Afhængigheder

Omarchys standard Qt-moduler plus `qt6-httpserver` (kun til `serve`). Intet andet uden en ADR.
