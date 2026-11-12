# Text entry into a running title, and the elevated-window notice (ZF2 and the safe half of ZF1), 2026-11-11

Plan of record: [PHASES-ZERO-FRICTION-2026-11-11.md](PHASES-ZERO-FRICTION-2026-11-11.md), section 6 (binding). This
file records what was built, how it was checked, and what is still unverified. Everything marked **INFERRED** is design,
not something the firmware does.

## ZF2: "Enter Text" in the in-game XMB

While a tracked title or web app runs and the in-game XMB is up, the running content's column carries an **Enter Text**
row right after Quit Game (id `ingame:text`, `Action::EnterText`, icon `system_plugin:tex_notification_keypad`).
**INFERRED:** the firmware's in-game rows have no such row; the glyph is the firmware's keypad notification icon, chosen
because it exists, not because the console uses it here.

Flow (`src/shell.cpp` `requestEnterText`, `src/view.cpp` `beginTitleText` / `deliverTitleText`, `src/text_entry.*`):

1. Cross on the row emits `Shell::requestTitleText`; the view opens the XMB's own OSK (`openOsk`, title "Enter Text", field
   limit 256 UTF-16 units, whitespace preserved). Not Windows' `osk.exe`. Text from the phone remote reaches the same field
   (`PhoneRemote::textRequested` already feeds an open OSK).
2. Circle (or Escape) is the ordinary OSK cancel: the in-game XMB stays as it was and nothing is sent.
3. On Enter the view checks the recorded window (`inGameHwnd_`, taken when the in-game XMB opened) before anything moves:
   it must still be a window, belong to the tracked process tree (`pidInTree` of the launched process; for a Steam title,
   which is not our process, the process that owned the recorded window), and not be elevated over this XMB. If not, a
   notification says so and the in-game XMB stays up. Nothing is sent.
4. Otherwise `returnToGame` hands the foreground back (the existing `takeForeground` recipe, including `SW_RESTORE`), and
   `TextDelivery` confirms `GetForegroundWindow() == recorded window` (one retry after 400 ms), checks the target again, then
   sends the text with `SendInput` and `KEYEVENTF_UNICODE`: one key-down and one key-up per UTF-16 unit in a single call, 8 ms
   between units. Before every unit it checks that the recorded window still exists and still holds the foreground; if the
   focus moved it stops and says how many units went out ("The window lost focus (N typed)").
5. Control characters (below U+0020, U+007F to U+009F) are dropped: a Unicode packet for a newline or tab is not a key press.
   The OSK cannot produce them anyway. There is no Enter key in the typed text; the title's own Enter is the pad's job.

**The typed text is never logged, at any level.** The lines are counts and verdicts only:
`textentry: start units=N dropped=M pid=P check=deliver` and `textentry: finished verdict=deliver unitsSent=N pid=P`.
The text is held only in the OSK field and the delivery's event list, which is cleared when the delivery ends.

### Limits, stated plainly

- `KEYEVENTF_UNICODE` arrives as `VK_PACKET` key messages that become `WM_CHAR`. Programs that read text through the normal
  message queue (Chromium, most Win32 and Qt programs) get it. A game or emulator that reads raw input, DirectInput or scan
  codes ignores it; no text path can fix that.
- An exclusive-fullscreen title may not take the foreground back; then the verdict is `no-foreground` and nothing is typed.
- The OSK on a TV is slow for long text; the phone remote (when enabled) is the fast path into the same field.

## ZF1 (safe half): say that the window is elevated

`src/elevation.*`: `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)`, `OpenProcessToken(TOKEN_QUERY)`,
`GetTokenInformation(TokenElevation)`. Needs no elevation. Any failure is `unknown`, never `elevated`. Delivery is blocked
only when the target is elevated **and the XMB is not** (an elevated XMB can reach elevated windows; UIPI only blocks upward).

