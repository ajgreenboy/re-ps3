# Installed artwork and folder boundaries — 2026-11-04

## Problem and causes

- Gray margins around installed games: `View::drawScene` painted the empty BG1 placeholder behind every tile, including decoded artwork. The firmware evidence in [GEOMETRY-ROOT.md, section 12.7](GEOMETRY-ROOT.md#127-the-item-plate-is-the-bg1-artwork-plane-not-a-focus-plate-corrects-101-and-111s-naming) identifies it as an empty-artwork plane, not a focus highlight.
- Missing delayed game backgrounds: the shell exposed row images but no focused PIC1/hero path, and the view never rendered a delayed item backdrop.
- Folder contents flying offscreen: clamped Down presses incremented `columnSink_` without a bound; the view added 40 pixels per press to the entire active column.
- Continue Watching/mixed-list overlap: wide thumbnails at deeper levels used the 60-pixel glyph label gutter, while their focused half-width could reach 160 pixels. Unfocused thumbnail envelopes could also exceed the 80-pixel row pitch.

## Current implementation

- BG1 remains only for an installed game with no artwork. Loaded cards, file images and glyphs do not receive a gray backing rectangle.
- Installed game cards use a uniform 320:176 canvas, including during focus scaling. Source images are centre-cropped into it without stretching. Neighbours have a 72-pixel-high envelope, leaving room within the 80-pixel row pitch. The neighbour envelope is a native layout fit, not a traced firmware dimension.
- Other file-backed square icons keep icon sizing. Wide media thumbnails use a separate label gutter at every depth; source aspect is preserved for non-game images.
- `Shell::focusedBackgroundPath()` resolves installed PIC1.PNG, explicit Steam artwork overrides, Steam library hero art, then a cached/downloaded public Steam hero. The public download requires no API key and is bounded, decoded off-thread and atomically cached. Local imported app registrations still have only their existing single artwork field.
- View decoding is asynchronous and guarded by request identity. A focus identity change clears the incoming image, invalidates stale work and fades out the departing backdrop. Delayed images render below the glass icons and UI. Wallpaper now uses the same scene ordering.
- Dwell **2000 ms**, fade **350 ms**: native fits. This pass does not establish exact firmware timing, PIC0 layering, ICON1 playback or SND0 playback.
- Both keyboard/controller navigation use the shell's clamped selection. Repeated boundary presses never accumulate a screen offset. `XMB_OVERSCROLL` remains an explicit historical capture diagnostic only.

## Source evidence and provider decision

- Local firmware captures/resource analysis: GEOMETRY-ROOT 12.7. No new RPCS3 runtime capture was made in this pass.
- Real PS3 homebrew source/documentation also distinguishes ICON0 from optional PIC0/PIC1/PIC2: [webMAN MOD Game Paths & Covers](https://github.com/aldostools/webMAN-MOD/wiki/Game-Paths-&-Covers#for-ps2-classics-binenc). This supports the separate file roles, not timing constants.
- Valve distinguishes library headers and hero graphics: [Steamworks Library Assets](https://partner.steamgames.com/doc/store/assets/libraryassets). Steam header dimensions are not the PS3 ICON0 dimensions; uniform presentation therefore belongs in the renderer regardless of provider.
- SteamGridDB is a suitable future optional source for curated replacement grids/heroes, especially imported games. Its [maintained API wrapper](https://github.com/SteamGridDB/node-steamgriddb) uses an authenticated API. No SteamGridDB/RAWG credential or new provider connection was added in this pass. Existing per-game local overrides accept downloaded art now.
- REA rejected the local explore_plugin PRX as an unsupported ELF architecture. No fresh decompilation or exact firmware timing claim is made from that failed attempt.

## Verification and handoff

Local evidence, excluded from Git: `out/native-reference/bugs-20261104/`.

- First Release candidate built successfully and exited normally from the scripted probes.
- Black Ops II: no gray rectangle with loaded art; background appears after dwell; category icons remain visible. Public hero cache populated for app IDs 202970 and 2807960.
- A disposable resume record produced the populated Continue Watching screenshot, with distinct thumbnail/text bounds.
- On the three-entry mixed folder, eight extra Page Down presses left selection at 2, and seven extra Page Up presses left it at 0. With render/background/UI/focus clocks pinned, the before/after PNG hashes were identical at **both** boundaries (`fixed-bottom-*`, `fixed-top-*`).
- Existing per-layer checks were run without refreshing references or changing thresholds. The final results and final candidate validation are recorded below after the run completes.

Next: measure dwell/fade against real firmware footage; add an optional curated-art provider through the importer if needed; extend registry artwork roles before promising imported applications separate ICON0/PIC1 assets.

### Final verification

- Final Release candidate: `out/icon-folder-fix-final/re-ps3-native.exe`; source fingerprint and executable SHA-256 matched build-info.json after the run.
- Final uniform-crop replay exited 0. `final-game-background.png` and `final-continue.png` were visually inspected.
- `final-bottom-first.png` and `final-bottom-repeat.png` have identical SHA-256 hashes, as do `final-top-first.png` and `final-top-repeat.png`; sidecars report 2/3 and 0/3 selections respectively.
- Existing canonical gate on the final candidate: five scored layers PASS; the existing content-differs item-label layer remains informative. No threshold/annotation/reference changes were made by this task.
- First candidate's full existing gate: stored pair 6/6 PASS; canonical five scored layers PASS plus one informative; named native self-baselines 7/12 PASS. Five old visual baselines fail on Steam root/notification/volume and video options/Continue Watching frames containing the intentionally changed tile layout. The volume HUD and video options panel regions themselves remain pixel-identical. These are recorded failures, not a green whole-set claim. The final card-crop follow-up was checked with canonical and targeted replays rather than rerunning the entire named-state set.
- Particle source was being edited separately during this task. Those edits were neither authored nor reverted here; rendering metrics belong to the recorded candidate, not an isolated particle change.

- Canonical integration output rebuilt successfully at `out/integration/re-ps3-native.exe`. Its recorded source fingerprint matched the current native source before launch. Reopened through run-native-xmb.ps1 with the existing user data directory.
