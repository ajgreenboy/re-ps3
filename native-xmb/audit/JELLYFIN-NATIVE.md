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

## Phase 1 completion, 2026-10-27 (evening)

This section supersedes the two "not implemented" statements of the sections above - "Playback currently starts
at zero" and "remembered sign-in is not implemented" - and the five-row sign-in page of section 2. Everything
below was written after the last source edit, from runs of `C:\dev\out\native-jf1\re-ps3-native.exe` (built
2026-10-27 20:41, later than every source file this pass edited), from the logs those runs wrote, from the state
sidecar each capture writes, and from the stub server's own request log. The stub, the runner, the captures and
the logs live under `C:\dev\out\native-jf1-evidence` and are not committed.

Scope: finish and own the uncommitted resume slice (1.1), then 1.2 posters, 1.3 search, 1.4 stream tracks and
1.5 DPAPI remembered sign-in, as far as the file fence allows. `view.cpp`, `view.h`, `main.cpp`,
`settings_menu.cpp`, `preferences.cpp`, `xmbml_view.*`, `particles.*`, `icons.cpp`, `text_renderer.*`,
`CMakeLists.txt` and the harness were not edited by this pass.

### 11. The in-flight resume slice: what it did, and what this pass fixed

The uncommitted slice in the working tree added: `RunTimeTicks`/`UserData` parsing in `JellyfinClient`
(`JellyfinItem::durationMs/resumeMs/played`), `EnableUserData=true` on the Items request, `Entry` fields for
remote progress, the "Resume at HH:MM" detail line, a resume seek path with a seekable guard, the Triangle
"Play from Beginning" row, completion -> Watched and stop/progress reporting. It compiled and its shape was
right; the problems were in how the pieces met:

1. **Two resume paths could fight over one seek.** The local-file path in `shell.cpp` applies `resumePending_`
   on `LoadedMedia`; the new remote path applies `jellyfinResumePending_` on the same signal. The local branch
   now refuses when `jellyfinQueue_` is set, and `startJellyfinMedia()` clears `resumePath_`/`resumePending_`,
   so exactly one owner exists at a time.
2. **The resume seek was not actually guarded.** It now requires `isSeekable()`, a `LoadedMedia`/`BufferedMedia`
   status, and one attempt flag (`jellyfinApplyingResume_`), re-checks 1.5 s later, and when the stream still
   cannot seek it says so (`This stream cannot seek; playing from the beginning.`) instead of silently
   pretending. The target is clamped to `duration-1`.
3. **Completion was not reported as completion.** `finishJellyfinPlayback()` now takes `completed`, which
   `advanceMedia()` passes as `!failed`; a completed title reports its **end** position even if the backend has
   already rewound to 0, reports `IsPaused=false`, sets the row to `played` (label "Watched", resume 0), and the
   start/stop reports are logged with their positions. An abandoned title reports where it actually stopped.
4. **Rows were only updated on the current page.** `finishJellyfinPlayback()` walks every open Jellyfin page, so
   a title finished on the search page is also correct on the folder page underneath it.
5. **The detail line is ASCII and does not claim a position for watched items.** `Movie - Resume at 00:08 -
   00:20` / `Movie - Watched - 00:20` (the in-flight code used a U+00B7 separator; this pass's ASCII-only rule
   for new text is why it is now a plain hyphen).
6. **Nothing was observable.** The resume decision, the applied seek, the start/progress/stop reports and the
   Watched transition now log one line each (section 12 quotes them); they carry item ids and millisecond
   positions, never a credential.
7. **`jellyfinResume_` outlived the session.** It is cleared on sign-out.

Verified end to end against the stub (below): the row reads "Resume at 00:08", Cross lands at 8 s (sidecar
`mediaPosition` 11541 for a server position of 8000 ms), Triangle offers "Play from Beginning", that starts at 0
(sidecar 3989), the clip runs to its end, the row becomes "Watched", and the stub receives a Stopped report at
`positionTicks=200000000` which it records as `played=true`.

### 12. The stub server and the four scripted runs

`C:\dev\out\native-jf1-evidence\stub-jellyfin.ps1` is a single-file TCP listener (Windows PowerShell 5.1, no
Jellyfin binaries, no account) that answers the routes this client uses:

