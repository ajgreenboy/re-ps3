# Web apps: kiosk window and return to the XMB (P6)

Status: implemented on branch wt/p6.

## Behaviour

A registered app of `kind: "web"` (Plex, YouTube TV, Spotify web in `examples/media-apps.json`) no longer goes to the OS default browser by default. `Shell::launchWebApp` (src/shell.cpp) handles the `Action::OpenUrl` row of a `custom:<id>` entry:

| `window` in apps.json | Result |
|---|---|
| `"app"` (default, also when the key is absent) | Chromium `--app=<url>` window, maximised, no tabs or address bar |
| `"kiosk"` | Chromium `--kiosk` (Edge also gets `--edge-kiosk-type=fullscreen`) |
| `"browser"` | the pre-P6 path: `QDesktopServices::openUrl`, untracked |

Both Chromium styles use a dedicated `--user-data-dir=<data>/browser-profile`, so the household's own Edge/Chrome profile and session are never read or touched; logins to Plex/YouTube persist in that profile between launches. Edge is preferred (`Program Files (x86)` then `Program Files`), then Chrome (`Program Files`, `Program Files (x86)`, `%LOCALAPPDATA%`). `XMB_WEB_BROWSER=<exe>` overrides the search and `XMB_WEB_BROWSER=none` forces the no-browser fallback (verification only). With no Chromium browser installed the app falls back to the default browser as before and logs `web: <id> no Chromium browser found, falling back...`.

The process is started through the existing `Shell::startTracked`, the same path as game launches, so everything built for titles applies unchanged: `TitleSession` (src/ps_watcher.*) pauses rendering while the XMB is not in front and restores the XMB foreground with the selection kept when the process ends; the in-game XMB opens on PS tap/hold over the browser window (captured frame as backdrop); Quit Game closes only the tracked pid tree (WM_CLOSE, then terminate, then kill), never any other browser process. Nothing new was added to those layers.

Validation: `ContentRegistry` accepts `window` only as `""`, `app`, `kiosk` or `browser`, keeps it for web apps and drops it for other kinds. The in-XMB app editor has no field for it; an edit preserves the value already in apps.json.

## Known limits

- If a previous browser instance still holds `browser-profile` (XMB killed while the app ran), the new launch forwards to it and its process exits at once, so the XMB reports an immediate return while the old window stays open. Close that window once.
- A web app that opens its own external window (OAuth pop-ups stay inside Chromium; links that hand off to another browser do not) is not tracked.
- The fallback path to the default browser was verified by reading the code only; running it would open a tab in the user's real browser.

## Evidence (scripted, local Bun server on localhost:18765, scratch data dir under C:\dev\out\p6)

Log lines from `XMB_LOG`:

```
web: p6test window=app browser=...\Edge\Application\msedge.exe profile=C:/dev/out/p6/data/browser-profile
launch: ...msedge.exe --user-data-dir=...\browser-profile --no-first-run ... --start-maximized --app=http://localhost:18765/ (tracked)
title: session begin ... selection=video/0/0/custom:p6test
title: render paused (XMB not in front) ...
ingame: shell inGame=1 title=P6 Test
ingame: Quit Game asks: Do you want to quit the game? title=P6 Test
ingame: quit stage 1 WM_CLOSE posted to 1 window(s) of pid 25472, waiting up to 10000 ms
returned from P6 Test exit 0 normal
ingame: quit ended by stage cooperative (WM_CLOSE) title=P6 Test
title: restore end xmbForeground=1
title: selection at return=video/0/0/custom:p6test launch=video/0/0/custom:p6test kept=1
```

The kiosk run logged `--kiosk --edge-kiosk-type=fullscreen http://localhost:18765/` and ended the same way; no process using the scratch profile was left afterwards.