# Render-parity sweep 3 (2026-11-12)

Sweeps 1 and 2 covered the root screen, window sizes, media players, dialogs, Settings sub-pages, Users, Network,
Friends and PSN columns, first-run pages, the on-screen keyboard and toasts. This one covered what they did not:
Steam Store and All Games, the Photo Gallery Playlist area, Jellyfin and Spotify (signed-out and error states only; no
sign-in, no credentials), System Check, the power rows (`XMB_POWER_DRY_RUN=1`), the in-game overlay over the stub game
(`work/v2-stubgame.cpp`), loading and busy states, and missing-asset fallbacks. It also closed the "seen, not changed"
items of sweep 2 that had a clear answer.

Method as in sweep 2: scripted runs (`--script`, `XMB_SINGLE_INSTANCE=0`, a scratch data dir per run,
`XMB_STEAM_ARTWORK=0 XMB_STEAM_ACTIVITY=0`, and once with artwork on), grabs read as contact sheets and zoom crops.
Grabs and fixtures are outside the repository (`out/par3`). Missing assets were tried on a scratch copy of `dev_flash`
(a renamed `explore_plugin_full.rco`, a renamed `system_plugin.rco`, a renamed `data/font`).

## Fixed

| Defect | Evidence | Fix |
|---|---|---|
| A settings choice list (Theme, Notification Messages) is translucent and the focused row's long help line ran on under it, colliding with the labels ("Classic") | Theme page with the list open | with a list open the help line wraps at the plane's left edge (1260 px, the band constant in `view.cpp`) instead of running 1182 px from the label. Wider text takes a second row. The console's own frame for this is not known (INFERRED) |
| A row scrolled up beside the info bar had its title, or its right-hand value, printed over the clock text | 14 long music titles, row 7 of 14; Settings > System Settings, About row | a title that reaches the bar, and any row with a value, fades to 15 % while level with the bar and returns as it moves off (INFERRED ramp: no console frame shows a list that far up). Shorter titles and rows away from the bar are untouched |
| Settings > System Language printed the raw Qt patterns "Date pattern: M/d/yy" and "Time pattern: h:mm AP" | System Settings > System Language | the page shows today's date and the time as that locale writes them; the narrow no-break space Qt puts before AM/PM is a plain space |
| Information pages cut a long sentence off with an ellipsis ("...syncs trophy information with the...") | Settings > System Settings > Automatic Update | a line wider than its box wraps onto further rows (a value continues under itself). An unbroken word such as a path is still elided |
| Eleven rows answered with the one line "This feature is not available in the native recreation yet." | PSN Sign In / Trophy Collection / PlayStation Store; Network Online Instruction Manuals / Remote Play / Internet Browser / Internet Search; TV My Channels; Friends Add a Friend / Chat Room / Start New Chat | each says what is true: no PlayStation Network connection (sign-in, trophies, chat, add a friend, channels), the Store's games are in the Steam folder, manuals were Sony-hosted pages, Remote Play needs a PS3, no built-in browser (a website can be added as a web app), Internet Search ran inside the PS3 browser. Other unavailable rows keep the generic line |
| The Search for Media Servers and Video Editor & Uploader descriptions named `dlna_plugin`, `scenefolder_plugin` and "this build" and listed protocol names | Music/Photo/Video columns, focused row | plain sentences |
| The Home Assistant setup row's description was the config error with a JSON snippet; its page printed the same snippet and split it at the colon as if it were a heading | Network > Home Assistant (unconfigured) | the row says "Not set up yet". The page is one paragraph naming the keys without braces. The client's own complaint only leads it when it says something the steps do not |
| The Spotify setup row printed a long developer instruction as its description, and Cross on it answered with the generic "not available" line | Music > Spotify (no client id) | the row says "Needs a Spotify client id"; Cross opens the instructions as a page |
| The Open Discord Friends row printed the client-id instructions as its description | Friends column (no client id) | "Not set up yet. Discord Connection says how." The Refresh row's error page and the Discord Connection page keep the full text |
| Photo Gallery Playlist area: a long playlist name ran under the next jacket's label ("Empty one" under "A Very Long Playlist Name ...") | Playlist area, three playlists | the name is elided to 92 % of the nearest jacket-to-jacket distance (INFERRED: the console's rule is not known). The Library area's labels are untouched |
| Steam Store title > Information showed "Format: Item, Size: Unknown, Created: Unknown, Details: $33.32" | Top Sellers > Bodycam > Triangle > Information | Price, Store, and one sentence on what selecting the title does (hands it to Steam) |
| "1 problem(s) found", "1 item(s)", "1 file(s)", "entr(ies)" in toasts, folder descriptions and copy/delete results | System Check toast, Home Assistant group count, file operation messages | singular and plural spelled out |

## Reviewed, nothing to change

