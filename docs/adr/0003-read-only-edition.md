# 0003 – Læseudgave via øjebliksbillede og tailnet

Status: besluttet 2026-11-23

Planen kan læses fra andre enheder via `omaplan serve`, der viser et øjebliksbillede (`plan.json`), som appen skriver
atomisk efter hver ændring. Serveren læser aldrig databasen og kan ikke skrive.

Den binder til 127.0.0.1 og deles med `tailscale serve`; adgang er lig med adgang til tailnettet. Transport af
øjebliksbilledet til en servermaskine er brugerens valg (samme maskine, Syncthing eller rsync over Tailscale).
Det holder appen fri for synk-logik og login.
