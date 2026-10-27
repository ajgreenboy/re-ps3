# HTPC ideas: making the native XMB a couch system without losing the XMB

Date: 2026-10-26. A brainstorm, not a plan of record. No source file was edited for it,
no build or GUI run was performed, and no capture was taken. Everything below is read
from source plus the existing audit records, which are cited by file and function rather
than by line where the working tree is dirty. Note that `shell.cpp`, `shell.h`,
`view.cpp` and `view.h` currently carry uncommitted changes (a PS-button menu, referenced
in a comment as `audit/PS-BUTTON.md`, which does not exist yet), so this document assumes
that direction is already in flight and does not recommend it again.

## 1. What this is, HTPC-wise, and the one gap

Read as an HTPC front end, this is already the rare kind: an XMB that is not a skin. The
motion, icon material, ribbon and cold boot come from the firmware's own data; the rows
behind it are real sources (Steam manifests and the owned library, a signed-in Jellyfin
session, RPCS3 titles, imported executables, local folders); launch and return have both
been made observable; playback reports its own position honestly; and a feature that is
not implemented says so instead of pretending. What it is not yet is a couch system: it
is a desktop application that happens to be navigable with a pad. The single biggest gap
is **controller-complete operation**. The pad drives columns, pages and the player
overlay flawlessly and then stops wherever the app needs a decision or a secret:
Jellyfin sign-in and the app editor are desktop dialogs of text fields, the entire pad
poll is short-circuited while any modal is open, there is no on-screen surface at all for
volume, subtitle or audio-track choice, and the only keyboard-free text entry that exists
(the native OSK) is wired to two places, the system name and a new user's name.
Everything else that is missing - real
power actions, keep-awake, metadata, subtitles, continue-watching - sits downstream of
that: the app is pleasant to browse and awkward to live with.

Evidence for the gap, in code:

- `src/view.cpp` `pollController` returns early with every button zeroed when
  `QApplication::activeModalWidget()` is non-null. Any modal dialog turns the controller
  off, not just the keyboard.
- `src/jellyfin_signin.cpp` is a `QDialog` with three `QLineEdit`s; `src/app_editor.cpp`
  is a `QDialog` with `QLineEdit`, `QComboBox` and `QFileDialog`. Neither has a pad path.
  `audit/JELLYFIN-NATIVE.md` records that remembered sign-in is not implemented, so this
  is not a one-time setup cost: it is every time the app is opened.
- `src/view.cpp` `drawOsk` draws `osk_.text` verbatim. The keyboard exists, is driven by
  the pad (`oskMove`/`oskPress`), and has no masked mode - so it cannot be reused as-is
  for a password in a room where the screen is 2 m away.
- The player band already has the right binding vocabulary (A pause, B back, LB/RB skip,
  X shuffle, Y repeat) but nothing for tracks or volume, and the overlay draws only the
  title, progress bar and clock (`src/view.cpp` media drawing, `src/media_controls.cpp`).

## 2. Candidate features

Effort: **S** is roughly half a day for someone who knows this code; **M** is one to three
days; **L** is a week or a new subsystem. "Local" means no new infrastructure beyond the
process it already is. Most rows are independent; rows 6, 7, 8, 3 and 16 all want the
same missing primitive, which is described after the table.

