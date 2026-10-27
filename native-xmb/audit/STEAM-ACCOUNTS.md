# Steam account switcher handoff - 2026-10-26

Scope: the native XMB's account list and account switch handoff, built from the
Steam client's own local files. The XMB never asks for, reads or stores a
password, token, cookie, SSFN file or session cache, and it never signs in.

Module: `src/steam_accounts.h`, `src/steam_accounts.cpp` (new; not yet wired into
the shell or the build by this document's author, who owns no other files).

## Verified local sources on this machine

Verified by reading the files/registry values below on 2026-10-26. The account
names, persona names, SteamID64 values and userdata directory names are
deliberately not recorded in this document.

| Source | What was verified |
|---|---|
| `HKCU\Software\Valve\Steam\SteamPath` | Present; resolves to the default `C:\Program Files (x86)\Steam` (case-normalized). |
| `HKCU\Software\Valve\Steam\SteamExe` | Present; points at `steam.exe` under that root. |
| `HKCU\Software\Valve\Steam\AutoLoginUser` | Set (non-empty). Matches the account that the client records as active. |
| `HKCU\Software\Valve\Steam\RunningAppID` | 0 (no game running at verification time). |
| `HKCU\Software\Valve\Steam\ActiveProcess` | Present while the client runs: `pid`, `SteamClientDll`, `SteamClientDll64`, `Universe`, `ActiveUser`. `ActiveUser` is non-zero and equals the active account's 32-bit account id. |
| `config/loginusers.vdf` | 442 bytes, `users` block with **2 account entries**. The block keys are the SteamID64 values. |
| Per-account keys present | `AccountName`, `PersonaName`, `RememberPassword`, `WantsOfflineMode`, `SkipOfflineModeWarning`, `AutoLogin`, `timestamp` (lower-case `t` on this client). |
| Per-account keys absent here | `AllowAutoLogin`, `MostRecent` (older clients use those; the module accepts them). |
| `userdata/` | 2 directories, one per account, and both contain `config/localconfig.vdf`. That is evidence of local use, not proof of a valid session. |

Observed values, without identifiers:

- Account 1 (file order): `AutoLogin` = 1, `RememberPassword` = 1, newest
  `timestamp` = 2026-10-26 22:13 UTC, userdata present, active.
- Account 2 (file order): `AutoLogin` = 0, `RememberPassword` = 1, older
  `timestamp` = 2026-10-11 04:30 UTC, userdata present, not active.
- Active is confirmed by two independent client-owned records: the registry
  `AutoLoginUser` name and the `ActiveProcess\ActiveUser` account id both match
  account 1. The running client therefore agrees with the file.
- SteamID64 to account id is `steamId64 - 76561197960265728`; both userdata
  directories match their account ids.
- The client was running at verification time. The module's live scan returned
  the same two accounts, with account 1 flagged active.

One source of drift matters for the rest of the XMB: `steam_library.cpp`
(account selection) reads `Timestamp` (capital T) and `MostRecent`, but this
client writes lower-case `timestamp` and no `MostRecent`. With the client closed,
the registry `ActiveUser` path is unavailable and the existing selection has no
usable timestamp on this installation; the module accepts both spellings so its
own `lastLoginUnix` and fallback ordering still work. `XMB_STEAM_ID` remains the
explicit override for the Friends/owned/collections/family services.

## What the XMB can honestly display

- The locally remembered accounts only: persona name (falling back to login
  name), last recorded login time, `rememberPassword`, `autoLogin`, presence of
  local userdata, and the active marker.
- The active marker is the client's own record (active registry id, then
  `AutoLoginUser`, then the file's `AutoLogin`/`AllowAutoLogin` marker, then a
  legacy `MostRecent`, then a sole remembered account). It is explicitly not
  proof that a session is live or valid, and a stale registry value can survive a
  client exit.
- That an account has no local userdata yet; Steam creates it after the first
  signed-in session.
- The real total of remembered accounts (2 here), and that a list is empty
  because nothing is remembered (empty list, empty error) or because Steam was
  not found / the file is unreadable (empty list, explicit error).

It must never display: "signed in as X" after a handoff, fabricated presence, a
success result for an account it did not authenticate, or any password/token
field. `hasUserdata` is not a session state; the client is the only authority.

## What it must hand back to Steam

Valve exposes no supported "switch to this account" command, and the client's
sign-in window is the only authenticator. The chosen handoff is therefore a
two-step, client-owned restart:

