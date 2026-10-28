# Workspace split for parallel agents

This project has grown to the point where one shared working tree is the main source of wasted agent time. The
evidence from the last few days is specific: six agents in one tree produced repeated build breaks in files
nobody else owned, unexplained named-state failures caused by a concurrent material change, a linker lock on the
user's running exe, and one agent's `Stop-Process` killing another's long-running capture. The fix is not fewer
agents; it is separate trees with one owner per boundary.

**Per-workspace handoff docs: [workspaces/00-INDEX.md](workspaces/00-INDEX.md)** - one document per workspace
(mission, ownership, boundaries, build/verify commands, current state, evidence index, gotchas, first tasks).

## 1. The rule that makes split work possible

**A workspace owns a set of files and the build output it produces; it never edits another workspace's files and
never builds into a shared output directory.** Where two workspaces must meet, they meet through a header whose
owner is declared, or the change is returned as a diff hunk for the owning workspace to apply.

## 2. The workspaces

| # | Workspace | Owns (write) | Reads (never writes) | Branch | Build dir | Agents |
|---|---|---|---|---|---|---|
| 1 | **core** | `src/shell.cpp`, `src/shell.h`, `src/settings_menu.cpp`, `src/preferences.*`, `src/profiles.*`, `src/catalog.cpp`, `src/content_registry.*` | everything | `ws/core` | `out/ws-core` | menu/behaviour work (P7), settings, profiles: **1 writer at a time** |
| 2 | **ui** | `src/view.cpp`, `src/view.h`, `src/text_renderer.*`, `src/font_raster.*`, `src/icons.*`, `src/dialogs.*`, `src/osk_layout.*` | core headers | `ws/ui` | `out/ws-ui` | screen layout, text, icons, dialogs, OSK: **1 writer at a time** |
| 3 | **scene** | `src/hdr.*`, `src/ribbon.*`, `src/particles.*`, `src/coldboot.*`, `src/p3t.*`, `src/rsx_program.*` | core/ui headers | `ws/scene` | `out/ws-scene` | background, wave, HDR, particles, boot, themes |
| 4 | **services** | `src/steam_*`, `src/jellyfin_*`, `src/subtitles.*`, `src/media_library.*`, `src/interaction_audio.*`, `src/host.*`, `src/pad.*`, `src/ps_watcher.*` | core/ui headers | `ws/services` | `out/ws-services` | Steam, Jellyfin, media, audio, power, pad |
| 5 | **tooling** | `work/**`, `cmake/**`, the reference set under `out/native-reference/layers/` (deliberate refresh only) | the app | `ws/tooling` | `out/ws-tooling` | harness, parity scorecard, captures, release checks: **the only workspace allowed to refresh named references** |
| 6 | **integration** | the `main` branch, `native-xmb/CMakeLists.txt` (or `cmake/*.cmake` fragments), the docs that describe state (`HANDOFF-*.md`, `PHASES-*.md`, `audit/FIRMWARE-PARITY-GAPS.md`) | all workspaces | `main` | `out/integration` | the Lead only: merges, gate, final evidence |

Two more that are not code workspaces but are worth naming:

| # | Workspace | Purpose |
|---|---|---|
| 7 | **docs** | `native-xmb/audit/*.md` evidence documents. One agent at a time; no source. Keeps the audits from being edited while their subject is moving. |
| 8 | **firmware-analysis** | read-only dev_flash/RCO/PRX analysis + `work/` dump tools. Never edits app source; produces the spec another workspace implements. This is where the "derive from the firmware, not free-hand" work lives. |

## 3. Boundary headers (who may change them)

| Header | Owner | Why |
|---|---|---|
| `src/shell.h` | core | Public action/entry/page API used by ui and services |
| `src/view.h` | ui | Render API used by core |
| service headers (`steam_*.h`, `jellyfin_*.h`, ...) | services | Their own public surface |
| `CMakeLists.txt` (or `cmake/sources-*.cmake`) | integration | Prevents two workspaces adding colliding entries |
| `work/ab-layers.ts`, `work/ab-layers.thresholds.json` | tooling | One owner or the gate silently becomes meaningless |

A change to a boundary header is announced before it lands: the owner edits it, then the dependent workspaces
rebase. The alternative - two agents editing `shell.h` at once - has already cost us two broken builds.

## 4. Git mechanics

Use `git worktree`, not copies, so every workspace shares one object database:

```powershell
# from C:\dev\xmb
git worktree add C:\dev\ws-core  -b ws/core
git worktree add C:\dev\ws-ui    -b ws/ui
git worktree add C:\dev\ws-scene -b ws/scene
git worktree add C:\dev\ws-serv  -b ws/services
git worktree add C:\dev\ws-tool  -b ws/tooling
```

Rules:

1. A feature agent commits on its workspace branch (`ws/ui`), never on `main`.
2. Integration merges one workspace at a time (`git merge --no-ff ws/ui`), runs the gate, and fixes ownership
   problems before merging the next.