- **Steam Store and All Games** (real library, read-only; artwork off and on): hub, sections, a store list of about 20
  rows, the options panel (Open / Information), All Titles with 282 titles and the collection folders. With artwork on the
  Game column draws the focused title's full-screen picture behind the column, as designed. Do not press Cross on a game
  tile in the Game column during a sweep: it launches the title through Steam (it did once here, `steam -applaunch`; no
  game process appeared afterwards).
- **Photo Gallery Playlist area**: entered with R1 (`NEXT`), the floor with jackets, the grid of one playlist, the menu
  strip with Sort disabled, the guide panel with L1. The camera leaves the other jackets under the header band on open
  (INFERRED in `PHOTO-GALLERY-PLAYLIST.md`). The 12-photo playlist's jacket was still a pale frame at grab time
  (thumbnails load lazily); not investigated further.
- **Jellyfin** (TV column, registered at a dead localhost port): sign-in page, Stay Signed In, and Sign In with blank
  fields answers "Enter a valid Jellyfin server and username." locally. No request carried credentials.
- **Spotify**: the unconfigured state only (see Fixed).
- **System Check**: reached from Settings > System Settings (the row after System Information). Rows and fix-it
  descriptions are fine. The Steam row prints the registry's lower-cased install path (`c:\program files (x86)\steam`).
- **Power rows (dry run)**: Turn Off System opens the black PS-menu confirm; Restart and Sleep ask once with No focused.
  After a confirmed Restart or Sleep the XMB returns to the list with no feedback. On a real restart that is a few idle
  seconds. Not changed: the console's behaviour is not traced and a "Restarting..." page would be invented.
- **In-game overlay** (stub game): the dimmed captured frame, Quit Game and Enter Text rows, the "Back to Game Screen" key
  plaque, a notification toast over the game, the options panel. No defect found. The faint "H5 Stub Game - mode wait"
  text under the focused row is the game's own picture.
- **Busy states**: Steam Store shows the loose status line "Asking the Steam store..." while loading (no spinner); the
  Restore Default Settings write shows the measured busy page and "Restore completed."; the spinner page for network
  loads could not be reached without a slow network.
- **Missing assets**: with `explore_plugin_full.rco` renamed the log reports "Cannot open local RCO" for a few textures
  and the screens still draw (most row art comes from the `explore_category_*` RCOs); with `system_plugin.rco` renamed
  the info bar loses its clock glyph and nothing else changes; with `data/font` renamed the UI falls back to Segoe UI.
  No crash, no blank screens.

## Seen, not changed

- After Restart or Sleep is confirmed there is no on-screen acknowledgement (see above).
- The System Check Steam row's path is lower-cased (the string comes from Steam's registry value).
- The Turn Off System confirm page shows the question and the Turn Off the Controller entry in one plain style, with the
  focus carried only by the key hint; not compared with a console frame.
- Open Discord Friends opens discord.com in the default browser when pressed even with no Discord client id set. A
  scripted run that presses Cross on it opens a tab in the real browser (it happened once during this sweep).
- File operation messages (`file_ops.cpp`) are developer-voiced ("Refused: ... (reason)") but were not rewritten.

## Gate and references

See the hand-off report for the final numbers. The first full run, on the first three commits: 6 layers pass and 25 of
27 states pass. The two failures were `first-run-autologon-step` (environment: two displays attached, as noted in the
brief) and `ingame-xmb`, which scored 52.4 dB on the whole-frame region during the full run and 60.2 dB (PASS) when run
alone on both this build and the pre-sweep build (the known Windows title-bar backdrop drift).

No reference was saved. No reference is stale: every state this sweep's pixel changes can reach (information pages,
Settings, Steam, media, gallery, in-game) scored infinity against its stored frame.

## Not reviewed

- Spotify beyond the unconfigured page (needs a client id and an account), Jellyfin beyond the sign-in page (needs a
  server), a live Steam download page, a spinner during a slow network load.
- Whether the PS3 clips or fades list rows that far up (needs a console frame): the new fade and the choice-list help
  line wrap are INFERRED.
- Real sleep and real restart screens (dry run only here).

## Leftovers pass (2026-11-12, after the sweep)

Four findings left over from sweeps 2 and 3, each checked in the app before any change. Grabs are outside the repository
(`out/left-run/g`); runs were scripted (`--script`, scratch data dir, `XMB_POWER_DRY_RUN=1` for every power row).

