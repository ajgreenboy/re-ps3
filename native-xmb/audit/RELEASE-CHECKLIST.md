# Release checklist: HTPC evidence run (plan V1)

Run this on the real living-room HTPC, in order. Record each row in the Pass/Fail column and save the evidence named in the row. Rows that depend on hardware or accounts the owner must supply are repeated in section 3 with their source.

## Conventions

- `<exe>`: `out\native-xmb\re-ps3-native.exe` in the build folder.
- `<firmware>`: the firmware root that holds `dev_flash`. INSTALL.md section 11 says it is not the RPCS3 install root.
- `<data>`: the writable data folder. Default `<exe folder>\data`. The launcher uses `out\native-xmb\data`.
- `<native-data>`: the folder the README names for `spotify.json`, `dlna.json`, `wol.json`, `homeassistant.json`, `weather.json` and `playlists\`. It is the `<data>` folder: every service reads its json from the data directory (`dataDirectory_`, set by `--data-dir`).
- `<log>`: the XMB log file, set with `XMB_LOG=<file>` (INSTALL.md section 7.2). Without it, crash stacks go to `<exe folder>\crash.log` (INSTALL.md section 6).
- `<evidence>`: the evidence folder. The docs define none. TODO: the owner picks one and records its path in the run notes.
- No tokens, passwords, refresh tokens or account details in screenshots or copied logs. Where a row says "file check", show only that the key is present or absent.
- `XMB_INPUT_TRACE=1` adds remote and pad decision lines to the log (INPUT.md section 1). Set it for sections 2.3 and 2.1.

## 1. Setup on the HTPC

The features documented in the README are Windows-only (XInput, DPAPI, Recycle Bin, `tailscale.exe`), so the HTPC must run Windows. (INFERRED from the README; not stated in the install docs.)

| Step | Action | Source |
|---|---|---|
| 1. Build | Build into `out\native-xmb` with `build.ps1` (the launcher's output folder). Build details are in INSTALL.md section 2, which this file did not read. | INPUT.md section 4 (`build.ps1 -OutputDirectory`), INSTALL.md section 3 |
| 2. Copy the build | Copy the whole `out\native-xmb` folder. Run the exe from its own folder so Qt finds `platforms\` and the image plugins. Copy method to the HTPC: TODO, not documented. | INSTALL.md section 3 |
| 3. Firmware | Put a local `dev_flash` folder on the HTPC and pass it as `--dev-flash <firmware>`. The app only reads it. The launcher defaults to a sibling `rpcs3\bin\dev_flash`. How the firmware reaches the HTPC: TODO, not documented. | INSTALL.md sections 3 and 11 |
| 4. Data folder | Pass `--data-dir <data>`. The first launch creates it with default preferences. | INSTALL.md section 3 |
| 5. Log | Before launching, run `$env:XMB_LOG = '<log>'` in the same PowerShell session. The launcher also writes session logs; its location: TODO, not read for this file. | INSTALL.md sections 3 and 7.2 |
| 6. Kiosk launch | `& <exe> --dev-flash <firmware> --data-dir <data> --kiosk --display 1`. The documented example omits `--data-dir`; adding it pins the folder. `--display 1` is the second screen, so confirm the TV is display 1 in Windows Display settings. | INSTALL.md section 3 |
| 7. Autostart at login | `packaging/install-autostart.ps1` registers the logon task that runs the watchdog; Windows automatic sign-in is a manual step (the package never stores a password). | packaging/README.md; AUTOLOGON.md |

## 2. Checklist

Columns: ID, what to do, what correct looks like, evidence, Pass/Fail (left blank).

### 2.1 Boot and kiosk

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| B1 | Run the kiosk command from setup step 6 without `--no-intro`. | The cold-boot intro plays, then the XMB fills display 1 with no window frame and no taskbar. | Photo of the TV; first 50 lines of `<log>`. | |
| B2 | Read the whole `<log>` after B1. | The file exists and has messages from this start. No crash stack. | Copy of `<log>`. | |
| B3 | Quit, then start the same command with `--no-intro`. | Starts directly at the XMB. Preferences from the first start are still there. | Photo of the TV; `<data>` file listing. | |

### 2.2 Navigation with controller only

Keyboard and mouse disconnected for this section. Use an Xbox-layout (XInput) pad, then a DualSense over USB if one is available.

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| N1 | D-pad and left stick through every column (Game, Music, Video, Photo, Network, Users) and into their rows. | One step per press; stick movement repeats; focus never sticks or skips. | Video of one full pass. | |
| N2 | Cross (A) on a row with a list. | The row's surface opens. | Photo of the TV. | |
| N3 | Circle (B) to go back. | Goes back one level. | Photo of the TV. | |
| N4 | Triangle (Y) on a title or item. | Options menu opens. | Photo of the TV. | |
| N5 | L1 and R1 on a list with more than 10 rows. | Selection moves 10 rows per press. | Photo; log line `source=pad action=l1`/`r1` (a `selection` line is written only by the remote Channel keys). | |
| N6 | Open a text field with the pad (Music or Photo > Playlists > Create New Playlist). | On-screen keyboard: D-pad picks keys, A types, X backspace, Y space, Start accepts, B cancels. | Photo of the keyboard in use. | |
| N7 | Tap PS (DualSense) or Guide (Xbox pad) while another window has focus. | The XMB comes to the front. | Photo of the TV. | |
| N8 | Hold PS 1 s (DualSense) or Guide 1 s (Xbox pad). | System menu opens; a second hold closes it. The Guide path is not verified (see G1). | Photo of the TV. | |

### 2.3 Remote and media keys

Use the owner's real remote (MCE/IR or Bluetooth HID). Start with `XMB_INPUT_TRACE=1`. The mapping in INPUT.md section 2 was only tested with posted messages and SendInput, not with a remote (see G1 and G2).

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| R1 | With a music track playing, press Play/Pause. | Playback toggles; one log line `remote: ... -> pause` or `-> play`. | Log lines. | |
| R2 | Press Stop with media open. | Playback stops and the resume point is kept; log `stop; mediaOpen now 0`. | Log lines. | |
| R3 | Press Next, then Previous, with an album playing. Hold Next briefly. | One track per press; a held key does not skip repeatedly. | Log lines. | |
| R4 | Press Fast Forward and Rewind in a video. | About 10 s per press. | Log lines with positions. | |
| R5 | Press Channel Up and Channel Down in a list. | Selection pages 10 rows. | Log line `selection` (written for the remote Channel keys). | |
| R6 | Press the remote's Back and Home keys. | Back acts as Circle. Home opens the system menu; a second Home closes it. | Photo of the TV. | |
| R7 | Press Volume Up, Volume Down and Mute. | Windows volume changes; the player follows. | Screenshot of the Windows volume indicator. | |
| R8 | Press one key once, then check the log. | One action per press. Any second copy within 100 ms is logged as swallowed. | Log lines. | |
| R9 | With no media open and no other media app running, press Play/Pause. | The key is passed on to Windows (declined path). Not exercised live before (see G2). | Log line for the key. | |

### 2.4 Local media

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| LM1 | Video > library folder > play a file. Press Stop. Open the file again. | Plays. Second open resumes at the stored position. | Photo of the TV. | |
| LM2 | Music: play a track, then Next and Previous. | Now Playing shows the title and changes with each track. | Photo of the TV. | |
| LM3 | Photo: open an image, zoom with + and -, reset with 0, rotate with Info. | Zoom and rotation apply; reset returns to the original view. | Photo of the TV. | |
| LM4 | Put a text subtitle file next to a video and select it. | Subtitle text draws over the video; the track can be switched off. Accepted text formats: TODO, not confirmed in the docs read. | Photo of the TV. | |
| LM5 | Put a PGS sidecar `<video>.sup` next to a video and select it. | Picture subtitles draw over the video. | Photo of the TV. | |
| LM6 | Put a VobSub pair (`.idx` and `.sub`) next to a video and select it. | Picture subtitles draw; one track per `.idx`. | Photo of the TV. | |
| LM7 | Playlists: Create New Playlist, Add to Playlist, Add All, Move Up, Move Down, Delete. | Each action works and the change persists after a restart. | Listing of the playlists folder under `<native-data>`. | |

### 2.5 Jellyfin

Server address and login: INSTALL.md section 9, not read for this file (TODO: confirm the steps).

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| JF1 | Connect to the real Jellyfin server and open one library. | Libraries and items list; artwork loads. | Photo of the TV. | |
| JF2 | Play one title that the client can direct-play. Stop, then play again. | Plays; the second start resumes. | Photo of the TV. | |
| JF3 | Set a lower Streaming Quality ceiling from the row menu, then play. | Playback starts. The server-side transcode decision is not verified (see G9). | Jellyfin dashboard "Now playing" entry; photo. | |
| JF4 | Play a title with a server text subtitle; switch tracks. | Text draws; switching works. | Photo of the TV. | |
| JF5 | Select a server PGS image subtitle. | Picture subtitles draw from the `Stream.sup` route. Not verified on a real server (see G8). | Photo of the TV; log line. | |
| JF6 | Select a VobSub stream, or a PGS stream whose `.sup` fetch fails. | Row is marked "Burned in by the server"; the title reopens at the same position; subtitle text is in the picture. Not verified on a real server (see G8). | Photo of the frame. | |
| JF7 | Open an item page with a backdrop. | The real server backdrop shows. Not checked before (see G10). | Photo of the TV. | |

### 2.6 Spotify

Needs a Spotify Premium account for playback control (README). Client id goes in `<native-data>\spotify.json`, redirect `http://127.0.0.1:8888/callback`.

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| SP1 | Put the client id in `spotify.json`. Music > Spotify > sign in through the browser. | Sign-in completes; Liked Songs and playlists list. | Photo of the TV. No token in evidence. | |
| SP2 | Triangle on a playlist > Play. | The track plays in the Spotify app on this PC; Now Playing shows it. | Photo of the TV; screenshot of the Spotify app. | |
| SP3 | Play/Pause, Next and Previous from Now Playing and from the remote. | The Spotify app responds to each press. | Photo; log lines. | |
| SP4 | Play one Liked Song. | Plays. Liked Songs queue path on a real device not verified (see G4). | Photo of the TV. | |
| SP5 | Sign in with a free (non-Premium) account, only if the owner supplies one. | A Premium-required message shows; nothing crashes. Not verified (see G4). Mark Pass only if the owner runs it. | Photo of the message. | |

