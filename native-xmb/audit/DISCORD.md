# Discord friends in the Friends column (2026-11-13)

The Friends column lists the signed-in Discord account's friends after the Steam rows. Selecting a friend opens
their Discord DM in the browser. This is not a firmware surface: the 4.93 XMB has no Discord.

## What it needs

- **Your own Discord application.** The app ships no client id. Each install signs in through an application the
  owner creates.
- **Public Client on.** The sign-in is Authorization Code with PKCE (S256). No client secret is sent or stored, so
  Discord must be told the app is a public client.
- **Discord's approval for `relationships.read`.** The friends list comes from `GET /users/@me/relationships`, which
  needs the `relationships.read` scope. Discord's OAuth2 docs list it as part of the Social SDK, with a link to
  request access, and warn that "Requesting them from a user without approval from Discord may cause errors or
  undocumented behavior in the OAuth2 flow"
  ([OAuth2 scopes](https://docs.discord.com/developers/topics/oauth2)). **Until Discord approves the app, expect
  sign-in or the friends request to fail.** The app says so in plain words (see below); it does not fall back to a
  smaller scope. Decision 2026-11-13: keep the scope and apply for approval.

## Setup (once)

1. Create an application at <https://discord.com/developers/applications> (New Application).
2. On the app's **OAuth2** page:
   - Copy the **Client ID** (the application id, 17 to 20 digits).
   - Under **Redirects**, add `http://127.0.0.1:8888/callback` exactly. Use the IP literal, not `localhost`, and the
     same port as `redirectPort` below.
   - Turn on **Public Client** and save. Without it Discord refuses the token exchange (`invalid_client`).
3. Copy [examples/discord.json](../examples/discord.json) to `<data>/discord.json` (the `--data-dir` folder, the
   same one that holds `spotify.json`) and put the Client ID in, **in quotes**:

   ```json
   { "clientId": "123456789012345678", "redirectPort": 8888 }
   ```

   `redirectPort` is optional (default 8888, allowed 1024 to 65535). The environment variables `DISCORD_CLIENT_ID`
   and `DISCORD_REDIRECT_PORT` override the file. A `clientSecret` key, if present, is ignored.
4. Request `relationships.read`: open the app's **Social SDK** page in the Developer Portal
   (<https://discord.com/developers/applications/select/social-sdk/getting-started>, the link Discord's scope table
   gives) and submit the access request. The form itself only renders in a signed-in browser, so its questions and
   eligibility rules are not recorded here. Discord's Social SDK pages describe it as being for games; how Discord
   treats a launcher/front-end like this one is unknown until the request is answered.
5. Restart the app. Friends > **Sign In to Discord** opens the browser on Discord's consent page.

The Friends column's **Discord Connection** row shows these steps on the TV, with the redirect URI for the
configured port.

## What each failure says

`src/discord_client.cpp` (`xmb::discord::authorizeError`, `tokenError`, `friendsError`) maps every failure to one
message. The Sign In / Open row shows its first sentence; the page a sign-in or refresh puts up shows all of it.

| Where | Discord's answer | Message points at |
|---|---|---|
| `discord.json` | missing, no env var | Not set up yet; Discord Connection has the steps |
| `discord.json` | invalid JSON, over 4 KB, unreadable | The file, compare with `examples/discord.json` |
| `discord.json` | `clientId` written as a number | Put it in quotes (a snowflake does not fit a JSON double) |
| `discord.json` | not 15 to 32 digits, or the all-zero example | Use the application id from the portal |
| `discord.json` | `redirectPort` outside 1024 to 65535 | The allowed range |
| Browser redirect | `error=access_denied` | Cancelled, sign in again |
| Browser redirect | `error=invalid_scope` | Not approved for `relationships.read`; request it on the Social SDK page |
| Browser redirect | `error=unauthorized_client` | Turn on Public Client |
| Browser redirect | nothing within five minutes | Check the redirect is registered exactly, and the scope approval (Discord may show its own error page and never redirect) |
| Token exchange | `invalid_client`, or a bare 401 | Check `clientId`, turn on Public Client |
| Token exchange | description mentions the redirect | Register `http://127.0.0.1:<port>/callback` exactly |
| Token exchange | `invalid_grant` | Code expired or reused, sign in again; on refresh, sign-in expired (and the sealed session is deleted) |
| Token exchange | `invalid_scope` | Not approved for `relationships.read` |
| Relationships | 403, or code 50001 / 50013 | Not approved for `relationships.read` |
| Relationships | 401 after one refresh | Sign-in expired (signs out) |
| Any | 429 | Wait `retry_after` seconds |
| Any | 5xx | Discord is having trouble |
| Any | no HTTP answer | Could not reach Discord, with Qt's network error |

## Sign-in and storage

- The app listens on `127.0.0.1:<port>` for one `/callback`, checks `state`, and exchanges the code with the PKCE
  verifier. A request with the wrong `state` gets 400 and does not cancel the sign-in in progress. The listener closes
  after the callback or five minutes.
- Scopes requested: `identify relationships.read`.
- The refresh token is sealed with DPAPI for the current Windows user in `<data>/discord/session.bin` (magic
  `XMBDC1`), bound to the client id. The access token stays in memory. A refused refresh signs out.
- Friends refresh when the column is entered (at most every 30 s) and every 60 s while it is shown.

## Tests and what is not verified

- `tests/discord_tests.cpp` (`-DXMB_DISCORD_BUILD_TESTS=ON`, target `xmb-discord-tests`, run in CI by
  `.github/workflows/tests.yml`) checks every message mapping above, `discord.json` parsing, `configure()` on a real
  folder, and that `examples/discord.json` parses but is refused as a placeholder.
- **Not verified against Discord:** no approved application has been tried. What Discord does for an unapproved app
  requesting `relationships.read` (error page, `invalid_scope` redirect, or a token without the scope and a 403 on
  the relationships call) is undocumented; each of those paths has a message. Discord's 4xx error bodies for the
  token endpoint follow RFC 6749 (`error`, `error_description`); the description-based redirect detection is a
  best guess.