| # | Finding | Verified | Result |
|---|---|---|---|
| 1 | The Friends column's `Discord Connection` row (signed out; 3x RIGHT from the start, DOWN x8) drew no icon | grab `d8.png`: the focused row shows only its title and a long description, no glyph. The log decodes `explore_category_friend/item_tex_Profile_Onlines` at 36x36: it is the firmware's small online-status badge, not a 128 px row icon | **Fixed**: the row uses `item_tex_ChatRoom` (128x128, the same RCO), grab `d8-after.png`. Which firmware icon the console would pick for a Discord row is not known (INFERRED: it is a chat service and the glyph was otherwise unused in the column). The same grab showed the row's description (the whole setup text) wrapping onto itself under the row; **fixed** as a second commit: the row says "How to set up Discord Friends." and the page keeps the full text |
| 2 | A confirmed Restart or Sleep gave no feedback | dry run: Restart confirmed, the list simply returned | **Fixed** for Restart, Sleep and Turn Off (see below) |
| 3 | `Assets::images_` is unbounded and hands out references | read the cache and its callers | **Not changed**, documented at the member (see below) |
| 4 | Rows scrolled far up pass under the clock bar | Settings column scrolled so rows sit at y 90 and 170 (`f-set7.png`); the draw code | One case remained and is **fixed** (second lines); icons, artwork and values need nothing (see below) |

### 2. Power feedback

A confirmed Restart or Sleep (Users column) and a confirmed Turn Off (appliance mode) now put up a message page, the
existing surface that replaces the list: the focused row's icon and title in the header, one plain line ("Restarting
the system...", "Going to sleep...", "Turning off the system...") and the Back key. No art was invented. The wording is
this build's own (the firmware has no such string in `explore_plugin_full`, `system_plugin`, `explore_category_user` or
`sysconf_plugin`; the console's restart and sleep screens are still not traced).

- The page is raised first and the request runs 250 ms later, so a frame is drawn before `SetSuspendState` blocks or the
  process ends.
- Sleep: the call returns on resume and the page is dropped. A refused request drops the page and shows the error page as
  before.
- Restart and Turn Off return while Windows closes programs, so the page stays until the process ends. If the process is
  still there after 30 s (the shutdown was cancelled by a program or the user) the page is dropped. Only the page this
  raised is dropped; a page the person dismissed, or a newer message, is left alone.
- A busy page was tried first. Over the root list it draws its line at the same spot as the focused row's description
  ("Restarts this PC.") and the two overlap, so the message page is used.
- Turn Off outside `--kiosk` still just quits the XMB and never shuts the PC down (unchanged). In `--kiosk` the PS menu is
  closed, the page is shown, the TV standby (up to 6 s of waiting for cec-client) and the shutdown run after it.
- Dry run (`XMB_POWER_DRY_RUN=1`) never acts: the log shows `power: restart (dry run)`, `power: sleep (dry run)` and
  `power: shutdown (dry run)` and no `requested` line. The page is shown for 2 s and dropped (Sleep: as soon as the dry
  call returns). Grabs: `z-notice2.png` (Restart), `s-notice.png` (Sleep), `t-notice.png` (Turn Off with `--kiosk`),
  `z-after.png` (back on the list).
- Not exercised: a real restart, sleep or shutdown (by rule), so the 30 s release and the real-path timing are by reading.

### 3. Image cache

Not bounded. The cache holds firmware RCO and QGL textures, failed lookups (null images, which `failedImages()` counts)
and, in the `"file"` container, one installed title's ICON0 per row, which the view's start-up pass loads. Artwork and
avatars are `QImage` values on the row, not entries here, so the growth the finding feared does not come from them: the
set is the asset set plus one icon per installed item. Eviction would invalidate what callers keep: 26 `const QImage&`
locals in `view.cpp` and `image=&bestImage(...)` (`view.cpp:2742`) used across a frame. A cap needs `image()` to return
`QImage` by value (implicitly shared) first, which touches every draw call. A comment at `images_` says so; revisit
when a content set shows the growth.

### 4. Rows passing under the clock bar

The bar is x 1329..1920, y 75..133. Row icons are drawn at x ~565 (one level in: up to ~775 for the incoming page) and the
widest tile is 320 px centred there, so icons, thumbnails and artwork never reach the bar; the Settings grab shows the
icons clear of it. Right-hand values start at x 1300 and titles that reach 1305 were already faded in the sweep. What was
left is the second line under a title (a missing file's reason, a download's state, a Steam account's last use): it starts
at the label column and can run past 1329 under a short title. It now takes the same ramp, centred on its own line (35 px
under the title's centre), only when its measured width reaches the bar. INFERRED, as the first fade: no console frame
shows a list that far up. The longest existing second line (a missing playlist file's reason, about 620 px wide) ends near 1300, so no stored
frame changes; the fade was checked in a scratch build that gave every row a 90-character second line (`h-sub.png`;
not committed).

### Gate

States run per change with `--state`: `continue-watching`, `steam-download-page` and `steam-game-root` pass against their
stored frames after the second-line change (the Friends icon and the power page are not in any stored state). Full run:
see the hand-off report. No reference was saved and none is stale.
