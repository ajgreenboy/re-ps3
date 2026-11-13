# HTPC release run: the guided version

This is how to run [RELEASE-CHECKLIST.md](RELEASE-CHECKLIST.md) on the living-room HTPC against the v0.3.0 release, in
twelve sittings, with `packaging/htpc-run.ps1` doing the bookkeeping. The checklist stays the single source of each
row's action, expected result and evidence; the script reads it and walks it with you. Nothing in the script changes a
Windows setting. Every live action (sleep, restart, network, Steam, Home Assistant) is something you do by hand when a row
says so.

The development-PC pass on 2026-11-12 ([RELEASE-RUN-2026-11-12.md](RELEASE-RUN-2026-11-12.md)) already passed 19 rows in
software and found the bugs listed under "Known findings" below. This run is about the 65 rows it could not finish and the
30 rows added for v0.3.0: real pads and remotes, real accounts and servers, real games, real sleep, reboot and power-off.

## 1. What to have on hand

**Hardware**

- The HTPC on its TV, with a keyboard and mouse you can unplug (sitting 3 is controller only; DM4 needs the mouse).
- An Xbox-layout (XInput) pad, plus a DualSense over USB if you have one.
- Your real remote (MCE/IR, Flirc or Bluetooth).
- A phone to take photos and videos, on the same network as the HTPC (PR1 also uses it as the phone remote).
- Two audio outputs the HTPC can switch between (for example TV HDMI and a headset), for AU1.
- A Bluetooth pad or headset you can put into pairing mode (BL1).
- Optional: a Pulse-Eight USB-CEC adapter (CE1). Without one, CE1 is NOT RUN.
- Another machine on the LAN that you can put to sleep and that has Wake-on-LAN turned on in its BIOS and network adapter
  (WOL3).

**Accounts and services** (signed in or reachable from the HTPC)

- Jellyfin: server address and a login. The library should have one title that direct-plays, one with a text subtitle, one
  with a PGS subtitle, and one with a VobSub subtitle if you have it (JF4 to JF6).
- Spotify Premium, plus your Spotify developer app's client id for `spotify.json`. A free account too, if you have one (SP5).
- A DLNA server running on the LAN: Plex DLNA, minidlna or the Jellyfin DLNA plugin (DL1 to DL3).
- Tailscale installed and signed in on the HTPC.
- Home Assistant: a long-lived access token, a room with a light or switch, a scene you don't mind running, and a media
  player if you have one.