| # | Feature | What the user sees | Why it matters on a couch | Effort | Touches | Infrastructure |
|---|---|---|---|---|---|---|
| 1 | Sign-in inside the XMB, masked OSK | Jellyfin asks for user and password as an XMB page, typed on the console keyboard, password as dots | The first thing a new user does is type a secret; a pad-only setup is the difference between "an HTPC" and "a PC with a nice menu" | M | `src/jellyfin_signin.cpp` (retired), `Shell::openJellyfin`/`authenticateJellyfin`, `View::openOsk`/`drawOsk` | local |
| 2 | Remembered sign-in | Opening Jellyfin goes straight to the libraries; Sign Out is a row | Otherwise every launch is a password, which is exactly the thing a couch cannot do comfortably | M | new `src/credential_store.*` (DPAPI), `src/jellyfin_client.h`, `src/preferences.cpp` (server + user name only, never the secret) | local (Windows DPAPI) |
| 3 | Real power actions behind a confirm | The PS menu offers Suspend, Restart, Turn Off Display, Turn Off the System, each asking once | The one thing a front end must own is the end of the evening; today the machine stays on a desktop | M | in-flight `Shell::psMenu*`, `src/host.cpp` (new power calls), `src/settings_menu.cpp` power page | OS API, privileged for shutdown, confirmed |
| 4 | Idle goes dark, not to the desktop | After System Auto-Off the screen goes black and any input returns to the same place | Today the only power behaviour closes the app and exposes the Windows desktop on the TV | S | `Shell` idle lambda (`power/idleMinutes`), `src/view.cpp` composite/dim path, `src/preferences.cpp` | local |
| 5 | Hold the machine awake while playing | Nothing visible; the film is still on after 90 minutes | Neither the app nor its media backend asserts anything, so the host power plan decides whether the evening ends early | S | `Shell` media/game transitions, new `host::holdDisplayAwake`, `src/host.cpp` | local OS API (no admin) |
| 6 | Volume HUD on L2/R2 and remote keys | A small bar appears in the info-bar area and fades; the pad's triggers change the level | Volume is the most-touched control in a living room and is currently four levels deep in a settings page with no feedback | S | `src/pad.cpp` (L2/R2 already parsed, unused), `view.cpp` `pollController` + overlay, `host::setMasterVolume`, `src/main.cpp` script keys | local |
| 7 | Subtitle and audio-track picker | Triangle opens a panel: Subtitles Off/name, Audio track, and the repeat and shuffle that live there on a console | A foreign film or a quiet-house film is unwatchable without it; both audits record subtitles as absent | M | `src/shell.cpp` player, `src/view.cpp` overlay, new `src/subtitles.*`, `src/text_renderer.cpp` | local (own SRT/VTT renderer) |
| 8 | Jellyfin track choice and resume | The same panel inside Jellyfin; a film resumes where it stopped | Server-side resume is the difference between "my library" and "a file browser" | M | `src/jellyfin_client.cpp` (PlaybackInfo MediaStreams, UserData), `src/jellyfin_stream.cpp`, `src/jellyfin_shell.cpp` | network (your server) |
| 9 | Metadata and artwork in rows | Music rows show artist - album; video rows show title, year, duration; Jellyfin rows show posters | A 500-title library of filenames is unusable at 3 m; this is the single largest visible quality jump | L | `src/media_library.cpp`, `src/shell.cpp` row build, `src/jellyfin_client.cpp` ImageTags, `src/steam_artwork.cpp` cache pattern | network (Jellyfin images) + local tags |
| 10 | Continue Watching | One row or folder listing what is in progress with time remaining | The first screen a couch user wants; the data already exists | M | `Shell::resumePositionFor`/`storeResumePosition`, `media/resume`, `src/jellyfin_client.cpp` UserData | local, then network |
| 11 | Downloads in Download Management | Network > Download Management lists Steam downloads, percentage and state | You start an install and walk away; you need to know from the sofa whether it finished | M | `src/steam_downloads.*` (scanner exists, report-only), `Shell` Network category, `src/main.cpp` report | local |
| 12 | Play all / next episode | Accepting an episode plays the rest of the folder, with an up-next countdown | Nobody wants to stand up between episodes, and the queue model already supports it | M | `Shell::openJellyfinMedia` (queue is built from the page), `advanceMedia` | network |
| 13 | Controller status page | Accessory Settings shows real pads, which one drives the XMB, battery level | "Which pad am I holding" and "why did it stop responding" are the two support questions a TV user has | M | `src/pad.cpp` (expose device + battery), `src/settings_menu.cpp` device page, `src/view.cpp` | HID, no driver install |
| 14 | Library index cache | A 2,000-file NAS folder opens instantly and refreshes in the background | The current scan is a flat walk with a 5,000-entry cap and no watch, so every open is a cold start | L | `src/media_library.cpp`, new cache file in the data directory | local |
| 15 | TV safe area and UI scale | Display Settings gains Overscan and UI Size; the layout stays inside the panel | Many TVs crop a few percent at the edges, and the design canvas is a hard 1920x1080 (`view.cpp` projection) | S | `src/view.cpp` projection, `src/settings_menu.cpp` display page, `src/preferences.cpp` | local |
| 16 | PIN gate for Store, browsers, Jellyfin | Security Settings holds a four-digit PIN, asked once per protected app | A living-room PC is a shared PC; Security Settings is currently an empty page of refusals | M | `src/settings_menu.cpp` security page, `Shell` activate path, OSK numeric mode, salted hash in preferences | local |
| 17 | Ambient notifications | A small info-bar panel for "download finished", "controller disconnected", "server unreachable" | The user cannot see the desktop, so nothing can happen silently off-screen | M | `src/view.cpp` info-bar area, firmware `tex_notification_*` from dev_flash, `Shell` events | local |
| 18 | A second audio path | Music under a photo slideshow, and optionally while browsing, with a now-playing line | Photos with music is the classic couch demo; browsing in silence is the current behaviour | M | `src/shell.cpp` (one `QMediaPlayer` today), `src/view.cpp` info bar | local |

