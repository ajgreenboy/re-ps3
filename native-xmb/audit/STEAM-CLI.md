# Steam CLI / headless control: what this client build actually accepts

Status: probe and audit only. Created 2026-10-26. This document is the only file written; no source,
CMake, Steam file or Steam registry value was modified. No game was launched, no download started, no
client shut down, no account switched, and no `steam://` URL was fired. Account names, account ids and
tokens are deliberately not recorded here.

Machine facts, verified read-only on this machine:

- Windows 11 Pro, NT 10.0.26200.
- Steam at `C:\Program Files (x86)\Steam`; `steam.exe` file version 11.15.70.27, binary timestamped 2026-10-21;
  HKLM `Version=1792618285`; `steamui\changelist.txt` = `11146165`.
- The client is running: `steam.exe` pid 11580, command line
  `"C:\Program Files (x86)\Steam\steam.exe" -silent` (tray mode), plus 8 `steamwebhelper.exe`
  processes. `RunningAppID` is 0; no `Apps\<id>\Updating` is 1; all four existing library
  `downloading` folders are empty.
- 39 `HKCU\Software\Valve\Steam\Apps\<appid>` keys; 11 `appmanifest_*.acf` files across the libraries.
- `steamcmd.exe` is not installed (checked the usual roots, `where.exe`, and PATH).

## 0. Bottom line

1. There is no supported headless or command-line mode of the Steam client. `-silent` is tray mode
   with the full CEF/steamwebhelper UI still running; it is not a service or daemon.
2. There is, however, an undocumented command channel that works headlessly today: a second
   `steam.exe` invocation forwards its command line to the already-running client, and arguments
   written as `+<command> [args]` are executed by the client's console. This was verified live with
   read-only commands (`app_status`, `apps_running`) and no-op commands (`app_stop` on a nonexistent
   app). Output goes to `Steam\logs\console_log.txt`; the forwarding process returns immediately with
   no stdout.
3. `steam://` verbs are real and documented (`install`, `uninstall`, `run`/`rungameid`, `validate`,
   `open/downloads`, `open/console`), but they only navigate or start/destroy content. There is no
   URL verb for stopping a game, pausing a download, or switching accounts.
4. `steamcmd.exe` exists only as a separate Valve download for dedicated servers and SteamPipe uploads.
   It is not installed here, it is not a client controller, it cannot launch or stop the client's
   games, and Valve documents a single-session rule that makes it a poor parallel backend for a
   logged-in client.
5. Account switching remains impossible from the command line. The XMB's existing
   `-shutdown`/relaunch handoff is still the honest maximum. Native login and QR in XMB style is a
   different layer: it requires implementing Steam's authentication protocol (the SteamKit2-class
   approach), and it produces an XMB-owned session, not a switch of the running client's session.

The practical answer: the XMB can keep `-applaunch` for launch and add exactly one real CLI capability,
stop (`+app_stop`). Download start is available; per-title pause is not. Account switch and native
client sign-in are not CLI problems.

## 1. Command-line switches that exist in this build

Evidence was read from the binaries' own string tables (ASCII and UTF-16), not from documentation.