| route | answer |
|---|---|
| `POST /Users/AuthenticateByName` | fixed test user `aaaa...0000`, token `stub-token-not-a-real-credential` |
| `GET /UserViews` | one movie library `Stub Movies` |
| `GET /Items` | two movies with `RunTimeTicks` + `UserData{Played,PlaybackPositionTicks}`: One resumes at 8 s, Two is unplayed; both play the same 20 s clip |
| `GET /Items?searchTerm=` | the same rows filtered by name (`Recursive=true` as the client sends) |
| `POST /Items/<id>/PlaybackInfo` | one DirectPlay source with four `MediaStreams`: video, AAC stereo, AC3 5.1, one SRT subtitle |
| `GET /Videos/<id>/stream` | the real 20 s `Top Level Clip.mp4` (3,459,152 bytes) with byte ranges (`200`/`206`, `Content-Range`, `416`) |
| `GET /Items/<id>/Images/Primary` / `Backdrop` | local fixture art, `401` without an `Authorization` header |
| `POST /Sessions/Playing`, `/Sessions/Playing/Progress`, `/Sessions/Playing/Stopped` | `204`; the log carries item, position ticks and the resulting stored state |
| `POST /Sessions/Logout`, `DELETE /Videos/ActiveEncodings` | `204` |

Two switches exist only to exercise the client's honest failure paths: `-RejectToken` answers every
signed-in route with `401`, `-FailPlayback` returns a source with no direct play, no direct stream and no
transcoding URL. The stub keeps `Played`/`PlaybackPositionTicks` in memory and updates them from a Stopped
report, so a re-open shows server-side state, not only the client's.

Registration went through the shipped route, not a hand-edited file: a fresh data directory plus
`--import-apps` with
`{"version":1,"apps":[{"id":"jellyfin","title":"Jellyfin","kind":"jellyfin","category":"tv","url":"http://127.0.0.1:8098"}]}`
wrote `apps.json` (the command printed `Apps imported successfully.`, exit 0). Each run then used
`--dev-flash C:/dev/rpcs3/bin/dev_flash --data-dir <data-final> --no-intro --script <tokens>`, one token every
300 ms, fullscreen 1920x1080.

Run A (`client-a.log`, `stub-a.log`) - sign-in with the masked OSK and the opt-in, resume, tracks, restart,
finish. Item ids are abridged to `1111...8888` in this document only; both logs carry the full 32-character
ids, and the capture sidecars carry them verbatim.

    jellyfin: sign-in page opened rows= Server URL|Username|Password|Stay Signed In|Sign In|Cancel focus= Username
    jellyfin: sign-in field username set to 4 character(s), value masked (not logged)
    jellyfin: sign-in field password set to 8 character(s), value masked (not logged)
    jellyfin: stay signed in on (an accepted sign-in will store the session for this Windows user)
    jellyfin: sign-in accepted
    jellyfin: remembered sign-in stored (DPAPI, this Windows user; no password kept)
    jellyfin: library page with 3 row(s); 0 poster request(s)
    jellyfin: folder page with 2 row(s); 2 poster request(s) and one backdrop
    jellyfin: play "Stub Movie One" (server position 8000 ms)
    jellyfin: resume pending at 8000 ms for Stub Movie One
    jellyfin: playback 1111...8888 DirectPlay 4 server stream(s) Video:1080p H.264|Audio:English AAC stereo|Audio:French AC3 5.1|Subtitle:English (SRT)
    jellyfin: resume applied at 8000 ms (server reported 8000 ms, duration 20000 ms)
    jellyfin: start report at 8000 ms
    jellyfin: progress report at 13098 ms
    jellyfin: stop report item=1111...8888 at 13440 ms
    shell: options opened category= tv depth= 2 entry= jellyfin:1111...8888 rows= 4 Resume|Play from Beginning|Information|Close
    shell: options selection 1 Play from Beginning
    jellyfin: play from the beginning "Stub Movie One" (server position 0 ms)
    jellyfin: start report at 0 ms
    jellyfin: progress report at 6741 ms
    jellyfin: progress report at 15317 ms
    jellyfin: stop report item=1111...8888 at 20000 ms (reached the end)
    jellyfin: row now reads Movie - Watched - 00:20