- Steam installed and signed in, with one game you own installed. Note its app id (the number in its store page address).
- Discord: only if Discord sign-in has shipped by the time you run this (it needs your own Discord application's client id).
  Otherwise DC1 and DC2 are NOT RUN.

**Games and apps** (for the in-game XMB rows, IG1 to IG6)

- A borderless game, an exclusive-fullscreen game, a Steam-launched game and an emulator (RPCS3 or another) registered in
  the XMB. One anti-cheat title, if you have one installed.
- One web app registered from `examples/media-apps.json` (Plex or YouTube TV).

**Media files** in the XMB library folders

- An album or folder with at least 3 tracks.
- A video of a few minutes, with a `.srt` next to it (LM4).
- A video with a PGS `.sup` next to it (LM5) and one with a VobSub `.idx` and `.sub` pair (LM6). Skip those rows if you have
  none.
- A folder of photos, and a folder with more than 10 items (N5, R5).
- A throwaway file you are happy to send to the Recycle Bin (CD1).
- A film or playlist that lasts at least 4 hours (LS2).

**Time.** Sittings 1 to 10 take 30 to 90 minutes each; sitting 11 is 8 hours of mostly waiting (two 4-hour soaks that
can run overnight); sitting 12 is about 30 minutes and ends with the PC off.

## 2. Before the first sitting

1. **Get the release.** From the GitHub release page for v0.3.0, download `re-ps3.zip` and `re-ps3.zip.sha256` into the
   same folder (your Downloads folder is where the script looks first).
   - **Path A (recommended, tests the updater):** also download `re-ps3.zip` from v0.2.0, install that first, and let the
     XMB update itself to v0.3.0 in sitting 1 (UP1). If the update fails, record UP1 as FAIL and fall back to path B.
   - **Path B:** unzip the v0.3.0 `re-ps3.zip` straight to the package folder.
2. **Install.** Unzip to a folder such as `C:\PS3\re-ps3` (the "package folder"). Put `dev_flash` somewhere on the HTPC,
   such as `C:\PS3\dev_flash`, and write `C:\PS3\re-ps3\xmb-config.json`:
   ```json
   { "devFlash": "C:\\PS3\\dev_flash", "dataDir": "C:\\PS3\\xmb-data" }
   ```
3. **Service files.** Put your `spotify.json`, `wol.json`, `homeassistant.json`, `weather.json` (and `dlna.json`,
   `discord.json` if needed) in the data folder (`C:\PS3\xmb-data`). Formats are in the README's "Network services and
   media sources" section. Don't register the autostart task yet: AS1 does it.
4. **Get the run helper.** On GitHub, open the repository's main branch and use Code > Download ZIP. From the zip, copy
   `native-xmb\packaging\htpc-run.ps1` and `native-xmb\audit\RELEASE-CHECKLIST.md` into one folder on the HTPC, for
   example `C:\PS3\run`. The script finds the checklist next to itself.
5. **Preflight.** In PowerShell:
   ```powershell
   cd C:\PS3\run
   powershell -ExecutionPolicy Bypass -File .\htpc-run.ps1 preflight -Package C:\PS3\re-ps3 -Display 1
   ```
   It lists what is ready (green), missing but optional (yellow) and missing and needed (red): the package files, the
   firmware, the app version, which screen is display 1, connected pads, the service files, Steam, Tailscale, Spotify,
   Edge, the autostart task, automatic sign-in, and whether GitHub is reachable for the updater. It remembers
   `-Package` and the evidence folder (`C:\xmb-evidence\<date>-htpc`), so later commands don't need them.

## 3. Running a sitting

```powershell
powershell -ExecutionPolicy Bypass -File .\htpc-run.ps1 run -Sitting 1
```

For each row the script shows what to do, what correct looks like and what evidence to keep. You do it, press Enter, and
the script then:

- runs the row's automatic check, if it has one (hash, version, scheduled task, automatic sign-in, Steam playtime from
  `localconfig.vdf`, Tailscale state, Wake-on-LAN reachability, Home Assistant token sealing, boot timings, wake time,
  watchdog log, soak numbers);
- copies the XMB log lines written while you did the row that match it (remote and pad decisions, `ingame:`, `desktop:`,
  `dry run`, ...), scrubbed, into `logs\<ID>.txt`;
- asks for the result (p = PASS, f = FAIL, a = PARTIAL, n = NOT RUN) and a note, and saves it to `results.csv`.

`s` leaves a row for later and `q` stops. Running `run` again picks up the rows with no result; `run -Only R7,R9 -Redo`
repeats rows. `status` shows how far each sitting is.

**Photos and videos:** take them on the phone, then copy them into the evidence folder's `photos\` named after the row
(`B1.jpg`, `N1-pass.mp4`, `IG1-borderless.jpg`). The report links every file whose name starts with the row ID.