### 2.7 DLNA

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| DL1 | Music (or Video, Photo) > first row, Search for Media Servers, with the real server running (Plex DLNA on 32469/tcp and 1900/udp, or minidlna or the Jellyfin DLNA plugin, per audit/DLNA.md). | The real server appears in the list. Not verified on a real server (see G5). | Photo of the TV. | |
| DL2 | Browse to an audio file, a video and a photo. | Audio and video play in the normal player with resume and controls; the photo opens in the viewer. | Photo of the TV. | |
| DL3 | If multicast does not reach the server, add `locations` and `unicast` to `<native-data>\dlna.json`, then search again. | Server found through the unicast address. | Photo of the TV. | |

### 2.8 Steam

Needs one installed title the owner owns. Steam must be running for the `steam://` hand-offs.

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| ST1 | Open an installed Steam title's detail line. Note the value. | Shows "N h M min played - Last played ..." and matches the value in `userdata\<account>\config\localconfig.vdf`. Installed titles are ordered by last played. | Photo of the TV; value copied from the vdf file. | |
| ST2 | Launch the title from the XMB. | Game starts and comes to the front. XMB rendering pauses while the game is in front. | Photo; Task Manager screenshot showing the game in front. | |
| ST3 | Quit the game normally from inside it. | XMB returns fullscreen with the same row selected. | Photo of the TV. | |
| ST4 | Launch again and close the game with Alt-F4. | XMB returns fullscreen with the same row selected. | Photo of the TV. | |
| ST5 | Launch again and End task on the game's process in Task Manager (crash path). | XMB returns fullscreen with the same row selected. | Photo of the TV; Task Manager screenshot. | |
| ST6 | Compare the playtime with the vdf value noted in ST1 after the sessions. | Playtime rises by about the session length. Steam's write timing is not documented (TODO). | Before and after vdf values; detail line photo. | |
| ST7 | Triangle > Open in Steam Library, then Store Page. | Steam opens each page through `steam://`. | Photo of the TV. | |
| ST8 | Triangle > Verify Game Files, then Delete, on an installed title. | Steam shows its own dialog. Cancel both; the owner decides separately whether to run either. | Photo of the Steam dialog. | |

