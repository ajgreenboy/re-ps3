# re-ps3 — a native PS3 XMB recreation for Windows

A native C++20 / OpenGL recreation of the PlayStation 3 **4.93 XMB**, built from the firmware's own
assets and matched against console captures, intended as a living-room HTPC front end. It ships two
programs from one CMake project:

| Program | What it is | Docs |
|---|---|---|
| **`re-ps3-native.exe`** | the XMB itself — category bar, item columns, pages, options menus, dialogs, settings, media panels, the on-screen keyboard, the PS menu, themes, the cold-boot intro and transitions | [native-xmb/README.md](native-xmb/README.md), [INSTALL.md](native-xmb/INSTALL.md) |
| **`xmb-manager.exe`** | a companion tool that detects emulators and imports ROMs/ISOs/game folders as virtual PS3-style packages into the XMB's library | [MANAGER.md](native-xmb/MANAGER.md) |

The manager writes the same `apps.json` registry the XMB reads, so an imported game appears as a
Game-column tile and launches through its mapped emulator. Nothing is a firmware replacement and no
Sony asset ships with this repository — the app reads your local `dev_flash` read-only.

## What it does

- **The XMB shell, end to end:** category navigation, a submenu page stack with selection restore,
  the item options menu, message boxes, settings pages, the PS system menu, themes (`.p3t`), the
  firmware's 4.93 cold-boot intro and health warning, and the traced interaction sounds.
- **Local media:** video with sidecar subtitles (`.srt`/`.vtt` and PGS/VobSub image subtitles) and
  resume/Continue Watching, a full-screen music player page, photo viewing/slideshow/zoom/rotate, and
  playlists (create, add, add-all, reorder, delete).
- **Real content backends** behind the console's presentation:
  - **Jellyfin** — library pages, direct play and HLS, streaming-quality ceiling, text and image
    subtitles, in-XMB sign-in with a remembered (DPAPI-sealed) session.
  - **Steam** — installed titles inline as ICON0 tiles (most recently played first), collections,
    the store, play activity, downloads, accounts, friends, and `steam://` actions.
  - **Spotify** (PKCE sign-in, Connect control), **DLNA/UPnP** discovery, **RAWG** artwork.
- **Home & network:** Tailscale, Wake on LAN, Home Assistant scenes/entities, and Weather — each a
  native page, not a browser shortcut.
- **System:** real Restart / Sleep / Shut Down (shutdown only in `--kiosk`), audio output switching,
  a read-only System Check, and user management (create/rename/delete, the firmware's 27 icons plus
  "from a file", Log Out, Automatic Login).
- **Input:** keyboard, XInput pads 0–3, DualShock 3/4 and DualSense over HID, and IR/HID remotes plus
  media keys (`WM_APPCOMMAND`). One press is one action; the PS/Guide button brings the XMB forward
  and a hold opens the system menu.

## Status

Buildable, gated, and measured — not "finished". The current snapshot:

| Measure | Result | Detail |
|---|---|---|
| Build | clean, reproducible (MSVC 2022 + Qt 6 + Ninja) | [INSTALL.md](native-xmb/INSTALL.md) §2 |
| Canonical render gate | **6/6 layers PASS** vs the matched console frame (frame ≈ 19.5 dB) | `work/ab-layers.ts --all` |
| Regression states | **12/12 PASS** (deterministic self-baselines) | same |
| Release hygiene | **PASS** (no secrets, no firmware data, links + cited commits resolve) | `work/release-check.ps1` |
| Feature breadth | feature-complete for daily HTPC use | [HANDOFF-2026-11-08.md](native-xmb/HANDOFF-2026-11-08.md) |
| Fidelity vs the console | partial and measured per surface | [FIDELITY-VERDICT.md](native-xmb/audit/FIDELITY-VERDICT.md), [FIDELITY-SURFACES-MAP.md](native-xmb/audit/FIDELITY-SURFACES-MAP.md) |

What still needs work is release plumbing and the remaining fidelity surfaces, not core features: the
on-device evidence run ([RELEASE-CHECKLIST.md](native-xmb/audit/RELEASE-CHECKLIST.md)), and fidelity
items like the playlist two-pane editor, dynamic (RAF) themes, Photo Gallery, in-game XMB, OSK
variants, the game boot splash, the music visualizer and layout-table tracing. The plan of record is
[PHASES-2026-11-07.md](native-xmb/audit/PHASES-2026-11-07.md).

