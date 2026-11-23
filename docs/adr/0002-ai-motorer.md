# 0002 – Udskiftelige AI-motorer bag én grænseflade

Status: besluttet 2026-11-23

Omaplan har ingen fast AI. En abstrakt `AiEngine` har implementeringer for Mistral (via `vibe -p`, så det går på
brugerens Mistral Pro-kvote), Claude (`claude -p`, brugerens abonnement), Ollama (lokalt) og en vilkårlig kommando.
Alle får samme prompt og skal svare efter `docs/ai-skema.json`; appen validerer.

Ingen API-nøgler i appen, konfigurationen eller repoet: de eksisterende CLI'er har allerede brugerens login.
Mistral er standard i første version. Sammenlign-tilstanden kører to motorer på samme tekst.

Konsekvens: appen afhænger af CLI'ernes headless-flag. Ændrer de sig, rammer det kun én `engine_*.cpp`.
