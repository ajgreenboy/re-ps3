# Behaviour gaps — implementation-ready spec (parity verdict group D)

Read-only analysis. No source, harness, threshold or other audit file was edited for this document.
This is the implementation spec for the "structure and behaviour" gaps named in
`FIDELITY-VERDICT.md` D.9/D.11 and `FIDELITY-BEHAVIOUR.md` section 13. It re-checks each gap against the
**current** tree (post the 2026-11-07 power pass, `system_power.*`), not against the audit's HEAD `07a4fe4`.
Three of the seven behaviours have since landed and are recorded here as **closed** with their verification
path; the four still open are spec'd to file and owner.

Evidence labels used throughout: **TRACED** = read from a firmware file/offset or the current source;
**measured** = a captured console frame or a measured run; **INFERRED** = reasoning, stated. Console
behaviour is cited to the audit that established it; nothing is invented into a `match`.

---

## 1. PS-menu power semantics

### Console behaviour (cited)
PS hold opens a black system menu with `Turn Off the System` and `Turn Off the Controller` (plus
`Quit Game` while a title runs) — **measured**, `audit/PS-BUTTON.md`, the `ps-menu` frames. `Turn Off the
System` asks once ("Turn Off the System?") then removes power; the console has no restart/sleep row and no
desktop (**TRACED** `impose_plugin` strings; the confirm step is native-only surface D9 in
`audit/DIALOGS-FIRMWARE.md`). `Turn Off the Controller` powers the pad off (**measured**, PS-BUTTON).
Circle at the confirm step backs out to the entries before closing the menu (the console reading recorded
in `FIDELITY-BEHAVIOUR.md` 12.7, contradicting the older A48 row).

### Current native state
- `system_power.{h,cpp}` landed (2026-11-07): `requestShutdown()` / `requestRestart()` / `requestSleep()`
  via `InitiateShutdownW` / `SetSuspendState`, gated by `canSleep()` / `canHibernate()`.
- The confirmed `Turn Off the System` branch in `main.cpp:1412-1432` now does: **kiosk** → real
  `xmb::requestShutdown()` (PC powers off); **non-kiosk** → `app.quit()` (XMB exits, PC stays on), logged as
  a deliberate safety choice. `Audit/SESSION-POWER.md` 4.1's "Turn Off quits to desktop" is superseded for
  kiosk mode.
- The Users column adds host rows `Turn Off System`, `Restart System`, `Sleep`, `Switch to Desktop`,
  `Exit to Desktop` (`shell.cpp:5020-5041`); Restart/Sleep each raise a Yes/No box
  (`shell.cpp:2110-2137`).
- `Turn Off the Controller` still shows a message page, never powers the pad off
  (`shell.cpp:2442-2445`).
- The confirm step draws the native second line `X turns the XMB off; the PC stays on   O cancels`
  (`view.cpp:3224-3225`).
- Circle/Esc at the confirm step calls `psMenuClose()` which clears **both** `psMenu_` and `psConfirmOff_`
  in one press (`shell.cpp:2469-2474`; routed from `view.cpp:4540`, `4716`).
- The controller/charge panel hard-codes slot `1` and the dash (`view.cpp:3226-3262`) although
  `pad::info()` carries the live kind/transport/battery.

### Gap
1. **P7 (open):** Circle at the "Turn Off the System?" confirm step closes the whole menu instead of
   backing out to the two entries. Proven console mismatch (`FIDELITY-BEHAVIOUR.md` P7 + 12.7).
2. **P6 (open, disclosed):** `Turn Off the Controller` does not power the pad off — Windows owns the
   HID device; the build says so instead of pretending. Recorded as a by-design limit, not a silent
   mismatch.
3. **P8 (open):** the PS-tap plate and the charge panel draw hard-coded `1` / `-` where a live pad
   report exists. `SESSION-POWER.md` 4.3 shipped the exact `pad::info()` hunk; it is still not applied in
   the view.
