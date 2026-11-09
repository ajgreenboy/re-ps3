# Handoff for Codex: native XMB, 2026-11-06

This is a native C++/Qt recreation of the PS3 4.93 XMB, traced from the firmware. The repo is `C:\dev\xmb`
(`ajgreenboy/XMB-Rewritten`, branch `main`), and the app lives in `native-xmb/`. Findings are written up in
`native-xmb/audit/*.md`; most recently `PAGE-MOVE.md`, `PARTICLES.md`, `SETTINGS-COVERAGE.md` and `TEXT-RENDERER.md`.

## Hard rules

- **Never commit firmware material.** That means firmware, decrypted modules (`fw493/vsh_module` stays local),
  extracted assets, captures, recordings, console identity and the VFS overlay. Commit only source, docs and
  approved screenshots.
- **Other sessions have uncommitted edits.** They are in `shell.cpp/.h`, `view.cpp/.h`, `HANDOFF-PHASES.md`,
  `ISA.md`, `PIPELINE.md`, `steam_*` and `rawg_client.*`. Commit only your own hunks:
  1. Build the file as HEAD plus your edit.
  2. `git hash-object -w --no-filters <file>`
  3. `git update-index --cacheinfo <mode>,<blob>,<path>`
- **Commit identity and push:** `git -c user.name=Al -c user.email=ajgreenboy@gmail.com commit`, then
  `git push origin main`.
- **Tooling:** Bun/TypeScript only (never npm). No Python without approval.
- **Never touch the user's RPCS3 data.** `C:\dev\rpcs3\bin\config\input_configs\global\Default.yml` must stay at
  sha256 `E624DA0D...CEEE`. Never close the user's own RPCS3 or XMB; stop only processes you started.
- **No faking.** Don't fake features or inputs, and don't fit values where tracing is possible. Mark each claim
  TRACED, measured or INFERRED.

## Build, run, gate

```powershell
$env:PATH="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH"
cd C:\dev\xmb\native-xmb; .\build.ps1 -OutputDirectory C:/dev/xmb/out/native-dim   # exits 255 even on success; look for "Built"
```

**Run.** Use `re-ps3-native.exe --dev-flash C:/dev/rpcs3/bin/dev_flash --vsh-modules C:/dev/xmb/fw493/vsh_module
--data-dir <dir> --script "<tokens>"`.
- Tokens: `LEFT/RIGHT/UP/DOWN/ENTER/ESC`, `pause=<ms>`, `grab=<png>` and `quit`.
- Settings → Game Settings is `full,LEFT×8,RIGHT,pause=800,DOWN,pause=800,ENTER`. Each further DOWN reaches the next
  Settings page.

**Gate.** `bun work/ab-layers.ts --all --exe C:/dev/xmb/out/native-dim/re-ps3-native.exe`, output lines 33..40. It
must stay at 19.58 dB, 6/6.

**Diagnostics:**
- `XMB_TEXT_COST=1` logs frames over 10 ms, with drawScene, glass-icon and glyph-miss costs and slow image loads.
- `XMB_FLOW_LOG=1` prints the icon-wind grid.
- `XMB_MOTION_LOG=1` and `XMB_INPUT_TRACE=1` log motion and input.
- `QT_MESSAGE_PATTERN='%{time process} %{message}'` adds timestamps to every line.

## RPCS3 trace rig (local, for firmware runtime evidence)

- **Executable:** `C:\dev\rpcs3-cap\rpcs3.exe`. The interpreter config is
  `C:\dev\out\padwind-2026-11-05\pctrace\capture-interp.yml`; the LLVM capture config is
  `C:\dev\out\rpcs3-xmb-cap\capture.yml`.
- **Start:** `--no-gui --config <cfg> --fullscreen C:\dev\rpcs3\bin\dev_flash\vsh\module\vsh.self`.
- **Keys:** swap the rig's own map in from `C:\dev\out\rpcs3-ref2-2026-10-28\config\Default.yml.capture` into
  `C:\dev\rpcs3-cap\config\input_configs\global\Default.yml`. Afterwards restore it from
  `C:\dev\out\padwind-2026-11-05\backup\Default.yml.orig` and log both hashes in
  `C:\dev\out\padwind-2026-11-05\logs\input-hash.txt`.
- **Plans:** `powershell -File C:\dev\out\padwind-2026-11-05\tools\refcap3.ps1 -Plan <plan> -ProcessId <pid> -Root
  C:\dev\out\padwind-2026-11-05`. Commands are `wait`, `key`, `grab`, `burst`, `cburst x,y,w,h`, `mark`. Wait until
  the guest clock passes 60 s (the `tb` field / 79.8 MHz in the trace) before sending keys.
