# Workspace handoffs

One handoff per workspace defined in [../WORKSPACES.md](../WORKSPACES.md). Every doc follows the same shape:
mission, what it owns, its boundaries, how to build and verify inside it, where it is today, the evidence it
relies on, the gotchas that have already bitten this project, and its first tasks.

**As of** `7e14147` on `main` (2026-10-27 evening, 189 commits). Several agents are still finishing work in the
shared tree; each doc's "In flight" section names what is not yet landed, and a handoff should be re-read when
those land rather than trusted blindly.

| Workspace | Doc | Owns |
|---|---|---|
| core | [CORE.md](CORE.md) | `shell.*`, settings, preferences, profiles, catalogue, content registry |
| ui | [UI.md](UI.md) | `view.*`, text renderer + raster, icons, dialogs, OSK |
| scene | [SCENE.md](SCENE.md) | HDR, ribbon, particles, cold boot, `p3t`, RSX programs |
| services | [SERVICES.md](SERVICES.md) | Steam, Jellyfin, media, subtitles, audio, host, pad |
| tooling | [TOOLING.md](TOOLING.md) | `work/**` harness and parity tooling, the reference set |
| integration | [INTEGRATION.md](INTEGRATION.md) | `main`, CMake, state docs, merges, the green claim |
| docs | [DOCS.md](DOCS.md) | `audit/*.md` evidence documents |
| firmware-analysis | [FIRMWARE-ANALYSIS.md](FIRMWARE-ANALYSIS.md) | read-only firmware extraction + specs |

## The three rules that keep the split honest

1. **One writer per file.** A workspace that needs a change in another workspace's file returns a hunk with the
   file, function and intent; it does not edit it. `shell.cpp` and `view.cpp` are each single-writer by design.
2. **Build where you live.** Every workspace builds to `-OutputDirectory out/ws-<name>`; nobody builds into
   `out/native-xmb` (the launcher path the user may have open), and nobody kills an exe they did not start.
3. **Evidence, not assertion.** Every workspace reports its branch/commit, build dir, gate result and files
   touched; a claim without a capture, dump or log line is not a claim.

## Cross-cutting evidence everybody needs

- [../audit/VERIFICATION-HARNESS.md](../audit/VERIFICATION-HARNESS.md) - the gate, recipes, named states.
- [../audit/FIRMWARE-PARITY-GAPS.md](../audit/FIRMWARE-PARITY-GAPS.md) - the per-surface register and score.
- [../PARITY-PHASES.md](../PARITY-PHASES.md) - the 1:1 track (P0-P11) with dispatch queue and write fences.
- [../PHASES-2026-10-27.md](../PHASES-2026-10-27.md) - the product track (phases 1-7).
- [../HANDOFF-2026-10-27.md](../HANDOFF-2026-10-27.md) - the repo-wide handoff.
- [../INSTALL.md](../INSTALL.md) - build, run, data paths, bindings, troubleshooting.
