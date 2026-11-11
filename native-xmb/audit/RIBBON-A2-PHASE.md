# Ribbon phase recheck — 2026-11-11

Base: clean `a65d2b3`, built separately in `out/a2-head-build` from the detached
`out/a2-head` worktree. The starting checkout already contained an uncommitted
0.7 emissive-gain patch and `RIBBON-A2.md`; those are not clean HEAD and are not
authored by this pass.

## Fresh baseline

Console: `C:/dev/out/rpcs3-allgraphics-2026-10-28/frames/col-game-r00.png`.
Native: `out/ribbon-a2/head-fresh.png`, with the root-bar recipe (1920x1080,
render clock 30, background 2026-10-28 10:00, UI 21:40, focus 2000, no intro),
an empty Steam directory and a scratch data directory. No `--vsh-modules`.

`bun work/ribbon-crest.ts <console> <native>` uses Rec.709 luma, the median of
nine horizontal pixels (clamped at the frame edge), and the maximum over
y330..859. It rejects tiny particle glints, not broad particles; it is a
frame-space proxy, not an isolated ribbon measurement. Maxima can switch folds.

| x | Console y | HEAD y | Delta | Console peak | HEAD peak |
|---:|---:|---:|---:|---:|---:|
| 0 | 555 | 473 | -82 | 114.86 | 137.72 |
| 300 | 371 | 576 | +205 | 102.43 | 113.86 |
| 400 | 361 | 444 | +83 | 104.43 | 128.72 |
| 1520 | 635 | 429 | -206 | 105.07 | 162.16 |
| 1700 | 648 | 443 | -205 | 106.36 | 100.58 |
| 1900 | 653 | 439 | -214 | 118.07 | 132.93 |

Fresh clean-HEAD ribbon crop `0,330,1920,450`: **17.09 dB / .7309**.
Fresh clean-HEAD whole frame: **19.73 dB / .8490**.

## Trace boundary

The local extracted `lines/override/day/LINE1.mnu` supplies STATE=1 and the
cloth/shader preset, not the random live state of this console still.
`custom_render_all.c`, FUN_0004e624, loads preset-1 positions at VA 0x97c18 and
velocities at 0x98d04. Although it initially calls FUN_0004b6a4 with 0x92da8,
it calls that setter again with 0x92db0 at the end: quoting the first call alone
does not establish the final wave clock. Reading the local ELF confirms that
0x92db0 is **0.0**, so the existing zero initializer is correct. The optional module reader
already consumes the actual tables. Neither these firmware bytes nor a copied
vertex buffer belongs in the repository.

The existing camera, LINE1 transform, FFD and spline program remain untouched.
Any fallback warm-up selected against this frame is a **fit**, not a trace of
the console still's phase. A smaller score or six-column RMS is not evidence
that the requested few-pixel bound is met.

## Bounded fallback improvement (partial, not R2/R3 closure)

Select **41,550 day-preset steps**, retaining the existing loop start, spring
solver, random generator, FFD, camera and shader. Both asynchronous and
synchronous warm-ups use one named constant. This is a **fit** to this one
day-root frame. It does not replace or alter the optional firmware table path.

A temporary 480x270 GPU sweep sampled 0..100,000 additional steps every 50,
ranking the clean-strip emission correlation plus six-column crest error.
The selected state is 14,400 + 27,150 steps. A separate 0..1,000,000-step
six-column-only sweep did not achieve a few-pixel match either; none of its
states or temporary probe/load hooks is shipped. No firmware or simulated
binary snapshot is committed.

Measured trial `out/ribbon-a2/day-fit.png` (full-size capture, probe disabled):

| x | HEAD y -> fit y | Console y | Fit delta | HEAD peak -> fit peak |
|---:|---:|---:|---:|---:|
| 0 | 473 -> 512 | 555 | -43 | 137.72 -> 150.00 |
| 300 | 576 -> 462 | 371 | +91 | 113.86 -> 100.58 |
| 400 | 444 -> 407 | 361 | +46 | 128.72 -> 100.50 |
| 1520 | 429 -> 572 | 635 | -63 | 162.16 -> 133.79 |
| 1700 | 443 -> 605 | 648 | -43 | 100.58 -> 121.00 |
| 1900 | 439 -> 630 | 653 | -23 | 132.93 -> 92.93 |

