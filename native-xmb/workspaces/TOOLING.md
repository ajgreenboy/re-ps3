# Workspace handoff: tooling

**Mission**: own the verification apparatus - the layer gate, the parity scorecard, the reference captures and
the release checks. Tooling is the only workspace allowed to say "this is green" and the only one allowed to
refresh a baseline. If tooling drifts, every other workspace's claim becomes meaningless.

## Owns (write)

`work/**` (harness and scorecard scripts, thresholds, reference JSONs, dump tools), the reference set under
`out/native-reference/layers/` (deliberate refresh only), `native-xmb/audit/VERIFICATION-HARNESS.md`, and
`native-xmb/audit/PARITY-SCORECARD.md` when it lands.

## Must not edit

Application source (any `src/**`), `CMakeLists.txt`, other audit docs (except the two above).

## The gate, as it stands

```powershell
# from C:\dev\xmb
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --all
#   --exe <path>            score a specific build (the default came from a session output dir and rotted;
#                           prefer passing a build you just made)
#   --capture               canonical capture only
#   --states                named states only
#   --states --save-reference   deliberately refresh the named self-baselines
#   --list-states, --state <name>, --ref <path>
```

Gate families: (1) stored native-vs-RPCS3 layer pair, (2) canonical firmware capture with pinned clocks, (3)
named Steam/XMB states with dump assertions and self-baselines. Exit 0 pass, 1 metric gate, 2 assertion/runtime.

**Known configuration bug (fix first)**: `work/ab-layers.thresholds.json` still names a 2026-10-26 build as the
default `capture.exe`. Against it, `--all` fails the named states (PSNR ~29-34) even though the source is fine.
Two fixes are wanted: point the default at a path a documented rebuild produces, and make the tool **exit 2 with
"stale build: exe older than newest source file, rebuild with <command>"** instead of four unexplained pixel
failures. `--exe` overrides must keep working.

## The parity track (P0, in flight)

`native-xmb/PARITY-PHASES.md` P0 asks for: a state catalogue generated from the firmware XMBML, a capture matrix
(state x theme x preset x resolution), an RPCS3 reference contract, and `work/parity.ts` scorecard
(`--catalogue`, `--capture`, `--score`, `--regression`, `--list`, `--references-missing`) writing a JSON +
markdown scorecard with Tier A/B/C status per screen. It must not overwrite `work/ab-layers.*`.

The P0 philosophy: a screen is "1:1" only when **Tier A** (RCO-derived dump equality), **Tier B** (measured score
against an RPCS3 frame with a stated budget) and **Tier C** (scripted input trace) all pass. Registration matters
more than numbers.

## Build and environment

- Build whatever you score into `C:\dev\out\ws-tooling` (or a per-task dir); never into `out/native-xmb`.
- Captures use pinned `--render-clock`, `--background-clock`, `--ui-clock`, `--focus-time`, a scratch data dir,
  `XMB_STEAM_ARTWORK=0` where relevant, and `--script` tokens.
- RPCS3 (the real XMB) capture rig: procedure in `HANDOFF-PHASES.md` ("Capturing the real XMB in RPCS3").
  Only one agent may drive it; the global input config must be backed up, hash-verified
  (`E624DA0D66A8E971173C39A69A1DC2C89FFE146D57BF2A03C2C8CFACB459CEEE`) and restored. Do not stop the user's
  RPCS3 instances.
- References and captures live under `out/` (gitignored). Nothing firmware-derived is committed.

## Current state (2026-10-27)

- Gate: layers 6/6 and canonical 6/6 pass on a current build; named states are **red by design** right now
  because the icon material changed (glass path) after the references were saved. They need one deliberate
  `--states --save-reference` once the visual wave settles.
- A `jellyfin-signin` state was added and is currently stale in two ways: its route resolves against an unseeded
  data dir (needs a Jellyfin registration seeded), and its asserted row count (5) is now 6 because DPAPI added
  "Stay Signed In". Fix both when refreshing.
- New states being added by the phase-5 verification agent: options panel, notification, volume HUD, subtitle
  cue, Continue Watching (populated/empty), Jellyfin sign-in, download page. Some depend on script tokens
  (`TRIANGLE`, `NOTIFY=`, `VOLSET=`, `pause=`, `F10`) and on fixture media/sidecars.
- The canonical clock/info-bar baselines are American-format numbers (11.39/0.4105, 14.78/0.6117); the stored
  RPCS3 reference frame is DD/MM 24-hour, so the two clock layers are a content difference by construction.
- Traced measurements recorded in the audits and usable as assertions: page move 18 frames / ~300 ms mode 4,
  open-page recession 189-190.5 px, row-y -250/-260, page shift 262.

## Gotchas already paid for

- Two owners of the gate = two definitions of pass. One writer (this workspace) at a time.
- A blanket `Stop-Process re-ps3-native` kills other agents' long captures.
- A stale default exe produces pixel failures that look like source regressions (see above).
- `--all` is not a test suite for new features; new surfaces need their own state + dump assertion + regions.
- Do not refresh a reference to "make it green" for a change that was not reviewed - that hides regressions.

## First tasks

1. Fix the stale-default-exe bug plus the stale-build guard (exit 2 with a rebuild instruction); document it.
2. Refresh the named references once the current visual wave (glass icons, dialogs, etc.) is accepted; update the
   `jellyfin-signin` row count and seed its data dir.
3. Land the phase-5 states (options/notification/volume/subtitles/Continue Watching/sign-in/download) with routes,
   assertions and region crops; measure pacing (swap intervals) and the startup budget.
4. Build P0: `work/parity.ts`, the catalogue from the firmware XMBML, the reference contract, and a first
   honest scorecard (Tier B only where a reference exists).
5. Add the RPCS3 capture matrix recipe and `--references-missing` reporting; do not run RPCS3 while another
   agent owns the rig.

## Report format

The exact command run, build dir and exe mtime, per-family results (layers/canonical/states), what changed in
thresholds or references and why (with the date), and anything that failed with its measured number.
