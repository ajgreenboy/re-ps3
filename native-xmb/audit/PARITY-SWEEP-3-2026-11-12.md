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