4. **Stale confirm text:** the second line "the PC stays on" is now wrong in kiosk mode, where the PC
   shuts down. Native-only surface, but the text contradicts the behaviour it sits on top of.
5. **Non-kiosk semantics (deliberate):** outside kiosk, `Turn Off the System` quits the app rather than
   powering off. This is a disclosed host safety choice, not a console parity claim; keep it, but state it.

### Implementation note
- **file + owner:** `shell.cpp` (`psMenuClose` / `psMenuActivate`, shell owner) — split the confirm-step
  back: when `psConfirmOff_` is set, Circle clears only `psConfirmOff_`; a second Circle then closes the
  menu. Mirror the two-step shape `Quit Game` already uses (close menu, then the box).
- **file + owner:** `view.cpp` (`drawPsMenu` region, view owner) — apply `SESSION-POWER.md` 4.3's two
  `pad::info()` hunks (charge panel + tap plate); change the confirm second line to be kiosk-aware
  (`shell.psMenuConfirmingOff()` + a `Shell` accessor for "will the PC power off", e.g. `kiosk` state from
  `main.cpp`).
- No new firmware strings: the confirm line is native (D9).

---

## 2. Auto-Off re-arm via input (the 600 s guide)

### Console behaviour (cited)
Any input ends the idle period and a later idle period may request display-off again
(`FIDELITY-BEHAVIOUR.md` S2). The 600 s Auto-Off guide (`msg_help_system_off_soon_by_setting`) is raised
once `S - E <= 600 s` and hidden when input pushes `S - E` back over 600 s — **TRACED** `vsh.elf`
`FUN_000c3bb4`, quoted in `coldboot.h:104`.

### Current native state
- `Shell::touch()` is the single input funnel and now calls `host::noteUserActivity()`
  (`shell.cpp:4851-4858`), closing the old "dead caller" mechanism gap; the re-arm was measured in
  `FIDELITY-BEHAVIOUR.md` 16.5 (`autoff-rearm`).
- The guide is implemented: `GameBoot::guideDue` (`coldboot.cpp:500-502`, `guideWindowSeconds=600`,
  `guideHideMs=200`), fed every frame from `main.cpp:1330` with `shell.idleMilliseconds()`; `touch()`
  restarts the idle clock, so the guide hides on input.

### Gap
None. This behaviour is **closed**. The only remaining nuance is a measurement, not a code gap: the guide's
hide ramp (200 ms) is a fit — the console burst separates the trigger point, not the hide curve.

### Implementation note
- **file + owner:** none required. Verification only: `XMB_GAMEBOOT_AT` / the `guide` sidecar state and
  the `autoff-rearm` recipe in `FIDELITY-BEHAVIOUR.md` 16.5 already cover it. If the hide curve is ever
  captured, `guideHideMs` in `coldboot.h` is the one constant to update.

---

## 3. OSK extras: L1/R1 caret, L2/R2 pages, Select mode, open/close fade

### Console behaviour (cited)
`audit/OSK-FIRMWARE.md` section 6: L1/R1 move the text caret left/right; L2 opens the accent page
(`a/a-grave`), R2 the caps page (`a/A`); SELECT is Input Mode. Open fade **~66-96 ms**, close fade
**~104 ms** (**measured**, OSK-FIRMWARE 13.2). Caret blink period **~936 ms** (lit ~470 / dark ~440)
(**measured**, OSK-FIRMWARE 13.1).

### Current native state
- The on-screen caret-left/caret-right arrow keys work (`OskAction::CaretLeft/CaretRight`,
  `view.cpp:4992-4993`) and carry the L1/R1 badges, but **L1/R1 are not bound in the pad branch**
  (`view.cpp:4783-4790` binds only the d-pad, A/B/X/Y and START).
