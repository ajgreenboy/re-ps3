# Agent handoff: PS3 XMB on Windows (native recreation)

Written for an agent picking this repository up cold. The active project is the native C++/Qt
recreation of the PS3 4.93 XMB under `native-xmb/`. The earlier firmware-recompilation track has been
removed; this repository is native-only.

## 1. Goal

Reproduce the PS3 XMB's look, feel, navigation and local services as a native Windows application,
using the user's own local firmware assets (read-only) and modern content backends (Steam, Jellyfin,
Spotify, DLNA, Home Assistant, Weather, Tailscale) behind the console's presentation. The XMB is a
living-room HTPC front end. Behaviour and appearance should match the console; the internals are a
from-scratch native renderer and services, not an emulator.

## 2. Start here, in this order

1. `native-xmb/HANDOFF-2026-11-09.md` - current state, build/run/verify, what landed, config files,
   open work. This is the newest handoff and takes precedence over everything else.
2. `native-xmb/audit/PHASES-2026-11-07.md` - the plan of record: done work, ranked backlog (Tier 1
   HTPC product, Tier 2 product gaps, Tier 3 fidelity), parallel workstreams and their write fences.
3. `native-xmb/ISA.md` - the completion checklist. An unchecked box stays open until the behaviour
   has actually been observed; a checked box has runtime evidence.
4. `native-xmb/README.md` - build, run, controls, architecture boundaries and every service's setup.
5. `native-xmb/audit/README.md` - the audit index; `native-xmb/audit/FIDELITY-SURFACES-MAP.md` and
   `native-xmb/audit/FIDELITY-VERDICT.md` for the per-surface fidelity picture.

## 3. Build and run

From `C:\dev\xmb` in PowerShell:

```powershell
# build into out\integration (exit 255 is normal; look for "Built")
$env:PATH="C:\Program Files (x86)\Microsoft Visual Studio\Installer;$env:PATH"
& .\native-xmb\build.ps1 -OutputDirectory C:/dev/xmb/out/native-xmb

# launcher: rebuilds when stale (-Build forces), runs out\integration, session logs in out\integration\sessions
& .\run-native-xmb.ps1 -Build
```

- **Toolchain:** VS 2022 Build Tools (MSVC), bundled CMake/Ninja, Qt 6.8+ MSVC 64-bit, C++20,
  OpenGL 3.3, FreeType.
- **Assets:** read read-only from `C:\dev\rpcs3\bin\dev_flash` by default (`--dev-flash` overrides).
  Decrypted VSH modules (`fw493/vsh_module`) feed the ribbon rest tables via `--vsh-modules`; without
  them the ribbon uses its approximate fallback.
- **Before relinking:** close the running native executable (LNK1104 otherwise).
- **Gates:** `bun work/ab-layers.ts --all --exe <exe>` (6 fidelity layers + 12 named states),
  `.\work\release-check.ps1 -SkipBuild` (secrets/data/link/commit hygiene),
  `bun work/parity.ts --references-missing` (which states still owe a firmware reference frame).

## 4. Rules that are easy to break

- **Firmware stays private.** Firmware, decrypted modules, extracted assets, recordings, console
  identity and API keys never go into Git or anywhere public. Only source, docs and approved UI
  screenshots are committed. The remote is the public `origin`; still ask before pushing.
- **Use only the firmware already on this machine.** Don't download any other.
- **Other agents work here too.** Run `git status` before editing and commit only your own files.
- **Report only what you observed.** A successful build, a surviving window or a logged FPS is not a
  feature or fidelity claim. Don't invent results, and don't add formal test suites unless asked.
- **Tooling:** Bun/TypeScript for scripts (`~/.bun/bin/bun.exe`; never npm, no Python without
  approval) and C++ for application code.
- **Online features must never fake anything:** no sign-in, purchases, trophies or messages.

## 5. Current state (from the 2026-11-08 handoff; re-verify before relying on it)

Feature-complete for daily HTPC use: category navigation, pages/options/dialogs/settings/OSK/PS menu,
themes, the music player page, local media (video with sidecar/PGS/VobSub subtitles, music, photos,
playlists), Jellyfin (direct play, HLS, text and image subtitles), Spotify (PKCE sign-in, Connect
control), DLNA, Steam (library, friends, downloads, accounts, actions, artwork), users (icons,
log-out, auto-login), power (restart/sleep/shutdown in kiosk), audio output switching, System Check,
Tailscale, Wake on LAN, Home Assistant, Weather, cold-boot intro, and input from keyboard, XInput,
PlayStation pads, remotes and media keys.

Gate: `bun work/ab-layers.ts --all --exe <exe>` = 6/6 layers (frame ~19.6 dB vs the console capture)
and 12/12 named states, whole set PASS. Release check (`-SkipBuild`) PASS.

What a release still needs (ranked): the real-HTPC evidence run (`native-xmb/audit/RELEASE-CHECKLIST.md`,
84 checks, 15 known-unverified items), plus the remaining fidelity surfaces (playlist two-pane editor,
animated RAF themes, Photo Gallery, in-game XMB, OSK variants, game boot splash, auto-off guide, music
visualizer, info-bar avatar slot).

## 6. When you finish a phase

1. Update `native-xmb/HANDOFF-2026-11-09.md` (or the newest handoff) and `native-xmb/ISA.md` with what
   was observed and what remains open.
2. Commit only your own files, with author `-c user.name=Al -c user.email=<author email>`.
   No AI attribution in commits, PRs or files.
3. Keep this file's section 2 pointing at the newest handoff.
