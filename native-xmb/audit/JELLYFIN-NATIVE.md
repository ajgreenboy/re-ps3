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