and the server side of the same session (`stub-a.log`, abridged; the full log has every request):

    POST /Sessions/Playing            item=1111...8888 positionTicks=80000000  paused=False method=DirectPlay
    POST /Sessions/Playing/Progress   item=1111...8888 positionTicks=130980000 paused=False method=DirectPlay
    POST /Sessions/Playing/Stopped    item=1111...8888 positionTicks=134400000 paused=False -> resumeTicks=134400000
    POST /Sessions/Playing            item=1111...8888 positionTicks=0         paused=False method=DirectPlay
    POST /Sessions/Playing/Stopped    item=1111...8888 positionTicks=200000000 paused=False -> played=true resumeTicks=0

| capture | what it proves | sidecar |
|---|---|---|
| `10-views.png` | the app opens on its own library page; 3 rows (Stub Movies, Search Jellyfin, Sign Out) | `pageKind` `jellyfinViews`, depth 1 |
| `11-rows.png` | **the row reads `Movie - Resume at 00:08 - 00:20`**, with the server's poster as its icon | `pageKind` `jellyfinFolder`, `entryTitle` `Stub Movie One` |
| `12-resume.png` | Cross resumed: playback starts at the server position | `mediaPosition` **11541**, `mediaDuration` 20000, `playMethod` `DirectPlay` |
| `13-tracks.png` | Triangle during playback: the stream track panel (section 15) | `audioTrack` 1 of 1 reported |
| `14-after-stop.png` | Back leaves playback; the row keeps what was watched | `mediaOpen` false, `pageKind` `jellyfinFolder` |
| `15-options.png` | Triangle on the row: `Resume`, `Play from Beginning`, `Information`, `Close` | `optionsOpen`, 4 rows in the log line above |
| `16-beginning.png` | **"Play from Beginning" starts at 0** | `mediaPosition` **3989** (4 s into a restart), start report at 0 ms |
| `17-watched.png` | **the row becomes `Movie - Watched - 00:20`** after the end | `entryTitle` `Stub Movie One`, focus on it |

Run B (`client-b.log`, `stub-b.log`) - the remembered session, then search:

    jellyfin: using the remembered session for http://127.0.0.1:8098 (DPAPI record, no password)
    jellyfin: library page with 3 row(s); 0 poster request(s)
    jellyfin: search term accepted ( 4  character(s))
    jellyfin: search stub from 0
    jellyfin: search page with 3 row(s); 2 poster request(s) and one backdrop

The stub log for this run contains **no** `AuthenticateByName` request at all - only `GET /UserViews` and
`GET /Items?...searchTerm=stub` - which is the point of the DPAPI record. `20-restored.png` is the library page
reached without a sign-in page (`pageKind` `jellyfinViews`, `signInOpen` false); `21-search.png` is the result
page (`pageKind` `jellyfinSearch`, 2 result rows + "Search Again").

Run C (`client-c.log`, `stub-c.log`) - a remembered session the server refuses:

    jellyfin: using the remembered session for http://127.0.0.1:8098 (DPAPI record, no password)
    jellyfin: the remembered session was refused; it has been forgotten, signing in again
    jellyfin: sign-in page opened rows= Server URL|Username|Password|Stay Signed In|Sign In|Cancel focus= Username

    GET /UserViews?userId=aaaa...0000 auth=yes rejected (expired-token mode)
    POST /Sessions/Logout auth=yes

`22-refused.png` is the sign-in page (`pageKind` `jellyfinSignIn`, `signInState` `idle`, `modalWidget` `""`),
and `data-final/jellyfin/session.bin` was gone afterwards (checked: `False`; the record run described in
section 16 created a fresh one later, on purpose). A stale record therefore costs one sign-in, never a dead end.

Run D (`client-d.log`, `stub-d.log`) - a stream the client cannot use:

    jellyfin: play "Stub Movie One" (server position 8000 ms)
    POST /Items/1111...8888/PlaybackInfo auth=yes directPlay=0 (source the player cannot use) streams=0

