# 0003 – Read-only edition through a snapshot and the tailnet

Status: decided 2026-11-23

The plan can be read from other devices through `omaplan serve`, which shows a snapshot (`plan.json`) that the app
writes atomically after every change. The server never reads the database and cannot write.

It binds to 127.0.0.1 and is shared with `tailscale serve`; access equals access to the tailnet. Moving the snapshot
to a server machine is the user's choice (same machine, Syncthing or rsync over Tailscale).
That keeps the app free of sync logic and login.