The maximum six-point error falls **214 -> 91 px**. The high portion moves to
the left and the right edge drops, but this is explicitly **not** the requested
few-pixel fit. The x0 peak still includes a broad particle; no particle count,
energy, shader or emitter parameter changes. Moving the sheet naturally moves
the particle birth locations, so frame-space particle maxima can change.

The existing uncommitted **0.7 emissive gain** is retained, not authored or
staged by this pass. It multiplies the ribbon-only sum before the colour3/4
gradient, not shared HDR exposure. The phase + inherited gain pair improves:

- Ribbon band: **17.09 / .7309 -> 20.14 / .7868**.
- Whole frame: **19.73 / .8490 -> 22.03 / .8767**.

R4 is also not closed at every column: the measured six-point range is
92.93..150.00, not uniformly 105..134. Claims of exact console phase or full A2
closure would be false. The benefit is a bounded, smaller fallback error while
retaining the traced table path.

Final no-probe build capture `out/ribbon-a2/final-fallback.png` is pixel-identical
to the trial above. `XMB_RIBBON_SETTLE_SYNC=1` produces the same pixels as the
prefetched path (infinite PSNR / 1.0000 SSIM). The measured warm-up was 715.9 ms
on the prefetch worker (108.1 ms join), versus 615.8 ms synchronous during the
concurrent harness run. The longer fit has a startup cost, not a per-frame cost.

Top background `0,0,700,200`, clean HEAD versus final fallback: **infinite PSNR /
1.0000 SSIM**. No background-field, HDR, layout, text, icon or particle constants
change in this pass.

The separate firmware-table path was rechecked as well: clean HEAD crest
530/512/527/719/719/720, peaks 175.19/125.79/111.79/147.36/160.36/152.21.
With the inherited 0.7 gain its crest is unchanged and peaks become
171.19/102.86/93.79/117.14/125.43/120.29. Ribbon crop **16.48/.7229 ->
17.66/.7477**, frame **19.34/.8480 -> 20.27/.8587**. That path does not consume
the fallback phase change and must not be conflated with the bare recipe.

## Final verification

- Required build: **Built C:\dev\xmb\out\native-xmb\re-ps3-native.exe**
  (`out/ribbon-a2/final-build.log`). Its bare-recipe capture
  `out/ribbon-a2/final-normal.png` is pixel-identical to `final-fallback.png`.
- `bun work/ab-layers.ts --all --exe C:/dev/xmb/out/native-xmb/re-ps3-native.exe`
  was run before reference refresh and again afterwards. Final log:
  `out/ribbon-a2/gate-final2.log`. **21/22 named states PASS; 21/21 excluding
  steam-download-page**. That sole excluded failure was neither chased nor
  re-saved. Stored pair: 6/6 layer gates PASS. Canonical capture: 5 gating layers
  PASS + 1 content-differs informative layer; clock text-presence guard PASS.
- Refreshed only the **15** changed native references, using `--state <name>
  --save-reference`: steam-game-root, steam-all-games, steam-store-sections,
  steam-not-installed, xmb-notification, xmb-volume-hud, video-options-panel,
  continue-watching, continue-watching-empty, jellyfin-signin, music-player-page,
  music-visual-player-panel, music-visual-player-on, auto-off-guide, ingame-xmb.
  The other six nonexcluded references were unchanged and not re-saved.
  No threshold, recipe, reference pointer or firmware binary was committed.
- `bun work/parity.ts --capture --score --state root` captures successfully but
  the local root reference is missing (Tier A PASS, Tier B no-reference). Its
  configured default executable is a **stale** `C:/dev/out/native-parity` build.
  Therefore also reran with `--exe C:/dev/xmb/out/native-xmb/re-ps3-native.exe`:
  `out/parity/run-20261111-065404`. Same no-reference result. Against col-game-r00
  manually: **19.45/.8397 frame, 17.37/.7388 ribbon band**. These are not the
  bare recipe's 22.03/.8767 and 20.14/.7868: parity uses its own 10-25 clock and
  boot recipe. The stale default's capture is not used as evidence of this fix.

Commit scope is the fallback phase/comment correction, this audit and the
small reproducible crest-measurement tool. The pre-existing gain hunk and
`RIBBON-A2.md` remain unstaged for their owner. An unrelated concurrent PS-menu
change was committed as `5d155d2` during this pass; none of its hunks was staged
by this pass.