## Build and run

Requirements: Windows 10/11 x64, Visual Studio 2022 with the C++ workload, a Qt 6 MSVC kit (6.8+),
and a local PS3 4.93 `dev_flash` directory.

```powershell
# build both programs (default output out\integration)
& .\native-xmb\build.ps1

# run the XMB (launcher resolves dev_flash / data-dir / vsh-modules for you)
& .\run-native-xmb.ps1

# run the companion manager
& .\out\integration\xmb-manager.exe
```

`run-native-xmb.ps1` defaults `--dev-flash` to a sibling `rpcs3\bin\dev_flash` and passes
`--vsh-modules` from `fw493\vsh_module` (decrypted `*.prx`) for the ribbon's rest tables; without
them the ribbon uses its approximate fallback. Full command-line reference, data-folder layout and
troubleshooting: [INSTALL.md](native-xmb/INSTALL.md).

## Packaging for the TV

`native-xmb/packaging/` turns a build into a self-contained folder for the living-room PC and sets up
unattended operation:

```powershell
& .\native-xmb\packaging\build-package.ps1    # -> dist\re-ps3  (add -Zip for an archive)
```

The package holds the executables, the deployed Qt runtime, and three scripts — `run-kiosk.ps1`
(the fullscreen launcher), `watchdog.ps1` (restarts the XMB after a crash), and
`install-autostart.ps1` (registers a logon scheduled task that runs the watchdog). Firmware is never
copied: write an `xmb-config.json` on the target with the `devFlash` / `dataDir` / `vshModules` paths.
Details and the auto-logon step: [packaging/README.md](native-xmb/packaging/README.md).

## Repository layout

| Path | Contents |
|---|---|
| `native-xmb/` | the application: `src/`, `manager/`, `packaging/`, `audit/` (fidelity + firmware audits), `CMakeLists.txt`, current documents |
| `native-xmb/archive/` | superseded dated handoffs and phase plans |
| `work/` | verification tools (`ab-layers`, `parity`, transition harness, release check) and fidelity probes |
| `out/`, `dist/` | local build, capture and package output (gitignored) |
| `fw493/vsh_module/`, `rea/`, `ghidra_scripts/` | decrypted VSH modules and RE analysis assets the fidelity work reads (local only, gitignored) |
| `run-native-xmb.ps1` | the launcher, at the root because it resolves its own paths |

## Verification

The project measures itself rather than trusting screenshots:

- `bun work/ab-layers.ts --all --exe <exe>` — the canonical render gate (six content-neutral layers +
  a text-presence guard) and the twelve named-state regression references.
- `bun work/parity.ts` — the matched-fidelity corpus (Tier A behavioural oracles, Tier B region
  budgets, Tier C traces).
- `bun work/transition-compare.ts` — frame-by-frame console-vs-native transition verdicts.
- `.\work\release-check.ps1` — the credential, firmware-data, dump, broken-link and dead-commit gate.

## Documentation

- [Application README](native-xmb/README.md) — architecture, build, controls, service setup.
- [Active handoff](native-xmb/HANDOFF-2026-11-08.md) — current state, what landed, open work.
- [Plan of record](native-xmb/audit/PHASES-2026-11-07.md) · [ISA](native-xmb/ISA.md) · [audit index](native-xmb/audit/README.md).
- [Install, build and run](native-xmb/INSTALL.md) · [Manager](native-xmb/MANAGER.md) · [packaging](native-xmb/packaging/README.md).

## Provenance and honesty

Every measurement is labelled by provenance throughout the audits: **traced** (from a console frame,
RCO object, MNU table or decoded program), **fit** (a native approximation with its removal
criterion), or **unprovable** (with the artifact that would settle it). The repository holds source,
docs and approved UI screenshots only — firmware, decrypted/derived assets, analysis databases,
console identities, API keys and build outputs stay local and gitignored.

Online services never fabricate anything: no sign-in, purchase, trophy or message is faked, and an
encrypted retail `.pkg` is refused with an explanation rather than emulated. No redistribution
license is assigned by this upload; see the dependency licenses for third-party components.