- **Desktop Mode** (`desktop_pad_win.cpp`, `DesktopPadDriver::deliver`): the foreground window's process is looked up at most
  every 250 ms. While it is elevated over a non-elevated XMB, nothing is sent except key-up and button-up events (so nothing
  this driver pressed stays down) and the keyboard toggle (not input injection). Once per such window: a log line with the
  process id, `Shell::notify`, and a tray balloon. The old "SendInput delivered nothing ... may be elevated" warning stays as
  the fallback for the unknown case and now reports the elevation state.
- **ZF2 delivery**: the same check in `checkTarget`, before and after the foreground handback; the verdict `elevated` leaves the
  in-game XMB up and shows the notice.
- **Wording.** The full sentence, "The window in front is running as administrator, so the controller cannot drive it.", is
  the log line and the docs. The pop-up plaque is 400 px wide with a single-line elided title and a one-line detail
  (`View::drawNotification`), so it carries the short pair **Administrator window** / **The controller cannot drive it.**
- **Desktop Mode caveat.** The XMB window is hidden there, so the queued pop-up is only seen on return; the tray balloon
  carries the full sentence at the time. A visible pop-up over the desktop would need a small always-on-top window, which is a
  separate decision and not built.
- **Not built, on purpose:** no elevated helper, no bridge, no change to any system or UAC setting. The two owner-side options
  and their trade-offs are in `packaging/README.md`, "Elevated windows and the controller".

## Verification (2026-11-11, build `C:\dev\out\zf2-build`, zero compiler warnings)

All against windows of our own; nothing was injected into any other application. `XMB_SINGLE_INSTANCE=0`, scratch data under
`C:\dev\out\zf2`. Only PIDs started by the scripts were stopped.

| Check | Command | Result |
|---|---|---|
| Mapping, pure | `bun work/zf2-text-test.ts` (uses `--zf-text-test <text-file> <json>`) | 30 UTF-16 units out of 32 in (tab and newline dropped), one down and one up each, in order; the emoji is two consecutive units |
| Delivery into a sink window | same script (`--zf-text-sink`, `--zf-text-deliver`) | sink receives exactly the expected text as raw `WM_CHAR` units, surrogate pair intact; 60 key-downs and 60 key-ups for two deliveries |
| Not the tracked tree | same script | verdict `not-tracked`, 0 units sent; a root that is an ancestor of the sink process delivers (tree rule) |
| Window gone | same script | verdict `no-window`, 0 units sent |
| End to end through the XMB | `bun work/zf2-ingame-test.ts` | the XMB launches a sink as its tracked title, PS tap, `Enter Text` row, OSK, Enter: sink receives the typed text; Circle: nothing received, `inGame` still true; the typed text appears in no log line |
| Elevation, not elevated | same text-test script (`--zf-elevation-check`) | a process the script starts reports `not-elevated`; this process and the foreground window report `not-elevated` |
| Elevation, elevated | `--zf-elevation-check <json> all` | two processes this session reports `elevated` (vendor utilities that run elevated), 177 `not-elevated`, 94 `unknown` (protected or system processes the query is refused for). Only their tokens were queried. Consistent with correct detection; the two were not independently confirmed elevated |

## Gate state `ingame-xmb`

`bun work/ab-layers.ts --state ingame-xmb` fails (PSNR 29.86 / 30.86 / 29.01 dB against the 55 dB gate), **and fails with
identical numbers on a build of the unmodified HEAD**, so the failure is the stored reference being older than HEAD (it still
shows the Steam Store and PS Vita rows that later commits removed), not this change. The new row sits after Quit Game, above
the selected entry in that state, so it is not in the captured frame. The reference was not re-saved.

## What is left

- A real elevated window: the detection is checked against real elevated processes by token, but a real elevated window in the
  foreground (and the Desktop Mode pop-up and tray balloon over it) needs a device test with a UAC-elevated installer. The
  secure-desktop UAC prompt itself is not reachable by design.
- A real web app: typing into a Plex or YouTube search box in a P6 browser window, and an exclusive-fullscreen emulator
  window's focus handback, are untested here.
