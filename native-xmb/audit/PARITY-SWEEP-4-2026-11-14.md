# Render-parity sweep 4 (2026-11-14)

Sweeps 2 and 3 left five things unreviewed: the live Steam download page, the slow-network spinner and loading states,
the real Sleep / Restart / Turn Off notice pages, whether rows far up or down the screen fade like the console, and
columns or pages nobody had walked. Method as before: scripted runs (`--script`, scratch data dir, `XMB_POWER_DRY_RUN=1`,
`XMB_AUDIO_DRY_RUN=1`, `XMB_STEAM_ARTWORK=0`, `XMB_STEAM_ACTIVITY=0`), grabs read as contact sheets. Grabs and fixtures
are outside the repository. Window sizes: the gate's 1920x1080, plus 1920x1065 and 1500x1000 windows (a 1920x1080 client
cannot be hosted here because of the taskbar, so these are also non-16:9). Scripts that press Cross were gated by a
helper that refused them until a look-only run had shown which row was focused.

## Fixed

| Defect | Evidence | Fix |
|---|---|---|
| While a wait page is up (for example "Searching for media servers..."), the focused row's help line was drawn under the status text and the two overprinted | Music > Search for Media Servers, grab during the scan | the focused row's second line and help line are not drawn while the wait page is up (`view.cpp`). Only that state changes |
| The Download Management page was a snapshot: percentages sat still until F5, and a pad has no F5 | a fixture queue whose manifest was edited while the page was open stayed at 26 % | the page re-reads Steam's queue every 5 s while it is the top page and nothing is drawn over it (information page, options panel, wait page, media). Selection is kept by id; a queue that empties closes the page as F5 already did. Measured: 26 % became 56 % without a key press |
| A download's information page showed `App: 2001`, `Steam state: downloading` (an internal word), a second byte line repeating Progress, `Pause/resume: not supported` and a `Note` saying the same | the page for a fixture download | four rows: Status, Progress ("Steam has not reported any yet" with no counters), Steam App ID, Pause and Resume ("In Steam..."). Not in a pinned frame |
| The Weather row's description named a file ("Set a place in weather.json") | Network column | "Not set up yet.", the same wording as Home Assistant; the page keeps the steps |

## Reviewed, nothing to change

- **Live Steam download page** (fixture queues, never the real Steam queue): 14 rows (active, staging, updating, paused with
  and without counters, queued, pending, a 110-character title, 149 GB). Long titles are elided, rows scroll, the
  information page lays out. Row art is the placeholder tile (no cached header in a scratch data dir).
- **Slow-network loading**: the only reachable wait state is the DLNA scan (about 3 s). It has no spinner by design
  (`dialogs.cpp`: the measured wait page has none and the console's busy icon has no recoverable rect). The Steam Store's
  loose "Asking the Steam store..." line sits below the list and collides with nothing.
- **Real Restart notice (dry run)**: confirm page, then "Restarting the system..." with the icon and title in the header
  and Back. No change; the wording is this build's own (as in sweep 3).
- **Columns walked**: every category at depth 0 (Users, Settings, Photo, Music, Video, TV, Game, Network, PSN, Friends,
  every row), the Game column's Manage hub, all 15 Settings sub-pages one level in, at 1920x1065 and 1500x1000. No
  overlap, cut-off text or wrong icon found beyond the table above. `*User` under Trophy Collection is the traced firmware
  scope marker (`subtitleText`).
- **Rows far up or down the screen**: the build draws rows scrolled far up at normal dim all the way to the top edge
  (they only fade under the clock bar, sweep 3). The one TRACED analogue is the Settings idle dim (rows at distance 1 to 13
  ease to 0.2 text alpha, `PAGE-MOVE.md`). No console frame of a long list scrolled far up is reachable here
  (the console captures are outside this checkout), so whether the console fades or clips rows near the top edge is
  **unknown**. Nothing changed.

## Left alone, decisions for the owner

These would change a pinned reference (`steam-download-page`), so they were not touched:

1. The row detail reads `Downloading 43% - (410.1 MB of 953.7 MB)`: the " - (" joint is awkward and the percentage is
   shown twice in the row. `Downloading 43% (410.1 MB of 953.7 MB)` would read better. The `.dl.json` assertions in
   `ab-layers.thresholds.json` pin the text too.
2. A queued or updating row reads `Queued - no progress published` (developer wording, also pinned in the frame). "Waiting
   to start" and "No progress yet" are candidates.
3. The row icon for a download without a cached header is `game_tex_default_ps3`, which draws as a small dark tile with a
   white corner, unlike the other placeholder tiles. It is in the pinned frame.

## Not reviewed

Real sleep and shutdown (dry run only, by rule); a real slow network (no way to slow one safely; only the DLNA scan wait
was seen); Tailscale, Wake on LAN and Phone Remote pages (they act on the system or network); Browse under Download Games
(it fetches a remote game list); a true 4K window.

## Near miss

An early look-only script pressed Cross on a row it had not verified: the Users column opens on the User row, so DOWN then
Cross opened Steam Account, and a later Cross in the same script reached a Steam account row. The "Switch Steam Account"
prompt came up with No focused, "nothing started yet" in the log, and the script ended there; Steam stayed running and
nothing was confirmed. After that the helper refused any script containing Cross until a look-only run had printed the
focused row id.

## Gate

Full gate on the final build: 26 of 27 states and all layers pass. `xmb-volume-hud` fails only its log assertion: the default playback endpoint was already muted on this PC ("start 87% muted 1"), so the expected "(muted 0)" line never appears; the frame is not at issue and nothing here touches audio. It needs the endpoint un-muted by its owner, which this sweep did not do. No reference was saved and none is stale: `steam-download-page` was
re-run alone after the download-page change and passed (rows inf, frame 98 dB, no pixel differs by more than 3).