`23-unplayable.png` shows the client's own message over the XMB: `Jellyfin` / `Jellyfin did not provide a
compatible stream.` - the exit criterion "a failed stream shows the client's error".

### 13. 1.2 Posters (and the backdrop, which needs a view change)

- `JellyfinClient::artwork(itemId, tag, backdrop)` fetches `/Items/<id>/Images/Primary?maxHeight=480` (or
  `.../Backdrop?maxWidth=640`) with the session `Authorization` header, over the same `QNetworkAccessManager` the
  rest of the client uses. Nothing is fetched without a signed-in session.
- Bounded on the network and in decode: the shared 4 MB response cap; the declared pixel size is refused above
  4096x4096 and anything above 1024x1024 is scaled down *before* the read; the decoded image must come back
  non-null and within the same bound. A malformed body yields no image, not an error the viewer sees.
- Cached per item under `<data>/jellyfin/images/<id>-p-<sha1(tag)[0..7]>.img` (and `-b-` for a backdrop). The key
  includes the server's `ImageTags.Primary` / `BackdropImageTags[0]`, so a replaced poster is a new file rather
  than a stale one. Writes go through `QSaveFile`; a corrupt entry is deleted and fetched again; the directory is
  capped at 400 files, oldest first.
- The row image is `Entry::artwork`, which `view.cpp` already draws in the item-icon slot, so **no view change
  was needed for posters** - and the icon is a real poster in `11-rows.png`/`17-watched.png` (the blue/orange
  fixture art, not the built-in film glyph). The fetch is bounded to the first 60 playable rows of a page.
- Stub evidence: `GET /Items/1111...8888/Images/Primary auth=yes bytes=1826`,
  `GET /Items/2222...9999/Images/Primary auth=yes bytes=1826`,
  `GET /Items/1111...8888/Images/Backdrop auth=yes bytes=2199`; the three cache files are on disk under
  `data-final/jellyfin/images` at exactly those sizes.
- **Backdrops are fetched, cached and exposed, but nothing draws them.** There is no page-level artwork surface
  in the view: `Entry::artwork` is a row icon, and a grep for `backdrop`/`hero` in `view.cpp`, `view.h`,
  `shell.cpp` and `shell.h` matches nothing before this change. The shell side is done
  (`Shell::pageBackdrop()`, the fetch, the cache), so the remaining hunk is the draw - and
  `view.cpp` is outside this change's fence, so it is reported here instead of applied (illustrative hunk; the
  surrounding paint path is the one that draws the background and the columns):

      --- a/native-xmb/src/view.cpp
      +++ b/native-xmb/src/view.cpp
      @@ View::paintGL(), after the background and before the columns are composited
      +    // The open Jellyfin page's own Backdrop, full-bleed under the columns. Shell owns the
      +    // authenticated fetch and the cache; this is the only surface that draws it.
      +    if(const QImage& hero=shell_.pageBackdrop(); !hero.isNull() && shell_.depth()>0) {
      +        const QSizeF cover=QSizeF(hero.size()).scaled(QSizeF(width(),height()),Qt::KeepAspectRatioByExpanding);
      +        p.save();p.setOpacity(.55);
      +        p.drawImage(QRectF(QPointF((width()-cover.width())/2.,(height()-cover.height())/2.),cover),hero);
      +        p.restore();
      +    }

  With that hunk the backdrop is already warm from the run that produced `11-rows.png`; no client or shell
  change would be needed. Until then the honest statement is: posters render, backdrops are fetched and cached
  but not drawn.

### 14. 1.3 Search, driven by the existing OSK

- The library page gained a `Search Jellyfin` row (and a search result page gains `Search Again`). Cross emits
  the existing `Shell::requestJellyfinFieldEdit` signal with the field key `search`; `view.cpp` already answers
  that signal with its own `openOsk`, and returns the accepted text through `Shell::setJellyfinSignInField`,
  which routes `search` to `Shell::openJellyfinSearch`. **No view code was added for this.**
- `JellyfinClient::search(term, startIndex)` is the user-scoped `/Items` route with `SearchTerm`,
  `Recursive=true`, `IncludeItemTypes` (movie/episode/video/music video/audio/trailer/recording/series/season),
  `EnableUserData=true` and the same page limit as a folder, so each result carries its `UserData`.
- Result rows are built by the same loop as a folder page: they carry the same detail line, the same poster
  fetch, and the same `Action::JellyfinPlay`, which is what gives them identical Play/Resume/Play from Beginning
  semantics (the row is a queue entry like any other, so the same resume path applies). Paging reuses the `More`
  row; the term travels in `Page::jellyfinParent`, and `pageKind()` reports `jellyfinSearch` so a scripted run
  can tell the pages apart.
- Evidence: `jellyfin: search term accepted ( 4  character(s))` / `jellyfin: search stub from 0` /
  `jellyfin: search page with 3 row(s); 2 poster request(s) and one backdrop`; stub
  `GET /Items auth=yes searchTerm=stub rows=2`; `21-search.png` (pageKind `jellyfinSearch`, 2 rows + Search
  Again). A term the server has no match for closes the page's rows to just `Search Again` and sets the status
  line the loading text already uses to `No Jellyfin items match that search.` (not captured in this pass).

### 15. 1.4 Stream audio and subtitle tracks, with honest disabling

- `PlaybackInfo`'s `MediaStreams` are parsed into `JellyfinPlayback::streams` (index, type, title/language/codec,
  channels, external, default) and logged when playback starts:
  `playback 1111...8888 DirectPlay 4 server stream(s) Video:1080p H.264|Audio:English AAC stereo|Audio:French
  AC3 5.1|Subtitle:English (SRT)`.
- The in-player panel (Triangle) keeps its local-file rows unchanged and, for a streamed item, lists the
  server's own tracks:
  - **Audio** rows are the server's audio streams, in server order. A row is selectable only when
    `QMediaPlayer::audioTracks()` reports a switchable track at the same position; otherwise it is drawn dimmed
    with the reason ("The player reports 1 audio track(s) for this stream, so the server's track 2 cannot be
    selected."). Selecting one goes through the existing `chooseAudioTrack()`, so the backend's own read-back is
    still the authority on whether the switch happened.
  - **Subtitle** rows are listed but disabled, each with the reason: an embedded stream says "the media backend
    cannot switch stream subtitles", an external one says this build does not download subtitle streams. That is
    the same rule the local panel already follows for tracks it cannot honour.
- Evidence: `shell: playback panel opened rows= 6 subtitles= 0 audio= 1 Subtitles Off|Subtitles 1: English
  (SRT)|Audio 1: English AAC stereo|Audio 2: French AC3 5.1|Repeat: Off|Shuffle: Off` and `13-tracks.png`: the
  SRT and the French track are dimmed, "Subtitles Off" and "Audio 1" carry the marker, and the sidecar reports
  `audioTrack` 1 of `audioTrackCount` 1 - the player really only has one, which is exactly why the second row is
  disabled rather than claimed.
- Not done, and stated: no subtitle stream is downloaded or rendered (the rows say so), and no quality/bitrate
  selection was added.

### 16. 1.5 Remembered sign-in behind DPAPI, explicit opt-in

- The sign-in page now has a sixth row, **Stay Signed In**, between Password and Sign In. Its value is the
  current choice; Cross flips it. It defaults to whether a record already exists, so the page shows the truth on
  open. A fifth scripted session (`client-e.log`, after runs A-D) pinned the two states:
  `24-signin-remember-off.png` (the row present, value `Off`, before Cross) and `25-signin-remember-on.png`
  (value `On`, focused, with the six-row page and the masked password), then signed in and quit, leaving the
  record inspected below.
- Turning it **off** deletes the record immediately, not at the next sign-in. Turning it on only takes effect
  when the sign-in is accepted: `Shell::initJellyfin()`'s `authenticated` handler calls
  `JellyfinClient::saveSession()` and logs
  `remembered sign-in stored (DPAPI, this Windows user; no password kept)` or, when off,
  `stay-signed-in is off; nothing was written`.
- The record (`<data>/jellyfin/session.bin`) is `XMBJF1\n` followed by `CryptProtectData` of a compact JSON
  `{v,server,userId,token}` with a fixed application entropy string, written atomically with `QSaveFile`.
  **The password is never in it, and the plaintext token never touches the disk.** Checked on the record the
  final binary wrote: 389 bytes, magic `XMBJF1`, and no occurrence of the token, the server URL or the user name
  as plaintext.
- `crypt32` is linked from the source with `#pragma comment(lib, "crypt32.lib")` (MSVC), so `CMakeLists.txt` -
  outside this change's fence - did not have to change.