Two pieces of shared groundwork make most of the table cheap, and neither exists yet:

- **A non-modal panel primitive.** The pad can only drive `pages_`; every overlay today is
  either a one-button text dialog (`message_`) or the OSK. The in-flight PS menu is the
  right shape (`psMenu_`, `psSelection_`, `psMenuMove`, `psMenuActivate`, drawn by the
  view) but it is a one-off. Generalise it once - rows, selection, accept, back, and a
  suppression rule for idle and media keys - and rows 3, 6, 7, 8, 12 and 16 become small.
- **A state-sidecar budget.** The `grab=` sidecar in `src/main.cpp` is the harness's only
  structured oracle. Adding a documented handful of fields (`pageKind`, `panelSelection`,
  `displayState`, `awakeHeld`, `masterVolume`, `subtitleCue`, `audioTrack`) is what lets
  `work/ab-scenarios.ts` expectations gate these features, instead of a human comparing
  PNGs. That is one change and it serves every row.

## 3. Ranked top five

Ranked by couch value per unit of work, not by how impressive the diff looks.

### 1. Couch credential path (rows 1 then 2)

This is the gap itself, and it is the only item that makes Jellyfin usable on a TV at all.
The dialog exists, the OSK exists, the client exists; what is missing is the join.

Smallest honest first slice:

- Push a real page when a Jellyfin app is opened while signed out: rows `Server`
  (read-only, taken from the registration), `Username`, `Password`, `Sign In`. Build it
  with the existing `Page` model so Back, the pad and the column fade work unchanged.