- `a/A` (`OskAction::Shift`) toggles lower↔caps correctly (`view.cpp:4986`); `a/a-grave`
  (`OskAction::Accent`) is **inert** and logs "accent page is not captured" (`view.cpp:4987`). L2/R2 are
  not bound.
- SELECT is not bound; `OskAction::Mode` logs "Input Mode is unavailable (no IME modules)"
  (`view.cpp:4994`).
- **No open/close fade:** `openOsk` flips `osk_.open` and calls `update()` (`view.cpp:4897-4905`); there is
  no OSK `PafEase` in `view.h`.
- The caret blink is already corrected to 468 ms half-phase (`osk_layout.h:119`, `blinkTraced=true`), so
  the earlier 500 ms fit is retired.

### Gap
1. **I64 (open):** physical L1/R1 do not move the caret (only the on-screen arrow keys do).
2. **I65 (open):** L2/R2 are not bound; the accent page (`a/a-grave`) has no content, so L2's page is
   missing and R2's caps page is reachable only via the on-screen `a/A` key.
3. **I66 (open):** SELECT Input Mode is unbound (the function itself is legitimately unavailable — no IME —
   but the button should at least be routed to the existing "unavailable" log, not dropped).
4. **T16 (open):** the panel appears/disappears in one frame; the console ramps ~66-96 ms in and ~104 ms
   out.

### Implementation note
- **file + owner:** `view.cpp` `pollController` OSK branch (~line 4783, view owner) — bind
  `XINPUT_GAMEPAD_LEFT_SHOULDER`/`RIGHT_SHOULDER` → `OskAction::CaretLeft`/`CaretRight`,
  `LEFT_TRIGGER`/`RIGHT_TRIGGER` → the accent/caps page actions, `XINPUT_GAMEPAD_BACK` → `OskAction::Mode`
  (the same `oskPress` dispatch the on-screen keys already use, so the badges and the pad agree).
- **file + owner:** `osk_layout.cpp` (OSK data owner) — add the accent-page row strings (the module's
  symbol-variant rows are already decoded in `OskLayout`; expose them as a third page so `Accent` stops
  being inert) and wire `Accent` to that page in `view.cpp` `oskPress`.
- **file + owner:** `view.cpp` / `view.h` (view owner) — add one `PafEase` for the OSK panel alpha with
  the measured 66-96 ms open and 104 ms close (a single ease driven in `paintGL`, mirroring
  `optionsOpenEase_`), and include it in the UI signature. No new texture; the panel is already an alpha
  composite of the resolved layout.

---

## 4. gameboot / XMB return transition

### Console behaviour (cited)
A title launch plays `gameboot1-5` background presets plus the `anim_gameboot` logo wordmark
(`tex_ps3logo`), then the title starts; on quit the XMB returns — **TRACED** `audit/PIPELINE.md` 158-161,
256-282; `FIDELITY-SURFACES-MAP.md` S31.

### Current native state
- `anim_gameboot` is implemented (`coldboot.cpp:482-531`): black plane + `tex_ps3logo`, fade-in 0.5→1.363 s
  (863 ms), fade-out 1.6→1.774 s (174 ms), Finished at 2.8 s, skippable after 0.4 s, `XMB_NO_GAMEBOOT=1`
  disables. Played on RPCS3/Steam/`game` app launches via `GameBoot::begin` (`shell.cpp:2236-2283`).
- XMB return: `TitleSession` restores window/foreground/selection; the tracked `finished` handler restores
  the display mode, `clearInGame()`, `refreshContent()`, and a crash raises a notification
  (`shell.cpp:575-598`). No distinct return *animation* exists — which matches the console (it has none;
  the XMB is already resident).
- **Not scheduled:** the `gameboot1-5` background presets — the ribbon/HDR/particles/background blends the
  BG events start. The native plays the logo over a plain black fill (`coldboot.cpp:551`).

### Gap
1. **B5 (open, narrowed):** the `gameboot1-5` background presets are absent; the logo plays over flat
   black where the console runs the `gameboot2/3/4/5` background events (0.25 s / 1.25 s / 0.5 s / 0.5 s)
   behind it. The wordmark itself, its timing and the skip rule are done.

