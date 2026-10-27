# HTPC ideas: making the native XMB a couch system without losing the XMB

Date: 2026-10-26. A brainstorm, not a plan of record, plus the dispatcher-ready task queue
in section 5. No source file was edited for it,
no build or GUI run was performed, and no capture was taken. Everything below is read
from source plus the existing audit records, which are cited by file and function rather
than by line where the working tree is dirty. Note that `shell.cpp`, `shell.h`,
**Status update 2026-10-26:** the PS-button menu and its capture doc landed in commit `6ca36df`
(`audit/PS-BUTTON.md` now exists and the tree is clean for shell/view), so the blocker below is
cleared; `src/icons.cpp` is also committed. The document was written when
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

## 5. Subagent task queue

These briefs are written to be pasted whole into a subagent dispatch. Each is
self-contained, names the files it owns, and asks for evidence the existing harness can
actually produce. This section was written from a session running at delegation depth 0,
so it could not spawn the tasks itself; the dispatch has to happen one level up.

### Shared rules (restate these in every dispatch)

- Repo root `C:\dev\xmb`. Build with
  `& .\native-xmb\build.ps1 -OutputDirectory ..\out\native-<task>` so the user's running
  `out/native-xmb/re-ps3-native.exe` is not locked; the build fails LNK1104 while that
  executable is running. Work from `native-xmb` when passing a relative output directory.
- Run with `--dev-flash <repo-parent>\rpcs3\bin\dev_flash --data-dir <scratch> --no-intro`,
  with `XMB_LOG=<file>` set. A scripted run starts 2.5 s after launch and consumes one
  token every 300 ms.
- Verify with `--script "...grab=<file.png>..."` (writes the PNG and its JSON sidecar) and,
  where a number is claimed, `bun work/ab-compare.ts` or `bun work/ab-scenarios.ts`. No new
  test framework, no new test suite, no committed fixture media.
- Never edit `src/icons.cpp` (an uncommitted change owned by another agent). Never edit a
  file outside the task's own list. Captures, fixtures, logs and reports stay under `out/`;
  firmware assets, credentials and accounts never enter Git.
- No fabricated data, no new online service, no privileged or silent system change: a power
  action is confirmed first. Commit only the task's own files, as
  `Al <ajgreenboy@gmail.com>`, and only when asked.

### G0 - Shared groundwork: panel, HUD, sidecar fields

Depends on: nothing, but it refactors the in-flight PS-menu code, so the owner of the
uncommitted `shell.cpp`/`shell.h`/`view.cpp`/`view.h` changes must land or park them first.
Blocks: T2, T3, T4, T5 (T1 only for its sidecar fields).

Read first: `src/shell.h` (the `psMenu*` block and `Page`), `src/view.cpp`
`drawPsMenu`/`drawControllerHud`/`pollController`, `src/media_controls.*` (the 3 s / 250 ms
model), `src/main.cpp` script runner (the `grab=` sidecar).

Slice:

- Generalise the PS menu into one `Panel` state (title, rows, selection) that the view draws
  over the scene and the pad drives; make the PS menu its first caller. Suppress idle, media
  keys and the OSK while it is open, as the PS menu already suppresses idle.
- A timed HUD: text plus a value, shown on change and faded with the `MediaControls` timing,
  drawn in the info-bar area. This is the volume bar and the controller indicator.
- Extend the `grab=` sidecar with `pageKind`, `panelOpen`, `panelSelection`,
  `displayState`, `awakeHeld`, `masterVolume`, `subtitleCue`, `audioTrack`, with stable
  defaults when a feature is absent, and document the fields in a comment there.

Evidence: the three existing named states still capture pixel-identically (the harness
already produces PSNR Infinity / SSIM 1 for self-repeatability, so this is a real
regression gate, not a claim); one scripted capture with a temporary panel row; the sidecar
JSON parsed and every new key present; a HUD capture at 0 s and 4 s showing the fade.

