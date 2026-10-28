# Workspace handoff: firmware-analysis

**Mission**: extract the console's own design from firmware so implementation workspaces rebuild it instead of
re-inventing it. This is the workspace that answers "what does the firmware actually do?" with object trees,
offsets, constants and measurements - and it never edits application code.

## Owns (write)

New documents under `native-xmb/audit/` named for their subject (`*-FIRMWARE.md`, e.g.
`DIALOGS-FIRMWARE.md`, `ITEM-MENU-INFO-FIRMWARE.md`, `MEDIA-PANELS-FIRMWARE.md`), and dump/decoder helpers under
`work/` (tracked `.ts`/`.ps1` only; raw dumps stay gitignored under `work/` or `out/`).

## Must not edit

Any `src/**` file, `CMakeLists.txt`, the harness (`work/ab-layers.*`), the reference set, or another workspace's
audit document. If an implementation is needed, the spec goes in the doc and the hunk goes to the owner.

## The firmware toolchain available

| Tool | What it does |
|---|---|
| `work/rco-tree-dump.ts` | object trees from the pre-digested `out/native-reference/rco-behavior/resources.json` (122 containers / 8,070 objects / 0 errors) |
| `work/rco-table-dump.ts` | one RCO's image table with real GIM dimensions, and its English string table |
| `work/media-rco-dump.ts` | RCO object tree with resolved image/text refs and real texture sizes (any container) |
| `work/prx-va.ts`, `work/prx-read.ts`, `work/elf-const.ts` | PRX/ELF virtual-address to file offset, float/u32 readers for decrypted modules |
| `work/rco-obj-probe.ts`, `work/rco-ref-probe.ts`, `work/p2-root-geometry.ts` | focused probes written during the parity work |
| `re-ps3-native --rco-images <rco@file>`, `--rco-text`, `--p3t-dump`, `--decode-fp`, `--dump-cg` | the app's own read-only dumpers |
| decompiles | `out/native-reference/*.c` (explore_plugin_all.c, xmb_plugin_all.c, paf-range.c, phase3-system-layout.c, ...) produced with Ghidra headless against the existing `vsh493w` project - do not run a new analysis unless needed |

Ground rules: firmware (`dev_flash`), decrypted modules and RCO dumps are **read-only and never committed**;
`dev_flash` path is the user's (`C:\dev\rpcs3\bin\dev_flash`); Ghidra headless can be used against the existing
project with `-noanalysis` (it may save the project - disclose that); RPCS3 captures belong to the agent that
owns the rig at that moment.

## The pattern that produced the last three analyses

1. Find which container declares the surface (search the 122 RCO object trees for the page/plane/group names;
   check `resources.json` first).
2. Dump the object tree and every image/text reference with real sizes; record offsets.
3. Find the code that builds/lays it out (decompile the plugin PRX range), and pull the numeric constants from
   `.data` with the ELF reader.
4. Measure what the object data does not give: use an existing RPCS3 capture or the stored frames, and mark
   anything uncaptured as **spec-only** with the exact capture needed.
5. Write the native fix list: file/function, what to read at runtime instead of hardcoding, and what to delete.
6. State the contradictions with current native design plainly (last passes found: no dialog RCO at all; no
   "Close" string; the volume HUD invents a plate/caption/percentage; the info bar uses the wrong texture).

## Current state (2026-10-27)

Delivered recently (all committed):

- **Dialogs**: no dialog RCO; `msgdialog_plugin.rco` has two objects and no `mbox_*` exists in any container; the
  box, plate (flat 12x12, no nine-slice), two bars, hint row and fades (100/200/1000) are built by `vsh.elf`;
  measured inline and titled-page layouts; full string map; 13 native fixes.
- **Item options menu + information panel**: the options menu is `explore_plugin`'s `expage_menu` (BG0/BG1
  plates, runtime-installed row strings, no scroll object, 100-unit open, confirmations are separate pages);
  labels come from the RCO text table and there is **no Close string**; the eject label is "Remove Disc". The
  info panel is `xmb_plugin_normal page_xmb_indicator` with a 9-slice `tex_indi_bg` (interior black alpha 13),
  a drawn 32 px analog clock and five 32×32 slots - no trophy/download/What's New objects; measured 591×58.
- **Media panels + volume**: video panel is a 25-button 13×3 matrix over a time page; the music panel exists
  twice in firmware and not at all natively; the volume display is a paf `PhLevelMeter` of 20×40
  `tex_default_lvmeter_on/_off/_focus` segments with a 150 ms fade, **no caption, percentage, plate or border**;
  photo viewer has a 20-button panel and 1920×1080 screen planes.

In flight: the **OSK** deep dive (layout, keys, entry field, caret, pages, animation, RPCS3 reference captures)
and the **Settings screens** deep dive (16 pages, choice/apply flows, strings).

## Known untraceable / unattributed (be honest about these)

- Dialog box rectangle, options-menu row pitch/font/panel box: laid out by paf (`PhMenuList`) in `vsh.elf` outside
  the local decompile; the audits name the exact capture or decompile range that would close each.
- Volume meter screen rect/segment count (page geometry zero, layout in `vsh.elf`).
- Video/photo panel fade timings (players not decompiled locally; no animations declared in their RCOs).
- Particles' emission volume (no emitter asset; program state is physics) - a documented fit.
- Clock writer's per-minute tick inside `vshcommon` export `0x33FB58` (outside the local decompile).
- 186 px faint pitch was superseded: measured 190.5 px.
- Dynamic theme RAF scenes: parsed but a new subsystem to render; verdict recorded, not a slice.

## Gotchas already paid for

- `msgdialog_plugin.rco` proving empty is a *negative* result worth writing down - absence of a container is
  evidence too.
- RCO `size=10` text does not map to pixels without a capture; say so rather than guessing.
- Do not run RPCS3 while another agent owns the rig; the global input config is shared and hash-verified.
- A decompile or dump saved into the repo is a leak; everything firmware-derived stays in `out/` or `work/`
  (gitignored).
- Ghidra may save the project it opens - disclose it in the report.

## First tasks

1. Finish the **OSK** analysis (the user's example) and deliver: object tree, full layout table, key textures,
   entry field/caret spec, pages, animation, RPCS3 reference set, runtime-read implementation spec.
2. Finish the **Settings screens** analysis (16 pages, choice/apply flows, strings, geometry).
3. Close the named gaps that have a known recipe: the six captures listed in `DIALOGS-FIRMWARE.md` D3, the
   video/photo panel decompile ranges, the volume meter rect, the options-menu Triangle capture set.
4. Feed each spec to the owning workspace (ui/core/services) and update the register with what became traced.

## Report format

The subject, the container/view/code sources with offsets, the measured values, the native fix list with
runtime-read sources, what is spec-only and the exact capture/decompile that would close it, and the files
written (doc + any `work/` tool).