- Opening the app while signed out and a record exists for that registration calls `restoreSession()` and loads
  the library directly (`using the remembered session for <url> (DPAPI record, no password)`); the server sees no
  `AuthenticateByName`. A refused session (a `401`) forgets the record, signs the client out and opens the
  sign-in page (`the remembered session was refused; it has been forgotten, signing in again`). **Sign Out of
  Jellyfin** forgets the record as well.
- Threat model, stated plainly: DPAPI's user scope means another Windows user on the same machine, or a copy of
  the file taken to another machine or account, cannot decrypt it. It does **not** defend against a process
  running as the same user - malware that can call `CryptUnprotectData` can read the token, so this is
  protection at rest, not a defence against a compromised session. What is stored is a revocable access token,
  not the password, and signing out on the server (or here) invalidates it; the opt-in is off by default and
  turning it off deletes the file. The file lives under the app's data directory, not in the registry, and never
  contains the password.
- One more exit criterion is covered by this: with the opt-in off, no record is written at all (run D logs
  `stay-signed-in is off; nothing was written`, and `data-final` had no `session.bin` when that run ended; the
  separate record-inspection run described below created one afterwards, on purpose).

### 17. Gate

    & C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-jf1
    -> Built C:\dev\out\native-jf1\re-ps3-native.exe (2026-10-27 20:41; no warning from the files this pass touched)

    & bun work\ab-layers.ts --all --exe C:/dev/out/native-jf1/re-ps3-native.exe

