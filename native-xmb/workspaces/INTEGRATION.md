# Workspace handoff: integration

**Mission**: be the single place where branches become one tree and where a "green" or "done" claim is made. The
integration owner is the Lead. Everything here is merge discipline, gate running and state documentation - not
feature code.

## Owns (write)

The `main` branch, `native-xmb/CMakeLists.txt` (or the per-workspace `cmake/sources-*.cmake` fragments once the
split is executed), and the state-of-record docs: `HANDOFF-<date>.md`, `PHASES-2026-10-27.md`,
`PARITY-PHASES.md`, `WORKSPACES.md`, `audit/FIRMWARE-PARITY-GAPS.md`, `ISA.md`, `README.md`.

## Must not do

- Write application code (`src/**`) except to fix a merge conflict in a file whose owner is not available - and
  then the fix is reported to that workspace in the handoff.
- Refresh references or thresholds (`tooling` owns those).
- Merge more than one workspace between gate runs.

## Merge protocol

1. The workspace reports: branch, commit, build dir, gate result, files touched, hunks it wants elsewhere.
2. Fetch/merge with `git merge --no-ff ws/<name>`; resolve conflicts **in the owning workspace's favour** for
   that workspace's files, and re-read before writing.
3. Build `C:\dev\out\integration` from the merged tree.
4. Run `bun work\ab-layers.ts --all --exe C:/dev/out/integration/re-ps3-native.exe`.
5. If it is red: classify it as (a) real regression, (b) intended visual change needing a reference refresh,
   or (c) harness configuration (stale exe, row-count assertion). Only (b) reaches tooling, with the evidence
   and the commit hash; (c) goes back to tooling; (a) goes back to the owning workspace.
6. Record the merge in the handoff: commit, what changed, gate numbers, and the next queue item.
7. Only then merge the next workspace. Suggested order per wave: services -> scene -> ui -> core -> tooling.

## The current tree state (2026-10-27)

- `main` is at `7e14147` (189 commits) and pushed; earlier pushes are on record.
- Several agents are finishing in the **shared tree**; their work is uncommitted or partially committed. The
  split migration checklist in `WORKSPACES.md` section 10 says: land the in-flight work first, then create the
  worktrees and start branches.
- Known uncommitted pieces at the time of writing: the phase-5 harness edits (`work/ab-layers.*`), the dialogs
  implementation (`view.*`, `shell.*`), the P0/P1/P2 parity groundwork (`work/parity.ts`, `src/rco_objects.h`),
  and analysis docs from the OSK/Settings deep dives.

## Gate responsibility

Integration is the only role that claims a green tree. It runs, on the merged build:

```powershell
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --all --exe C:/dev/out/integration/re-ps3-native.exe
```

and records the three families separately (stored pair / canonical / named states), because they fail for
different reasons and are fixed by different owners. A reference refresh is a **dated, recorded decision**, never
a merge convenience.

## What the handoff documents must say (and must not)

- Say: HEAD, commit count, push state, what ships, what the gate covers, known gaps by surface, evidence
  locations, references to the audits.
- Do not: carry over old numbers ("~47% parity", "40+ commits ahead", "272 owned titles") without re-measuring;
  do not present a fit as a trace; do not claim a surface is done because a capture exists - only because the
  gate and the evidence agree.
- Current honest headline numbers: canonical frame 18.28 dB / 0.8285 SSIM (stored-pair 18.20/0.8388); clock
  11.39/0.4105 and info bar 14.78/0.6117 under the American clock; 282 owned Steam titles, 10 installed, 275
  Not Installed rows; four named states (growing).

## Release checklist (7.5, from the product plan)

1. Gate green on the merged build, with the named states intended for the release.
2. `work/release-check.ps1` passes: credential scan, firmware/raw-data scan, doc links.
3. `git grep` for firmware dumps, keys, tokens, personal paths; `git status` clean of unintended files.
4. Docs current: handoff dated today, phases/parity register match reality, `INSTALL.md` paths correct.
5. Nothing pushed that adds firmware-derived content; tag naming agreed before tagging.

## Gotchas already paid for

- Merging while a workspace is mid-task: the workspace branch must be clean and rebased before merge.
- Two owners editing `CMakeLists.txt` collide on every source addition; keep it here.
- A merge that includes a rendering change and a harness change together cannot be classified when the gate is
  red - merge them separately.
- `out/native-xmb` is the launcher path; the integration build dir is `out/integration`.

## First tasks

1. Land the in-flight shared-tree work (dialogs pass, phase-5 harness, parity groundwork) as separate commits
   with the owning workspace credited, then run the gate and classify the named-state result.
2. Execute the workspace migration: create worktrees/branches, verify each builds to `out/ws-*`, move harness
   ownership to `wo-tooling`.
3. Fix the stale-default-exe bug with tooling, then refresh the named references for the accepted visual wave.
4. Start the split queue: services hunks (account switch, avatars, wording) -> core (hunks application) -> ui
   (OSK, dialogs, panels) -> scene (presets, particles).

## Report format

For every merge: workspace, branch, commit, what it contains, gate family results, classification of any red,
references refreshed (or not) with date, and the next queue item.