Never: change menu motion, sizes or colours; change what the PS menu does.

### T1 - Native Jellyfin sign-in with a masked OSK

Rows 1 and 2 of the table. Read first: `src/jellyfin_signin.cpp`, `src/jellyfin_shell.cpp`
(`openJellyfin`), `Shell::openChoices` (the way an XMB page is pushed), `src/view.cpp`
`openOsk`/`oskClose`/`oskInsert`/`drawOsk`, the `keys` and `type=` handling in
`src/main.cpp`.

Slice:

- On opening a Jellyfin app while signed out, push a page with rows `Server` (read-only,
  from the registration), `Username`, `Password`, `Sign In`, built with the existing `Page`
  model so Back, the pad and the column fade work unchanged.
- Reuse `View::openOsk` for both fields and add a masked mode for the password; the OSK's
  cached UI signature must not carry the plaintext.
- Keep values in memory only. Retire `JellyfinSignIn` and `requestJellyfinSignIn` in the
  same change so there is exactly one sign-in surface.

Evidence: captures of the page, of the username OSK, and of the masked password field with
no plaintext in the PNG or the sidecar (`pageKind` from G0); a `XMB_LOG` line
`jellyfin: sign-in request for <user>` plus the HTTP outcome; a deliberate wrong password
producing 401 and the inline error, which proves the typed values reached
`/Users/AuthenticateByName` without needing an account. State in the result that a live
session is unproven without the operator's server.

Never: persist the password (remembered sign-in is a separate, opt-in slice), relax the
client's redirect/nonce rules, or log the password.

### T2 - Session: stay awake while playing, go dark when idle

Rows 5 and 4. Read first: the `Shell` idle lambda (`power/idleMinutes`), the media
status/`gameRunning()` transitions, `src/host.cpp`, `src/view.cpp` composite dim path,
`src/preferences.cpp` key validation.

Slice:

- `host::holdDisplayAwake(bool)` wrapping `SetThreadExecutionState(ES_CONTINUOUS |
  ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)`; held while `mediaOpen_` or `gameRunning()`,
  released on stop, return, error and shutdown. One log line per transition.
- System Auto-Off becomes display-off: a black frame, audio stopped, idle suppressed, any
  input restoring the same page and row. Quitting stays an explicit confirmed choice.

Evidence: `idle=301000` gives a near-black frame (the existing dim measures a 0.450688
mean-RGB ratio; this must be far below it) with `displayState` in the sidecar; the wake
frame is pixel-identical to the pre-idle frame, reusing the screensaver replay's gate; the
log shows the hold and the release and only in the right states; one manual 20-minute audio
run with the Windows power plan set to sleep in 5 minutes, reported by the operator. Say
plainly that `powercfg /requests` needs an elevated shell and that the log proves only that
the app asked.

Never: call any shutdown, restart or suspend API (that is a separate, confirmed task);
change host brightness or power settings; quietly rename the existing Auto-Off behaviour.

**Landed 2026-10-26: the first half only.** `host::holdDisplayAwake` plus one call site in
the idle timer; System Auto-Off still quits. The hold is keyed on
`QMediaPlayer::playbackState()` rather than `mediaOpen_`, because the overlay stays open and
paused at 0:00 after a title reaches its end. Built and run from an isolated copy of HEAD
plus the four changed files, because `view.cpp` was mid-edit in another agent's working tree
at the time. Evidence in `out/native-reference/htpc-session`:

- Run A (play, then Back): `session: display-awake held (previous 0x80000000)` and
  `released (previous 0x80000003)`, the release falling between the two grabs, with the
  first sidecar showing `mediaOpen: true, mediaPaused: false, mediaPosition: 650` and the
  second `mediaOpen: false`.
- Run B (play to the natural end): held, then released before the grab at the end, while the
  sidecar still reported `mediaOpen: true, mediaPaused: true, mediaPosition: 0`. This run is
  what caught the bug in the first cut: keyed on `mediaOpen_`, the hold outlived the film
  and only dropped at process exit.
