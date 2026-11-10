# Safe start and diagnostics (PHASES-HTPC D6)

The HTPC has no keyboard on the couch. A crash loop must end somewhere that still shows the XMB, and a crash must
leave something to read. Everything here is local: nothing is uploaded, and the firmware has no analogue, so every
surface below is **INFERRED** (this build's own).

## Safe mode (`--safe`)

`re-ps3-native.exe --safe` changes what the process starts, never what is stored:

| Skipped | How |
|---|---|
| Network clients: Spotify, Jellyfin, DLNA, Tailscale, Home Assistant, Wake on LAN, Weather, Steam Store | their startup work does not run (Spotify session restore, Tailscale and Weather refresh, Home Assistant and Jellyfin server icons); the rows stay in the columns and, when activated, say "Not available in safe mode. Restart the XMB normally to use it." (`Shell::activate`) |
| Steam artwork, RAWG artwork, web-app site icons, Steam friends and owned-games refresh | the fetches are not requested (`refreshContent`, hero requests, `loadSiteIcon`) |
| The theme | `Shell::loadTheme` draws the default (empty path); the stored `appearance/theme` is untouched and applies again on a normal start |
| Boot extras | no cold-boot intro and no health warning |

There is no media that autostarts in this build (no resume-on-boot, no startup playlist), so nothing needed skipping
for that item; the intro and the Spotify/Jellyfin restore were the only audio or media work at start.

On boot the XMB raises the ordinary notification pop-up (title "Safe Mode", text "Started in safe mode after repeated
errors") 2.5 s after start-up, and Settings > System Settings > Diagnostics says so (below). `--safe` is
allowed with the single-instance guard like the other interactive flags.

## Watchdog (`packaging/watchdog.ps1`)

| Parameter | Default | Meaning |
|---|---|---|
| `-SafeAfterCrashes` | 2 | crashes in a row that switch the next start to `--safe` |
| `-CrashWindowSeconds` | 300 | the window those crashes must fall in |
| `-StableSeconds` | 60 | a run this long clears the crash streak; a safe run this long returns to normal |
| `-MaxRapidRestarts` | 5 | the existing give-up rule; it counts safe-mode starts too |
| `-MaxLogBytes`, `-KeepLogs` | 5 MB, 3 | `watchdog.log` rotation |

A crash is any non-zero exit other than the usage error 2. Every decision is a line in `watchdog.log`: the start mode,
the exit, `crash N of M within the last W s`, the switch to safe mode, a cleared streak, the return to normal, a failed
safe-mode start and the give-up. A failed safe start (under `-MinUptimeSeconds`) is a rapid failed start like any
other, so five in a row (safe or not) end the loop exactly as before. Exit 0 still stops the watchdog.

The watchdog also points the app's log at `<data>\logs\xmb.log` (unless `XMB_LOG` is already set), so a kiosk run
leaves a log at all.

## Crash capture (`src/diagnostics.cpp`)

* An unhandled exception writes `<data>/crashes/crash-YYYYMMDD-HHMMSS-<pid>.dmp` (a minidump with thread info) and a
  `.txt` beside it: `time`, `build` (application version plus the source fingerprint from `build-info.json`),
  `safe_mode`, `pid`, `thread`, `code`, `address`, `module` (faulting module + offset), `dump` (written/failed). The
  older symbolised stack still goes to `XMB_LOG` or `crash.log` afterwards.
* The newest 5 pairs are kept; older ones are deleted at start and after each crash.
* The filter ends the process (`EXCEPTION_EXECUTE_HANDLER`) instead of passing the fault on, so Windows Error
  Reporting never leaves a dialog on the TV that the watchdog cannot see past. The exception code is the exit code.
* `qFatal` aborts without an exception: the log handler writes the same files (`code=qFatal`, with the message) before
  Qt aborts, and a `SIGABRT` handler covers `abort()` and `std::terminate` (`code=qFatal`, message `abort() called`).
* The folder is known only after `--data-dir` is parsed; a crash before that has no dump (only the old stack).
  A stack overflow runs the filter on the exhausted stack and may not manage a dump.

## Where it shows

Settings > System Settings > **Diagnostics** (a native-only row at the end of the page, after Start at Sign-in; a
read-only page like System Information). It is a row of its own because System Information's panel holds about 15
lines and a machine with several drives already fills it:

```
Last crash:  2026/11/09 17:58:03, code qFatal           (or: ..., code 0xc0000005 in re-ps3-native.exe+0x2e2959)
Run mode:    Started in safe mode after repeated errors (or: normal)
Log folder:  C:...datalogs
Crash files: C:...datacrashes (newest 5 kept, never uploaded)
```

"Last crash" reads the newest `.txt` under `<data>/crashes`; "none recorded" when there is none.

## Logs

`XMB_LOG` (and the watchdog's `xmb.log`) rotate at 5 MB on start and while running: `x.log` -> `x.log.1` -> `.2` -> `.3`,
the oldest dropped. `watchdog.log` does the same from PowerShell. There are no other session logs in this build.

## Test trigger

`XMB_TEST_CRASH` is read only when set. Comma list: `after-ms:N` (any run), `normal-after-ms:N`, `safe-after-ms:N`
(only that mode), `kind:qfatal` (qFatal instead of a null write). Example for the watchdog test:
`XMB_TEST_CRASH=normal-after-ms:3000,safe-after-ms:14000` with `-StableSeconds 10`.

## Not done

* Safe mode is chosen by the watchdog only; there is no on-screen way to start it, and no way to leave it without a
  restart (the watchdog does that after a stable safe run, but the run itself stays safe).
* The Home Assistant, Spotify and Jellyfin clients are not individually gated, only their startup work and the rows
  that reach them; a client reached some other way (none known) would still work.
* No "send report" (by design). The Diagnostics row shows only the newest crash; the older files are in the folder it names.