**Privacy:** no tokens, passwords or account pages in photos. Logs are scrubbed with the same rules as
`work/scrub-log.ts`, but the scrub cannot know your media paths or device names, so read `logs\` before sharing it.
`raw\` holds unscrubbed copies and stays on the HTPC.

**Starting the XMB the way a sitting needs:** `htpc-run.ps1 launch` quits the running XMB (with `--quit`, so its
watchdog stops too) and starts it again under the watchdog, with the app log at `<data>\logs\xmb.log`. Test switches go
in `-Set`: `launch -Set XMB_INPUT_TRACE=1`. `-NoIntro`, `-FirstRun` and `-Windowed` start the exe directly instead.

## 4. The sittings

| # | Sitting | Rows | Start the XMB with | Notes |
|---|---|---|---|---|
| 1 | Install, version and first start | V1, FR1, UP1, UP2, V2, UP3 | Path A: v0.2.0 under the watchdog. Path B: `launch -FirstRun` | FR1 is the first start on an empty data folder. Pass `-Zip <path>` to `run` if the zip is not in Downloads. |
| 2 | Boot and kiosk | B1-B3, AS1, AS2 | `launch`, then `launch -NoIntro` for B3 | AS1: turn on Start at Sign-in from the XMB (or `install-autostart.ps1 -Display 1 -Shortcuts`). AS2: turn on Windows automatic sign-in yourself first (packaging/README.md). |
| 3 | Controller only | N1-N8 | `launch -Set XMB_INPUT_TRACE=1` | Unplug keyboard and mouse. N7: put another window in front first (plug the mouse back in for that, then unplug it). |
| 4 | Remote and media keys | R1-R9 | `launch -Set XMB_INPUT_TRACE=1` | R7 changes the Windows volume. R9: close every other media app first. |
| 5 | Local media and files | LM1-LM7, CD1-CD3 | as is | CD2 on a junction is a known finding (below). |
| 6 | Settings, users and dry runs | PW1, AU2, AU1, SC1, SC2, US1, US2 | `launch -Set XMB_POWER_DRY_RUN=1,XMB_AUDIO_DRY_RUN=1` for PW1 and AU2, then plain `launch` | PW1: answer Yes to Sleep and Restart; with the dry run nothing happens. SC2 applies real fixes, so read each one first. |
| 7 | Media accounts | JF1-JF7, SP1-SP5, DL1-DL3, DC1, DC2 | as is | SP5 and DC1/DC2 are NOT RUN unless you have a free Spotify account and Discord sign-in has shipped. |
| 8 | Home network | TS1-TS4, WOL1-WOL3, HA1-HA5, WE1-WE3 | as is | TS4 drops the network for a moment. WOL3 and HA3 act on real devices. |
| 9 | Steam, games and apps | ST1-ST8, IG1-IG6 | as is | ST1 asks for the app id and reads the playtime; ST6 reads it again. ST8: cancel both Steam dialogs. Run IG1-IG4 once per title type and note each in the run notes. |
| 10 | Desktop Mode, display and accessories | DM1-DM4, AS3, DP1, DP2, CE1, BL1, WF1, PR1, DM5 | as is; `launch -Set XMB_PHONE_REMOTE_LAN=1` for PR1 | DM4 shows a UAC prompt; accept it with the mouse. DM5 (Exit XMB) stops the watchdog, so it is last. |
| 11 | Long session | LS1-LS3 | `launch`, then `soak -Label LS1` in a second PowerShell window; later `soak -Label LS2` during playback | Each soak samples memory, handles and CPU every minute for 4 hours. Mark LS1/LS2 in `run` after the soak finishes; the script summarises the CSV. |
| 12 | Recovery and power (last) | RC1, RC2, PW5, PW2, PW3, BT2, BT1, PW4 | `launch` for RC1; `launch -Windowed` for PW5 | After PW3 and BT2 the PC restarts: open PowerShell again and `run -Sitting 12` picks up where it stopped. BT1 needs a full shut down and a stopwatch. PW4 turns the PC off, so record it from the phone afterwards with `run -Only PW4` on the next start. |

## 5. Known findings to expect

From the 2026-11-12 development-PC run. Record what you see; these are not new bugs.

- **WE1/WE3:** with no `units` key in `weather.json` the app shows Celsius, while the docs say Fahrenheit is the default.
  Put `"units": "imperial"` or `"metric"` in explicitly and note which.
- **CD2:** Delete follows a directory junction out of a library folder. The checklist case (a file outside every library)
  cannot be reached from the UI otherwise.
- **G12 / LS3:** every graceful exit logs 18 OpenGL texture warnings. They are known; count anything else.
- **System Information** may name Windows 11 as Windows 10 (Qt's product name).
- **N5/R5:** only the remote Channel keys write a `selection` log line; a pad L1/R1 writes `source=pad action=l1`.

## 6. After the run

```powershell
powershell -ExecutionPolicy Bypass -File .\htpc-run.ps1 collect   # scrubbed logs into logs\, raw copies into raw\
powershell -ExecutionPolicy Bypass -File .\htpc-run.ps1 report    # RELEASE-RUN-<date>-htpc.md in the evidence folder
```

The report has the same shape as [RELEASE-RUN-2026-11-12.md](RELEASE-RUN-2026-11-12.md): counts, one table row per
checklist row (result, your note, the automatic evidence and the linked photos) and the FAIL and PARTIAL causes. Read it,
then share the report, `results.csv`, `logs\` and the photos you are happy to publish. It goes into `audit/` as the HTPC
evidence run, and every FAIL becomes a fix before the next release.