| layer gate | result |
|---|---|
| 6 stored-baseline layers (frame, top background, category icon row, item label row, clock text, info bar) | **PASS 6/6** |
| canonical capture, same 6 layers | **PASS 6/6** |

The canonical capture's own numbers, for the record (PSNR | baseline | delta; SSIM | baseline | delta):

| layer | PSNR | baseline | delta | SSIM | baseline | delta | gate |
|---|---:|---:|---:|---|---:|---:|---|
| frame | 18.28 | 18.02 | +0.26 | .8288 | .8301 | -.0013 | PASS |
| top background | 20.22 | 20.24 | -0.01 | .9336 | .9344 | -.0008 | PASS |
| category icon row | 20.89 | 21.05 | -0.16 | .8966 | .8950 | +.0016 | PASS |
| item label row | 11.94 | 11.94 | +0.00 | .3909 | .3910 | -.0001 | PASS |
| clock text | 11.39 | 11.39 | +0.00 | .4105 | .4105 | +.0000 | PASS |
| info bar | 14.78 | 14.78 | +0.00 | .6117 | .6117 | +.0000 | PASS |

**The named states did not come back green in this pass, and the cause is not this change.** Two separate
things were happening in the shared tree while this section was being written:

1. `--all` cannot complete: the harness in the working tree has grown new states (`xmb-notification`,
   `xmb-volume-hud`, `video-options-panel`, `continue-watching`, `continue-watching-empty`, `subtitle-cue`,
   `jellyfin-signin`, `steam-download-page`) from a concurrent task, and the run stops at `jellyfin-signin` with
   `catalogue has no entry "custom:jellyfin" in column "tv"; rows: [services]` - its route is resolved against
   the shared discovery capture, whose data directory carries no Jellyfin registration. `work/ab-layers.ts`
   (20:42), `work/ab-layers.thresholds.json` (20:45) and five new reference PNGs (20:48-20:50) are modified in
   the tree by that task; **this pass did not edit the harness, the thresholds or any reference.**
2. The four original named states, run individually, FAIL on their full-frame region only: `steam-game-root`
   48.82 dB, the other three 50.59 dB, all at SSIM 0.9996, against the 55 dB / 0.9995 self-baseline; every
   content crop (`item column`, `rows`, `sections`) is **`inf / 1.0000`**, i.e. pixel-identical to its
   reference. The frame difference is confined to one box, `(112,247)-(1213,326)` - the category icon row -
   and is the Game icon's new inner-glass/specular material:

   - two runs of the same binary are byte-identical (`diff bbox None`), so the capture is still deterministic;
   - the integration build the references were saved from (`C:\dev\out\native-phase3\re-ps3-native.exe`,
     19:25) still reproduces `steam-game-root` at **`inf / 1.0000`**, so the reference itself is intact;
   - `icons.h` changed at 20:40:06 and `icons.cpp` at 20:47:20 (and `audit/ITEM-ICONS.md` is modified) in the
     shared tree between this pass's first gate run - which was 4/4 with every region `inf / 1.0000` at 20:33 -
     and the later runs; the band crop shows the Game icon gaining an inner highlight and rim that the
     reference does not have. Those files are outside this change's fence and were not touched here.

For the record, the 20:33 run of the same command on the previous binary did report `states: 4/4 pass` with every
region `inf / 1.0000`; the icon material landed after it. So the layer gates are green, the content of every
named state is unchanged, and the full-frame delta the states now report belongs to the concurrent
category-icon work, not to this pass. Nothing here draws in the idle Game root: the Jellyfin rows, panels and
pages are only reachable from the TV column's registered app.

