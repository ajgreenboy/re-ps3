# Release evidence run, 2026-11-12 (development PC)

This is the first pass over `audit/RELEASE-CHECKLIST.md` (84 rows, B1 to RC2, and the gaps G1 to G15) on the Windows
development PC, with no hardware and no side effects on the owner's devices, accounts or services. It is not the
HTPC run. Every row appears once in the table below. Anything marked NOT RUN needs the owner, the HTPC, or consent for a
live action, and the reason is in the row.

- Build: HEAD `5c5f390` built once into `C:/dev/out/rr-build` with `build.ps1`; "Built" printed, **0 warnings** in the
  build log.
- Machine: Windows 11 Pro (10.0.26200), windowed runs only (`--start-windowed`), one scratch `--data-dir` per run under
  `C:/dev/out/rr/runs/`, `XMB_SINGLE_INSTANCE=0`, `XMB_LOG=<file>` per run.
- Re-run the automated subset with `bun work/htpc-evidence.ts [--only ID,...]`; `--doc` rebuilds the table below from the
  stored per-row results. Raw logs, grabs, CSVs and screenshots are under `C:/dev/out/rr/` and are not committed.
- Input: script tokens (`--script`), `XMB_PAD_FAKE` synthetic pad states, and `WM_APPCOMMAND` / `WM_KEYDOWN` messages
  posted to the XMB window handle of the process the script started. No `SendInput`, no global media keys, no physical
  pad or remote. Rows that need those say PARTIAL (the software path passed, the physical device is still open) or NOT RUN.
- Every live-system action was a dry run for every run of the script: `XMB_POWER_DRY_RUN`, `XMB_AUDIO_DRY_RUN`, and the
  Bluetooth, Wi-Fi, CEC, display, firmware and updater dry-run switches, with a scratch autostart task name
  (`XMB_AUTOSTART_TASK`). The Steam root was an empty scratch folder except in two read-only runs (SC1, ST1).

## Result counts

84 checklist rows: **19 PASS, 0 FAIL, 20 PARTIAL, 45 NOT RUN**. The 15 known gaps (G1 to G15) are copied through as OPEN with
their reason, except G12, which this run measured: the OpenGL texture and context warnings **still appear on every exit**
(see the soak section).

## Table