| Switch | Evidence in this installation | Meaning / effect | Used by XMB |
|---|---|---|---|
| `-applaunch <appid>` | `SteamUI.dll`, in the `CSteamDoc::ExecCommandLine` argument list next to `-shutdown` | Asks the running client to launch the app; the client owns the process | Yes, `shell.cpp` line 178 |
| `-shutdown` | Same `ExecCommandLine` list; also in `steamclient.dll` / `steamclient64.dll` | Asks the client to exit | Yes, `steam_accounts.cpp` handoff |
| `-silent` | `SteamUI.dll`: `Silent startup mode (tray mode only)` | Start hidden in the tray; the client that is running now was started this way | No |
| `-console` / `-noconsole` | `SteamUI.dll` switch pair | Show/hide the developer console tab; required for the console page, not for `+commands` | No |
| `-login`, `-logon` (and `+` spellings) | `steam.exe` bootstrapper argument table | Startup sign-in path; accepts an account name (password and Steam Guard are the client's business) | No (unsafe) |
| `-bigpicture`, `-tenfoot` | `SteamUI.dll`: `Start in Steam Big Picture mode.` | Force Big Picture | No |
| `-gamepadui` | `SteamUI.dll`: `Start in gamepadui mode.` | Force the Deck-style UI | No |
| `-nobigpicture` | `SteamUI.dll`: `Start in regular mode (force Big Picture mode off)` | Force desktop UI | No |
| `-steamdeck`, `-steamdeckdisplay`, `-steammachinedisplay` | `steam.exe` / `SteamUI.dll` | Deck hardware modes | No |
| `-cef-disable-gpu`, `-cef-disable-sandbox`, `-no-cef-sandbox`, `-cef-disable-seccomp-sandbox` | `steamui.dll`, `steam.exe` | Chromium/CEF flags | No |
| `-devsites`, `-devsiteshost` | `steamclient64.dll` help text | Point the client at local dev sites | No |
| `-install`, `-steaminstaller`, `-uninstall`, `-quit`, `-exitsteam`, `-foreground`, `-drop`, `-gameidlaunch`, `-accesscode=`, `-testoobe`, `-testoobestage2` | `SteamUI.dll`, same `ExecCommandLine` list | Installer, exit and launch-control arguments; the exact role of `-accesscode=` was not verified | No |
| `-?` | Not a help switch. Ran it: returned in 9 ms, no stdout/stderr, no new process; `console_log.txt` only recorded `ExecCommandLine: "...steam.exe" "-?"` | Nothing. There is no text help. | No |
| `-noreactlogin` | Not present in `steam.exe`, `SteamUI.dll`, `steamclient.dll` or `steamclient64.dll` | Gone from this build | No |

Notes:

- The switches above are internal Valve strings. Only `-applaunch` and `-shutdown` are used by the XMB;
  both are already exercised by the shipped app.
- `steam://` is registered per Windows user as
  `"C:\Program Files (x86)\Steam\steam.exe" -- "%1"` under `HKCU\Software\Classes\steam`.
- A second `steam.exe` with switches does not start a second client: it forwards to the running one.
  That forwarding is what makes the `+command` channel in section 3 possible.

## 2. `steam://` verbs that matter here

Status column: "documented" is Valve's Steam browser protocol page; "present" means the verb or its
client handler is also visible in this installation (steamui JavaScript route table or binary strings).
None of these was live-fired during this audit, so acceptance is evidenced by the client's own
tables, not by a clicked URL.

| Verb | Documented effect | Present in this build | Fired |
|---|---|---|---|
| `steam://install/<id>` | Installs an application | Yes: `steam://install/` + appid is constructed in `steamui\sp.js` | No |
| `steam://uninstall/<id>` | Deletes the app's cache files (uninstall path) | Yes: `-uninstall` in `ExecCommandLine`; `app_uninstall` console command | No (destructive) |
| `steam://run/<id>` / `steam://run/<id>//<args>/` | Runs the app, installing if necessary | Yes: `steam://run/%d` in `steamclient64.dll`; `steam://run/` in JS | No (would launch) |
| `steam://rungameid/<id>` | Same as run, with mod/shortcut support | Yes: `%s://rungameid/%llu` and JS `steam://rungameid/` | No (would launch) |
| `steam://open/downloads`, `steam://nav/downloads` | Open the Downloads page | Yes: both in the `sp.js` command table (`[["open/downloads","nav/downloads"], ...]`) | No (would navigate the UI) |
| `steam://open/console`, `steam://nav/console` | Open the developer console | Yes: both in the `sp.js` command table; `steam://open/console` also in a `SteamUI.dll` URL list | No (would navigate the UI) |
| `steam://validate/<id>` | Validates local files | Yes: `["validate", ...]` entry in the `sp.js` command table | No (disk I/O, could re-download) |
| `steam://exit` | Exits Steam | Yes: JS verb list | No |
| `steam://open/settings`, `steam://settings/*` | Open settings pages | Yes: JS verb list | No |

Important negatives:

- No `steam://` verb stops a running game.
- No `steam://` verb pauses, resumes or throttles a download.
- No `steam://` verb switches the signed-in account. The browser protocol page lists an internal
  `steam://url/SteamIDLoginPage/<pass>` one-time sign-in page; it needs a pass and is not a supported
  account switcher. Do not use it.
- The URL command table in `sp.js` is the client's front-end router. `open/console` and
  `nav/console` being present means the route exists; it does not mean the console page is enabled in
  a client that was started without `-console`.

## 3. The real headless command channel: `steam.exe +<command>`

### 3.1 Mechanism, as observed

1. The client is running (here: `-silent`, no `-console`).
2. The XMB (or any process) starts `steam.exe` with arguments `+app_status 202970`.
3. That second `steam.exe` forwards the command line to the running client and exits in 20-40 ms.
   It produces no stdout and no exit status that describes the command.
4. The client logs `ExecCommandLine: "..."` and then executes the `+` token as a console command.
   Its text output is appended to `Steam\logs\console_log.txt`.
5. Unknown commands log `>>> command not found: <name>`.

This worked on the current `-silent` client without `-console`, so `-console` is only the UI switch
for the console page, not a prerequisite for the command dispatcher.

### 3.2 Commands relevant to the XMB

| Command | Help text in this build | Verified live? |
|---|---|---|
| `app_status <appid>` | `AppID %u (%s): ... release state ... install state ... mounted depots ... size on disk ... update state ...` | Yes, for 202970 and for a nonexistent id |
| `apps_running` | `displays information about running games tracked by Steam` | Yes (empty output, nothing running) |
| `app_stop <appid> [force:0 or 1]` | `<appid> [force:0 or 1] stop this running app` | Command accepted; only tested against a nonexistent app, never against a live game |
| `app_install <appid> [volumeindex]` | `<appId> [volumeindex]` | Yes, entered the install machinery; bogus id failed safely (see 3.3) |
| `app_uninstall [-complete] <appid>` | `app_uninstall` (argument help is from Valve's older console list) | Not run (destructive) |
| `app_download_enable <0 or 1>` | `<0 or 1>` | Not run (changes global download state) |
| `app_start_validation <appid>` / `app_mark_validation <appid> <0 or 1:full>` | present | Not run (disk I/O) |
| `download_depot <appid> <depotid> [manifest] [delta]` | `download a single depot` | Not run (developer command, downloads to a separate folder) |
| `download_item <appid> <PublishedFileId>` | `download a workshop item` | Not run |
| `licenses_for_app <appid>` / `licenses_print` | present | Not run (prints account data) |
| `app_info_print <appid> [section]` | `print app info for appID` | Not run (read-only, safe in principle) |
| `quit` | present | Not run (would close the client) |

Positive negative results, equally useful:

- There is no `app_launch` console command in this build. `app_launch` appears only inside the
  protobuf field `blocking_app_launch`. Launch stays `-applaunch` / `steam://run`.
- There is no `app_update` console command. `steam.exe +app_update 999999999` logged
  `>>> command not found: app_update`. The update path is `app_install` (or the URL).
- There is no per-app pause/suspend command (`app_pause`, `download_pause`, `pause_download` and
  similar names are absent). The only download switch is the global `app_download_enable 0|1`.
- There is no `login`, `logout` or `switch_user` console command. Account handling is not exposed.

### 3.3 Exact probes and results

All probes were safe by construction: read-only commands, or commands aimed at an app id that does
not exist. `RunningAppID` stayed 0 and no `Apps\999999999` registry key was created.

```
steam.exe -?
  returned in 9 ms, no stdout/stderr, no new process
  console_log.txt: ExecCommandLine: ""C:\Program Files (x86)\Steam\steam.exe" "-?""

steam.exe +app_status 202970
  console_log.txt: ExecCommandLine: ""...\steam.exe" +app_status 202970"
  AppID 202970 (Call of Duty: Black Ops II):
   - release state: released (Subscribed, Permanent)
   - owner account line omitted
   - install state: Fully Installed, Shared Only
   - install dir: "D:\SteamLibrary\steamapps\common\Call of Duty Black Ops II"
   - mounted depots: 202972 / 202973 / 202975 / 212931 / 202998 with sizes and manifest ids
   - size on disk: 3580495282 bytes, BuildID 24784288
   - update state: ( No Error )

steam.exe +apps_running
  console_log.txt: ExecCommandLine: ""...\steam.exe" +apps_running"
  (no output; nothing running)

steam.exe +definitely_not_a_command
  >>> command not found: definitely_not_a_command

steam.exe +app_stop 999999999
  console_log.txt: ExecCommandLine: ""...\steam.exe" +app_stop 999999999"
  (no output, no error; the app does not exist)

steam.exe +app_status 999999999
  AppID 999999999 ():
   - release state: unknown (No License)
   - install state: uninstalled

steam.exe +app_install 999999999
  console_log.txt: ExecCommandLine: ""...\steam.exe" +app_install 999999999"
  content_log.txt: Failed installing AppID 999999999 (Missing configuration)
```

Costs and risks of the `+command` channel:

- It is undocumented. Valve can change or remove it in any client update.
- There is no return channel: no stdout, no exit code, only `console_log.txt` and the client's state.
- `console_log.txt` is shared with all client spew. To read one command's output, snapshot the file
  size first, then read the appended region and stop at the next `ExecCommandLine`.
- Log rotation across client restarts was not observed here (one 500 KB file, append-only since June).
  A reader must still handle truncation/rotation defensively.
- Commands run with the signed-in user's authority inside the client. A typo cannot be sandboxed by
  the XMB; only a fixed allow-list of arguments should ever be passed.

## 4. `steamcmd.exe`: what it is and is not

Not installed on this machine. Valve's own page describes it as:

> "The Steam Console Client or SteamCMD is a command-line version of the Steam Client. Its primary use
> is to install and update various dedicated servers available on Steam using a command-line
> interface."

Where it comes from: `https://steamcdn-a.akamaihd.net/client/installer/steamcmd.zip` (Valve's
documented Windows download). It is a separate program with its own directory, own config, own
`steamapps` tree and its own login. It is not part of the desktop client installation.

What it can do that is relevant: `login`, `force_install_dir`, `app_update <id> [validate]`,
`app_info_print`, `workshop_download_item`, `run_app_build`/SteamPipe uploads, `quit`, and `+`
command-line scripting (`steamcmd +login ... +app_update ... +quit`).

What it cannot do, which decides the question:

- It cannot launch a desktop game. It installs/updates content; it does not run the client's game
  processes, and it cannot attach to a client-launched game.
- It cannot stop a running game. It has no connection to the desktop client or its process tree, and
  it never launches games, so there is nothing for it to stop.
- It cannot switch the desktop client's account, display a QR code, or read the client's session.
- It cannot safely share the client's library: it writes its own manifests under its own install dir
  and is a different content consumer.
- Valve's SteamCMD page states: "A user can only be logged in once at any time (counting both
  graphical client as well as SteamCMD logins)." So using the same account while the client is
  logged in is a documented session conflict, not a parallel backend. A different account avoids the
  conflict but then it is not the user's library.
- No redistribution grant is documented anywhere in Valve's material. Shipping `steamcmd.exe` inside the
  XMB is a licensing question at best; downloading it on demand from Valve's URL is the only
  defensible route if it is ever used at all.

Conclusion: SteamCMD is a dedicated-server/content-upload tool, not a client controller. It is the
wrong component to "bundle as the Steam CLI" for this app.

## 5. "Headless" reality

- `-silent` is tray mode. The running client still has a window object, CEF, 8 `steamwebhelper.exe`
  processes and the full store/library front-end loaded.
- There is no service mode, no `--headless`, and no way to start the client without the web UI.
  The only supported alternative process is SteamCMD, which has no game client.
- The console dispatcher does work without `-console`, but the client must already be running and
  signed in. A cold `steam.exe +app_status ...` (client not running) was not tested; the bootstrapper
  would start the client first, and the outcome would depend on sign-in state.
- Sign-in itself is a client UI concern. The client can be asked to restart (`-shutdown`, then
  `steam.exe`), but the account and Steam Guard are handled by its own windows.

## 6. Capability matrix

Verified = actually executed/observed in this audit. "Recognized" means the command/switch exists in
the client's own tables but was not fired for safety.

| Capability | Mechanism | Exact command | Status | Cost / risk |
|---|---|---|---|---|
| Launch a title | Client launch switch | `steam.exe -applaunch <appid>` | Recognized (string table) and already used by `shell.cpp`; end-to-end launch not fired here | Low; client owns lifecycle; observe registry/console log |
| Launch a title (alternative) | Steam URL | `steam://run/<id>` or `steam://rungameid/<id>` | Documented; present in build; not fired | Same, but goes through the UI router |
| Launch from console | none | no `app_launch` command exists | Impossible | n/a |
| Stop a running title | Client console | `steam.exe +app_stop <appid> [force:0 or 1]` | Command exists and was accepted; not tested on a live game | Medium: stops the game, unsaved progress; undocumented channel |
| Stop a running title | Steam URL | none | Impossible | n/a |
| Stop a running title | Kill the process | `TerminateProcess` on the game tree | Not Steam's path; works but is not Steam-managed | High: data loss, Steam may report a crash; last resort only |
| Start / install a download | Steam URL | `steam://install/<id>` | Documented; present; not fired | Disk/network cost; license required; visible in client |
| Start / install a download | Client console | `steam.exe +app_install <appid> [volumeindex]` | Verified to enter the install path (bogus id failed safely) | Same cost; headless variant |
| Update an installed title | `app_install` | `steam.exe +app_install <appid>` | Unverified for an outdated app; `app_update` does not exist | Same cost; client decides update |
| Pause / resume a title | none per title | none | Impossible | n/a |
| Pause / resume all downloads | Global console flag | `steam.exe +app_download_enable 0` / `... 1` | Help string present; not fired | Global, affects every title and auto-update |
| Switch account | Restart handoff only | `steam.exe -shutdown`, then `steam.exe` | Recognized; no switch command exists | Ends the session; user completes sign-in in the client |
| Login / QR / Steam Guard | Steam auth protocol | SteamKit2-class `BeginAuthSessionViaQR` + polling | Not available via `steam.exe` or SteamCMD | A separate project; creates an XMB session, not a client session |

## 7. Observe versus command

Already observed by the XMB (keep it):

- `HKCU\Software\Valve\Steam\RunningAppID`: 0 when idle, the app id while a title runs.
- `HKCU\Software\Valve\Steam\Apps\<appid>\Running`, `Updating`, `Installed`, `UpdateRequired`,
  `Name`: per-app flags, 39 keys here. `Updating=1` covers pre-launch updates and downloads.
- `<library>\steamapps\appmanifest_<appid>.acf`, `StateFlags` and
  `BytesToDownload` / `BytesDownloaded` / `SizeOnDisk` / `LastUpdated`. Verified examples on this
  machine: `StateFlags=4` (Fully Installed), `6` (Update Required + Fully Installed, e.g. skate.),
  `68` (Fully Installed + Shared Only, e.g. Black Ops II). `STEAM-DOWNLOADS.md` has the full flag
  discussion; nothing in this audit changes it.
- `config\loginusers.vdf`: remembered accounts and the client's own active marker
  (`STEAM-ACCOUNTS.md`).

Additional observation available through the console channel:

- `app_status <appid>` gives detail the ACF does not: mounted depots with manifest ids, byte sizes,
  shared-only state, owner and release state, update state and error. Useful for a title detail page.
- `apps_running` lists what the client thinks is running.
- `console_log.txt` records every `+command` with a timestamp, so an XMB command can be confirmed as
  delivered even when it produces no output.
- `content_log.txt` records state changes, e.g. `AppID 2807960 state changed : Fully Installed,
  Update Queued,` and the install failure line quoted above. This is a better lifecycle log than the
  registry for downloads.

Command surface, in one line: the XMB can command launch, stop, install/update-start and global
download pause through the client; it cannot command per-title pause, account switch or sign-in. It
can observe far more than it can command, and the client remains the authority for every state.

## 8. Recommendation

### 8.1 Keep what works

- Keep `steam.exe -applaunch <appid>` plus registry observation for launch. It is the client's own
  path, works with the tray client, and has no console-command fragility.
- Keep the `-shutdown` + relaunch handoff for accounts. Do not pass `-login`, a user name, a password
  or a token: the client must own authentication.

### 8.2 Add exactly one new command: stop

- Implement `steam.exe +app_stop <appid>` behind an explicit confirmation, only when the client's own
  state says the app is running (`Apps\<id>\Running=1` or `RunningAppID=<id>`).
- Verify the stop by polling the same registry values until they clear (bounded timeout), then treat
  the title as returned. Do not claim success from the forwarding process: it exits immediately with
  no result.
- Treat `force:1` as a separate, more strongly confirmed action; the default should be the client's
  normal stop.
- Keep "kill the process tree" as an unadvertised last resort only. It is not Steam's stop path and
  risks unsaved progress and crash reporting.

### 8.3 Downloads

- Start installs with `steam://install/<id>` if the UI should show the client's Downloads page, or
  with `+app_install <id>` if the XMB wants it to happen without navigating the client. Both need a
  confirmation because they consume disk and network.
- Do not build a per-title pause button: the only pause is global (`app_download_enable 0|1`), and it
  affects every title and auto-update. If a global "pause all downloads" toggle is acceptable, test it
  manually once and treat it as a global setting, not a per-title control.
- Uninstall and validate are destructive/heavy. Keep them out of the automated path.

### 8.4 The "bundled Steam CLI"

- Do not bundle `steamcmd.exe`. It is Valve's proprietary dedicated-server tool, it conflicts with a
  logged-in client by Valve's own single-login rule, it cannot launch or stop games, and it has no
  redistribution grant.
- The CLI worth bundling is the XMB's own small helper (or just an internal `Shell` method): a fixed
  allow-list wrapper over `steam.exe -applaunch`, `+app_stop`, `+app_install`, `+app_status`,
  `+apps_running`, plus the existing registry/ACF readers and a bounded `console_log.txt` tail
  reader. That helper is stable, auditable, and contains no Valve binary.
- If the helper is a separate executable, it should be only that: argument validation, process
  start, log-slice read, JSON out. No credentials, no UI automation, no network.

### 8.5 Native login and QR in XMB style (new requirement)

This is a real option, but it is a different layer from "Steam CLI", and its limits must be stated
plainly.

What exists: Steam's authentication is a service API, not a command. SteamKit2 (C#/.NET) implements
it, including QR:

- `BeginAuthSessionViaQRAsync` returns a `challenge_url`, `client_id`, `request_id`,
  `allowed_confirmations` and a poll interval. `challenge_url` is the QR payload.
- The user scans it with the Steam mobile app and approves; the desktop polls
  `PollAuthSessionStatus` until a refresh token arrives. `ChallengeURLChanged` exists to refresh the
  displayed QR.
- The password path is `BeginAuthSessionViaCredentialsAsync` (RSA-encrypted password) plus
  `UpdateAuthSessionWithSteamGuardCode` (the API call behind `SendSteamGuardCodeAsync`) for email or
  device codes, then the same poll.
- Node has `steam-session` with the same flows. There is no Valve C++ SDK for this; a Qt
  implementation would be a port of the protocol, with SteamKit2 as the reference.

What it would give the XMB:

- An XMB-owned authenticated session: owned games, licenses, friends, app info, and possibly depot
  downloads (DepotDownloader is built on SteamKit2), without the client UI.
- A genuinely XMB-styled login screen: the QR is just the challenge URL rendered as an image, and
  the polling/confirmation state is a normal async UI flow.

What it would not give the XMB:

- It does not sign the Steam client in, and it does not switch the client's account. Tokens obtained
  by the XMB are for the XMB's own session; handing them to the client is unsupported and would mean
  handling credentials the app currently refuses to touch.
- It does not let the XMB launch a game as an account the client is not signed into. Game launch and
  licenses still belong to the client's session.
- It is not a CLI feature. It is a protocol implementation plus secure token storage (DPAPI), and it
  should be a separate phase with its own audit.

Recommended shape: keep the client as the game/session owner; add an optional XMB-owned read-only
Steam session for library/friends/login UI. If the goal is specifically "the XMB QR signs the client
in", that is not possible through any supported interface; the client's own sign-in window (or its
`-login` restart path, which we should not use) is the only authenticator.