1. `steam.exe -shutdown` - ask a running client to exit. It is the client's own
   exit request, the same string used by launchers to close it. The module
   returns it from `steamShutdownArguments()`; the caller should use it only
   while Steam is running, because a stopped client starts briefly and closes
   again when given this option.
2. `steam.exe` (no arguments) - start the client again. `steamRelaunchArguments()`
   returns an empty list on purpose: the client decides whether to auto-sign-in a
   remembered account or show its sign-in window. The user completes sign-in, or
   uses Steam's own "Change Account" UI, in the client.

`steamAccountSwitchPlan()` packages the resolved executable, both argument
lists, an `alreadyActive` flag and an honest explanation. The XMB must not pass
`-login`, an account name, a password or any token, must not use an undocumented
credential flag, and must not claim a switch occurred. The caller owns the
process start/stop sequence; the module performs no process work.

Note on `steam://` handoffs: the Steam URL scheme covers store/library/friend/
install/run navigation, not account selection or sign-in. There is no known URL
that switches the signed-in account, so this module does not fabricate one. If a
future client exposes a supported switch URL, it replaces the restart path; the
XMB must still not own authentication.

## What remains impossible without credentials

- Signing in, signing out, validating a session, or switching the active account
  programmatically. Only the Steam client can do these.
- Discovering accounts that Steam does not remember (they are not listed).
- Changing "remember password" or creating/removing accounts.
- Reading owned games without the client or a Steam Web API key, or verifying
  that a remembered account still owns this machine's titles.
- Proving the active account after a crashed/shutdown client, or during a
  pending sign-in.
- Any password/PIN/token handling. The module deliberately ignores the client's
  credential and session files (`config/config.vdf`, `ssfn*`, connect caches).

## Interaction with `XMB_STEAM_ID`

- `XMB_STEAM_ID` (environment) is the existing explicit account override for
  Friends, owned games, collections and Steam Families, and for the
  `--steam-*-report` commands. It is read once from the environment; it is not a
  preference and is not written by anything.
- This module does not read or change `XMB_STEAM_ID`. It reports what the local
  client records, so a running XMB whose `XMB_STEAM_ID` names account 2 while the
  client is signed in as account 1 shows exactly that split honestly: the account
  page marks account 1 active, and the Steam services keep using the override.
- A handoff cannot update a running process's environment. After the user signs in
  as another account, Steam-backed XMB services keep the old id until XMB is
  restarted with the new `XMB_STEAM_ID` (or the override is removed and the
  registry active user is rescanned). Integration should either re-scan/re-point
  after a handoff or tell the user plainly that Steam-backed lists need a restart.
- `XMB_STEAM_ROOT` / `--steam-root` is respected by both this module and
  `scanSteam()`; when an explicit root differs from the registry installation,
  the registry's `ActiveUser`/`AutoLoginUser` are not trusted (the module falls
  back to the file's own markers) so a different host installation is never
  presented as the active account of the chosen root.

## Integration contract and evidence

- Add `src/steam_accounts.cpp` / `.h` to the `re-ps3-native` sources (this
  document does not modify `CMakeLists.txt`).
- Call `scanSteamAccounts()` when the user opens the account page (bounded local
  read, no network, no writes); call `steamAccountSwitchPlan()` for the selected
  row; pass the plan's argument lists to the process owner after a confirmation.
  Do not persist the list; rescan instead.
- The module compiled clean with the project's toolchain (MSVC 14.44, Qt 6.12.0,
  `/W4 /permissive- /std:c++20`) and was run against this machine: 2 accounts,
  active markers as described, both userdata flags set, `steam.exe` resolved.
  Parser/fallback behavior was also checked against temporary relocated roots
  (no-userdata, no-registry, `AutoLogin` and `MostRecent` fallbacks, malformed
  content); the temporary files were deleted after the check.
- Not yet verified: in-XMB display and the actual restart handoff, since neither
  the shell nor the build is modified here. No client restart was performed on
  this machine during verification.

## Safety boundaries

- Read-only inputs: `<root>/config/loginusers.vdf` (bounded 1 MiB) and the two
  registry values (`SteamPath`/`SteamExe`/`AutoLoginUser`, `ActiveProcess`).
- Never opened: any credential/session/cache file, any userdata content, any
  network endpoint.
- Never written: the Steam installation, preferences, or any new file.
- This document records counts, key names, booleans and timestamps only; no
  account names, persona names, SteamID64 values or userdata directory names.