### Implementation note
- **file + owner:** `coldboot.cpp` `GameBoot::render` (coldboot/gameboot owner) — replace the flat black
  fill with the preset timeline already decoded in `audit/PIPELINE.md` 256-282 (the BG event routing and
  the per-preset ribbon/particle/HDR tables in `audit/PARTICLES.md` 344-361); reuse `ribbon.cpp` /
  `particles.cpp` so no new backend is needed. The logo plane is unchanged. Verify with
  `XMB_GAMEBOOT_AT=<s>` against the traced BG timings; the preset blend curve is the open detail (mark
  INFERRED).

---

## 5. Multi-select copy / delete

### Console behaviour (cited)
The item menu offers `msg_copy_plural` / `msg_del_plural` ("Copy Multiple" / "Delete Multiple") which open
select-all / deselect-all / decide pages, then a confirmation, progress and result dialog — **TRACED**
`ITEM-MENU-INFO-FIRMWARE.md` A3/A5 and `DIALOGS-FIRMWARE.md` D6/D7 (`fdel_cmdbtn_select_all`,
`fdel_cmdbtn_deselect_all`, `fdel_cmdbtn_decide_src`; `msg_del_images_ask`, `msg_deleting`, `msg_deleted`).

### Current native state
- Copy and Delete are **single-item only**: `OptionCommand::Copy` builds a target chooser for the focused
  file (`shell.cpp:3461-3488`) and `OptionCommand::Delete` deletes the one row (`shell.cpp:3499-3509`).
- The plural labels `msg_copy_plural` / `msg_del_plural` exist only in the `optionLabel` probe list used by
  `XMB_DUMP_ENTRIES` verification (`shell.cpp:2971-2972`) — there is no multi-select state, no
  select-all/deselect-all, no decide page, and no bulk copy/delete path anywhere in `src/`.

### Gap
**X3/X6 (open, fully absent).** Every multi-item file command the console offers on local content is
missing. This is the one whole file-management surface still unbuilt.

### Implementation note
- **file + owner:** `shell.cpp` + `shell.h` (shell owner) — add a multi-select mode keyed off the item
  menu: a `bool multiSelect_` and a `QSet<QString> checkedIds_`; when it is on, a marker key (the firmware's
  select-all/deselect-all/decide command buttons) toggles rows, and `Copy`/`Delete` become the plural
  commands that fan the set into the existing `fileOps_.copy({...})` / `fileOps_` delete loop (both already
  take a source list — `fileOps_.copy({source},target)`). Surface the three command buttons as a
  `MessageKind::Box`-style decide row reusing the `msg_deleting` / `msg_deleted` strings already resolved.
- **file + owner:** `view.cpp` (view owner) — a check/count indicator on the page rows while
  `multiSelect_` is set (the console has no measured check glyph, so draw the active-state plate and mark
  the glyph INFERRED).
- No new firmware strings: `msg_copy_plural` / `msg_del_plural` / the decide verbs are already resolved.

---

## 6. Steam account-switch surface

### Console behaviour (cited)
None — the 4.93 account surface is the PSN binding, excluded by decision (`PARITY-GAPS` 4 item 9,
`FIDELITY-BEHAVIOUR.md` U4). This is a native-only surface.

### Current native state
Closed. `Shell::openSteamAccounts()` builds the Users-column `Steam Account` page from
`scanSteamAccounts()` (`shell.cpp:5113-5168`); selecting a row runs the two-step handoff
(`steamAccountSelected` → `steamSwitchNextStep` / `Confirmed` / `Run` / `Result` / `Cancel`,
`shell.cpp:5170-5270`): close Steam, start Steam, honest result text, no fabricated sign-in. It honours the
`steam_accounts.*` safety boundaries (never reads/writes credentials; never claims a switch occurred).