- Reuse `View::openOsk` for the two fields, with a masked mode for the password (dots, and
  no echo in the OSK's cached UI signature - `drawOsk` currently draws `osk_.text`).
- Keep both values in memory only for this slice; storing the secret is row 2's job.
- Delete the `JellyfinSignIn` dialog and its `requestJellyfinSignIn` path in the same
  change, so there is one sign-in surface, not two.

Evidence that would prove it:

- `--script "LEFT,...,ENTER,grab=signin.png"` produces a capture with four rows and a
  sidecar carrying `pageKind: "jellyfinSignIn"`; `work/ab-scenarios.ts` gates on the
  expected page kind and selection, so navigation drift fails the run instead of the eye.
- A `type=<user>` capture shows the OSK holding the typed name; a `type=<password>` capture
  shows the same character count as dots and no plaintext anywhere in the PNG or the
  sidecar.
- `XMB_LOG` records `jellyfin: sign-in request for <user>` and the HTTP outcome. A
  deliberately wrong password returns 401 and the inline error, which proves the typed
  values reached `/Users/AuthenticateByName` without needing a real account.
- Pad reachability is then structural, not asserted: no modal is open, so
  `pollController` no longer zeroes the pad, and the rows go through the same
  `horizontal`/`vertical`/`activate`/`back` path as every other page.

Not proven by this: a successful live session, which needs the operator's own server.

### 2. Session ownership: stay awake, then go dark (rows 5 and 4, then 3)

Two small, local changes that fix the two ways the host currently overrules the front end.

Smallest honest first slice:

- `host::holdDisplayAwake(bool)`: `SetThreadExecutionState(ES_CONTINUOUS |
  ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)` held while `mediaOpen_` or `gameRunning()`,
  released on stop, return or error. One log line per transition.
- System Auto-Off stops meaning "quit the app": it enters a display-off state (black
  frame, audio stopped, idle suppressed) and any input returns to the same page and row.
  Keep quitting as an explicit, confirmed choice - it already is in the in-flight PS menu.
- While in there, make the PS menu's `Turn Off the System` honest: it currently emits
  `requestQuit`, which closes the window and leaves a Windows desktop on the TV. Either
  label it "Quit XMB" or wire it to a confirmed Suspend/Shutdown. Turning the display off
  needs no privilege (`WM_SYSCOMMAND`/`SC_MONITORPOWER`); suspend and shutdown do, and both
  must sit behind the confirmation the menu already shows.

Evidence that would prove it:

- `idle=301000` with auto-off set and nothing playing: the capture is black (mean RGB near
  0, against the 0.450688 mean-RGB ratio already measured for today's 55 % dim), and the
  sidecar carries `displayState: "off"`.
- Any key wakes without moving the cursor or the row, and the wake frame is pixel-identical
  to the pre-idle frame - the screensaver replay already established that gate (PSNR
  Infinity, SSIM 1), so this reuses it rather than inventing a threshold.
- `XMB_LOG` shows `session: display-awake held` on play and `released` on Back, and only in
  those states; a second run with no media shows no hold at all.
- One manual run of a 20-minute track with the Windows power plan set to sleep in 5
  minutes, observed by the operator: no blank, no sleep. This is the only direct proof
  available; `powercfg /requests` needs an elevated shell the harness does not have, and
  the log only proves that the app asked.

### 3. Subtitles and audio tracks (row 7, then 8)

The single most common "it cannot do that" moment in a living room, and the audit already
records subtitles as absent for local video and Jellyfin.

Smallest honest first slice:

- A player panel on Triangle (which also gives Repeat and Shuffle the home a console gives
  them) with rows `Subtitles: Off/<name>` and `Audio: <track>`.
- Do subtitles ourselves first: parse a sidecar `.srt`/`.vtt` next to the file, keep a cue
  index against `player_->position()`, and draw the active cue with the existing GL text
  renderer over the video frame. No new codec, no backend dependency, and it is the same
  path Jellyfin's external VTT would take later.
- List `QMediaPlayer::audioTracks()` in the same panel and apply
  `setActiveAudioTrack` when the backend reports more than one.

Evidence that would prove it:

- Fixture: the existing generated 20-30 s clip plus a hand-written two-cue `.srt`, both
  under `out/`, never committed.
- Captures at a cued and a non-cued timestamp: a crop over the subtitle band differs
  (SSIM below 1) between them, and with the row set to Off that crop matches the
  no-subtitle run.
- Sidecar carries `subtitleFile`, `subtitleCue` and `audioTrack`; the log prints the cue
  index and its start time, so the timing is checkable without the image.
- The panel capture shows both rows and the current choice, and Triangle closes it.

Not proven: embedded subtitle streams in MKV (a follow-on through
`QMediaPlayer::subtitleTracks`), and Jellyfin's server-side subtitle delivery.

### 4. Volume HUD (row 6)

Cheapest visible win in the document, and it uses hardware input the app already parses.

Smallest honest first slice:

- Bind L2/R2 (parsed per pad in `src/pad.cpp`, never read in `src/view.cpp`) and
  `VOLUP`/`VOLDOWN`/`MUTE` script keys to `host::setMasterVolume` in 5 % steps, muted while
  the OSK or a dialog is up.
- Draw one bar in the info-bar area, shown on change and faded with the timing model
  `src/media_controls.cpp` already owns (3 s visible, 250 ms fade) rather than a new one.
- Add the three tokens to the script table in `src/main.cpp` so the harness can drive it.

Evidence that would prove it:

- Captures at 30/60/90 with the sidecar's `masterVolume` read back through
  `host::masterVolume()`, so the number on screen and the endpoint must agree.
- A capture 4 s later shows the bar gone, at the same 3000/3125/3250 ms boundary the video
  controls were tested at.
- `XMB_LOG` line per change; the script restores the original level at the end, because an
  isolated data directory does not isolate the Windows audio endpoint - and the run must
  say so in its log.

Not proven: the physical trigger, since the harness has no pad; the claim there is
structural (pad L2/R2 feeds the same shell action as the key).

### 5. Continue Watching (row 10)

The one screen a couch user opens first, and the state already exists.

Smallest honest first slice:

- A `Continue` folder at the top of the Video column (and later in Jellyfin) listing what is
  in progress: title, time remaining, `Resuming at m:ss`.
- Accept opens the file through the existing `resumePending_` path, so the seek behaviour
  is the one already verified rather than a second implementation.
- Two data notes that shape the slice: `media/resume` is keyed by a truncated SHA-1 of the
  path and stores no path (`Shell::resumePositionFor`), and it is capped at 64 entries. So
  either resolve the ids against the last library scan, or move to a small
  `{id: {path, ms, title}}` record with a migration - the latter is better and is still
  one session.

Evidence that would prove it:

- Play, Back at about 8.8 s: `preferences.json` `media/resume` holds the entry (this exact
  check has been recorded once already), and a capture of the Continue folder shows the row
  with the remaining time; sidecar `entryId` and `mediaOpen: false`.
- Accept, then a capture 1 s later: the overlay position is at or beyond 8.8 s and the log
  prints `media: resumed at <ms>`.
- Playing to the end removes the row, verified by a second capture and an empty map.

Not proven: Jellyfin resume, which needs `UserData/PlaybackPositionTicks`, and any
cross-device history.

Just outside the five: row 9 (metadata and artwork) is the largest visible jump after these,
but it is an L and it is the one item that is safe to do second. If only one thing ships
this month, ship 1.

## 4. Non-recommendations

Things that look appealing on a feature list and should stay out.

- **Becoming the Windows shell** - autostart as `shell`, hiding the taskbar, kiosk mode,
  disabling Alt+Tab. The app is one Qt window by design; replacing the shell adds a
  privileged, hard-to-recover surface, breaks the user's escape route, and is exactly the
  kind of silent system change the project's ground rules forbid. Fullscreen plus a
  single-instance lock gets 95 % of the feel with none of the risk.
- **HDMI-CEC and TV power.** Turning the TV on or off, or switching inputs, needs a CEC
  adapter and a driver or service. It cannot be done from a window, cannot be verified by
  the harness, and would be the app claiming control of hardware it does not have.
- **Refresh-rate and 24p switching, exclusive fullscreen for video.** The window is
  composited by DWM; changing the display mode can black-screen a TV on a bad EDID and
  fights the desktop the app lives on. The project's own measurements already distinguish
  swap-callback cadence from physical presentation, so the benefit cannot even be proven
  here. A 10-foot front end is better served by not touching the mode.
- **Audio bitstream passthrough (Dolby/DTS to an AVR) and WASAPI exclusive.** Qt's media
  backend does not expose either; forcing them would break Jellyfin's transcode
  negotiation and the app's single volume model. The honest version of this feature is a
  Sound Settings page that reports the real endpoint format and channel count (it already
  reads the name and volume via Core Audio) and says where to change it in Windows.
- **Steam purchase, checkout, cart, refunds; PSN sign-in, trophies, store, messaging.**
  Every one of those is either a fabricated online service or a payments surface. Steam
  owns purchase, DRM and install, and the existing split - browse in XMB, hand off to
  Steam - is the correct one. The Friends column handing chat to Steam is the same call and
  should stay that way.
- **A real chat or invite client inside the XMB.** It duplicates Steam, needs a protocol
  and presence model the project does not have, and any partial version becomes a fake
  social feature. "Open Steam Friends" is honest; a half chat window is not.
- **Theme gallery, skin downloads, community plugin store.** New online service, rights
  and moderation problems, and it would pull firmware-derived art into a distribution
  path. Themes stay a file dialog on a `.p3t` the user already owns.
- **ROM, ISO and disc-image importing for the Game column.** Installed-title discovery via
  `games.yml`/`EBOOT.BIN` is the legal line; scanning for disc images is piracy-adjacent
  and would put the project's name on it. Firmware assets staying out of Git is the same
  rule seen from the other side.
- **A resident tray agent, global hotkeys, a mini-player window, or a background service.**
  That turns a single-window recreation into a desktop program with a second audio session
  and a second place for preferences to disagree. If two instances are the worry, a
  `QLockFile` in the data directory is the whole fix.
- **A phone remote or an HTTP control API.** A listening socket needs pairing, auth and a
  threat model, and the pad already is the remote. It is a large security surface bought
  for a small convenience.
- **Per-game process priority, affinity, exclusive display handoff, GPU driver tweaks.**
  Privileged, game-specific and unverifiable; games take fullscreen themselves and the
  current tracked launch plus return is the right amount of involvement.
- **Filling the empty Settings pages with PS3-shaped rows that have no backend** (TV type,
  RGB range, deep colour, audio multi-output, controller auto-off, Bluetooth pairing).
  The app's credibility comes from refusing to fake these; a row that stores a value
  nothing reads is worse than a row that says "not available". Report the host, do not
  simulate the console.
- **Default-on background music across the whole XMB.** The original stops playback on
  Back. Keep it opt-in, off by default, so the recreation still behaves like the console
  when someone compares it with the reference.
- **Controller rumble, LED control and console-style controller auto-off.** Rumble on menu
  navigation is not the console's behaviour, and the pad's power and light are owned by
  Windows and the hardware. Say that in Accessory Settings; do not emulate it.
- **Always-listening voice control.** Microphone permission, privacy and a driver surface,
  for a feature the console never had.
