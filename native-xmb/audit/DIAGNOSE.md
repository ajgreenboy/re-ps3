# `--diagnose`: design for a bounded diagnostic bundle

Status: **design only, not implemented.** There is no `--diagnose` flag in `src/main.cpp` today and no
collector in `work/`. This document specifies what the flag must do so the implementation can be
small, reviewable and safe. It is the Phase 7.2 deliverable; the crash-reporting half of 7.2 already
ships (section 8).

Goal: one command produces a bundle that explains a failure on a machine the maintainer cannot see,
without ever containing a credential or private user data. The bundle is something a user can attach
to a report or hand over with confidence.

## 1. Command shape

```text
re-ps3-native.exe --dev-flash <dev_flash> --data-dir <data> --diagnose
re-ps3-native.exe --data-dir <data> --diagnose --diagnose-out <folder>
```

- `--diagnose` collects and exits. It does not open the XMB window, so it also works when the window
  cannot start.
- `--diagnose-out <folder>` overrides the write location. Default is
  `<data>\diagnostics\diagnose-<YYYYMMDD-HHMMSS>\`.
- Exit codes: `0` bundle written; `2` the data directory is unreadable or the output path cannot be
  created. Partial bundles are removed rather than left half-written.
- `--diagnose` must not require `--dev-flash`. If it is absent, the manifest records that the
  firmware path was not supplied instead of failing.
- The flag never modifies `dev_flash`, the Steam installation, the Jellyfin server or any tracked
  repository path. It reads; it does not repair.

A standalone `work/diagnose.ps1` that reads the same files is the preferred first implementation if
an application-source change is not wanted in the release window. It must follow sections 2-6
exactly, and the app-side flag should later call the same collector or produce the same bundle
layout.

## 2. What the bundle collects

| Bucket | Contents | Notes |
|---|---|---|
| `manifest.json` | Bundle schema version, creation time, app version (`0.2.0`), executable file name and SHA-256, OS version/build, CPU and GPU names, display resolution and refresh, Qt runtime version, OpenGL version/renderer, `dev_flash` present and readable (boolean), firmware file counts only, data-directory path (redacted), command line (redacted), collector version. | Enough to identify the build and the host. No firmware contents, no file hashes of firmware. |
| `logs\qt.log` | Last 2,000 lines of the `XMB_LOG` file if that variable was set. | Truncated from the tail; a truncation marker is added. |
| `logs\crash.log` | The existing crash stack (`crash.log` beside the executable, or the crash section inside `XMB_LOG`). | Already produced by the unhandled-exception filter. |
| `logs\session-stdout.txt`, `logs\session-stderr.txt` | Last 2,000 lines of the newest launcher session under `out\native-xmb\sessions\`. | The launcher, not the app, owns these. |
| `logs\services.log` | Tail of the service-specific `XMB_*_LOG` files that are set (Steam artwork/downloads, subtitles, volume, notifications, audio, PS watcher, XMBML, motion). | Each file is redacted and capped separately. |
| `state\preferences.json` | Redacted excerpt of `preferences.json`: `version`, clock and appearance keys, display/fullscreen, audio volume/mute, media repeat/shuffle, and the **count** of `media/resume` records. | The resume map itself is omitted: its paths are private. |
| `state\profiles.json` | Profile count and active-profile presence; profile names replaced with `<user>`. | IDs are not needed for a diagnosis; omit them. |
| `state\registry.json` | Imported-app count, app kinds and categories, and URLs reduced to scheme plus `<host>`; no titles, arguments or executable paths. | Jellyfin base URLs are private-network addresses. |
| `state\inventory.json` | Counts and sizes only: artwork overrides, cached Steam headers, `family-shared.json` entries, `apps.json` entries, presence of legacy `settings.ini`. | No image files, no artwork names (an app id is a purchase record). |
| `state\environment.json` | Allowlisted variables only: `XMB_*` boolean flags, `QTDIR`, OS architecture. | Never the whole environment. |
| `README.txt` | What is in the bundle, what was redacted, the size cap and how to attach it. | Plain text, written last. |

Explicitly **not** collected: the Steam Web API key or its file; Jellyfin access token, user id or
password; Steam ids, account names, persona names or friend names; library titles or media file
names; saved data or trophy data; contents of `dev_flash`; screenshots; the Windows registry;
arbitrary files from the user's disk.

## 3. Redaction rules

Redaction is a whitelist plus fail-closed line filtering. A line that matches a secret pattern is
**dropped whole** and counted in `README.txt` ("N lines withheld by the redactor"), because partial
replacement of an unknown token format is how secrets survive.

| Data | Rule |
|---|---|
| Steam Web API key | The key file is never included. The collector may read its value only to test whether that exact literal appears in collected text, then discards it and never writes it. If a key value appears anywhere, drop the line. Treat any 24+ character hex/alphanumeric run next to `key`, `api_key` or `token` as a secret. |
| Jellyfin token / password | Drop the line for `AccessToken`, `X-Emby-Token`, `Authorization`, `password`, `pwd`, `api_key`, `ApiKey` and URL query parameters named `api_key`, `token` or `ApiKey`. |
| Bearer/basic headers | Drop any line containing `Authorization:` or `Bearer `. |
| Private keys | Drop any block containing `BEGIN ... PRIVATE KEY`. |
| User paths | Replace `%USERPROFILE%` and `C:\Users\<name>` with `%USERPROFILE%`; replace the repository root with `<repo>`; replace library roots, media paths and theme paths with `<path>`; keep only the file extension when it is needed for a codec diagnosis. |
| Profile names | `<user>` (one token; the count is kept). |
| Steam identity | Steam id, account name and persona name become `<steam-id>`, `<account>`, `<persona>`; the signed-in boolean is kept. |
| Jellyfin server | Scheme plus `<host>`; no port if it identifies a private service; no username or token. |
| Steam/Jellyfin library content | Counts only. No titles, app ids, collection names or friend names. |
| Environment | Allowlist only; unknown variables are omitted rather than filtered. |
| Absolute executable path | `%PROGRAMFILES%`-relative or `<exe>`, not the user's home path. |

The redactor should be tested with fixtures that contain a fake key, a fake bearer token and a fake
password, and the test must assert that none of those literals appear in the produced bundle.

## 4. Size and truncation caps

| Limit | Value |
|---|---|
| Whole bundle, uncompressed | 5 MiB |
| One text log | 1 MiB or 2,000 lines, whichever is smaller |
| One JSON state excerpt | 256 KiB |
| Total files in the bundle | 200 |
| Single environment value | 1 KiB |

When a cap is hit, keep the **tail** of a log (the failure is at the end) and write an explicit
`[truncated: N bytes omitted]` marker. Never silently shrink a file. If the total still exceeds
5 MiB after per-file caps, drop the oldest service logs first, then the session logs, and record the
drop list in `README.txt`. The zip is written to a temporary name and renamed on success; a failed
run leaves no partial zip.

## 5. Where it writes

- Default: `<data>\diagnostics\diagnose-<YYYYMMDD-HHMMSS>\` plus a sibling
  `diagnose-<YYYYMMDD-HHMMSS>.zip` at `<data>\diagnostics\`. The default data directory is
  `<exe folder>\data`, which is under the gitignored `out/native-xmb/` in a repository build.
- `--diagnose-out <folder>` writes there instead, with the same timestamped folder name.
- The collector refuses to write inside a tracked path: if the resolved output is not inside the data
  directory and not inside `%TEMP%`, it must be an explicitly supplied `--diagnose-out` with a
  confirmation message in the console. It never writes into `dev_flash`, the Steam folder or the
  repository's tracked files.
- Bundles are not uploaded anywhere. There is no telemetry, no auto-send and no network access in the
  collector; attaching a zip is a manual user action.

## 6. What it must never include

Credentials of any kind (Steam API key, Jellyfin token, passwords, cookies, private keys), the Steam
api key file's contents, personal library names, profile names, friend names, media paths, console
identity, firmware file contents or RCO dumps, screenshots and the Windows registry. If a future
diagnostic needs one of these, it needs a separate, explicit, opt-in consent path - not this bundle.

## 7. Verification of the implementation

| Check | Expected |
|---|---|
| Fixture with a fake Steam key, bearer token and password in the logs | Bundle contains none of the three literals; `README.txt` reports the withheld count. |
| 10 MiB log file | Bundle keeps the tail, marks truncation, total stays under 5 MiB. |
| Missing data directory | Exit 2, no partial folder or zip. |
| `--diagnose` with no `--dev-flash` | Exit 0; manifest says the firmware path was not supplied. |
| Corrupt `preferences.json` | Excerpt records the parse failure; the original file is untouched. |
| Bundle opened on another machine | `README.txt` explains every file; no absolute home path remains. |

## 8. Crash reporting that already ships

`src/main.cpp` installs a Windows unhandled-exception filter. On a crash it appends the exception
code and a symbolized stack (up to 40 frames, resolved with the `re-ps3-native.pdb` next to the
executable) to `XMB_LOG` when that variable is set, otherwise to `crash.log` beside the executable.
It is local-only; there is no upload. `--diagnose` should include that file rather than duplicating
the handler. The release build keeps `/DEBUG` and `/OPT:REF /OPT:ICF` so the PDB matches the binary.

## 9. Implementation notes for the next session

- Preferred first step: `work/diagnose.ps1`, Windows PowerShell 5.1 or PowerShell 7, no
  application changes. It can read the data directory, the launcher sessions and the allowlisted
  environment, apply sections 3-5 and write the zip. Keep it ASCII and dependency-free.
- App-side later: a `--diagnose` branch in `main.cpp` before the window is created, calling the same
  collector logic. That is a source edit and needs its own review; it is not part of the Phase 7
  documentation pass.
- A C++ implementation must not link a zip library for this; a folder plus a PowerShell
  `Compress-Archive` step in the helper is enough for the first cut.
- Do not add a new test suite. Extend `work/` with the fixture described in section 7 only if the
  collector is implemented.
