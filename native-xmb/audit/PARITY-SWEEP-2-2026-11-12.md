# Render-parity sweep 2 (2026-11-12)

The first sweep covered the root screen, a few states and window-size scaling. This one went through the pages it did
not: media players and their panels, options and context menus, dialogs and message pages, Settings sub-pages, Users,
Network, Friends and PSN columns, first-run pages, the on-screen keyboard, notification toasts, the PS menu, error and
empty states, and window sizes 1280x720, 1920x1080, 2560x1080 and 900x1000. Method: scripted runs (`--script`, scratch
data dir, `XMB_STEAM_ARTWORK=0 XMB_STEAM_ACTIVITY=0`), grabs read as contact sheets and zoom crops. Grabs are kept
outside the repository (`out/par2`).

## Fixed

| Defect | Evidence | Fix |
|---|---|---|
| A window that is not 16:9 cropped the UI: on 2560x1080 the clock, lower item rows, the music player bar and hint line and the video control panel were off screen; on 900x1000 both sides were | `sz-play-2560x1080`, `sz-opt-900x1000` | `DesignSpace` takes the smaller scale (contain-fit); bars are black. Unchanged at 16:9 |
| Photo Gallery bars on a non-16:9 window were grey while every other page has black bars | `gl-2560x1080-a` | the scene clears its grey inside the 16:9 area only |
| Notification detail line ran past the plaque and off the screen edge (the title was already elided) | `ps-6`, `ps-7` | the detail is elided the same way, in the XMB and in the over-game overlay |
| A long path in an Information row ran off the right edge; any single line wider than its box did | `i-b` | `drawMessageText` elides it (middle for a path, end otherwise) |
| A video queue with no playable file left a half-open player under the error page: a music page for a video, "Track 2 / 2 Tracks", the play-state icon and two hint lines drawn over the message page | `b-1` (before), `b-1` re-grab (after) | `Shell::abandonFailedMedia()` closes the player surface before the page goes up, as the photo path already did |
| One-line Information answers ("No players were found.", "No persons have been added.") became the heading of an empty page | `fr-2` | shown as the row's own message page: title in the heading, text in the body |
| Information page for a file repeated its size and date as a "Details" row and labelled the path "Path (native)" | `d-info` | the Details row is dropped for a file on disk; the label is "Path" |
| Phone Remote page told the user to open `http://127.0.0.1:...` on a phone (the server listens on loopback unless `XMB_PHONE_REMOTE_LAN=1`) | `n2-4` | the page says the remote is limited to this computer and how to allow phones |
| Wi-Fi Networks showed "The wireless local area network interface is powered down ... (Windows error 2150899714)" | `n2-5` | that code reads "Wi-Fi is turned off on this computer." |
| Developer text in user-visible places: source file names in the Search for Media Servers and Download Management descriptions, `audit/STEAM-CLI.md` in a download page, `XMB_STEAM_ROOT` and "steamapps directory" in two Steam empty states | `m-root`, `net-5`, `usr-2` | rewritten in plain words |
| The disabled theme-colour note started in lower case | `t-4` | capitalised |

## Seen, not changed

- Video control panel open: the bottom key strip shows the eight key icons without labels. Intentional per
  `MEDIA-PANELS-FIRMWARE.md` (the help plane's runtime composition is not available); noted for the next media pass.
- White key labels over a bright video frame have poor contrast (`MEDIA-PANELS-FIRMWARE.md` already records it).
- Photo viewer and slideshow hint line is still one ASCII sentence rather than the firmware key icons (known gap).
- Local Photo/Music/Video library rows use the firmware's placeholder tile (`photo_tex_album_pic` etc.), which reads as a
  folder; there are no thumbnails for local photos or video frames in the lists.
- A settings choice panel (Notification Messages, Theme) is translucent, so the focused row's long description shows
  through it and collides with the choice labels. No firmware frame to say whether the console clips the description.
- Settings > System Language prints raw Qt format strings ("h:mm AP") in the Information table.
- Item rows scrolled far up pass under the clock bar (a-2 of the long-title set); no reference.
- The Home Assistant row's description prints a JSON snippet and a file name.
- Pages for PSN, Remote Play, Online Manuals, Internet Browser and Search answer "This feature is not available in the
  native recreation yet." (honest, but one wording for six different reasons).
- `first-run-autologon-step` fails with two displays attached (environment, as noted in the brief).

## Not reviewed

Steam Store and All Games with live data (needs network and a Steam library), the Playlist area of the Photo Gallery,
Jellyfin and Spotify pages (need accounts), System Check (not found in the root list; lives under a Settings page),
sleep and real shutdown screens (dry-run switches only), the in-game overlay over a real game, loading spinners that need
a slow network, and missing-asset states (needs a broken `dev_flash`).
