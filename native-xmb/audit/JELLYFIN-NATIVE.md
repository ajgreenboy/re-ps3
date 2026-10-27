# Native Jellyfin playback — implementation and handoff

## What works

Jellyfin now authenticates, lists libraries/folders and plays video inside native XMB using the existing Qt multimedia player. The same queue, pause, ten-second seek, repeat/shuffle and Back behavior apply. Browse requests are asynchronous and bounded; collections page in groups of 200 with a More row. Back preserves the parent selection, and F5 refreshes the current Jellyfin page.

Direct-compatible media uses the authenticated server stream. Other compatible sources use the server's negotiated HLS URL. Playback start, pause/resume, periodic position and stop are reported to Jellyfin. Sign Out clears local credentials immediately, then requests server logout after outstanding stop/cleanup reports finish. A new sign-in has its own connection identity so a delayed old logout cannot invalidate it.

## Configure your server

1. Open Game → Import Games and Apps → Add media app.
2. Choose Jellyfin, enter a name and your server URL, then Save registration.
3. Open the resulting app in TV/Video Services and sign in in the local dialog.

Use the server's HTTP(S) base URL, including a reverse-proxy subpath if present. Do not include a password, token, query or fragment. The sign-in dialog masks passwords, supports cancel during requests and displays sign-in errors inline. Credentials and session tokens remain in memory for this app process; remembered sign-in is not implemented.

The version-1 registry now accepts kind jellyfin. examples/jellyfin.json and the Jellyfin entry in examples/media-apps.json use it. Existing kind web entries keep opening their browser app until explicitly edited or reimported as jellyfin. Games and other browser services retain their existing registration format.

## Source boundaries

| Component | Responsibility |
|---|---|
| src/jellyfin_client.h/.cpp | Header authentication, modern UserViews/Items APIs, paging, PlaybackInfo negotiation and session reports |
| src/jellyfin_stream.h/.cpp | Loopback-only HTTP stream gateway, Range/HEAD, backpressure and HLS URI rewriting |
| src/jellyfin_signin.h/.cpp | Local sign-in dialog; no credential persistence |
| src/jellyfin_shell.cpp | XMB pages, queue/player lifecycle, generation checks, stop/return and sign out |
| src/content_registry.cpp and app_editor.cpp | Typed Jellyfin registrations and native configuration tooling |

The loopback gateway gives QMediaPlayer a local URL and attaches the session authorization only to requests to the configured server origin. It removes ApiKey/api_key/token/access_token parameters before player URLs are constructed, including nested HLS variant, segment, key and map URIs. Requests with invalid local nonces and off-origin playlist/redirect targets are rejected. HTTP headers/playlists and upstream/downstream buffering are bounded. Stream cancellation aborts upstream work and invalidates the local nonce.