## 9. Not tested, deliberately

- `app_stop` against a really running game.
- `+app_install` on a real, licensed title (would download).
- `app_uninstall`, `app_start_validation`, `app_mark_validation`, `download_depot`,
  `download_item`, `app_download_enable`.
- Firing any `steam://` URL, including `steam://open/console` and `steam://open/downloads`.
- `-applaunch` end to end.
- `-shutdown`, `-login`, `-console`, `-dev`, or any behavior with the client stopped.
- Reading account/session/credential files (`config\config.vdf`, `ssfn*`, connect caches).

## 10. Evidence appendix

Environment and registry (read-only):

```
HKCU\Software\Valve\Steam
  SteamPath       = c:/program files (x86)/steam
  SteamExe        = c:/program files (x86)/steam/steam.exe
  RunningAppID    = 0
  AutoLoginUser   = set (value not recorded)

HKCU\Software\Valve\Steam\ActiveProcess
  pid = 11580 ; Universe = Public ; ActiveUser set (value not recorded)

HKLM\SOFTWARE\WOW6432Node\Valve\Steam
  InstallPath = C:\Program Files (x86)\Steam ; Version = 1792618285 ; BetaName = (empty)

steam.exe command line (Win32_Process):
  "C:\Program Files (x86)\Steam\steam.exe" -silent

steamcmd.exe: not found in C:\steamcmd, D:\steamcmd, Program Files, Program Files (x86)\Steam,
  %LOCALAPPDATA%, %USERPROFILE%, or on PATH.
```