3. Rebase a workspace on `main` before it starts a task, not halfway through.
4. A workspace that breaks for more than a few minutes is reset to its last good commit; nothing is "kept
   because it is nearly working".
5. Every workspace reports: branch, commit, build dir, gate result, and the files it touched. A report without
   those is not mergeable.

## 5. Build and runtime isolation

- Every workspace builds into its own `-OutputDirectory` (`out/ws-<name>`); never into `out/native-xmb`, which is
  the launcher path the user may have open.
- Never `Stop-Process re-ps3-native` globally: a capture agent elsewhere may be measuring a ten-minute run.
  Kill by pid, or better, let the script quit.
- Capture data dirs are copied per workspace (`out/ws-<name>/data-*`); no agent writes the shared seeded
  `out/native-reference/store/ui3-data`.
- Firmware (`dev_flash`) and the reference frames are read-only to every workspace except tooling.

## 6. Verification split

- The **tooling** workspace owns the gate (`work/ab-layers.ts`, `work/parity.ts`), the thresholds and the named
  references. Only it runs `--states --save-reference`, and only after the integration owner says a rendering
  change is intended.
- Every feature workspace must produce its own evidence: a capture/build log under `out/ws-<name>/`, and the
  exact command that reproduces it in its report.
- **Integration** runs the full gate on the merged tree and is the only place a "green" claim is made.

## 7. Agent capacity under the 8-agent limit

| Workspace | Max concurrent writers | Typical agent |
|---|---|---|
| core | 1 | menu behaviours / settings / profiles |
| ui | 1 (2 if one only returns hunks) | screens, text, icons, OSK, dialogs |
| scene | 1 | background/HDR/particles/themes |
| services | 2 (disjoint modules) | Steam, Jellyfin, media/audio |
| tooling | 1 | harness/parity/captures |
| firmware-analysis | 2 | read-only spec producers |
| integration | 1 | Lead |

That is 8-9 agents with no two writing the same file - which is the point. The previous arrangement allowed six
agents to write into one tree and one to break the others' builds.

## 8. Mapping the plans onto workspaces

| Plan item | Workspace |
|---|---|
| P0 parity infrastructure, P11 closure | tooling (+ integration) |
| P1 RCO object model, all firmware analyses | firmware-analysis (spec) then the owning workspace (implementation) |
| P2 geometry, P3 text, P4 icons, dialogs, OSK | ui |
| P5 background/wave/HDR/particles, P9 themes | scene |
| P6 motion traces | tooling (captures) + ui (easing) |
| P7 screens/behaviour, P8 audio, profiles, settings | core |
| Phase 1 Jellyfin, Phase 6 Steam | services |
| Phase 5 verification, Phase 7 packaging | tooling / docs |
| Phase 4 session/power | services |

## 9. What not to split (and why)

- **`view.cpp` and `shell.cpp` are each a single writer.** They are the two integration hubs; splitting them by
  feature reproduces the conflicts. Other workspaces return hunks for them (already the working pattern).
- **The harness and reference set.** Two owners means two definitions of "pass".
- **`CMakeLists.txt`.** Source lists must have one owner or workspace branches conflict on every addition.
- **The seed data dirs and the firmware.** Read-only outside tooling/firmware-analysis.

## 10. Migration checklist (one sitting)

1. Commit or park the current uncommitted work; `main` must be clean.
2. Add the worktrees above and verify each builds: `& native-xmb\build.ps1 -OutputDirectory C:\dev\out\ws-<name>`.
3. Move the harness/reference ownership to `ws/tooling`: it owns `work/` and only it refreshes named references.
4. Move the current phase agents: finish in-flight work in their old tree, land it on `main`, then new work starts
   only in a workspace branch.
5. Record the ownership table in this file and in each agent's brief; the brief names the workspace, the branch,
   the build dir, and the boundary headers it must not edit.
6. Integration merges in this order each wave: services -> scene -> ui -> core -> tooling (tooling last so the
   gate and references settle after every visual change).

## 11. Failure modes this removes (from this project's own log)

| Incident | Cause | Fixed by |
|---|---|---|
| `LNK1104` "cannot open re-ps3-native.exe" | two builds, one output path | per-workspace build dirs |
| Named states red after an unrelated change | glyph/icon change landed in the shared tree between reference save and gate run | tooling owns references; integration runs the gate |
| `icons.cpp` duplicate `ambient` / `text_renderer.cpp` undefined symbol | two agents in one tree, line drift | one unshared writer per file |
| A capture run killed mid-measurement | blanket `Stop-Process` by another agent | pid-scoped kills; no shared exe |
| A `jellyfin-signin` state asserted 5 rows after a sixth was added | state spec and feature in different trees | tooling updates states from integration, after the feature lands |
| Docs describing 272 owned titles vs 282 | docs agent read stale state | docs workspace gets state from integration only |
