# 0002 – Interchangeable AI engines behind one interface

Status: decided 2026-11-23

Omaplan has no fixed AI. An abstract `AiEngine` has implementations for Mistral (via `vibe -p`, so it runs on the
user's Mistral Pro quota), Claude (`claude -p`, the user's subscription), Ollama (local) and an arbitrary command.
All get the same prompt and must answer according to `docs/ai-schema.json`; the app validates.

No API keys in the app, the configuration or the repo: the existing CLIs already hold the user's login.
Mistral is the default in the first version. Compare mode runs two engines on the same text.

Consequence: the app depends on the CLIs' headless flags. If they change, only one `engine_*.cpp` is affected.
