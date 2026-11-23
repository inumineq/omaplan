# 0001 – C++ og Qt Quick frem for Rust

Status: besluttet 2026-11-23

Omaplan bygges i C++ og Qt Quick efter omarchy-app-skill'en.

Rust blev overvejet for fart og størrelse. Det giver ikke noget her: Qt er allerede installeret på Omarchy, så
binæren bliver få hundrede kB, mens en Rust-GUI typisk pakker sin værktøjskasse ind (5–20 MB). Qt Quick tegner på
GPU'en og har modne byggeklodser til kalender, tidslinje og træk-og-slip. Skill'en har en færdig, afprøvet måde at
følge Omarchys tema og sprog live, som skulle bygges forfra i Rust. Læseudgaven (`serve`) holdes i samme binær med
en lille HTTP-server over `QTcpServer` (Qt Network), så projektet har ét sprog og ingen ekstra afhængighed.
