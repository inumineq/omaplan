# Omaplan

Planlægning til Omarchy: skriv eller diktér dine opgaver i almindelig tekst, lad en AI lægge dem på datoer, og se
planen som overblik, kalender, liste eller tidslinje.

Under udvikling. Se `SPEC.md` og `docs/arkitektur.md`.

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