Binary string evidence (ASCII if not noted):

```
steam.exe:
  -silent  -skipinitialbootstrap  -inhibitbootstrap  -noverifyfiles  -norepairfiles
  -nobootstrapperupdate  -forcesteamupdate  -forcepackagedownload  -login  +login  -logon  +logon
  -console  -noconsole  -gamepadui  -steamdeck  -bigpicture  -tenfoot  -devsites  -devsiteshost
  -cef-disable-gpu  -cef-disable-sandbox  -no-cef-sandbox

SteamUI.dll:
  CSteamDoc::ExecCommandLine  -login  -accesscode=  -applaunch  -quit  -gameidlaunch
  -exitsteam  -shutdown  -foreground  -drop  -uninstall  --  -
  >>> command not found: %s
  Non-user command %s received at console; ignoring
  Silent startup mode (tray mode only)  -silent
  Start in Steam Big Picture mode.  -bigpicture  -tenfoot
  Start in gamepadui mode.  -gamepadui
  <appid> [force:0|1] stop this running app  app_stop
  <appid> <seconds> force delay update  app_delay_update
  display users's Steam3 licenses.  licenses_print
  <appid> : shows active licenses for appid  licenses_for_app
  download_depot <appid> <depotid> [target] [delta] : download a single depot
  download_item <appid> <PublishedFileId> : download a workshop item
  app_uninstall  <0|1>  app_download_enable
  app_info_print <appid> [section] : print app info for appID

steamclient64.dll:
  app_status <appId> ; AppID %u (%s): ; - install state: ; State flags ; app_update_status
  licenses_for_app ; app_delay_update ; app_stop ; apps_running

steamui\sp.js (route table):
  ["open/downloads","nav/downloads"]   ["open/console","nav/console"]   ["validate", ...]
  steam://install/  steam://rungameid/  steam://run/  steam://open/settings
```

