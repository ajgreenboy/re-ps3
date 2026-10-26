# Logged-in Steam Friends — 2026-10-26

The Friends column now prefers the user's running, signed-in Steam client. On this machine it returns **46 real friends**, including persona states and one in-game friend, while the Web API friends endpoint returns HTTP 401. No profile privacy change is required for this working local-client path.

## Evidence and corrected diagnosis

The selected profile was verified as the user's account. With the supplied key, GetPlayerSummaries returned HTTP 200 and GetFriendList returned HTTP 401 with an empty object. The earlier error text treated every 401 as proof of a private-list setting and told the user to make the list public. That conclusion was too strong. The fallback now reports denied Web API authorization without prescribing a privacy change.

An isolated proof using Valve's public SDK 1.54 interface headers connected through SteamClient020 / SteamFriends017 and returned 46 Immediate friends with no configured AppID. The integrated helper returns the same list. It does not use the API key or read login/session tokens.

## Architecture

- Optional Windows x64 helper: src/steam_client_helper.cpp, built as xmb-steam-client.exe beside the main application.
- Loads steamclient64.dll only from the configured installed Steam directory, using restricted DLL search paths. The DLL remains external and is not redistributed.
- Uses CreateSteamPipe, ConnectToGlobalUser, ISteamUser and ISteamFriends from the public headers. Requires a logged-on user and checks its SteamID against the requested account before returning any contacts.
- Reads Immediate friends, persona names/states and currently played AppIDs. Installed-game names are resolved locally; an unknown title is labeled Steam game. Real Steam avatar downloads, richer game-name lookup and callbacks are future work.
- Short-lived helper process isolates ABI failures and blocking IPC from the Qt render thread. The parent has an eight-second deadline, a 2 MiB output cap, cancellation generations and bounded JSON parsing. Success lists are capped at 4096 contacts.
- The helper releases its user/pipe handles. It does not launch a game, configure a borrowed AppID, send messages, alter relationships, change privacy or change the Steam installation.
- The parent removes STEAM_WEB_API_KEY from the helper environment. The optional Web API fallback sends its credential in Valve's supported x-webapi-key HTTPS header, follows no redirects and never logs request URLs/errors containing a credential.

Normal refresh occurs on first content load, stale Friends entry, F5/manual refresh and once per minute while selected. Availability recognizes the helper even without a Web API key. First successful load selects a friend; later refresh preserves the selected friend by SteamID as presence sorting changes.

## Build and run

Provide an external Steamworks public-header directory:

```powershell
native-xmb/build.ps1 -SteamworksHeaders <sdk/public/steam>
# Equivalent configuration input: STEAMWORKS_HEADERS or CMake XMB_STEAMWORKS_HEADERS.
& .\out\native-xmb\re-ps3-native.exe --steam-friends-report .\out\friends.json
```

CMake remembers the configured include directory in the build output. SDK headers, downloaded research headers, Steam DLLs, the key, reports and contact screenshots remain outside Git. Without configured headers the main application still builds and can use the optional Web API fallback. XMB_STEAM_WEB_ONLY=1 explicitly selects that fallback for diagnosis.

The supported high-level SteamAPI_Init flow normally requires an application identity. This lower-level global-user connection was empirically verified on the installed Windows Steam client without assigning one; cross-version/platform behavior is not established. Keep the helper optional and isolated, and fail explicitly if the interface becomes unavailable.

## Verification

- Isolated Release build and helper link succeeded using external SDK 1.54 headers.
- Integrated report exits 0: source steam-client, 46 contacts, 12 online, one playing (live counts at capture time).
- A mismatched expected user exits 2 and returns zero contacts.
- Forced Web-only report exits 2 with an honest HTTP 401 authorization message and no instruction to make the list public.
- Native Friends replay shows 49 entries: 46 contacts plus three controls. First-load focus lands on a contact, and refresh preserves the selected contact.
- Existing Game root, Store sections and Not Installed gates pass 3/3 with pixel-identical self-reference captures (PSNR Infinity, SSIM 1). These are regression checks, not firmware-fidelity proof.
- Local evidence: out/native-reference/steam-client-proof/ (build log, reports, captures and state JSON). Earlier friends.png captured the first-load focus defect; use friends-final.png and selected-before/after-refresh.png after the correction.

Remaining: Steam exit/reconnect and account-switch runtime coverage, larger lists, avatars and richer presence. The Web API key remains needed for the separate owned-games provider; that service was not replaced by this friends connection.

## Primary references

- [ISteamClient](https://partner.steamgames.com/doc/api/ISteamClient)
- [ISteamFriends](https://partner.steamgames.com/doc/api/ISteamFriends)
- [Valve Proton SDK 1.54 public interfaces](https://github.com/ValveSoftware/Proton/tree/proton_10.0/lsteamclient/steamworks_sdk_154)
- [Web API authentication and the credential header](https://partner.steamgames.com/doc/webapi_overview/auth)
