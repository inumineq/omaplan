# 0004 – Agenter er førsteklasses brugere

Status: besluttet 2026-11-23

Omaplan skal fungere sømløst med alle AI-grænseflader på en Omarchy-maskine. Derfor:

- Kommandolinjen dækker alt, GUI'en kan (opret, ret, flyt, afslut, slet, fortryd, list, opsummér), med `--json`
  og stabile id'er. GUI'en er én klient blandt flere oven på samme lager.
- `omaplan mcp` udstiller de samme handlinger som MCP-værktøjer over stdio, for agenter der foretrækker MCP.
- `omaplan skill install` lægger en bundtet skill i de agent-mapper, der findes, så agenter ved, hvornår og hvordan
  de bruger Omaplan.
- Ændringer fra agenter ses live i GUI'en og kan fortrydes derfra.

Al logik bor i C++ under både CLI, MCP og QML, så de tre aldrig kan opføre sig forskelligt.