### 2.9 Tailscale

Tailscale must be installed and signed in on the HTPC.

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| TS1 | Network > Tailscale. | Row reads "Connected as <host> - <tailnet IP>", matching the Tailscale client on the HTPC. | Photo of the TV; screenshot of the Tailscale client. | |
| TS2 | Cross on one listed device. | "pong from ..." with that device's address. | Photo of the TV. | |
| TS3 | Open the Exit Node page. Do not choose a node. | Page lists nodes; the network does not change. | Photo of the TV. | |
| TS4 | Turn Tailscale Off, then On. Run only if the owner accepts a short network interruption. | Off asks first. The state changes, and On reconnects. Not exercised before on purpose (see G7). | Photo of each state. | |

### 2.10 Wake on LAN

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| WOL1 | Put real machines in `<native-data>\wol.json` (name, MAC, host). Launch. | Each row shows Online or Offline matching a ping from the HTPC. | Photo of the TV; ping output. | |
| WOL2 | Add from This Network. | Machines Windows has seen appear in the list. | Photo of the TV. | |
| WOL3 | Put one target machine to sleep (owner's choice; its Wake-on-LAN setting must be on, and that setup is TODO). Cross on its row. | The machine wakes and answers; the XMB reports when it answers. Not verified before (see G6). | Photo; time from Cross to answer. | |

### 2.11 Home Assistant

Needs a long-lived access token in `<native-data>\homeassistant.json` as `{"url":"http://<HA address>","token":"<token>"}`.

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| HA1 | Launch once with the token file present. | The token moves to a DPAPI-sealed file; `homeassistant.json` no longer holds the token. | File check: show that the token key is absent. Do not capture the token. | |
| HA2 | Open a room folder. | Entities list; lights, switches and fans show their state. | Photo of the TV. | |
| HA3 | Run one scene (Cross on it). | Home Assistant runs the scene and the devices change. Not verified before (see G6). | Photo of the room; HA history entry. | |
| HA4 | Toggle one light or switch. | Toggles and the row shows the new state. | Photo of the TV; HA state. | |
| HA5 | Play/pause one media player, if one exists. | Play/pause works. | Photo of the TV. | |

### 2.12 Weather

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| WE1 | Set the owner's place in `<native-data>\weather.json`. Launch. | Current conditions on the row match the Open-Meteo page for that place. | Photo of the TV; Open-Meteo screenshot. | |
| WE2 | Open the Weather page. | Five days with icons. | Photo of the TV. | |
| WE3 | Set `"units":"metric"` and relaunch. | Temperatures show in Celsius. | Photo of the TV. | |

### 2.13 Power

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| PW1 | Launch once with `XMB_POWER_DRY_RUN=1`. Choose Users > Sleep, then Restart, then Turn Off, confirming each. | The log records the intended action each time; nothing happens. | Log lines. | |
| PW2 | Real run: Users > Sleep, confirm. | The PC sleeps. The wake method is not documented (TODO). | Photo of the TV, time stamps. | |
| PW3 | Real run: Users > Restart System, confirm. | Windows restarts. Whether the XMB returns depends on the autostart task being registered (setup step 7, see G13). Record what happens. | Photo of the TV after restart. | |
| PW4 | Real run in kiosk (last item of the run): Users > Turn Off the System, confirm. | Windows shuts down. | Photo of the screen going off; time stamp. | |
| PW5 | Launch without `--kiosk`. Users > Turn Off the System, confirm. | The XMB quits; Windows stays up. | Screenshot of the desktop. | |

### 2.14 Audio output

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| AU1 | Settings > Sound Settings > Audio Output Settings. Choose a second playback device while a track plays. | The Windows default changes and playback follows. | Photo of the TV; Windows sound settings screenshot. | |
| AU2 | Launch once with `XMB_AUDIO_DRY_RUN=1` and repeat AU1. | The log says it would switch; the device does not change. | Log lines. | |

### 2.15 Copy and Delete

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| CD1 | Copy a test file into a library folder. Delete it from the XMB and confirm. | File is moved to the Recycle Bin and leaves the library. | Recycle Bin screenshot. | |
| CD2 | Try to delete a file outside the library folders, if one exists. | The file is not moved. The refusal message is TODO, not documented. | Screenshot of the result. | |
| CD3 | Copy on a local file. | The destination list (Video Library, Photo Library, This Folder) appears; choosing one writes a byte-identical copy (the 2026-11-12 run passed this). | Photo of the TV. | |

### 2.16 System Check

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| SC1 | Settings > System Settings > System Check. | Read-only checks run; problems are listed first with their fix. No file changes. | Screenshot of the result. | |
| SC2 | Apply each listed fix, then run System Check again. | Each fixed problem clears, or the remaining one is recorded with its reason. | Screenshot before and after. | |

### 2.17 Users

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| US1 | Users > Log Out. Choose No, then start again and choose Yes. | No: nothing changes. Yes: the session ends. Exact screens and states are in audit/FEATURE-STATUS-2026-11-04.md (Users items). | Photo of each dialog. | |
| US2 | Set Automatic Login On for a user row, then Off. | On stores that user as the only automatic one; Off clears it (FEATURE-STATUS-2026-11-04.md). Next-start behaviour: TODO, Windows autologon is separate. | Screenshot of the row after each change. | |

### 2.18 Long session

Pass criteria for memory and CPU are not documented. Record the numbers and let the owner decide.

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| LS1 | Leave the XMB idle for 4 hours with the TV on. Check Task Manager at 0 h, 1 h, 2 h and 4 h. Press a remote key at the end. | No freeze; the key responds. Memory and CPU of `re-ps3-native.exe` recorded at each point. | Task Manager screenshots; values in the run notes. | |
| LS2 | Play one title through to the end, for at least 4 hours of playback, with Task Manager values at the same points. | No audio dropout or freeze. | Task Manager screenshots; photo of the end. | |
| LS3 | After LS1 and LS2, read `<log>`. | No new warnings beyond the known OpenGL texture and context warnings on exit (VERIFICATION-HARNESS.md). | Copy of `<log>`. | |

### 2.19 Recovery

The packaged `watchdog.ps1` (see packaging/README.md) restarts the XMB after a crash and drops to `--safe` after repeated crashes. Run RC1 through the watchdog; a bare `re-ps3-native.exe` has no supervisor.

| ID | What to do | Correct looks like | Evidence | Pass/Fail |
|---|---|---|---|---|
| RC1 | With the XMB started by `watchdog.ps1` and in front, End task on `re-ps3-native.exe` in Task Manager. | The watchdog restarts the XMB; after repeated crashes inside its window it starts in safe mode. Started without the watchdog, nothing restarts it, which is expected. | Photo of the TV; Task Manager screenshot. | |
| RC2 | Start again with the kiosk command from setup step 6. | Starts; preferences and playlists from `<data>` are present. | Photo of the TV; `<data>` listing. | |

## 3. Known gaps to expect

Each gap is a row above that the docs say was not verified. Expect these to fail or stay open on this run.

| Gap | What is not verified | Source | Rows |
|---|---|---|---|
| G1 | Physical MCE/IR, Flirc and Bluetooth remotes; an XInput pad (Guide, slot merge, battery read). The DualSense was read but its buttons were not pressed. | audit/INPUT.md, "Not verified here" (lines 137-139) | N8, R1-R8 |
| G2 | The declined path (transport key with no media open) was not exercised live, because the key goes on to Windows. | audit/INPUT.md (lines 140-141) | R9 |
| G3 | The Media Center keyboard shortcut list is INFERRED, not tested against a remote. | audit/INPUT.md (line 139) | R1-R8 |
| G4 | Spotify: the Liked Songs queue path on a real device, and a free (non-Premium) account. | audit/SPOTIFY.md, "Not verified" (line 107) | SP4, SP5 |
| G5 | DLNA against a real server. Only a local mock was used. | audit/DLNA.md, "Not verified" (line 51) | DL1-DL3 |
| G6 | Home services: a real Wake on LAN, and a real Home Assistant action. | audit/HOME-SERVICES.md, "Not verified" (line 40) | WOL3, HA3 |
| G7 | Tailscale: turning it off or on, and choosing an exit node, were not run because they change the live network. | audit/TAILSCALE.md (lines 36-37) | TS4 |
| G8 | Jellyfin image subtitles (the `.sup` delivery route) and burn-in, against a real server. Both were exercised only against a local stub. | audit/FEATURE-STATUS-2026-11-04.md (lines 121-122); audit/JELLYFIN-NATIVE.md (lines 841-842) | JF5, JF6 |
| G9 | Jellyfin server-side transcode decision. | audit/JELLYFIN-NATIVE.md (line 855) | JF3 |
| G10 | Jellyfin backdrop on a real server. Only the stub fixture was checked. | audit/JELLYFIN-NATIVE.md (line 550) | JF7 |
| G11 | Weather has no "Not verified" line. Its only live check was Chicago, IL on 2026-11-07, so other places are untested. | audit/WEATHER.md (line 13) | WE1-WE3 |
| G12 | Long-session stability is not established. OpenGL texture and context warnings appear on exit. | audit/VERIFICATION-HARNESS.md (lines 79-80) | LS1-LS3 |
| G13 | Autostart registration and a kiosk autostart were not exercised. A watchdog exists (`packaging/watchdog.ps1`) and restarted the XMB after a kill (RC1 in RELEASE-RUN-2026-11-12.md). | packaging/README.md; audit/RELEASE-RUN-2026-11-12.md (G13) | RC1, PW3 |
| G14 | Resolved: Copy offers a destination list and completed in the 2026-11-12 run. Kept for traceability. | audit/RELEASE-RUN-2026-11-12.md (G14) | CD3 |
| G15 | The `<native-data>` folder is assumed to be `<data>`. | README.md, "Network services and media sources" (throughout) | Section 1 step 4; WE1, WOL1, SP1, DL3, HA1 |