<!-- table:start -->
| ID | Row | Result | Evidence / reason |
|---|---|---|---|
| B1 | Kiosk start with intro | NOT RUN | A kiosk start takes over a whole display with no window frame; no safe way to do that on this PC was proven (the owner may be using it), and the evidence is a photo of the TV. Owner: run the kiosk command from setup step 6 on the HTPC. |
| B2 | Log exists, no crash | PASS | log exists (17935 bytes, 233 lines, first frame 849 ms) for the default start with intro (windowed, stopped by WM_CLOSE: closed, exit 0); crash lines/files: 0. B1 itself (fullscreen takeover) not run. |
| B3 | Second start, preferences persist | PASS | second start (--no-intro) on the same data dir reached Music Library with the stored folder (3 items, depth 1); preferences.json keys 3 -> 3, unchanged text: true; data dir: cache,crashes,jellyfin,preferences.json,profiles.json,settings.ini. |
| N1 | D-pad/stick through every column | PARTIAL | software path only (script key taps, no physical pad or stick): one column per key through all 10 columns (left 5>4>3>2>1>0, right 1>2>3>4>5>6>7>8>9), rows 0>1>2>1 on Music; a held Down repeated to row 15. Pad decode of one synthetic press each through the live pad code (XMB_PAD_FAKE, no physical pad): down:ok stick-down:ok cross:ok l1:ok r1:ok circle:ok triangle:ok. Stick repeat and focus-sticking on a real pad not exercised. |
| N2 | Cross opens a row | PARTIAL | Cross (Enter token) on Music Library opened the folder (depth 0 -> 1, 3 rows); software path, no pad press. |
| N3 | Circle goes back | PARTIAL | Circle (Esc token) went back one level (depth 1 -> 0, selection 2); with the options open the first Esc closed only them (depth stayed 1). Software path. |
| N4 | Triangle options | PARTIAL | Triangle (T token) on a track opened the options (1 'options opened' line, rows: 9 Sort By/Play/Add to Playlist/Add All/Copy/Delete/Copy Multiple/Delete Multiple/Information ). Software path. |
| N5 | L1/R1 ten rows | PARTIAL | PageDown/PageUp (the key form of L1/R1) on a 47-row folder moved the selection 0>10>20>10>0 (10 rows per press, clamped at the ends). Pad L1/R1 reached the pad path (see N1 decode list) as 'source=pad action=l1/r1' log lines with a synthetic pad state, but the 10-row result was not observed on a pad press, and no pad was physically pressed. |
| N6 | On-screen keyboard | PARTIAL | keyboard opened from Create New Playlist; typed 'Esc' then Esc cancelled (no playlist, rows stayed 1); typed 'Mixx', Backspace, then the on-screen Enter key (cursor Down x3, Right x3, Return) created playlist 'Mix' on disk. Screenshot of the keyboard in use: C:/dev/out/rr/runs/n1-osk/keyboard.png (not committed). Pad bindings (A/X/Y/Start/B) not pressed. |
| N7 | PS/Guide tap brings XMB forward | NOT RUN | Needs the real HTPC hardware: a DualSense or Xbox pad and its PS/Guide button; scripted runs withhold the pad and a synthetic pad state does not exercise the global watcher on another window. Owner: tap PS/Guide with another window in front. |
| N8 | PS/Guide hold system menu | NOT RUN | Needs the real HTPC hardware: hold PS/Guide for 1 s on a real pad (the Guide path is unverified, G1). Owner at the HTPC. |
| R1 | Play/Pause | PARTIAL | posted WM_APPCOMMAND (not a physical remote): Pause -> pause (paused 1), Play -> play (paused 0), Play/Pause toggled 1 then 0; one decision line per press. Raw: C:/dev/out/rr/r-rows-evidence.txt. Transport posts skipped by the no-media guard: 0. |
| R2 | Stop | PARTIAL | posted Stop with a track open: stop: stop from appcommand 13 (key) -> stop; mediaOpen now 0 // after stop mediaOpen 0 paused 1 position 0 ms. The resume point itself is checked for video in LM1. Posted message, not a physical remote. |
| R3 | Next/Previous | PARTIAL | posted Next/Previous moved the queue 1->2->1 of 3; a held Next (one WM_KEYDOWN + 3 auto-repeat WM_KEYDOWNs + WM_KEYUP) caused 1 skip. Posted messages, not a physical remote. |
| R4 | Fast Forward/Rewind | PARTIAL | video, posted Fast Forward/Rewind: 'ff: fast-forward from appcommand 49 (key) -> seek + 10 s from 6166 ms // after fast-forwa' / 'rew: rewind from appcommand 50 (key) -> seek - 10 s from 17664 ms // after rewind mediaOp' (positions after: 16554 / 8064 ms). The clip is 20 s long. Posted messages, not a physical remote. |
| R5 | Channel Up/Down | PARTIAL | posted Channel Down x2 / Up on a 47-row folder: selection 0 -> 10, selection 10 -> 20, selection 20 -> 10. Posted messages, not a physical remote. |
| R6 | Back and Home | PARTIAL | posted Browser Back acted as Circle (back: back from appcommand 1 (key) -> Circle (back); depth 0 mediaOpen 0 ); Browser Home opened the system menu and a second Home closed it. Posted messages, not a physical remote. |
| R7 | Volume keys | NOT RUN | Volume keys change the real Windows master volume and mute state of this PC (a system setting); not touched. Needs the owner at the HTPC with the real remote. |
| R8 | One press, one action | PARTIAL | two WM_APPCOMMAND copies posted back to back: double: pause from appcommand 47 (key) -> pause // trace pause appcommand 47 (key) duplicate of a press 0 ms ago (swallowed) // after pause mediaOpen 1 paused 1 position 5851 ms title "Alpha.wav" . Real VK + WM_APPCOMMAND pair from a keyboard/remote not tested. |
| R9 | Declined path | NOT RUN | The declined path hands the key to Windows (DefWindowProc, then the shell hook), which would reach the owner's real media player; not safe on this PC. Needs the HTPC with no other media app running. |
| LM1 | Video play, stop, resume | PASS | scratch clip (20 s, from the project's own test fixtures): first open playing at 3221 ms; Esc stopped it (mediaOpen false); reopening resumed at 6293 ms (>= first position). Screen not photographed. |
| LM2 | Music Next/Previous | PASS | silent generated WAVs: play queue 1 of 3 "Alpha.wav" > 2 of 3 "Bravo.wav" > 3 of 3 "Charlie.wav" > 2 of 3 "Bravo.wav" > 1 of 3 "Alpha.wav" ; each Next/Previous changed the playing item. The on-screen Now Playing title was not photographed (sidecar has no title field). |
| LM3 | Photo zoom/rotate | PASS | image opened in the viewer; zoom steps 200 > 240 > 200 %, reset -> reset 180; rotation 90 degrees > 180 degrees > 90 degrees; Esc closed the viewer. Rotate tested with the Q/E keys (the remote Info key form is R-row territory). |
| LM4 | Text subtitles | PASS | sidecar .srt next to the clip: track count 1, label 'On (SRT)', cue 1 text drawn ('First cue: the sidecar file beside the v'); Subtitles Off in the video panel cleared it (file ''). Accepted text formats beyond SRT not tested. |
| LM5 | PGS subtitles | NOT RUN | No PGS .sup sample exists on this PC (none in the repo, work/ or out/); a hand-built stream would test my encoder as much as the app. Needs a real clip with a PGS sidecar. |
| LM6 | VobSub subtitles | NOT RUN | No VobSub .idx/.sub pair exists on this PC (the only .idx files found are unrelated capture indexes). Needs a real clip with a VobSub pair. |
| LM7 | Playlists | PASS | scratch playlist: Create 'Mix' via the keyboard; Add to Playlist (Alpha) then Add All -> items Alpha.wav,Bravo.wav,Charlie.wav (3); editor Move/Delete states seen on disk: / Alpha.wav / Alpha.wav,Bravo.wav,Charlie.wav / Bravo.wav,Alpha.wav,Charlie.wav / Bravo.wav,Charlie.wav,Alpha.wav / Bravo.wav,Alpha.wav / ; final Bravo.wav,Alpha.wav; after a restart the folder listed Create New + 'Mix' (2 rows); Delete playlist (Yes) left 0 playlists on disk. Move Up/Down/Delete observed through the file, not photographed. |
| JF1 | Jellyfin connect | NOT RUN | Needs the owner's Jellyfin server address and credentials; no token or sign-in file was read or used. Owner: run the row on the HTPC. |
| JF2 | Jellyfin direct play and resume | NOT RUN | As JF1. |
| JF3 | Jellyfin streaming quality | NOT RUN | As JF1 (and the Jellyfin dashboard). |
| JF4 | Jellyfin text subtitle | NOT RUN | As JF1. |
| JF5 | Jellyfin PGS | NOT RUN | As JF1 (G8: image subtitle route against a real server). |
| JF6 | Jellyfin burned-in | NOT RUN | As JF1 (G8). |
| JF7 | Jellyfin backdrop | NOT RUN | As JF1 (G10). |
| SP1 | Spotify sign-in | NOT RUN | Needs the owner's Spotify sign-in (browser consent) and controls real playback in the owner's Spotify app. Owner at the HTPC. |
| SP2 | Spotify play playlist | NOT RUN | As SP1. |
| SP3 | Spotify transport | NOT RUN | As SP1. |
| SP4 | Spotify Liked Song | NOT RUN | As SP1 (G4). |
| SP5 | Spotify free account | NOT RUN | Needs a free (non-Premium) account the owner supplies (G4). |
| DL1 | DLNA discovery | NOT RUN | No real DLNA server is available to this run (G5). Owner: start Plex/minidlna/Jellyfin DLNA and run the row. |
| DL2 | DLNA playback | NOT RUN | As DL1. |
| DL3 | DLNA unicast | NOT RUN | As DL1. |
| ST1 | Steam playtime line | PASS | read-only: 10 installed titles listed by the XMB (names not recorded); 10 show a 'N h M min played' line, 10 of them equal the Playtime minutes in a localconfig.vdf under this PC's Steam userdata (only apps > id > Playtime/LastPlayed were read). Installed-title ordering by last played and the on-screen TV photo were not checked. |
| ST2 | Steam launch | NOT RUN | Would launch a real Steam title on the owner's PC and take the foreground; not allowed in this run. Owner at the HTPC with one owned title. |
| ST3 | Steam quit in game | NOT RUN | As ST2 (closes a real game). |
| ST4 | Steam Alt-F4 | NOT RUN | As ST2 (Alt-F4 on a real game). |
| ST5 | Steam End task | NOT RUN | As ST2 (End task on a real game). |
| ST6 | Steam playtime after | NOT RUN | Needs a launched game session before and after (ST2-ST5), which this run must not start. |
| ST7 | Steam library/store hand-off | NOT RUN | Opens the real Steam client through steam:// hand-offs; not allowed in this run. |
| ST8 | Steam verify/delete | NOT RUN | Opens Steam dialogs for Verify/Delete on a real title; not allowed in this run. |
| TS1 | Tailscale status | PASS | Network > Tailscale page opened: XMB reads state Running, 8 peers (10 rows); the Tailscale CLI status says Running, 8 peers. The exact 'Connected as <host> - <ip>' wording was not compared (names and addresses are deliberately not captured here). |
| TS2 | Tailscale ping | NOT RUN | Cross on a device sends a Tailscale ping to one of the owner's devices; outside the read-only scope of this run. Needs the owner at the HTPC. |
| TS3 | Tailscale exit node page | PASS | Exit Node page opened (depth 2, 1 rows incl. None); nothing chosen, Esc went back; exit node stays none before and after. Cross was never pressed on a node. |
| TS4 | Tailscale Off/On | NOT RUN | Turning Tailscale Off/On changes the live network of this PC (G7); not run. Needs the owner at the HTPC, accepting a short interruption. |
| WOL1 | WoL online state | NOT RUN | Acts on real home devices: real machines in wol.json and a real ping. Owner at the HTPC. |
| WOL2 | WoL add from network | NOT RUN | As WOL1 (Windows neighbour cache of this PC). |
| WOL3 | WoL wake | NOT RUN | Sends a real magic packet to wake a sleeping machine (G6). Owner picks the machine. |
| HA1 | HA token sealed | NOT RUN | Needs the owner's Home Assistant long-lived token; none was read or placed. Owner at the HTPC. |
| HA2 | HA room folder | NOT RUN | As HA1. |
| HA3 | HA scene | NOT RUN | Runs a real scene on real devices (G6). Owner. |
| HA4 | HA toggle | NOT RUN | Toggles a real light/switch. Owner. |
| HA5 | HA media player | NOT RUN | Controls a real media player. Owner. |
| WE1 | Weather row | PARTIAL | Chicago, IL, units imperial: client current temperature 40.5 F against live Open-Meteo 40.5 F fetched seconds later (within 3 F: true); the on-screen row was read from C:/dev/out/rr/runs/we-f/row.png (not committed). FINDING: with only {"place":"Chicago, IL"} (no units key) the app shows Celsius (cache units 'metric'), although audit/WEATHER.md and checklist WE3 say Fahrenheit is the default and "units":"metric" switches to Celsius; readConfig() sets imperial only for "units":"imperial". |
| WE2 | Weather page | PARTIAL | Weather page opened with 7 rows and the forecast holds 5 days; day rows with icons were seen in C:/dev/out/rr/runs/we-f/page.png (not committed). |
| WE3 | Weather metric | PARTIAL | "units":"metric": client current temperature 4.7 C against live 4.7 C (within 2 C: true); the page showed Celsius text (C:/dev/out/rr/runs/we-c/page.png, not committed). Because the default is also Celsius, this row does not distinguish the setting from the default (see WE1). |
| PW1 | Power dry run | PARTIAL | XMB_POWER_DRY_RUN=1, windowed: Users > Sleep and Restart System each confirmed Yes: Sleep confirmed / sleep (dry run) / Restart System confirmed / restart (dry run) ; non-dry power lines: 0. Turn Off the System in a window (not --kiosk) quits the XMB instead of reaching the dry-run line, so the kiosk Turn Off dry-run line was not observed (it needs a --kiosk start, a display takeover: left to the owner). |
| PW2 | Real Sleep | NOT RUN | A real Sleep would end this session and sleep the PC. Owner at the HTPC. |
| PW3 | Real Restart | NOT RUN | A real Restart would end this session. Owner at the HTPC. |
| PW4 | Real Turn Off (kiosk) | NOT RUN | A real Turn Off in kiosk would power the PC off. Owner at the HTPC as the last item. |
| PW5 | Turn Off without kiosk | PASS | not --kiosk: Users > Turn Off the System > Yes logged 'Turn Off the System confirmed; the XMB quits (not --kiosk, so the PC stays on) ' and the XMB process exited with code 0; Windows stayed up (this run continued). No desktop screenshot taken. |
| AU1 | Audio output switch | NOT RUN | The real audio output switch changes the owner's Windows default playback device (a system setting). AU2 (dry run) covers the code path. Owner at the HTPC with two outputs. |
| AU2 | Audio dry run | PASS | XMB_AUDIO_DRY_RUN=1, Sound Settings > Audio Output Settings > second device: log "audio: would set default endpoint <device> (SetDefaultEndpoint not called)";; Windows default output before/after (read through --settings-report): unchanged (4 outputs listed). |
| CD1 | Copy then Delete | PASS | Copy (above) then Delete on the copy: first answer No left the file (entry 'RR-Probe-Delete-Me (2).wav'), second answer Yes removed it from the library (copy exists: false, original still there: true); Recycle Bin items named RR-Probe-Delete-Me*: 0 -> 1 (entry: RR-Probe-Delete-Me (2).wav deleted-from C:\dev\out\rr\lib\music). LEFTOVER: the Recycle Bin holds 'RR-Probe-Delete-Me (2).wav'; the owner can empty/remove it. Not removed by this script (removing a Recycle Bin item through the shell verb opens a confirmation dialog; a first attempt hung on it and was stopped): 1 item of that name remains. |
| CD2 | Delete outside library | PARTIAL | file reached through a junction whose target (outside) is outside every library root: after Delete > Yes the file was removed; Recycle Bin entries named outside.wav: 1; notification 'Delete completed.'; FINDING: the library-root check is on the path as listed, so a junction inside a library lets Delete move a file whose real location is outside every library root (no row outside the library can be reached from the UI without a link); the genuine refusal case was therefore not reachable. Leftover in the Recycle Bin: outside.wav. |
| CD3 | Copy local file | PASS | Copy on a local file offered Video Library / Photo Library / This Folder (the destination picker exists, so the README's 'needs a destination picker' note is stale); This Folder wrote 'RR-Probe-Delete-Me (2).wav' (byte-identical, sha256 match: true); log: Copy completed: 1 file(s), 32 KB. . Not photographed. |
| SC1 | System Check | PASS | System Check opened read-only: 14 check(s), 1 problem(s) ; rows in order: Steam, System Software, System Assets, System Fonts, Data Folder, Free Space, Graphics, Network Connection, Internet Name Lookup, Jellyfin, Home Assistant, Spotify, Tailscale, Game Artwork (RAWG). First row shown: Steam. Per-row detail text is in the screenshots C:/dev/out/rr/runs/sc1/c*.png (not committed); the doc lists the problems found. No fix applied. |
| SC2 | System Check fixes | NOT RUN | Applying fixes changes files, services or settings on this PC. Needs the owner to run it on the HTPC. What each fix would change is listed in the run doc. |
| US1 | Log Out | PASS | two scratch users (Alpha signed in): Log Out > No left the session (logged-out lines: 0); Log Out > Yes logged out ('profiles: logged out 11111111-1111-4111-8111-111111111111 - login column rows= 7', 7 login rows); a later start on the same data dir: profiles: start signed in as 11111111-1111-4111-8111-111111111111 (automatic login, or the last user of a file without t. Dialogs not photographed. |
| US2 | Automatic Login | PASS | Automatic Login On for the second scratch user stored it as the only automatic user (profiles.json autoLogin = second user: true); Off cleared it (autoLogin now ''). Whether Windows signs in on the next boot is a separate Windows setting, not tested. |
| LS1 | Idle soak | PARTIAL | first soak, default start (render gate on): 32.0 min after a 15 s startup, 64 samples every 30 s (C:/dev/out/rr/soak.csv); after the first 5 minutes working set stayed 268.7-270.6 MB, private bytes 405.7-406.4 MB, handles 1611-1645, threads 11-14 (start 313 MB / 457 MB / 1680 / 20). Flat: no leak signal. CPU 1-39 % of one core, median 12 %: the window was partly covered by other windows while I worked, so the render gate closed 60 times and the frame loop stopped for those stretches (see LS3). Shortened run: LS1 is not the 4 h run, and no remote key was pressed at the end. SECOND SOAK: second soak, XMB_RENDER_GATE=observe so the frame loop never stops: 30.0 min after 15 s, 60 samples (C:/dev/out/rr/soak2.csv); after the first 5 minutes working set 271.3-291.7 MB (final 271.5), private 408.8-442.4 MB (final 409.3), handles 1608-1671, threads 11-15; start 310 MB / 457 MB / 1680 / 20. Memory and handles fall after startup and then sit flat for the last 10 minutes: no leak signal. CPU 37-45 % of one core (median 38 %), frame rate median 240 fps. |
| LS2 | Playback soak | NOT RUN | Needs 4 h of real playback with a title; only the idle soak was run. |
| LS3 | Log after the soaks | PARTIAL | soak: 523 lines, 18 known OpenGL exit warnings, 0 other warning-like lines, render gate closed 60x, frame rate median 234.8 fps (min 3.16367) ; soak2: 642 lines, 18 known OpenGL exit warnings, 0 other warning-like lines, render gate closed 0x, frame rate median 239.952 fps (min 230.835). Read after the idle soaks only (no playback soak, LS2 not run). |
| RC1 | Kill the XMB | PASS | packaged watchdog.ps1 (copy with only '--kiosk --display N' replaced by '--start-windowed'; scratch data; -StableSeconds 10): killed only the XMB PID it had started (taskkill /PID ok); watchdog log: policy: safe mode after 2 crashes within 300 s; back to normal after a safe run of 10 s or more; give up after 5 rapid failed starts / starting the XMB in normal mode / exited with code 1 after 13.5s / crash 1 of 2 within the last 300 s / restarting in 3 s / starting the XMB in normal mode ; new XMB PID different: true. The checklist text 'no watchdog exists yet' is stale: a watchdog exists and restarts after a kill. |
| RC2 | Start again | PASS | after the restart the same scratch data dir was in use (marker file and preferences present: true); closing the restarted XMB normally made the watchdog log 'clean exit (code 0); watchdog stopping' and end (stopped by itself: true). Crash files from a taskkill: 0 (a kill leaves none). Playlists persistence is covered by LM7; kiosk mode itself not used. No scheduled task registered. |
| G1 | Known gap | OPEN | Physical MCE/IR, Flirc and Bluetooth remotes; an XInput pad (Guide, slot merge, battery read); the DualSense buttons. This run posted WM_APPCOMMAND and key messages to the XMB window and used a synthetic pad state only. Rows N7, N8, R1-R8. |
| G2 | Known gap | OPEN | The declined path (transport key with no media open) is still not exercised live: posting it reaches the owner's real media players. Row R9. |
| G3 | Known gap | OPEN | The Media Center keyboard shortcut list is still inferred, not tested against a remote. |
| G4 | Known gap | OPEN | Spotify Liked Songs queue on a real device, and a free account. Rows SP4, SP5. Not run. |
| G5 | Known gap | OPEN | DLNA against a real server. Rows DL1-DL3. Not run. |
| G6 | Known gap | OPEN | A real Wake on LAN and a real Home Assistant action. Rows WOL3, HA3. Not run. |
| G7 | Known gap | OPEN | Tailscale Off/On and choosing an exit node. Row TS4. Not run (the exit node page was only opened, TS3). |
| G8 | Known gap | OPEN | Jellyfin image subtitles (.sup route) and burn-in on a real server. Rows JF5, JF6. Not run. |
| G9 | Known gap | OPEN | Jellyfin server-side transcode decision. Row JF3. Not run. |
| G10 | Known gap | OPEN | Jellyfin backdrop on a real server. Row JF7. Not run. |
| G11 | Known gap | OPEN | Weather beyond Chicago, IL: this run again used Chicago (neutral place), so other places stay untested. Rows WE1-WE3. |
| G12 | OpenGL warnings on exit | FAIL | 18 OpenGL texture/context warning lines in the soak log after a graceful WM_CLOSE exit (exit code 0). QOpenGLTexturePrivate::destroy() called without a current context. / Texture has not been destroyed |
| G13 | Known gap | OPEN | Update: a watchdog now exists (packaging/watchdog.ps1) and restarted the XMB after a kill (RC1); autostart registration and a kiosk autostart were not exercised (no scheduled task registered). PW3 not run. |
| G14 | Known gap | OPEN | Update: Copy offers a destination list (Video Library, Photo Library, This Folder) and completed in CD3; the README line saying the destination picker is not built is stale. |
| G15 | Known gap | OPEN | The folder named native-data is the --data-dir folder: weather.json and profiles were read from there in this run (WE1, US1). Not confirmed for spotify.json, dlna.json, wol.json, homeassistant.json (not run). |
<!-- table:end -->

## FAIL and PARTIAL: causes

No row failed outright. The PARTIAL rows, with the cause:

- **N1 to N6** (pad navigation, Cross, Circle, Triangle, L1/R1, on-screen keyboard): the behaviour passed through script
  key taps (one column or row per press through all 10 columns, held-Down repeat, PageDown/PageUp moving 10 rows,
  Triangle options, Cross and Circle depth changes, typing, Backspace, Esc-cancel and the on-screen Enter key creating a
  playlist). A synthetic pad press (`XMB_PAD_FAKE`) reached the live pad code as `source=pad action=...` for D-pad down,
  stick down, Cross, Circle, Triangle, L1 and R1. Cause of PARTIAL: no physical pad, no stick, and the pad's own
  10-row result and the A/X/Y/Start/B keyboard bindings were not observed. The pad path cannot run inside a `--script`
  run (the gate withholds it), so a pad press and its effect cannot be seen in one run on this PC.
- **R1 to R6, R8** (remote keys): all passed as `WM_APPCOMMAND` and `WM_KEYDOWN` messages posted to the XMB window
  (Pause, Play, Play/Pause, Stop, Next, Previous, a held Next counted as one skip, Fast Forward and Rewind in a video at
  10 s, Channel Down/Up moving 10 rows, Back as Circle, Home opening and closing the system menu, a duplicate swallowed).
  Cause of PARTIAL: not a physical remote (G1, G3 stay open). R7 and R9 are NOT RUN, see the table.
- **CD2**: the row's expected case (a file outside every library folder) cannot be reached from the UI. Through a
  directory junction inside a library the file was moved to the Recycle Bin, see the bugs section.
- **PW1**: Sleep and Restart logged `(dry run)` after Yes. Turn Off in a window (no `--kiosk`) quits the XMB instead (that
  is PW5, PASS), so the kiosk Turn Off dry-run line was not observed.
- **WE1 to WE3**: values match live Open-Meteo for Chicago, IL in both units (40.5 F, 4.7 C, five forecast days). PARTIAL
  because the on-screen text and icons were judged from screenshots by eye, and because of the default-units finding below.
- **LS1, LS3**: two idle soaks of 30 minutes or more instead of 4 hours, no remote key, no playback (LS2 NOT RUN).
- **G12 (FAIL, a gap row)**: every graceful exit prints 9 pairs of `QOpenGLTexturePrivate::destroy() called without a
  current context` and `Texture has not been destroyed` (18 lines per run, identical in both soak runs, which were
  stopped by WM_CLOSE). Diagnosis, not verified in the code: texture objects owned by the view or the asset cache are
  destroyed after the GL context is gone, which is harmless at process end but is the "known warning" LS3 has to allow.

## Bugs and doc errors found

1. **Weather default units contradict the docs** (WE1, WE3). Reproduction: put `{"place": "Chicago, IL"}` (no `units`
   key) in `<data>/weather.json`, start, open Network > Weather. It shows Celsius (5 C Overcast) and rewrites the file
   with `"units": "metric"`. `WeatherClient::readConfig` sets imperial only for `"units": "imperial"`. `audit/WEATHER.md`
   and checklist WE3 say Fahrenheit is the default and `"units":"metric"` switches to Celsius; the 2026-11-07 live check
   in WEATHER.md showed 39 F. Either the default or the docs need to change. As the checklist is written, WE3 cannot
   distinguish the setting from the default.
2. **Delete follows a junction out of the library** (CD2). Reproduction: `mklink /J <library>/link <folder outside every
   library root>` holding a media file, open the library, enter `link`, Triangle on the file > Delete > Yes. The file is
   moved to the Recycle Bin ("Delete completed."). The library-root test is made on the path as listed, not on the
   resolved location. Whether that is wanted (a library that is a junction to another drive) is the owner's call; the
   checklist wording ("a file outside the library folders is not moved") is not met for that case.
3. **`work/v2-fixtures.ts` fails in a fresh worktree.** `bun work/v2-fixtures.ts` ends with `ENOENT copyfile` at
   `seed()` (line 110): it copies `settings.ini` and `profiles.json` from `out/native-reference/store/ui3-data`, which is
   untracked and absent in a new worktree. The WAV, stub game and gallery fixtures already existed, so `--check` passes;
   the seeds the gate reads are not written. This run did not need them.
4. **System Information names the wrong Windows.** `--settings-report --settings-page system` gives
   `System Information: Windows 10 Pro 25H2, 47.9 GB RAM` on this Windows 11 Pro (10.0.26200) PC. Not traced in the
   code; the string probably comes from the Qt product-name call.
5. **Stale checklist and README text** (not app bugs): RC1 says no watchdog exists (it does and it restarts after a
   kill); CD3 and G14 say Copy needs a destination picker that is not built (it offers Video Library, Photo Library and
   This Folder, and copied byte-for-byte); N5 and R5 expect a `selection` log line, which only the remote Channel keys
   write, not a pad L1/R1 press (the pad path logs only `source=pad action=r1`).
6. **Logs carry account details.** The XMB's own log writes the Windows account name (`autologon: on for <user>`), the
   Tailscale host name (`tailscale: status ... self <host>`), and audio device names. The checklist's "no account
   details in copied logs" rule means every log copied off the HTPC has to be scrubbed first.

Behaviour worth knowing (not bugs): Add All skips tracks already in the playlist; Log Out is only offered with two or
more users; the on-screen keyboard has no script token for Start/accept, so the run presses its Enter key instead.

## Idle soak (LS1, LS3, G12)

Two windowed soaks of the unscripted XMB idling on the Game root, one sample every 30 s of working set, private bytes,
handle count, thread count and CPU (`C:/dev/out/rr/soak.csv`, `soak2.csv`; not committed):

| Soak | Length | Working set | Private bytes | Handles | Threads | CPU (one core) |
|---|---|---|---|---|---|---|
| 1: default start, render gate on | 32.0 min | 313 MB at start, then 268.7-270.6 MB after 5 min | 457 MB, then 405.7-406.4 MB | 1680, then 1611-1645 | 20, then 11-14 | 1-39 %, median 12 % |
| 2: `XMB_RENDER_GATE=observe`, frame loop never stops | 30.0 min | 310 MB at start, then 271.3-291.7 MB (final 271.5) | 457 MB, then 408.8-442.4 MB (final 409.3) | 1680, then 1608-1671 | 20, then 11-15 | 37-45 %, median 38 %, about 240 fps |

Trend: **flat, no leak.** Memory and handles fall after the startup ramp and stay level for the last 10 minutes of both
runs. Soak 1 was partly covered by other windows while the other rows ran, so the render gate closed 60 times and the
frame loop stopped for those stretches (frame rate median 235 fps, minimum 3 fps in the 5 s reports). Soak 2 is the
continuous-render measurement. Other rows ran in parallel on the same PC, which adds noise to the CPU figures only.
This is not the 4-hour run: LS1 is PARTIAL and LS2 is NOT RUN.

LS3: both soak logs hold only the 18 known OpenGL exit lines and no other warning-like line.

## System Check (SC1) and what SC2 would change

SC1 ran read-only against this PC's real Steam folder: **14 checks, 1 problem.** The problem is Steam: "Warning - Found
at the Steam folder; 1 of 5 libraries unavailable: G:/SteamLibrary", fix text "Connect the drive that holds the missing
library; its games are hidden until then." The 13 passes, in order: System Software, System Assets, System Fonts, Data
Folder, Free Space, Graphics, Network Connection, Internet Name Lookup, Jellyfin, Home Assistant, Spotify, Tailscale and
Game Artwork (RAWG).

SC2 is NOT RUN, and reading `self_check.cpp` shows there is nothing for the XMB to apply: each fix is a sentence of
advice that the owner follows by hand. The XMB changes nothing. The advice the checks can give: reinstall the firmware in
RPCS3 or point `--dev-flash` at a good `dev_flash`; run with a writable `--data-dir`; delete a leftover `.selfcheck` file;
install the GPU vendor's driver; check the Ethernet or Wi-Fi connection and the DNS server; fix or re-import
`apps.json`; add a Jellyfin server or check its address; fix `homeassistant.json`, start Home Assistant, or create a
long-lived token for it; open Spotify in the Music column and sign in; install, reinstall, restart or start Tailscale; connect
the drive that holds a Steam library, or open Steam > Settings > Storage.

## What the owner has to do

- **Hardware at the HTPC (physical pad, remote, kiosk display):** B1 (kiosk start with the intro, photo of the TV), N7 and N8
  (PS/Guide tap and hold, with a DualSense and with an Xbox pad), a physical pass of N1 to N6, R1 to R8 with the real
  remote, R7 (volume keys), R9 (declined path with no other media app running), the kiosk Turn Off dry-run line (PW1),
  LS1 for 4 hours with Task Manager screenshots and a remote key at the end, LS2 (4 hours of playback), PW2 to PW4 (real
  Sleep, Restart, Turn Off, the last item), PW3's "does the XMB come back" question (autostart is not registered by this run).
- **Media files this PC does not have:** LM5 (a clip with a PGS `.sup` sidecar) and LM6 (a VobSub `.idx`/`.sub` pair).
- **Credentials or accounts:** JF1 to JF7 (Jellyfin server and sign-in), SP1 to SP5 (Spotify sign-in, Premium; SP5 also
  a free account), HA1 to HA5 (Home Assistant token), DL1 to DL3 (a real DLNA server).
- **Consent for a live action on real things:** ST2 to ST8 (launching, closing, killing and verifying or deleting real
  Steam titles, opening Steam), WOL1 to WOL3 (real machines, a real wake), HA3 to HA5 (a real scene, light and media
  player), TS2 (ping a tailnet device), TS4 (Tailscale Off and On, a short network interruption), AU1 (the real
  default audio device switch), SC2 (follow the advice by hand after reading SC1).
- **Decisions:** the weather default (Celsius or Fahrenheit) and whether Delete should follow junctions; empty the two
  leftover Recycle Bin items named below.

## Things this run left behind or got wrong

- **Recycle Bin leftovers (2 items, scratch files from CD1 and CD2):** `RR-Probe-Delete-Me (2).wav` (deleted from
  `C:/dev/out/rr/lib/music`) and `outside.wav` (deleted from `C:/dev/out/rr/outside`). Both are 2 s of silence or less. I did
  not remove them: removing a Recycle Bin item through the shell verb opens a confirmation dialog, and a first attempt
  hung on it (my own PowerShell, stopped by its PID). The owner can delete just those two items.
- **A mistake in the first version of the R-rows script.** Its video step waited on a log pattern that matched too early
  and posted Fast Forward and Rewind (`WM_APPCOMMAND` 49 and 50) to the XMB window before the video had opened. The XMB
  declined them, and a declined transport key goes on to Windows (INPUT.md section 1), so those two keys may have reached
  a media player on this PC. Nothing was observed to react. The script now posts a transport key only while the log says
  media is open, and skips it otherwise (counter in the R1 evidence: 0 skipped in the final run).
- Audio: the default start in B2 plays the cold-boot intro sound, and the interaction cues play, through this PC's default
  output at its current volume. No volume, mute or default-device setting was changed (the audio dry-run switch was on in
  every run; the settings report showed the same default device before and after AU2).
- Not touched: the owner's real XMB data folder, RPCS3 data, `C:/dev/out/rpcs3-cap`, any token or sign-in file, Windows
  settings, scheduled tasks and shortcuts (the autostart task name was a scratch name that was never registered). The
  watchdog test used a copy of `watchdog.ps1` in the scratch package with only `--kiosk --display N` replaced by
  `--start-windowed`; the repository file is unchanged. Only PIDs the script started were closed (WM_CLOSE, with
  `taskkill /PID` as the last resort).
- A few exploration runs before the script existed pressed Cross on rows to learn the layout (for example the Users >
  Steam Account row, with an empty Steam root) and copied a scratch WAV between scratch library folders; nothing outside
  `C:/dev/out/rr` was written. Two of them read the Steam library read-only (the Game column and SC1).