- Run C (no media): no session line at all, so the request is not unconditional.

The release line is the useful part: `previous 0x80000003` is the OS reporting
`ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED` was in effect, which is stronger
than a log of the call having been made. Not proven: that a host power plan honours the
request across a 90-minute film (`powercfg /requests` needs an elevated shell the harness
does not have, so that claim still needs one manual run), and the `gameRunning()` half of
the predicate, which shares the path but was not exercised here.

### T3 - Subtitles and audio tracks

Row 7. Read first: the `QMediaPlayer` wiring in `src/shell.cpp`, the media overlay in
`src/view.cpp`, `src/text_renderer.*`, `src/media_controls.*`, the player bindings in
`pollController`, `CMakeLists.txt`.

Slice:

- A player panel (G0) on Triangle with `Subtitles: Off/<name>` and `Audio: <track>`, and move
  Repeat/Shuffle into it so the face buttons are free.
- New `src/subtitles.{h,cpp}`: parse a sidecar `.srt`/`.vtt` next to the media, index cues
  against `player_->position()`, draw the active cue through the existing GL text renderer.
- Populate `Audio` from `QMediaPlayer::audioTracks()` and apply `setActiveAudioTrack` when
  more than one is reported.

Evidence: a hand-written two-cue `.srt` beside the existing generated clip (both under
`out/`); captures at cued and non-cued timestamps where a subtitle-band crop differs
(SSIM below 1) and matches the Off run when subtitles are off; `subtitleCue` and
`audioTrack` in the sidecar; the log printing cue index and start time; a panel capture
showing both rows and the current choice.

Never: add a test suite or fixture media to Git; claim embedded MKV tracks or Jellyfin
subtitles, which are follow-ons; write anything next to the user's media.

### T4 - Volume HUD on the pad

Row 6. Read first: `src/pad.cpp` (`State::l2`/`r2` are parsed and unused), `pollController`
in `src/view.cpp`, `src/host.cpp` endpoint volume, the `keys` table in `src/main.cpp`,
`src/media_controls.*`.

Slice:

- Route L2/R2 and new `VOLUP`/`VOLDOWN`/`MUTE` script keys to `host::setMasterVolume` in 5 %
  steps, ignored while the OSK, a panel or a dialog is open.
- Show the G0 HUD bar in the info-bar area with the existing 3 s / 250 ms fade.

Evidence: captures at 30/60/90 where the sidecar's `masterVolume`, read back through
`host::masterVolume()`, matches the bar; a capture after 4 s with the bar gone at the same
3000/3125/3250 ms boundary the video controls were tested at; a log line per change; the
script restoring the original level, and the result saying so, because an isolated data
directory does not isolate the Windows audio endpoint.

Never: touch mute state, the app's own `audio/volume`, or per-app mixer entries; leave the
host endpoint at a different level than it started.

### T5 - Continue Watching

Row 10. Read first: `Shell::resumePositionFor`/`storeResumePosition`, the `media/resume`
format, the Video category build, `src/view.cpp` row rendering.

Slice:

- A `Continue` folder at the top of the Video column listing in-progress titles with
  `Resuming at m:ss`, capped at ten, most recent first, dropping entries whose file has
  vanished.
- Move the map to `{id: {path, ms, title}}` with a migration that keeps old hash-only
  entries working (or clears them with one log line), because the current key is a
  truncated SHA-1 of the path and stores no path.

Evidence: play, Back at about 8.8 s, and a capture of the folder with the row and its
remaining time; Accept then a capture 1 s later with the overlay position at or beyond
8.8 s and the log printing `media: resumed at <ms>`; playing to the end removing the row
and emptying the map.

Never: change the 64-entry cap silently, change resume thresholds, or store anything about
a file outside the data directory.

