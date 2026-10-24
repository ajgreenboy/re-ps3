# Functional analysis and recreation requirements

## 1. Shared shell behavior

The shell is a stateful front end to multiple activities. A horizontal category
and vertical item selection are only the first layer. Content options, player
controls, text entry, modal decisions, notifications and the PS overlay are
separate interaction states. Sony documents the Triangle options/control panels
and simultaneous music with other activities in its
[XMB operations guide](https://manuals.playstation.net/document/en/ps3/current/basicoperations/xmb.html).

| Surface | Required behavior and data | Current native gap |
|---|---|---|
| Category bar | Conditional category visibility, ordered icons, per-category focus history | Ten curated categories; TV artwork/visibility remains incomplete |
| Item lists | Content-specific rows, metadata, grouping, scrolling, selection retention and lazy loading | Basic lists and page history exist; grouping and content databases are incomplete |
| Options menu | Triangle toggles actions appropriate to selection, media type, ownership and capability | No original context/options menu |
| Control panel | Triangle toggles player-specific controls without confusing navigation focus | No original photo/music/video control panel |
| PS overlay | Return to XMB while an activity continues; resume/quit and restricted operations | Back currently stops media; separate foreground/background activity model missing |
| Information panel | Clock, busy state, account/avatar, online friends, messages and news indicators | Clock implemented; other indicators largely absent |
| Content grouping | Album/format/date/name grouping, sorting and Unknown metadata groups | No persistent metadata/group model |
| Bulk operations | Mark/unmark items; confirm copy/delete; progress; cancel; refresh selected survivors | Not implemented |
| Text keyboard | Layout, candidate/prediction dictionary, edit cursor, validation and controller navigation | Native desktop text dialogs are an adapter, not the PS3 keyboard |
| Notifications | Queued appearance/dismissal, content-aware placement, sound and background operation | No general notification subsystem |
| Error/progress dialogs | Owned operation, cancellation policy, retry, consistent return selection | Basic message overlay only |
| Media/device changes | USB/disc insertion/removal, permissions, missing files, storage changes | Flat async scans and manual refresh; device discovery/watch model missing |
| Session lifecycle | Startup, user login/logout, clean shutdown, recovery and background work ownership | Basic launch/preferences; no full original lifecycle |

[Storage display rules](https://manuals.playstation.net/document/en/ps3/current/basicoperations/changeview.html)
also distinguish normal media folders from Display All. A generic host folder
browser is useful but does not reproduce that visibility rule.

## 2. Users

Original scope: local user creation, user selection/login, automatic login,
logout, name/icon editing, deletion and system shutdown. User-owned saves,
playlists, messages and account associations must follow the selected user.
Deleting a user therefore has consequences beyond removing a menu row.

The new native `Profiles` service stores stable UUIDs and an active local name,
atomically creates/selects profiles and preserves unreadable/newer files. It
does not implement console accounts, logout, avatars, deletion, automatic login
or user-scoped preferences. Preferences remain shared in this checkpoint.

Recreation requirements: explicit session identity; separate user and shared
content stores; selection after create/delete; validation and duplicate policy;
owned background activity teardown; exact confirmation and cancellation flows.
Do not treat the new native profile as a PSN sign-in.

## 3. Settings: all sixteen groups

The [Settings index](https://manuals.playstation.net/document/en/ps3/current/settings/index.html)
contains sixteen groups. The catalog enumerates their guide entries. Several
current native pages contain convenient host controls that differ from the
original page contents; those controls do not close original coverage gaps.

| Group | Original feature family | Required backend / remaining work |
|---|---|---|
| System Update | Network/storage update source, version checks, preparation and restart | A native application updater needs its own signed release lifecycle; never install firmware into the host |
| Game | PS upscaling and smoothing | Configured game execution backend must consume those settings |
| Video | Disc languages, captions/style, cinema/aspect/upscaling, HDMI output, 24 Hz, dynamic range, audio routing, stored-content language, sequential playback, 50 Hz | Per-format capabilities and real player/disc/device adapters; current generic player options are insufficient |
| Music | CD import format/quality, crossfade, output frequency, normalizer, bitmapping | Optical import, queue transitions and DSP/output chain; current volume/repeat page is not the original Music Settings page |
| Chat | Initial voice/video session behavior | Camera/microphone/session policy and reset-on-exit semantics |
| Theme | Theme install/select/delete, color, background/wallpaper, font | Package/resource overrides and theme lifecycle; current colors/fonts/wallpaper implement only a subset |
| System | Update scheduling, name/language/encoding/dictionary, notifications, trophies/news, disc auto-start, HDMI control, licensed formats, Vita, backup/transfer/format/reset, system information | Most services missing; destructive operations need transactions and recovery; native app name is already stored separately from Windows |
| Date and Time | Date/time, automatic setting, date/time format, zone, daylight saving | Display formats and IANA zone exist; host clock is read only; full original edit/automatic-time semantics remain open |
| Power Save | System and controller auto-off | Current idle setting closes native XMB and exempts media; it does not emulate console/controller power management |
| Accessory | Motion calibration, assignment/vibration, BD remote/Bluetooth, camera, audio devices, voice changer, keyboard, repeat, mouse | Capability-aware hardware/device adapters; current instructions page is not device configuration |
| Printer | Printer management/selection | Printer discovery, job settings, spool and error lifecycle |
| Display | Output selection, screensaver, cross-color filter, RGB range, super-white, deep color | Host display/render-output abstraction; full screen alone is not equivalent |
| Sound | Output formats/devices, multiple outputs, key tone | Original key sounds decoded; media volume/mute work in source; output topology not implemented |
| Security | PIN/password, BD/DVD/game age restrictions, region, browser restriction | Content restrictions and credential storage with exact locked/unlocked UI states; absent |
| Remote Play | Register/list device, connection settings, remote start, audio destination | Pairing and remote-session transport; absent |
| Network | Status, connection enable/configuration/test, wired/Wi-Fi/advanced options, media-server connection | Read-only status plus deliberate native network configuration/service adapters; absent |

Every choice needs a typed value, allowed values, default, scope, capability
predicate, side effects, failure state and persistence rule. Settings with no
backend must not display invented successful results. The current `Preferences`
service validates known keys and commits a versioned JSON document atomically;
the menu/view consume those values rather than maintaining separate copies.

## 4. Photo

Original families include browsing sources, supported-format behavior, EXIF,
view modes, zoom/pan, rotation, transitions, slideshow, playlists, copies,
trimming, printing, wallpaper and Photo Gallery. The
[photo panel](https://manuals.playstation.net/document/en/ps3/current/photo/panel.html)
distinguishes simple fitting from cropping/filling; wallpaper reflects the
displayed transform, and trimming creates another file.

Recreation requirements:

- Image identity and metadata must survive grouping and playlist edits.
- Decode off the UI thread; bound memory; ignore stale completions after Back.
- Separate transient rotation/zoom from changes saved to content.
- Preserve selected photo and viewport when closing a panel or dialog.
- Implement slideshow sequencing, transition effects, cancellation and end rules.
- Use actual printer/storage operations, with progress and failure handling.

Current source implements bounded async photo decode, previous/next, 90-degree
rotation, timed slideshow, repeat and fit/fill. It lacks the original panel,
EXIF screen, zoom/pan, trimming, playlists, copying/deletion, printing, transformed
wallpaper capture and Photo Gallery. Complete workflows have not been observed.

## 5. Music

Original scope includes local/removable/disc/network sources, tags/artwork,
sorting/grouping, CD import, playlists, copy and icon changes, player and mini
panel, previous/next, scanning, play/pause/stop, repeat modes, shuffle,
visualizations and playback alongside other XMB activities. The
[music panel](https://manuals.playstation.net/document/en/ps3/current/music/panel.html)
documents distinct repeat-current, repeat-all and ordinary sequential modes.

Required implementation: persistent media database; stable queue rather than
one selected file; decoder and audio-device lifecycle; metadata/artwork updates;
DSP/crossfade; genuine visual-player state; separate background playback session;
position/duration and queue index; storage and licensing capabilities.

Current Qt player integration plays individual local files, pauses, seeks,
reports progress and applies volume/mute. A boolean repeat-current setting does
not implement the full queue/repeat/shuffle model. No CD import, mini panel,
original visualizers or simultaneous-activity shell is complete.

## 6. Video

Original controls extend well beyond playing a file. The
[video panel](https://manuals.playstation.net/document/en/ps3/current/video/panel.html)
includes disc navigation, scene/chapter/time selection, tracks/subtitles, display
and AV controls, variable scanning, slow/frame stepping, instant replay/advance,
repeat and A–B loops. Availability changes by source and format.

Required implementation: source-specific capabilities; media timeline and
chapter/scene indexes; track/subtitle selection; resume data; thumbnail generation;
disc navigation; A/V output configuration; content management and protection
states. Scene-search previews and other timed controls need independent timing
references, not the root-menu easing constant.

Current local playback/progress/seek are a foundation. Disc menus, scene search,
editing, transfer/backup, protected playback, subtitles, alternative tracks,
frame/slow controls and original player overlays remain unimplemented. Not every
file with a listed extension is guaranteed playable by the host codec stack.

## 7. TV / Video Services

This category is a region/service/application catalog and launcher. Installed,
installable, unsupported and signed-out states differ. Requirements include
correct artwork, catalog/version/region rules, installation progress, launch and
return lifecycle, update prompts and a truthful unavailable state.

The current TV row reuses Video artwork and an unavailable My Channels entry.
This is a known mismatch. [Sony service notices](https://www.playstation.com/en-us/support/important-notice/)
state that PS3 Netflix support ended in March 2026. Service availability must be
verified individually rather than marking the entire category functioning or dead.

## 8. Game and saved content

The [Game overview](https://manuals.playstation.net/document/en/ps3/current/game/game.html)
shows state-dependent disc/installed/downloaded content, Quit Game, game/save
utilities, memory cards and Vita application data. Model support also matters.

Required work:

- Discover real titles and parse title metadata/artwork and content state.
- Distinguish installed data, DLC/cache, saves and downloadable packages.
- Manage save copying/deletion/information and ownership constraints safely.
- Create/assign memory-card slots and expose real PS/PS2 saves.
- Handle game launch, in-game XMB restrictions, process exit and return selection.
- Apply parental restrictions, corruption/locked states and insertion/removal.
- Handle install/update progress and partial/interrupted content.

Current Game rows are curated and unavailable. They do not enumerate or launch
real games, manage saves, install packages or expose memory-card state.

## 9. Network

The audit includes manuals, Remote Play variants, browser navigation and window
mode, bookmarks/history/settings, keyboard/mouse shortcuts, search, upload and
download management. The browser is a separate interactive application with
focus, text input, loading/error state and return lifecycle. Remote Play needs
registration, connection, streaming/input, disconnection and failure handling.

A real download manager needs persistent jobs, destination selection, progress,
pause/resume where supported, cancellation, failure and completion notifications.
DLNA/media-server discovery needs asynchronous network enumeration and source
lifetime; media titles are not a list of synthetic successful results.

Current Network labels exist but there is no browser engine, download queue,
remote-play transport or media-server discovery backend.

## 10. PlayStation Network, trophies and store

Separate local account presentation from authenticated service operations.
Required domains: sign-in/session, existing accounts and recovery, account/profile
management, activation, privacy, What's New, trophies, Plus, catalog/entitlements,
wallet/checkout, download/service/transaction history and code redemption.

[Trophy behavior](https://manuals.playstation.net/document/en/ps3/current/trophy/trophy.html)
includes game collections, earned state, offline viewing, sync and comparison.
An empty local collection screen is a real state; fabricated trophy progress is not.
Account-management scope is documented in the
[account guide](https://manuals.playstation.net/document/en/ps3/current/accountinfo/accountinfo.html).

Current native code has unavailable sign-in/store/trophy rows and no authenticated
backend. Native local profiles are unrelated to authenticated Sony accounts.
Current [Sony notices](https://www.playstation.com/en-us/support/important-notice/)
require device setup passwords for PS3 sign-in, move some account operations off
the console, and restrict console payment methods. The page also announces a
regional PS3/Vita store purchasing closure for July 2027. Re-check these facts
when implementing; do not infer that every PSN service is already unavailable.

## 11. Friends, messages and chat

Required families: relationship requests, status/presence, profiles and trophy
comparison, players met, block lists, text rooms, voice/video rooms, invitations,
message compose/receive/read/protect/delete and unread indicators. Online,
offline, pending, blocked, signed-out and restricted users require different UI.

The [chat panel](https://manuals.playstation.net/document/en/ps3/current/friends/panel.html)
adds camera/mic state, audio devices/levels, screen modes and shared content.
Some values reset after a session while others propagate to Accessory Settings.
This requires owned session state, not a set of disconnected menu values.

Current Friends actions are unavailable. Relationships, messages, rooms,
notifications and media-device capture/transmission do not exist in the native
app. A network backend and explicit operation outcomes are required.

## 12. Cross-cutting completion matrix

Each feature needs the following state variants where applicable:

| Dimension | Cases to record and implement |
|---|---|
| Data | Empty, one item, long list, missing metadata, corrupt, unsupported, locked |
| Ownership | Current user, another local user, shared/system, removable, remote |
| Connectivity | Offline, connecting, signed out, authenticated, interrupted, expired |
| Device | Absent, inserted, removed mid-operation, read only, full, unsupported |
| Operation | Start, progress, cancel, retry, failure, completion, return focus |
| Input | Tap, hold/repeat, rapid reversal, input during transition, controller loss |
| Presentation | Selected/unselected, panel open, modal open, busy, background activity |
| Lifecycle | Cold start, resume, user switch, app/game return, clean/interrupted exit |
| Availability | Firmware, region, hardware model, installed app, service lifecycle |

The catalog identifies the breadth; this matrix identifies why one successful
menu activation cannot establish feature parity.

## 13. Visibility, editions and historical features

Keep stock firmware4.93 as the baseline. A resource string, query or plugin can
exist without being offered on the current retail model or region. The local
XML includes conditional/debug/QA branches; their presence is not a reason to
enable a debug menu in the replica. Installed service apps, optical-drive and
legacy backward-compatibility hardware, capture/TV peripherals and account
permissions need separate capability predicates.

Historical features from older firmware or discontinued applications require a
separately declared archival target. They must not silently expand the baseline
or be represented as currently functioning services. Conversely, an unavailable
backend does not remove the need to reproduce its documented menu, unavailable
state and return behavior when that entry is actually offered in the reference.