One interaction the concurrent task must handle: the new `jellyfin-signin` state expects `entryCount` 5 and the
log line `jellyfin: sign-in page opened rows= Server URL|Username|Password|Sign In|Cancel focus= Username`.
Section 16 added a sixth row, `Stay Signed In`, so that expectation and any reference saved for it need to be
refreshed to `rows= Server URL|Username|Password|Stay Signed In|Sign In|Cancel` and `entryCount` 6. The harness
was not edited here.

### 18. Files touched by this pass

- `native-xmb/src/jellyfin_client.h` / `.cpp`: `JellyfinItem::primaryTag/backdropTag`; `JellyfinTrack` and
  `JellyfinPlayback::streams`; `search()`; `artwork()` with the authenticated image request, bounded decode,
  tag-keyed cache and 400-file cap; `setImageCache()`; the DPAPI store (`setSessionStore`, `saveSession`,
  `restoreSession`, `forgetSession`, `hasStoredSession`); `Operation::Search/Artwork`; `#pragma comment(lib,
  "crypt32.lib")`.
- `native-xmb/src/jellyfin_shell.cpp`: cache and session-store wiring; the in-flight resume/completion
  coherence fixes and their log lines; poster/backdrop requests and `artworkReady`; the search page helpers;
  restore-on-open and the refused-restore fallback; `jellyfinRemembered()`; ASCII detail separator.
- `native-xmb/src/shell.h`: `Action::JellyfinSearch`, `Action::JellyfinSignInRemember`;
  `Page::jellyfinSearch`; `pageBackdrop()`/`jellyfinRemembered()`; the new private state
  (`jellyfinServiceServer_`, `jellyfinRestoring_`, `signInRemember_`, `jellyfinSearchTerm_`, backdrop image/id)
  and the two search helpers.
- `native-xmb/src/shell.cpp`: the search and Remember branches in `activate()`; the search-page `More` branch;
  the Stay Signed In row and its default; the `search` route in `setJellyfinSignInField()`; the Remember state in
  the sign-in request log line; `pageKind()` `jellyfinSearch`; the streamed-track branch in
  `buildPlayerOptions()`; `entryKind()` for the search row.
- `native-xmb/audit/JELLYFIN-NATIVE.md`: this section.

Not touched: `view.cpp`, `view.h`, `main.cpp`, `settings_menu.cpp`, `preferences.cpp`, `xmbml_view.*`,
`particles.*`, `icons.cpp`, `text_renderer.*`, `subtitles.cpp/.h`, `CMakeLists.txt`, the harness and its
thresholds/references. The stub server, the runner, the captures and the logs are under
`C:\dev\out\native-jf1-evidence` and are not committed.

### 19. What remains

- **The backdrop draw** (section 13): the hunk above is the only missing piece; the fetch, cache and accessor
  are working.
- **A real Jellyfin 12 server.** This pass proves the client against a stub whose payloads match the documented
  schema; the 2026-10-26 section above is still the only real-server pass, and it predates resume, played
  state, search, tracks and DPAPI. Re-running those four scripts against a real library is the next honest step,
  and the only one that can catch a schema detail the stub and the client agree on but the server does not.
- **Subtitle streams** are listed and honest, not rendered. Rendering them means downloading the
  `DeliveryUrl`/embedded stream and feeding the existing sidecar cue renderer; that is a new slice.
- **Audio-track switching for streams** is wired but only as good as the backend's `audioTracks()` report; on
  this clip the player reports one track, so the second server stream is correctly disabled. A multi-audio
  fixture would prove a real switch.
- **A pad.** No controller is attached to this machine; the scripted runs drive the same `Shell` methods the pad
  calls (the argument is section 7's). The remembered-sign-in page, search page and track panel are ordinary
  pages and rows, so nothing here is modal or pad-inaccessible, but that is structural plus the sidecar's
  `modalWidget` `""`.
- **Quit-time stop reports** remain best effort: the process exits with a report in flight (the runs above quit
  after an explicit Back, which is why their Stopped lines are complete).
- Poster prefetch is bounded to the first 60 playable rows per page and the cache to 400 files; a very large
  library re-fetches art after eviction by design.