### Dispatch order and conflicts

| Task | Depends on | Files owned | Conflicts with |
|---|---|---|---|
| G0 | PS-menu work landed or parked | `shell.*`, `view.*`, `main.cpp` | every task below |
| T1 | G0 (sidecar only) | `jellyfin_signin.*` (delete), `shell.*`, `view.cpp`, `main.cpp` | T2, T3, T4 |
| T2 | G0 | `host.*`, `shell.cpp`, `view.cpp`, `preferences.*` | T1, T3, T4 |
| T3 | G0 | new `subtitles.*`, `CMakeLists.txt`, `shell.cpp`, `view.cpp`, `main.cpp` | T1, T2, T4 |
| T4 | G0 | `pad.*`, `view.cpp`, `host.cpp`, `shell.cpp`, `main.cpp` | T1, T2, T3 |
| T5 | G0 | `shell.cpp`, `preferences.*`, `view.cpp` | T1-T4 |

Every task touches `shell.cpp` and `view.cpp`, and `src/icons.cpp` is already dirty, so these
are **not** parallel-safe in one working tree: run them one at a time (G0, T1, T2, T4, T5,
T3), each in its own output directory. If parallel dispatch is required anyway, give each
agent its own worktree or branch, forbid it from touching another task's files, and expect
to resolve `shell.cpp`/`view.cpp` conflicts by hand. T2 and T4 are the cheapest; if only two
slots are available, take those plus T1.

Not queued: the remaining table rows (3, 8, 9, 11-18) stay as candidates until these land.
Row 3 in particular should follow T2, since it is the same evening-ending problem with a
privileged call added.

## 6. T4 landed: the couch volume path, 2026-10-26

Written after the last source edit. Every number below came from a run of the built binary
(`out/native-volume/re-ps3-native.exe`, 22:43:49, later than every edited source file), not
from reading source. `shell.cpp`/`shell.h` were being edited by another agent and were not
touched: the whole path lives in `view.cpp`/`view.h`, and `main.cpp` only gained script
tokens, as the task's fence required.

### 6.1 Input mapping

| input | where | what happens |
|---|---|---|
| L2 held (`pad::State::l2` or XInput `bLeftTrigger`) | `View::pollVolumeRamp`, called from `pollController` | one 5% step down on the press, then a step while held |
| R2 held (`pad::State::r2` or XInput `bRightTrigger`) | same | the same upwards |
| VOL UP / VOL DOWN / MUTE | `VolumeKeyFilter` (a `QAbstractNativeEventFilter` on the application) | `WM_APPCOMMAND` `APPCOMMAND_VOLUME_*` and `WM_KEYDOWN`/`WM_SYSKEYDOWN` `VK_VOLUME_*` are applied and swallowed |
| the same keys, if Qt delivers them as key events | `View::keyPressEvent` | `Qt::Key_VolumeUp/Down/Mute` reach the same handler |
| `VOLUP`,`VOLDOWN`,`MUTE` | `--script` | `replayKeyTap(Qt::Key_Volume*)`, i.e. the keyboard path |
| `VOLSET=<0..100>` | `--script` | an exact level through the same apply function |
| `VOLKEY=up|down|mute|vkup|vkdown|vkmute` | `--script` | posts the real `WM_APPCOMMAND` / `WM_KEYDOWN` to the window, so the native filter is exercised end to end |
| `VOLRESTORE` | `--script` | puts back the level and mute state read at start-up |

The triggers are analog axes, never D-pad bits: they are not merged into pollController's
`buttons`, so a held trigger cannot move the XMB, press Cross or consume the D-pad repeat
(`repeatAt_` is untouched). A press threshold of 0.35 keeps a resting trigger quiet; a squeeze
past 0.75 shortens the repeat from 120 ms to 60 ms, which is the analog half of the feature.
The step is taken from `host::masterVolume()` (not from a cache), so a change made outside the
app cannot accumulate drift.

