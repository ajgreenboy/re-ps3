# Spotify in the Music column (2026-11-07)

Music > Spotify browses the signed-in account's library and plays it on a Spotify Connect device. This is not a
firmware surface: the 4.93 XMB has no Spotify. The rows are ordinary XMB pages, like the Jellyfin service in the
TV column.

## What it can and cannot do

- **Can:** sign in through the browser, list Liked Songs and the user's playlists, list a playlist's tracks, start
  playback from any track on a Spotify Connect device, and Play/Pause/Next/Previous from the Now Playing row.
- **Cannot:** play Spotify audio inside this app. Spotify streams only to its own clients and Connect devices; the
  Web API only controls them. A track plays on the Spotify app on this PC, a phone, a speaker or a console that
  runs Spotify, whichever is active, and the XMB shows the result in Now Playing.
- **Spotify's own limits** are shown, not hidden:
  - Playback control is refused for non-Premium accounts (`PREMIUM_REQUIRED`).
  - A command with no active device fails with "No Spotify device is active...".
  - Playlists Spotify itself makes (editorial, algorithmic) return 404 to apps in development mode, and the
    page says so.

## Setup (once)

1. Create an app at <https://developer.spotify.com/dashboard>.
2. Add the redirect URI exactly as `http://127.0.0.1:8888/callback`. Spotify accepts only loopback IP literals
   for http redirects, never `localhost` ([Redirect URIs](https://developer.spotify.com/documentation/web-api/concepts/redirect_uri);
   [2025 security requirements](https://developer.spotify.com/blog/2025-02-12-increasing-the-security-requirements-for-integrating-with-spotify)).
3. Put the app's client id in `<data>/spotify.json`:
   `{"clientId": "<32 hex characters>", "redirectPort": 8888}`.
   The environment variables `SPOTIFY_CLIENT_ID` and `SPOTIFY_REDIRECT_PORT` override the file. No client secret
   is used or stored: the flow is PKCE.
4. In development mode only the users added in the dashboard (up to five) can sign in. According to a third-party
   write-up (not verified against Spotify's own pages), since February 2026 the app's owner must hold Premium
   ([endpoint51](https://www.endpoint51.com/blog/spotify-api-2026-changes/)).

Without a client id, the Spotify page shows one row, "Set Up Spotify", which carries these steps.

## Sign-in and storage

- "Sign In with Spotify" opens the browser on accounts.spotify.com's consent page (Authorization Code with PKCE,
  S256). It requests the read-library scopes and the playback-state / modify-playback-state scopes.
- The app listens on `127.0.0.1:<port>` for the redirect, checks `state`, and exchanges the code with the verifier.
  The browser tab says "Signed in to Spotify". The listener closes after one callback or five minutes.
- The refresh token is sealed with DPAPI for the current Windows user in `<data>/spotify/session.bin` (magic
  `XMBSP1`). The access token stays in memory. A rotated refresh token is resealed. A refused refresh
  (`invalid_grant`) signs out rather than retrying.
- Sign Out asks, then deletes the sealed record.

## Code

- `src/spotify_client.*`: the PKCE sign-in, the loopback listener, the token refresh (one 401 retry), and the Web
  API calls `/me`, `/me/playlists`, `/playlists/{id}/tracks` (falling back to `/items` once on 404/410),
  `/me/tracks`, `/me/player/devices`, `/me/player`, `/me/player/play|pause|next|previous`. Responses are bounded at
  4 MB and requests time out after 15 s. Paging follows `next` only on the API host, up to 20 pages / 2000 items.
  `XMB_SPOTIFY_API_BASE` / `XMB_SPOTIFY_ACCOUNTS_BASE` override the hosts for a local stub and are honoured only for
  `http://127.0.0.1`. `XMB_SPOTIFY_AUTHORIZE_FILE` writes the consent URL to a file instead of opening the browser
  (tests).
- `src/spotify_shell.cpp`: the pages. Root: Now Playing, Liked Songs, playlists, Sign Out. Track pages. "Play on",
  shown only when several devices are free and none is active. A playlist plays as its own context from the
  chosen track; Liked Songs plays a queue of the chosen track and up to 99 after it.
- `src/shell.*`: the Music column row (`spotify`, a native row after Choose Media Folder), the actions and the Now
  Playing options.

## Verification (local stub, never committed)

`C:\dev\out\native-spotify\stub.ts` stands in for accounts.spotify.com and api.spotify.com on `127.0.0.1:8766`. It
checks the PKCE verifier against the challenge and requires the Bearer token. `run.ts` plays the browser's part
by following `/authorize`'s 302 to the app's callback. Stub log of the run:

    GET /authorize client=0123...cdef method=S256 redirect=http://127.0.0.1:8888/callback scope=user-read-private ...
    POST /api/token authorization_code pkce=verified
    GET /v1/me ; GET /v1/me/playlists?limit=50 ; GET /v1/me/playlists?offset=1&limit=50
    GET /v1/playlists/pl1/tracks?limit=100   (404)  ->  GET /v1/playlists/pl1/items?limit=100
    GET /v1/me/player/devices
    PUT /v1/me/player/play?device_id=dev-phone  {"context_uri":"spotify:playlist:pl1","offset":{"uri":"spotify:track:t1"}}
    PUT /v1/me/player/pause
    GET /v1/playlists/pl2/tracks (404) -> /items (404): "Spotify does not let this app read that playlist's tracks ..."
    (second launch) POST /api/token refresh_token ok=true ; GET /v1/me ; GET /v1/me/playlists ...

The frames show the signed-in root (Now Playing, Liked Songs, Morning Mix "3 tracks - Stub Listener", Editorial
Picks, Sign Out), the track page (First Light "Stub Artist - Stub Album - 03:01", the local file dimmed), and the
second launch signed in with no browser step.

## Real account (2026-11-07, measured)

Signed in with the owner's own client id on a Premium account (redirect `http://127.0.0.1:8888/callback`):

- Sign-in by the browser, then the sealed refresh token restores it on later launches with no browser step.
- `/me/playlists` lists 99 playlists; `/me/tracks` lists Liked Songs (400).
- `GET /playlists/{id}/tracks` answers **403 Forbidden** for this development-mode app, even on the user's own
  playlist (the 2026 Web API migration). The client now asks `/playlists/{id}/items` first and falls back to
  `/tracks` on 403/404/410; `/items` returned the playlist's 14 tracks.
- Three Connect devices listed (a computer, an Echo Dot, and one that reports only a hex id, shown as "Unnamed
  speaker"); with none active the "Play on" page appears. Play on the computer started the playlist; Now
  Playing showed the track, artist, device and cover; Pause from Now Playing stopped it.
- `PUT /me/player/shuffle` sent the moment the play is accepted answered `404 NO_ACTIVE_DEVICE` (the transfer was
  still settling). It is now sent to the chosen device one second later, and a refusal only logs.

## Look

- The Music column's Spotify row and the sign-in rows wear Spotify's mark (drawn in code, no image file).
- Liked Songs is a violet-to-mint tile with a heart; playlists, tracks and Now Playing show their covers from
  Spotify's image CDN (`*.scdn.co` / `*.spotifycdn.com` over https only; 2 MB cap; cached under
  `<data>/spotify/images`; cropped square to 192 px).
- Devices get computer / phone / TV / speaker glyphs; Sign Out gets an exit glyph.
- Triangle on a playlist row: **Play** (from the first track, shuffle off), **Shuffle Play** (from a random
  track, shuffle on), **Open**.

**Not verified:** the Liked Songs queue path on a real device, and a free (non-Premium) account.