One full console round trip (quoted in sections 3.3), and the client's own record of the probe:

```
[2026-10-26 20:39:50] ExecCommandLine: ""C:\Program Files (x86)\Steam\steam.exe" +app_status 202970"
[2026-10-26 20:39:50] AppID 202970 (Call of Duty: Black Ops II):
[2026-10-26 20:39:50]  - install state: Fully Installed, Shared Only,
[2026-10-26 20:39:50]  - size on disk: 3580495282 bytes, BuildID 24784288
[2026-10-26 20:41:25] >>> command not found: app_update
[2026-10-26 20:41:28] ExecCommandLine: ""C:\Program Files (x86)\Steam\steam.exe" +app_install 999999999"
content_log.txt: [2026-10-26 20:41:28] Failed installing AppID 999999999 (Missing configuration)
```

State after the probes: `RunningAppID=0`, no `Apps\<id>` flag changed, no `Apps\999999999` key, no
download folder entry, client pid unchanged.

## 11. Sources

- Valve Developer Community, Steam browser protocol (archived 2025-12-30):
  https://web.archive.org/web/20251230201212/https://developer.valvesoftware.com/wiki/Steam_browser_protocol
- Valve Developer Community, SteamCMD (archived 2026-01-01):
  https://web.archive.org/web/20260101130455/https://developer.valvesoftware.com/wiki/SteamCMD
- SteamKit2 authentication reference:
  https://raw.githubusercontent.com/SteamRE/SteamKit/master/SteamKit2/SteamKit2/Steam/Authentication/SteamAuthentication.cs
  https://raw.githubusercontent.com/SteamRE/SteamKit/master/SteamKit2/SteamKit2/Steam/Authentication/AuthSession.cs
  https://raw.githubusercontent.com/SteamRE/SteamKit/master/SteamKit2/SteamKit2/Steam/Authentication/QrAuthSession.cs
- Community console command list (2016 client, used only to corroborate argument syntax):
  https://github.com/Bluscream/Steam-Client-Docs/blob/master/Console/Commands.MD
- Local evidence: registry, `steam.exe` process command line, binary strings, `logs\console_log.txt`,
  `logs\content_log.txt`, `steamapps\appmanifest_*.acf`, and the probes in section 3.