### 6.2 What the HUD is, and where it sits

The firmware's own volume display is one page holding a single horizontal level meter:
`videoplayer_plugin.rco` `videoplayer_volume_page` / `videoplayer_volume_level_meter`
(`out/native-reference/rco-behavior/resources.json`), with the same object pair in
`audioplayer_plugin.rco` (`audioplayer_volume_page`/`audioplayer_volume_level_meter`) and
`explore_plugin_full.rco` (`volume_page`/`volume_level_meter`). The page's projection is in the
plugin's paf code and is **not** recovered, so the rect is fitted rather than traced - the same
honesty the notification composite carries in `audit/NOTIFICATIONS.md`.

- Rect: 222x58 at (1000, 75), i.e. the band immediately left of the information panel
  (x 1329..1920, y 75..133), which no other composite uses. It is above the notification plaque
  (y 145), left of the options panel (x 1330), clear of the OSK (y 290..788) and clear of the
  media overlay (y 895+).
- Material: the information bar's own recipe - a `QColor(0,0,0,20)` rounded plate, `tex_infobar`
  at 0.12 and the 1 px white 90-alpha border - so it reads as part of the XMB.
- Contents: the firmware string `msg_volume_control` ("Volume Control") at 15 px, the value at
  19 px right-aligned ("45%", or "Mute" when the endpoint is muted), and one 196x6 level meter.
  The meter keeps showing the level while muted, greyed; nothing here writes the mute state
  except the mute key itself.
- Timing: `MediaControls` (`src/media_controls.*`) is instantiated in the view and `opened()` is
  called on every change, so the HUD lives on the exact model the video controls use - 3000 ms
  visible, then a linear 250 ms fade - instead of a second timing scheme.
- No input: nothing in the draw path reads a key or a button, and the composite is only drawn
  when it is on screen. The UI layer's signature carries its quantised opacity, so the fade
  updates without a shell change.

Suppression, reusing the rules the other corner composites already follow:

| surface | HUD | why |
|---|---|---|
| PS system menu | hidden | the menu blacks the frame and owns the screen; the HUD returns with its remaining lifetime when it closes (captured) |
| information page / message dialog | hidden | those replace the frame |
| item options panel | hidden | the panel owns the keys that could change the level |
| on-screen keyboard | hidden | the OSK owns the keyboard |
| a modal desktop dialog | behind the dialog (a separate window) | `pollController` already returns early while a modal is open, so the pad and key paths decline |
| intro | hidden, and the pad/key paths decline | the cold boot owns the screen |

Volume input is declined in exactly those states, so a media key that is not taken is left to
Windows rather than swallowed. That fallback was observed: with the PS menu open, a posted
`APPCOMMAND_VOLUME_MUTE` was declined (trace line), passed through, and Windows muted the
endpoint itself - one mute, no HUD, no double step.

### 6.3 Read-back (requested vs what Windows reports)

Every change logs `host::masterVolume()` after the write, unconditionally; `XMB_VOLUME_LOG=1`
adds the trace lines (HUD visibility/expiry, the fade curve, ramp start/release, declined
keys, duplicate presses). The driven run (pinned clocks `--render-clock 20
--background-clock 2026-10-25T10:00:00 --ui-clock 2026-10-26T21:50:00 --focus-time 2000`,
data directory a copy of `out/native-reference/store/ui3-data`):

| step | source | requested | Windows reported | HUD crop reads |
|---|---|---|---|---|
| start | constructor read | - | **0%** (muted 0) | - |
| `VOLSET=30` | script | 30% | 30% (muted 0) | `30%`, meter 30% |
| `VOLSET=60` | script | 60% | 60% (muted 0) | `60%`, meter 60% |
| `VOLSET=90` | script | 90% | 90% (muted 0) | `90%`, meter 90% |
| `MUTE` | key path | mute 1 | muted 1, level 90% | `Mute`, meter grey at 90% |
| `MUTE` | key path | mute 0 | muted 0, level 90% | - |
| `VOLSET=0` | script | 0% | 0% (muted 0) | `0%`, empty meter |
| `VOLRESTORE` | script | 0% | 0% (muted 0) | `0%` (bit-identical to the `VOLSET=0` frame) |

