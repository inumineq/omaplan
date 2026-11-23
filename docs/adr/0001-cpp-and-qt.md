# 0001 – C++ and Qt Quick rather than Rust

Status: decided 2026-11-23

Omaplan is built in C++ and Qt Quick following the omarchy-app skill.

Rust was considered for speed and size. It gains nothing here: Qt is already installed on Omarchy, so the binary
ends up a few hundred kB, while a Rust GUI typically bundles its toolkit (5–20 MB). Qt Quick draws on the GPU and
has mature building blocks for calendar, timeline and drag and drop. The skill has a finished, tested way to follow
Omarchy's theme and language live, which would have to be built from scratch in Rust. The read-only edition
(`serve`) is kept in the same binary with a small HTTP server over `QTcpServer` (Qt Network), so the project has one
language and no extra dependency.