### Gap
None. This behaviour is **closed**. Remaining risk is runtime-only: the unheld handoff needs a machine
whose Steam client can be closed (`XMB_STEAM_SWITCH_HOLD=1` is the dry-run proof in
`FIDELITY-BEHAVIOUR.md` 16.3).

### Implementation note
- **file + owner:** none required. Verification only: the `.steam-accounts.json` dump and the
  `steam-chain` log from `FIDELITY-BEHAVIOUR.md` 16.3.

---

## 7. Settings writes: confirm chain and result page

### Console behaviour (cited)
A value write resolves into one of three shapes: a `page_*_finish` result page, the full
`page_*_top -> *_ask / *_choice_type -> *_proceed -> *_finish` chain, or nothing (the row value just
changes) — **TRACED** `SETTINGS-FIRMWARE.md` section 4; the gap was `I77` / `D27`/`D28`.

### Current native state
Closed. `Shell::beginSettingWrite()` (`shell.cpp:4436-4476`) models the chain as
`WriteStage { Ask, Proceed, FinishOk, FinishFailed }`: a host write asks first
(`msg_setting_ask_back_prev_setting`), the appearance/clock rows go straight to the result page, and
`Restore Default Settings` runs the full ask → `msg_initializing` proceed → `msg_initialized` finish
(`beginResetWrite`, `shell.cpp:4477-4505`). The finish page is the `MessageKind::Page` dialog carrying
`msg_saved` + the written value, or `msg_error_setting` + the real error (`runSettingWrite`,
`shell.cpp:4524-4590`). A failed write keeps the band open and the row focused; a successful one pops the
band (`finishSettingWrite`, `shell.cpp:4592-4606`). This matches the console's "successful change = no
dialog" for the appearance/clock rows and the result-page shape for the host/reset rows.

### Gap
None for the chain and result page. Two **deliberate deviations** remain, recorded not gapped:
- the app-store rows' success page (a departure from `DIALOGS-FIRMWARE.md` 3.6's "no dialog on success"),
  which this brief explicitly asked for;
- the per-row ask evidence — only the host-write class and Restore Default Settings have a real ask body;
  if a capture shows the console's volume row committing without one, delete `askBody` for `host/*` in
  `beginSettingWrite`.

### Implementation note
- **file + owner:** none required. If the success-page deviation is to be removed for appearance/clock
  rows, clear `okText` for those classes in `beginSettingWrite` (`shell.cpp`, shell owner); the failure
  page stays. Commit cue (`Cue::Accept`) is already mapped on success in `runSettingWrite`.

---

## Ranked implementation order (open items only)

1. **OSK extras (behaviour 3)** — L1/R1/L2/R2/SELECT bindings + accent page + open/close fade. Highest
   visibility: the key faces already draw the L1/R1/L2/R2/SELECT badges, so the UI advertises four dead
   buttons. `view.cpp` + `osk_layout.cpp`.
2. **Multi-select copy/delete (behaviour 5)** — the last whole file-management surface; single-item
   Copy/Delete already give the backend to fan out. `shell.cpp` + `view.cpp`.
3. **PS-menu confirm-step back + charge plate (behaviour 1, items 1/3)** — small but on the console's only
   system surface, and the charge panel fix is a ready hunk (`SESSION-POWER.md` 4.3). `shell.cpp` +
   `view.cpp`.
4. **gameboot1-5 background presets (behaviour 4)** — narrow: the logo, timing and skip rule are done;
   only the background preset blend behind the logo remains. `coldboot.cpp` (+ `ribbon.cpp`/`particles.cpp`).
5. **Stale confirm text (behaviour 1, item 4)** — trivial string fix tied to item 3's view pass.

Closed and to be verified rather than re-implemented: Auto-Off re-arm (2), Steam account surface (6),
settings confirm chain (7), and the kiosk power-off half of (1).

---

status: groundwork only, no code changed, not built