Second run, for the key and message paths:

| step | source | requested | Windows reported |
|---|---|---|---|
| `VOLSET=50` | script | 50% | 50% |
| `VOLUP` | keyboard event | 55% | 55% |
| `VOLUP` | keyboard event | 60% | 60% |
| `VOLDOWN` | keyboard event | 55% | 55% |
| `VOLKEY=up` | `WM_APPCOMMAND` | 60% | 60% |
| `VOLKEY=vkdown` | `WM_KEYDOWN VK_VOLUME_DOWN` | 55% | 55% |
| `VOLKEY=mute` | `WM_APPCOMMAND` | mute 1 | muted 1, level 55% |
| `VOLKEY=vkmute` | `WM_KEYDOWN VK_VOLUME_MUTE` | mute 0 | muted 0, level 55% |

Each requested value equals the reported one, so the HUD is drawing the endpoint's value and
not the request. The meter fraction and the percentage in the crops agree with the read-back at
30, 60 and 90.

### 6.4 The fade, measured

`XMB_VOLUME_LOG=1` traces the fade one line per frame. A single change at 45%, with the
animation otherwise idle:

```
volume: trace HUD visible (level 45 muted 0, 3 ms after the change)
volume: trace HUD fade opacity 0.988 at 3003 ms after the change
volume: trace HUD fade opacity 0.952 at 3012 ms after the change
volume: trace HUD fade opacity 0.504 at 3124 ms after the change
volume: trace HUD fade opacity 0.020 at 3245 ms after the change
volume: trace HUD expired (level 45 muted 0, 3254 ms after the change)
```

That is the `MediaControls` boundary exactly: full opacity through 3000 ms, a linear 250 ms
fade, reported hidden on the first frame past 3250 ms. A capture cannot be landed inside a
250 ms window with a 300 ms script step, so the curve is measured from the log rather than
guessed from pixels; the pixels still prove the two ends (`hud-show.png` differs from a frame
that never had a HUD at PSNR 15.84 dB / SSIM 0.4341; `hud-gone.png`, taken about 3.9 s after
the change, is bit-identical to it at PSNR Infinity / SSIM 1.0000).

### 6.5 Non-collision, measured

```
bun work/ab-compare.ts hud-show.png hud-none.png 1329,75,591,58    -> PSNR Infinity  SSIM 1.0000  (info bar)
bun work/ab-compare.ts hud-show.png hud-none.png 1496,145,400,90   -> PSNR Infinity  SSIM 1.0000  (notification slot)
bun work/ab-compare.ts hud-show.png hud-none.png 995,70,235,70     -> PSNR 15.84 dB     SSIM 0.4341  (the HUD's own slot)
```

A frame with the HUD up and a frame without it are bit-identical over the information panel and
over the notification plaque, and differ only in the HUD's own band. The same pair of crops
proves the options-panel rule: with the options panel open (`shell: options opened category=
game depth= 0 entry= steam-store rows= 3`) the HUD slot is bit-identical to the rest frame
(PSNR Infinity, SSIM 1.0000) while the panel's own area differs (19.98 dB / 0.9219), and the
HUD returns, mid-fade, when the panel closes (22.01 dB / 0.5747). The PS menu frame at
`psmenu-suppressed.png` is black with no HUD; `after-psmenu.png`, taken 0.6 s after Escape,
shows the HUD at 70%.

### 6.6 Gates