The client uses the current [Jellyfin OpenAPI schema](https://repo.jellyfin.org/files/openapi/jellyfin-openapi-stable.json). Jellyfin 12 removed the legacy user-scoped Items/Views routes; the implementation uses /Items and /UserViews. The [official Windows server archive](https://jellyfin.org/downloads/windows/) was used only for isolated verification.

## Verified 2026-10-26

- Native Release builds pass in out/native-jellyfin and the regular out/native-xmb output.
- A disposable real Jellyfin 12.4 server, isolated data/config and generated clips verified the actual service, rather than only a simulated API. One clip is H.264/AAC direct play; the other is MPEG-2/MP2, which the server transcodes to H.264/AAC HLS.
- Real-server native replay: 20 checks passed, including native library/media rows, decoded frames/duration, direct play and HLS negotiation, pause, seek, Back/selection restoration, sign out and four stage screenshots. Final logs: real-probe.log and real-probe.stderr.txt.
- Authentication lifecycle: six checks passed for sign-in, immediate local sign-out, rapid reconnect, new session surviving old logout, and stale browse cancellation. Evidence: auth-probe.log.
- Earlier transport fixture: 24 checks covered invalid credentials, subpaths, Range/HEAD/query forwarding, complete slow-reader transfer, nonce rejection, nested HLS rewriting, off-origin rejection and a native decoding/queue lifecycle. Its original browsing route fixture predates the modern-route correction; real-server verification covers the current routes.
- Native player stderr was scanned after live HLS playback: no credential-bearing query parameters remain. The first live attempt exposed the server's camel-case ApiKey variant; that filter was corrected, the disposable exposed session was revoked, and playback/log checks were repeated successfully.
- Existing three named-state visual regression gates passed pixel-identically. These are native baselines, not new firmware-fidelity claims.
- Screenshot inspection also found that attaching a View to an already-open non-default category left its column cross-fade opaque when there was no outgoing column. That initial state now starts at zero fade. Jellyfin rows use compact film/account icons to avoid overlapping their labels.

All server binaries, generated media, test accounts, tokens, logs and disposable probes remain under ignored out/native-reference/jellyfin. No user account or media library was used. The temporary server is not installed as a Windows service. The user's server URL and credentials have not been supplied or tested.

## Local stage captures

- [Libraries](../../out/native-reference/jellyfin/shots/libraries.png)
- [Media items](../../out/native-reference/jellyfin/shots/items.png)
- [Paused direct playback](../../out/native-reference/jellyfin/shots/direct-paused.png)
- [Paused transcoded HLS](../../out/native-reference/jellyfin/shots/hls-paused.png)

## Next phases and practical limits

- Posters/backdrops, detailed episode metadata, search and resume UI remain open. Playback currently starts at zero; the server still receives progress/stop positions.
- Subtitle and audio-track selection, quality selection, Quick Connect/SSO and remembered sign-in remain open.
- Audio routing exists but was not part of the real-server playback checks. Live TV and off-origin external-media redirects were not validated.
- The player retains the current native control overlay; original firmware player RCO panels/timing remain phase-3/4 work.
- Stop/logout reports are best effort during full process shutdown. Server stop reports released real test transcodes; the optional legacy ActiveEncodings cleanup endpoint is absent on Jellyfin 12 and its failure is ignored.
- No new formal test suite was added. Rebuild disposable probes before reusing them: they link current native objects and are not standalone deliverables. The regular build/CLI and the existing visual harness remain the maintained verification entry points.

## Sign-in inside the XMB (T1 of audit/HTPC-IDEAS.md), implemented 2026-10-27

This section supersedes the two sign-in sentences of "Configure your server" above: step 3 ("sign in in the
local dialog") and the paragraph beginning "The sign-in dialog masks passwords". Both described the modal
`QDialog` in `src/jellyfin_signin.*`, which this change retires from the shell. Everything below was written
after the last source edit, from a run of `C:\dev\out\native-jfsignin\re-ps3-native.exe`
(2026-10-27 00:27:30, later than every edited source file), from the log those runs wrote, and from the JSON
sidecar each capture writes.

### 1. What replaced the dialog, and why the dialog could not stay

A Jellyfin app opened while signed out now pushes an ordinary page onto `pages_` - the same page/column model
the media folders, the users column and the Jellyfin library already use - instead of emitting a signal that a
`QDialog` caught. `openJellyfin()` is unchanged (`jellyfin_shell.cpp`, outside this change's fence): it still
emits `Shell::requestJellyfinSignIn(server)`. What changed is the receiver:

    connect(&Shell::requestJellyfinSignIn, &Shell::openJellyfinSignInPage)   // shell.cpp constructor

so the sign-in surface is `Shell::openJellyfinSignInPage()` and its rows, and `view.cpp` no longer includes
`jellyfin_signin.h` or constructs a widget of any kind. The view only answers `Shell::requestJellyfinFieldEdit`
by opening its existing on-screen keyboard.

Why no fallback was kept: the dialog was the exact thing T1 asks to remove. `view.cpp`'s `pollController`
returns early with every button zeroed while `QApplication::activeModalWidget()` is non-null, so a modal
sign-in page switches the controller off for the whole time someone is typing - the audit's "biggest impedance
to controller-only use". There is now exactly one sign-in surface, and `activeModalWidget()` is null at every
capture of it (the sidecar reports the widget class, see section 6). **Superseded 2026-10-27:** at the time of
writing the retired `JellyfinSignIn` class was still in the tree because `CMakeLists.txt` listed
`src/jellyfin_signin.cpp` and that file was outside this change's fence. The follow-up commit (`81958e4`) deleted
`src/jellyfin_signin.{h,cpp}` and its CMake entry, so the class is gone: a grep for `JellyfinSignIn` in
`native-xmb/src` now matches only the per-process sign-in state (`jellyfinSigningIn_`), the
`cancelJellyfinSignIn()` call and the comment that records the retired name - not a widget.

### 2. The page

Title: the app registration's own name (`Jellyfin` for the example registration). Five rows, in the order T1
specifies, and the row set is the whole page - there is no sixth "status" row:

| row | id | icon | Cross |
|---|---|---|---|
| `Server URL` | `jellyfin-signin:server` | settings bullet (`qgl/icontex` / `23n`) | opens the XMB keyboard on the registration's URL |
| `Username` | `jellyfin-signin:username` | settings bullet | opens the XMB keyboard on the current user name |
| `Password` | `jellyfin-signin:password` | settings bullet | opens the XMB keyboard **masked** |
| `Sign In` | `jellyfin-signin:submit` | `tex_Sign_In` | sends `Users/AuthenticateByName` |
| `Cancel` | `jellyfin-signin:cancel` | settings bullet | leaves the page and aborts a request in flight |

- The three fields draw their current value on the right of the row, the way a settings row draws its choice,
  through a new `Entry::fieldValue` that every other row leaves empty (so `Shell::valueText()` is still the
  only source for every existing row and no frame changes). `Password`'s value is one bullet per character.
- Focus starts on **Username**, not on `Server URL`: the address comes from the registration and is normally
  already right, so the first Cross is the press the couch actually needs. The captured page (section 5,
  `01-page.png`) shows the server line above the focused Username row.
- The focused row's help line says what Cross will do (`Cross to type your Jellyfin user name.`), the same
  help-line idiom the rest of the XMB uses.
- The page is a level like any other: Back/Circle pops it (and cancels, section 4), the column cross-fade runs,
  Left/Right do nothing (a page has no horizontal move), and Triangle is suppressed on it - its five rows have
  no item-level options, and a panel offering "Start"/"Information" over a password field is noise a credential
  page should not have.
- Typed values live in `Shell` for this process only (`signInServer_`, `signInUser_`, `signInPassword_`). The
  password is overwritten with NULs and cleared when the page closes; nothing is written to disk, no new
  preference key exists, and the page is rebuilt in place after every field so the rows and the sidecar show
  the current state without a second source of truth.

### 3. Masking, and why it does not touch the normal keyboard

`View::openOsk` gained a defaulted `masked` parameter, and `View::Osk` a `masked` flag (default false). In the
masked case only, two places change:

- `drawOsk` composes the field from `QString(text.size(), QChar(0x2022))` (U+2022 BULLET) and measures the
  caret against those bullets, so the caret still sits after the last dot. The characters are never drawn.
- the UI layer's cached signature carries the dot count instead of the text (`paintGL`). That matters beyond
  cosmetics: the signature is the cache key for the 2x UI framebuffer, so a plaintext string there would be one
  bug away from being composited onto a frame that `grab=` also writes to a PNG.

Every other caller (`requestSystemName`, `requestNewUser`) passes the default; `composed` is then literally
`osk_.text`, and the drawn string, the signature and the cached-frame behaviour are byte-identical to before -
which is why the five non-frame gate layers reproduce their baselines exactly (section 8).

On the page, the password's `Entry::fieldValue` is built by the same helper (`maskedValue`), so the row shows
seven bullets for a seven-character password. The secret is never part of an `Entry` that is drawn, dumped or
serialised: `Entry::value` on that row is the literal string `password`, i.e. the field key the activate branch
reads.

### 4. Sign in, errors, cancel

- **Sign In** (`Shell::submitJellyfinSignIn`) sets `status_` to `Signing in to Jellyfin...` and hands the three
  values to the existing `Shell::authenticateJellyfin` and then `JellyfinClient::signIn`. The page stays
  navigable while the request runs: `loading_` is deliberately *not* set, because `activate()` and `vertical()`
  both return early while it is, which would make the Cancel row unreachable exactly when it is needed. A second
  Cross on Sign In logs `jellyfin: sign-in ignored, a request is already in flight` and does nothing.
- **Errors** are the client's own string, unedited: `jellyfinSignInFinished(false, message)` sets `signInError_`
  and `status_`, and the page draws it in the band under the rows where the loading line lives
  (`Jellyfin sign-in failed. Check the server, username, and password.` for an unreachable server, and
  `Enter a valid Jellyfin server and username.` for a URL or name the client refuses). A warning line with the
  same text goes to the log. The page stays up with the fields intact, so a retry is one Cross away.
- **Cancel**, and **Back/Circle**, call `Shell::cancelJellyfinSignInPage()`: `cancelJellyfinSignIn()` drops
  `jellyfinSigningIn_`, bumps the client's generation (which invalidates the reply) and signs the client out, so
  a late answer cannot land on a page the user has left; then the page is popped and the password cleared. The
  log distinguishes the two cases: `jellyfin: sign-in cancelled` when nothing was running,
  `jellyfin: sign-in cancelled, the request in flight is aborted` when it was.
- On success the sign-in page is popped *before* the library page is pushed (the `authenticated` handler emits
  `jellyfinSignInFinished(true)` and then calls `loadJellyfinPage`), so the user lands at depth 1 on the
  library, not at depth 2 with a dead form underneath. Section 5 proves this with a local stub server.

### 5. Scripted evidence

Everything below is under `C:\dev\out\native-jfsignin\evidence` and none of it is committed. Registration: the
shipped `examples/jellyfin.json` path - the data directory is a copy of
`out/native-reference/store/jf-smoke`, whose `apps.json` is `{"version":1,"apps":[{"id":"jellyfin","title":
"Jellyfin","kind":"jellyfin","category":"tv","url":"http://localhost:8096"}]}`, the same shape
`--import-apps` writes. Nothing listens on 8096 on this machine, which is the unreachable server. Launch:
`--dev-flash C:/dev/rpcs3/bin/dev_flash --data-dir <copy> --no-intro --script <tokens>`, one token every
300 ms. Navigation to the app: `LEFT` (Game to TV), `UP` (the Jellyfin row is first in the column; the column
opens on "My Channels"), `ENTER`.

Run 1 - the walk (`walk.log`):

    jellyfin: sign-in page opened rows= Server URL|Username|Password|Sign In|Cancel focus= Username
    jellyfin: sign-in field username set to 5 character(s), value masked (not logged)
    jellyfin: sign-in field password set to 7 character(s), value masked (not logged)
    jellyfin: sign-in request to http://localhost:8096 (username set, password set; values masked)
    jellyfin: sign-in failed: Jellyfin sign-in failed. Check the server, username, and password.
    jellyfin: sign-in cancelled

| capture | what it shows | `pageKind` | `focusedRow` | `signInState` | `modalWidget` |
|---|---|---|---|---|---|
| `01-page.png` | the new page, five rows, server value on the right, focus on Username | `jellyfinSignIn` | `jellyfin-signin:username` | `idle` | `""` |
| `02-osk-server.png` | Cross on `Server URL` opens the existing keyboard **prefilled** with the registration URL, unmasked | `jellyfinSignIn` | `jellyfin-signin:server` | `idle` | `""` |
| `03-osk-username.png` | `type=alice` typed through the keyboard's real handlers; field shows `alice` | `jellyfinSignIn` | `jellyfin-signin:username` | `idle` | `""` |
| `04-page-username.png` | accepted (5 DOWN then 7 RIGHT to the keyboard's Enter key, then Cross); row shows `alice` | `jellyfinSignIn` | `jellyfin-signin:username` | `idle` | `""` |
| `05-osk-password.png` | Cross on `Password`: same keyboard, field is **seven dots** for the 7-character password | `jellyfinSignIn` | `jellyfin-signin:password` | `idle` | `""` |
| `06-page-password.png` | accepted; the page's Password row is **seven dots**, Username still reads `alice` | `jellyfinSignIn` | `jellyfin-signin:password` | `idle` | `""` |
| `07-signin-focus.png` | focus on `Sign In`, no error yet | `jellyfinSignIn` | `jellyfin-signin:submit` | `idle` | `""` |
| `08-error.png` | Cross sent the request; after the refusal the page carries the client's message in the band under the rows | `jellyfinSignIn` | `jellyfin-signin:submit` | `failed` | `""` |
| `09-after-back.png` | Circle/Back cancels and closes the page; back at the TV column | `root` | `custom:jellyfin` | `""` | `""` |

Crops (2x nearest-neighbour, from the same runs):

- `crop-osk-username-field.png` and `crop-osk-password-field.png` - the same keyboard field rectangle
  (500,322,420,76): `alice` with a caret after it, versus seven bullets with the caret after them. The keyboard
  is otherwise identical, which is the point: masking is a property of the field, not a second keyboard.
- `crop-page-username-value.png` and `crop-page-password-value.png` - the same row-value rectangle
  (1285,472,320,66): `alice` versus seven bullets.

Run 2 - cancel with a request genuinely in flight (`cancel.log`). Same data directory with the registration URL
changed to `http://192.0.2.1:8096` (TEST-NET-1, no route, so the request hangs until the client's 15 s transfer
timeout):

    jellyfin: sign-in request to http://192.0.2.1:8096 (username set, password set; values masked)
    jellyfin: sign-in cancelled, the request in flight is aborted

| capture | `pageKind` | `signInState` | note |
|---|---|---|---|
| `10-inflight.png` | `jellyfinSignIn` | `signingIn` | taken about 1.2 s after Cross, request still running |
| `11-cancelled.png` | `root` | `""` | Cross on the `Cancel` row; page closed, back at the TV column |
| `12-late.png` | `root` | `""` | 19.5 s after the submit - past the 15 s timeout - and no error, no reopen: the aborted reply was discarded |

Run 3 - the success path, against a local stub (`stub-run.log`). A 60-line PowerShell TCP listener
(`stub-server.ps1`, in the evidence directory, never in the repo) answers `POST /Users/AuthenticateByName` with
a fixed token and `GET /UserViews` with one folder; the registration URL points at `http://127.0.0.1:8097`.
This is a stub, not a Jellyfin server: it proves the page's success handling, not a real session.

    jellyfin: sign-in request to http://127.0.0.1:8097 (username set, password set; values masked)
    jellyfin: sign-in accepted
    stub log: POST /Users/AuthenticateByName        (no query, no path parameter: the credentials are in the body)
    stub log: GET  /UserViews?userId=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa

`20-signed-in.png` is the result: `pageKind` `jellyfinViews`, depth **1**, two rows (`Stub Movies`,
`Sign Out of Jellyfin`) - the sign-in page was popped rather than left underneath, and `modalWidget` is `""`.

### 6. Credentials never reach a log, a frame or the sidecar

- The log lines this feature writes name the field, the number of characters, the server and whether a user
  name and password were supplied. No value is ever interpolated:
  `jellyfin: sign-in field password set to 7 character(s), value masked (not logged)`.
- The capture sidecar gained `pageKind`, `focusedRow`, `signInOpen`, `signInState`, `signInError` and
  `modalWidget`. It carries no field value: `entryTitle` for the password row is the literal `Password`, and
  `signInError` is one of the client's fixed sentences.
- `JellyfinClient` logs nothing at all, and it was not touched.
- Grep over every log and every sidecar JSON in the evidence directory (38 files, including `walk.log`,
  `cancel.log`, `stub-run.log` and the 13 `.png.json` sidecars) for the two strings typed in the runs:

      typed string 'alice'   -> 0 hit(s)
      typed string 'hunter2' -> 0 hit(s)

- Remembered sign-in (audit/HTPC-IDEAS.md row 2) is **not** implemented and is out of scope here: it needs a
  DPAPI-backed store, an opt-in preference and its own threat model (T1's "Never" list forbids persisting the
  password in this slice). So the password is typed once per launch until that lands.

### 7. Input: what is proven, and what is structural

Keyboard and script drive the same `Shell` methods the pad does, and the pad path is now unobstructed because
no modal exists:

| input | code path | exercised here |
|---|---|---|
| D-pad / left stick Up-Down, Left-Right | `pollController` -> `Shell::vertical` / `horizontal` | the same functions driven by key taps |
| Cross on a row | `pollController` (A) -> `Shell::activate` | the same function driven by `ENTER` |
| Circle | `pollController` (B) -> `Shell::back` | the same function driven by `ESC` |
| Cross in the keyboard | `pollController` (A) -> `oskPress` | the same function driven by `ENTER` |
| D-pad in the keyboard | `pollController` -> `oskMove` | the same functions driven by arrow keys |
| Square / Triangle / START in the keyboard | `oskBackspace` / space / `oskClose(true)` | not exercised; unchanged code |
| `--script` keys | `replayKeyTap` -> `keyPressEvent` | this is what the runs use |

The pad itself could not be exercised: no controller is attached to this machine and the harness has no
controller injection (`audit/OPTIONS-MENU.md` section 4 and `audit/SUBTITLES.md` record the same limitation).
The claim is therefore structural plus one measured fact: `pollController`'s only blocker for a modal surface
is `QApplication::activeModalWidget()`, and the sidecar reports the active modal's class name at every one of
the thirteen captures above - it is `""` in all of them, where the retired dialog would have reported
`JellyfinSignIn`.

### 8. Gates

    & C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-jfsignin
    -> Built C:\dev\out\native-jfsignin\re-ps3-native.exe (2026-10-27 00:27:30, no new compiler warning)

    & "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --capture --exe C:/dev/out/native-jfsignin/re-ps3-native.exe

| layer | PSNR | baseline | delta | SSIM | baseline | delta | gate |
|---|---:|---:|---:|---|---:|---:|---|
| frame | 18.29 | 18.02 | +0.27 | .8295 | .8301 | -.0006 | PASS |
| top background | 20.24 | 20.24 | +0.00 | .9344 | .9344 | +.0000 | PASS |
| category icon row | 20.91 | 21.05 | -0.14 | .8947 | .8950 | -.0003 | PASS |
| item label row | 11.94 | 11.94 | +0.00 | .3913 | .3910 | +.0003 | PASS |
| clock text | 11.39 | 11.39 | +0.00 | .4105 | .4105 | +.0000 | PASS |
| info bar | 14.78 | 14.78 | +0.00 | .6117 | .6117 | +.0000 | PASS |

`PASS: 6/6 layers within budget`, and `--states` is **4/4 pass** with every region `inf / 1.0000`, so no named
state changed. The five non-frame layers reproduce their baselines to the last digit, which is the expected
result: none of this change draws anything in the idle Game root. The +0.27 dB frame delta is the same number
`audit/HTPC-IDEAS.md` section 6.6 and `audit/SUBTITLES.md` section 8.6 already report for this shared tree, and
belongs to the rest of the working tree, not to this change. No harness, threshold or reference file was edited.

### 9. Not proven, and known limits

- **A real pad.** Section 7; structural only.
- **A real Jellyfin server.** The success path is proven against a stub and the failure path against a closed
  port; neither is an authenticated session against Jellyfin 12. No account or password of the operator's was
  used, and the two strings typed in the runs are throwaway test values.
- **Remembered sign-in** is not implemented (section 6).
- **Server URL validation** is the client's own: the page does not duplicate it, so a malformed URL is reported
  by `JellyfinClient` after Cross, not before.
- **The keyboard has no clear-field gesture for an existing secret**: opening `Password` starts from the current
  value (that is how every field edit works), so the first dots correspond to the characters already there.
  Clearing a field is still impossible, because `oskClose` ignores an empty accept - pre-existing behaviour,
  unchanged.
- **Clipboard, IME and a numeric-only keyboard mode** are unchanged: none of them applies to this page.

### 10. Files touched

- `native-xmb/src/shell.h`: `Action::JellyfinSignInField/Submit/Cancel`; `Entry::fieldValue`; the public
  sign-in page API, `pageKind()`, `focusedRowId()`; the `requestJellyfinFieldEdit` signal; `Page::signIn`; the
  four private state strings and the two page helpers.
- `native-xmb/src/shell.cpp`: the two constructor connections (the page replaces the dialog, the client's
  message reaches it); the sign-in branch in `activate()`; the cancel branch in `back()`; the Triangle guard in
  `toggleOptions()`; `openJellyfinSignInPage`, `refreshJellyfinSignInPage`, `setJellyfinSignInField`,
  `submitJellyfinSignIn`, `cancelJellyfinSignInPage`, `closeJellyfinSignInPage`, `jellyfinSignInOpen`,
  `jellyfinSignInState`, `pageKind`, `focusedRowId`, `maskedValue`.
- `native-xmb/src/view.h`: `Osk::masked`; the defaulted `masked` parameter on `openOsk`.
- `native-xmb/src/view.cpp`: the `requestJellyfinFieldEdit` to `openOsk` connection; the dialog connection, the
  `jellyfin_signin.h` include and the `JellyfinSignIn` use deleted; the masked composition and caret in
  `drawOsk`; the masked signature term in `paintGL`; `Entry::fieldValue` preferred over `valueText` in the row
  draw.
- `native-xmb/src/main.cpp`: `pageKind`, `focusedRow`, `signInOpen`, `signInState`, `signInError` and
  `modalWidget` in the `grab=` sidecar.
- `native-xmb/src/jellyfin_signin.h`: marked RETIRED with the reason it is still built (CMake is outside this
  change's fence) and the instruction to delete it with that entry. `jellyfin_signin.cpp` is untouched.
- `native-xmb/audit/JELLYFIN-NATIVE.md`: this section.

Not touched: `jellyfin_client.*`, `jellyfin_stream.*`, `jellyfin_shell.cpp`, `app_editor.cpp`,
`settings_menu.cpp`, `preferences.cpp`, `xmbml_view.*`, `subtitles.*`, `CMakeLists.txt`, the verification
harness and its thresholds and references. Captures, the stub server and the run logs live under
`out/native-jfsignin/evidence` and are not committed.