- **Syscall/import trace (`XMB_TRACE*`):**
  - `XMB_TRACE=<file>` and `XMB_TRACE_MODULES=C:\dev\xmb\work\spurs-hle-baseline\boot493\modules.csv`.
  - `XMB_TRACE_AFTER_S=58` skips the boot, and `XMB_TRACE_MAX` raises the line cap.
- **PC watch (`XMB_PC_*`):**
  - `XMB_PC_TRACE=<file>`, `XMB_PC_WATCH=<hex>`, and `XMB_PC_WATCH_PRX=<module>` for PRX-relative addresses.
  - `XMB_PC_DUMP=reg:off:...:count;...` dumps a pointer chain on each hit.
  - `XMB_PC_BULK=<every N>;reg:off:...:bytes` with `XMB_PC_BULK_OUT=<prefix>` writes raw bulk dumps.
  - PC-watch backtraces are raw runtime addresses: rebase them with the trace's `# prx` lines.
- **Ghidra (headless, read-only):**
  - Run `C:\dev\deps\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat` with `JAVA_HOME=C:\Program Files\Eclipse
    Adoptium\jdk-25.0.4.101-hotspot`, `JAVA_TOOL_OPTIONS=-Duser.home=C:\dev\xmb\ghidra_home` and project
    `C:\dev\xmb\ghidra_work boot493/boot`.
  - Use `-process <x>.prx -noanalysis -readOnly -scriptPath C:\dev\xmb\ghidra_scripts`.
  - Scripts: `DecompileContaining`, `DecompileStringRefs`, `RefsTo`, `ListRange`. Plugin image base is 0.

## Done today (all pushed)

| commit | what |
|---|---|
| 642adda | Settings page open: the 5-frame job-chain floor (83 ms) before the page moves |
| c0be2df | Settings idle dim: 3 s timer, the other rows' text eases to 0.2 with a 30 ms stagger |
| fe9c117 | Game Settings: the PSP Remaster rows appear only when `/setting/game/pspemu3dMenu`=1, so they are dropped |
| a29492f | PS rows show the firmware's "Off"; help line 19 px, y+19, 22 px pitch |
| 860cdfb | Host snapshot off the paint thread (it caused 150 ms and 45 ms stalls on Settings pages); adds `XMB_TEXT_COST` |
| be02ef7, c2eab3a | Icon wind: per-icon NDC deltas into the 32x16 flow field, sampled by the kernel; fixes the category icon's 10 px pop |
| 5eaec7f, ec7d473, 2309fc9 | 15-page Settings sweep: printer order, `noValueOnConsole`/`fixedReadings` value tables, value elision, `%1` filled, help box 1182 px, Power Save |

## Open, in suggested order

1. **Dust on scroll.** Done: same direction and size on both (PARTICLES.md, "The dust's response to a scroll"; `work/dust-flow.ts`).
2. **Icon-wind tick sampling.** Done in 6054fdf: ticks sample the icon positions one tick late, by interpolation.
3. **Grid decay.** The writer of the flow grid's 1-per-tick decay is untraced. The native models the measured rate.
4. **Non-Settings page opens.** Closed (PAGE-MOVE "Non-Settings page opens"): Music/Photo Playlists load no PRX; only Saved Data Utility loads x3_mdimp12 for its lifetime. The native's sysconf-only open floor stands.
5. **Conditional rows.** Done: Chat's two extra rows were Accessory rows and are dropped (traced). Music's Repeat is a native-only row by design.
6. **Help-text fades.** Closed (PAGE-MOVE "help-text crossfade"): the swap is a 4-line ellipsised label2_text -> 10-line label2_text_ex, visible only for help past 4 lines; no English Settings help reaches that, so nothing to draw.
7. **The native's own Settings stall.** None left after 860cdfb; re-check with `XMB_TEXT_COST` after any host-read
   change.
8. **The "△ : Options" hint plate.** Done (FIDELITY-ROOT-BAR 6.1 update 3): traced through explore_plugin -> xmb_plugin XMB2 +0x58 -> vshcommon operation guide (1000 ms delay, 200 ms fades, 3000+200 ms life); shown on user and game-title rows (rig walk). Gaiji now drawn from vsh/resource/imagefont.bin (update 4). Open: media-file rows (unmeasured).