- Build: `& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-volume` - passed.
- `bun work\ab-layers.ts --capture --exe C:/dev/out/native-volume/re-ps3-native.exe` -
  **PASS 6/6**. Every layer except the whole frame reproduces its baseline to the last digit
  (info bar 14.78/.6117, clock 11.39/.4105, item labels 11.94/.3913, icon row 20.91/.8947, top
  background 20.24/.9344); the frame layer reads 18.29 against 18.02 (+0.27 dB, i.e. slightly
  *closer* to the stored RPCS3 frame). That frame delta cannot be the volume work: the HUD
  cannot draw without a change, and the same binary's rest capture (`rest.png`, same pinned
  clocks) has an empty HUD slot. It is the rest of the shared tree (other agents' `shell.cpp`,
  `icons.cpp`, font and audio changes were already in the working tree when this build was
  made), which is worth knowing when reading the number.
- `bun work\ab-layers.ts --states --exe ...` - **4/4 pass**; no named state changed.
- No harness, reference or threshold file was edited.

### 6.7 The restore, and what it was

The Windows endpoint is not isolated by `--data-dir`, so the run has to put it back. The start
level read by the first run - and confirmed independently through Core Audio with a throwaway
C# interop probe outside the repo - was **0% unmuted**, and that is what the runs left behind:
`volume: restored the start-up state 0% muted 0 -> Windows reports 0% muted 0 (mute call ok 1)`,
then a fresh process (and the independent probe) read **0% muted 0** again. One intermediate
run (the declined-key experiment) left the endpoint at 5% and Windows-muted; it was put back
with `MUTE` + `VOLSET=0` and re-verified.

### 6.8 Not proven, and why

- **The physical trigger.** No pad is attached to this machine and the harness has no
  controller injection, so L2/R2 are structural: they are read at the top of `pollController`
  and feed the same `volumeStep` the keys use. This is the same limitation
  `audit/OPTIONS-MENU.md` records for its controller path.
- **The duplicate guard.** One press can arrive as both `WM_APPCOMMAND` and `VK_VOLUME_*`; the
  filter applies the first and swallows the second within 100 ms. The harness posts one form at
  a time (300 ms apart), so the guard is coded and reasoned about but not exercised here.
- **The HUD's on-screen rect** is fitted to the information bar, not traced from the firmware
  page projection; closing that needs the paf page projection in `videoplayer_plugin`, which is
  not recovered anywhere in this project.
- **A capture inside the 250 ms fade.** The curve is measured from the log (6.4); a pixel
  capture of a half-faded HUD would need a sub-100 ms script step the harness does not have.

### 6.9 Files touched

- `native-xmb/src/view.h`: the volume state (`volumeHud_`, level, mute, start-up snapshot, ramp
  deadline, fade-trace fields), the native-filter handle, the public `setMasterVolumePercent` /
  `restoreMasterVolume`, and the private helpers.
- `native-xmb/src/view.cpp`: `VolumeKeyFilter` (the `QAbstractNativeEventFilter`),
  `volumeInputBlocked`, `volumeApply`, `volumeStep`, `volumeToggleMute`, `volumeKeyAction`,
  `setMasterVolumePercent`, `restoreMasterVolume`, `pollVolumeRamp`, `drawVolumeHud`; the
  start-up read and the filter install/uninstall; the trigger read in `pollController`; the key
  branch in `keyPressEvent`; the HUD draw and its suppression in `drawScene`; the signature term
  and the fade trace in `paintGL`.
- `native-xmb/src/main.cpp`: `VOLUP`, `VOLDOWN`, `MUTE`, `VOLSET=`, `VOLKEY=`, `VOLRESTORE`.
- `native-xmb/audit/HTPC-IDEAS.md`: this section.

Not touched: `shell.cpp`, `shell.h`, `xmbml_view.*`, `settings_menu.cpp`, `preferences.cpp`,
`host.cpp`, `host.h`, `CMakeLists.txt`, the verification harness, or any reference/threshold.
Captures, logs and crops are under `out/native-reference/volume/` and are not committed.
