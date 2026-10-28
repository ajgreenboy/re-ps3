# Interaction audio parity (ISC-11)

Scope: the original XMB interaction cues and the native playback layer in
`src/interaction_audio.cpp` / `.h`. Visual presentation, media playback audio,
and actions outside the six `InteractionAudio::Cue` values are out of scope.

Status: the audio layer now decodes every original cue, maps actions to the
right original sample, does not play the silent source entry as an error sound,
and suppresses retriggers of a cue that is still playing. ISC-11 stays open:
no firmware audio recording was available to measure latency or the final
mix against a reference.

## 1. What the firmware actually contains

There is no `snd_*.rco` file and no SAM sidecar in this dev_flash image. Every
XMB menu cue is an entry in the sound table of:

`C:\dev\rpcs3\bin\dev_flash\vsh\resource\system_plugin.rco`

The table header is at `0x13f0` in the decompressed table (type `0x0600`,
nine `0x0601` entries, 68-byte stride), labels at `0xd89c` in the decompressed
table, and VAG data at `0x2fee4` in the file. Each entry has a 28-byte
descriptor: codec 1, channel count (1 or 2), total entry bytes, entry offset,
and per-channel VAG length/offset. There is no gain or pan field.

The label text is inside the compressed RCO table, so `out/native-reference/rco-text.txt`
and `out/native-reference/rco-behavior/resources.json` do not list it (text and
object resources only). The table was parsed directly from the RCO for this
audit; the native loader now does the same.

Measured with the bounded VAG decoder in `interaction_audio.cpp`. Peak/RMS are
full-scale PCM (1.0 == 32767), before the user volume control.

| # | label | entry offset | VAG bytes (all ch) | ch | frames | duration | peak | RMS |
|---|-------|--------------|--------------------|----|--------|----------|------|-----|
| 0 | snd_cursor | 0 | 2304 | 2 | 1876 | 39.1 ms | 0.093811 | 0.007040 |
| 1 | snd_decide | 2304 | 2304 | 2 | 1876 | 39.1 ms | 0.093811 | 0.007040 |
| 2 | snd_cancel | 4608 | 2304 | 2 | 1876 | 39.1 ms | 0.041595 | 0.004050 |
| 3 | snd_category_decide | 6912 | 2304 | 2 | 1876 | 39.1 ms | 0.093811 | 0.007040 |
| 4 | snd_option | 9216 | 2304 | 2 | 1876 | 39.1 ms | 0.093811 | 0.007040 |
| 5 | snd_error | 11520 | 256 | 2 | 84 | 1.8 ms | 0.000000 | 0.000000 |
| 6 | snd_system_ok | 11776 | 16672 | 2 | 14448 | 301.0 ms | 0.027313 | 0.005894 |
| 7 | snd_system_ng | 28448 | 44064 | 2 | 38416 | 800.3 ms | 0.071014 | 0.007258 |
| 8 | snd_trophy | 72512 | 27520 | 1 | 48020 | 1000.4 ms | 0.504944 | 0.071501 |

Waveform identity (decoded sample SHA-1, first channel):

- `snd_cursor`, `snd_decide`, `snd_category_decide`, and `snd_option` decode to
  identical samples (`d5307521d331...`, 1876 frames). The firmware stores four
  entries that decode to one tick, not four different ticks.
- `snd_cancel` is a different, quieter tick (`cf232f4a1b76...`).
- `snd_error` is all zero samples.
- `snd_system_ok`, `snd_system_ng`, and `snd_trophy` are distinct.

Balance: `snd_cursor`, `snd_decide`, `snd_cancel`, `snd_category_decide`, and
`snd_option` have bit-identical left/right channels. `snd_system_ok` and
`snd_system_ng` are centered but differ by at most 5 LSB between channels
(measured mean difference 0.29 and 0.23 LSB). `snd_trophy` is mono. Every cue
is centered; there is no pan information to restore.

## 2. Cue map: firmware to native

The native actions are the six call sites in `src/shell.cpp` (read-only for this
task): focus move (817, 824), activate (832-833), message dismiss (828), back
(994).

| Native action | Cue | Firmware source | Before this change | After |
|---------------|-----|-----------------|--------------------|-------|
| Focus move, horizontal or vertical | Move | `snd_cursor` | `snd_cursor` | unchanged |
| Activate a normal item | Confirm | `snd_decide` | `snd_decide` | unchanged |
| Open a choice list | Options | `snd_option` | theme `se_optionmenu` only; system fallback was out of range, so silent | `snd_option` |
| Back / close page | Back | `snd_cancel` | `snd_cancel` | unchanged |
| Dismiss a message | Accept | `snd_system_ok` | fell back to `snd_decide` | `snd_system_ok` |
| Activate an unavailable item | Error | `snd_system_ng` | `snd_error` (silent) | `snd_system_ng` |
| Trophy awarded | none | `snd_trophy` | never played | still unwired; no trophy action exists |

Theme sounds still override Move/Confirm/Back/Options/Accept through the five
`.p3t` roles read by `src/p3t.cpp:156` (`se_cursor`, `se_decide`, `se_cancel`,
`se_optionmenu`, `se_system_ok`). A missing theme role now falls back to the
matching original cue instead of the previous wrong or absent fallback.

### Evidence for the assignments

Decompiled firmware (`out/native-reference/explore_plugin_all.c` from
`explore_plugin.sprx`):

- `snd_cursor` in focus/selection handlers, lines 9245 and 9318.
- `snd_decide` on activation, lines 33256, 34446, 34899, 34903, 34914.
- `snd_cancel` on close, line 26546.
- `snd_system_ok` on module-action creation/completion, lines 22617 and 38936;
  line 10335 also selects it for one item-sound path instead of `snd_cursor`.
- Every call goes through one wrapper at line 138289:
  `paf_paf_B93AFE7E((double)DAT_00272000, DAT_002deed0, name, ..., 0)`.
  The sound context is created for `system_plugin` at line 137837. The numeric
  value of the single volume constant was not recovered from the packed PRX,
  but there is exactly one constant for all names, not a per-name table.
- `snd_option`, `snd_system_ng`, `snd_trophy`, and `snd_error` have no
  decompiled call site in this dump. Their mapping here is based on the label
  names/timbre and the absence of any other cue with that role. This is weaker
  evidence than the four confirmed calls and is marked as such below.

## 3. Is `snd_error` genuinely silent, or is the decode wrong?

It is genuinely silent. The raw entry was compared byte for byte with the
decoder's assumptions. Entry offset `11520`, two channels of 128 bytes:

```
00  56 41 47 70 00 00 00 06 00 00 00 00 00 00 00 50  VAGp...........P
10  00 00 bb 80 00 00 00 00 00 00 00 00 00 00 00 00  ................
20  53 45 30 39 5f 45 72 72 6f 72 00 00 00 00 00 00  SE09_Error......
30  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
40  0c 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
50  0c 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
60  0c 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
70  00 07 77 77 77 77 77 77 77 77 77 77 77 77 77 77  ..wwwwwwwwwwwwww
```

- Header: `VAGp`, version 6, dataSize `0x50` (80), rate 48000, name
  `SE09_Error`, 64-byte header. This matches the decoder's version/header rule
  (`dataSize == length - 48`, 16-byte blocks) used for the audible cues.
- Blocks 0 and 1: `0c 00` = predictor 0, shift 12, flags 0, all nibbles zero,
  so 28 zero samples each.
- Block 2: `0c 01` = same but flags 1 (last block), another 28 zero samples.
- Block 3: `00 07 77...` has flags 7 (silent VAG terminator) and is not reached
  because block 2 already ended the stream.

The entry is exactly the size its header declares. It is not truncated, not a
misaligned read, and not a predictor failure. It is a deliberate 84-frame
silent stub (1.8 ms), so `interaction_audio.cpp` still decodes and logs it but
no longer uses it for `Cue::Error`. `Cue::Error` uses the non-silent negative
cue `snd_system_ng` from the same table.

## 4. Volume and mix

There is no per-cue volume or pan in the RCO. The 28-byte descriptor has only
codec, channel count, total size, entry offset, and per-channel length/offset.
The firmware's own playback wrapper uses one volume argument for every cue.
Therefore the reference per-cue level is the level baked into the VAG PCM:

- tick group (`cursor`/`decide`/`category_decide`/`option`): peak 0.0938,
  RMS 0.0070;
- `cancel`: peak 0.0416, RMS 0.0041 (about 7 dB below the tick);
- `system_ok`: peak 0.0273, RMS 0.0059;
- `system_ng`: peak 0.0710, RMS 0.0073;
- `trophy`: peak 0.5049, RMS 0.0715 (about 5.9 dBFS peak, intentionally loud).

The native path passes that PCM through unchanged. It does not normalize, add
gain, or resample the system cues: `decodeClip` preserves the original rate,
channel count, and sample values, and `QAudioSink` applies one linear volume
(the XMB Volume preference, default 60 in `src/preferences.cpp:16`) to every
cue, matching the single firmware volume constant. Adding per-cue gain would
have changed the original mix, so no gain was added. Theme WAVs are resampled
to 48 kHz and measured after conversion; they keep their own recorded level.

`QAudioSink` is deliberately restarted for each cue, so two different cues
never mix; the old buffer is stopped first. What was missing was protection
against retriggering the same cue while it is still sounding.

## 5. Fixes in `src/interaction_audio.cpp` / `.h`

1. **All nine original cues are decoded.** The old `Labels` array held only
   `snd_cursor`, `snd_decide`, `snd_cancel`, `snd_error`. The loader now reads
   the complete table (`interaction_audio.cpp:40`, loop at 265-297). This makes
   `snd_option`, `snd_system_ok`, `snd_system_ng`, `snd_category_decide`, and
   `snd_trophy` available and logs all of them at startup.
2. **Wrong cue mapping fixed for three actions** (`Impl::slot`, line 324):
   `Error` -> `snd_system_ng` (was the silent `snd_error`), `Options` ->
   `snd_option` (was theme-only/inaudible), `Accept` -> `snd_system_ok` (was
   `snd_decide`). `Accept` and `Options` still have a defensive fallback to
   `snd_decide` only if their source entry fails to decode.
3. **The silent source is preserved, not faked.** `snd_error` is decoded,
   measured, and logged as silent (`:285-287`) but never played. The log states
   that the source entry itself is silent.
4. **Same-cue retrigger guard** (`:411-423`): a cue is not restarted until its
   own decoded duration has elapsed; a different cue still interrupts it. Move
   additionally has a 45 ms floor because its 39 ms sample is shorter than the
   controller repeat interval (67 ms, `src/view.cpp:1348-1350`). History is
   cleared when theme sounds change. This prevents the old behavior where
   repeated actions restarted a long `system_ng`/`system_ok` sample from the
   beginning.
5. **Opt-in trace** (`XMB_AUDIO_LOG=1`, also accepts the older
   `XMB_AUDIO_TRACE`): every play prints cue name, source label, sample rate,
   channel count, byte count, peak, RMS, duration, and elapsed milliseconds;
   suppressed retriggers print `skip <cue> <age> ms of <guard> ms`
   (`:303-305`, `:419-421`, `:452-458`). Startup always logs the nine decoded
   entries with rate, channels, PCM bytes, source VAG bytes, peak, and RMS.
6. **Header documentation** now states the full cue map and the `snd_error`
   exception.

## 6. Scripted verification

Build:

```
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-audio
```

Result: `Built C:\dev\out\native-audio\re-ps3-native.exe` (exit 0). The only
warning in `interaction_audio.cpp` is the pre-existing deprecated
`QtAudio::UnderrunError` compare.

Startup decode log for the required run:

```
interaction audio: decoded original snd_cursor 48000 Hz 2 channels, 7504 PCM bytes from 2304 VAG bytes, peak 0.093811 rms 0.007040
interaction audio: decoded original snd_decide 48000 Hz 2 channels, 7504 PCM bytes from 2304 VAG bytes, peak 0.093811 rms 0.007040
interaction audio: decoded original snd_cancel 48000 Hz 2 channels, 7504 PCM bytes from 2304 VAG bytes, peak 0.041595 rms 0.004050
interaction audio: decoded original snd_category_decide 48000 Hz 2 channels, 7504 PCM bytes from 2304 VAG bytes, peak 0.093811 rms 0.007040
interaction audio: decoded original snd_option 48000 Hz 2 channels, 7504 PCM bytes from 2304 VAG bytes, peak 0.093811 rms 0.007040
interaction audio: decoded original snd_error 48000 Hz 2 channels, 336 PCM bytes from 256 VAG bytes, peak 0.000000 rms 0.000000
interaction audio: original snd_error contains genuinely silent PCM; preserving the source entry rather than substituting a cue
interaction audio: decoded original snd_system_ok 48000 Hz 2 channels, 57792 PCM bytes from 16672 VAG bytes, peak 0.027313 rms 0.005894
interaction audio: decoded original snd_system_ng 48000 Hz 2 channels, 153664 PCM bytes from 44064 VAG bytes, peak 0.071014 rms 0.007258
interaction audio: decoded original snd_trophy 48000 Hz 1 channels, 96040 PCM bytes from 27520 VAG bytes, peak 0.504944 rms 0.071501
```

Required run:

```
re-ps3-native.exe --dev-flash C:\dev\rpcs3\bin\dev_flash \
  --data-dir C:\dev\xmb\out\native-reference\store\ui3-data \
  --script "full,wait,RIGHT,wait,DOWN,wait,ENTER,wait,ESC,wait,quit"
```

with `XMB_AUDIO_LOG=1`. Cue sequence (exit 0):

```
play Move    source snd_cursor     48000 Hz 2 ch  7504 bytes peak 0.093811 rms 0.007040 duration 0.039s at 4786 ms
play Move    source snd_cursor     48000 Hz 2 ch  7504 bytes peak 0.093811 rms 0.007040 duration 0.039s at 5410 ms
play Error   source snd_system_ng  48000 Hz 2 ch 153664 bytes peak 0.071014 rms 0.007258 duration 0.800s at 6039 ms
play Back    source snd_cancel     48000 Hz 2 ch  7504 bytes peak 0.041595 rms 0.004050 duration 0.039s at 6664 ms
```

The script lands on the Game column, moves focus twice, activates an
unavailable entry, and backs out. Before the fix the same run printed
`play ... system 336 PCM bytes` for the activation, i.e. 1.8 ms of silence.
After the fix it prints the 153664-byte `snd_system_ng` sample with measurable
energy.

All six cues were also driven in one run (Settings -> Theme Settings ->
choice list; Settings -> Sound Settings -> unavailable row and message):

```
play Confirm source snd_decide     48000 Hz 2 ch  7504 bytes peak 0.093811 rms 0.007040
play Options source snd_option     48000 Hz 2 ch  7504 bytes peak 0.093811 rms 0.007040
play Error   source snd_system_ng  48000 Hz 2 ch 153664 bytes peak 0.071014 rms 0.007258
play Accept  source snd_system_ok  48000 Hz 2 ch 57792 bytes peak 0.027313 rms 0.005894
```

Debounce run (`DOWN,DOWN,ENTER,ENTER,ENTER` on an unavailable Game row):

```
play Error  source snd_system_ng at 6278 ms
play Accept source snd_system_ok at 6591 ms
skip Error  626 ms of 800 ms at 6904 ms
```

The third activation re-encountered the unavailable row 626 ms after the first
`system_ng` started; it was suppressed instead of restarting the 800 ms cue.

Visual gate (unchanged code path except the audio calls):

```
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --capture --exe C:/dev/out/native-audio/re-ps3-native.exe
```

All six layers pass. Final deltas: frame PSNR 18.01 vs 18.02, SSIM 0.8301 vs
0.8301; category icon row 20.91 vs 21.05, SSIM 0.8947 vs 0.8950; the other
four layers match the baseline exactly. `PASS: 6/6 layers within budget`.

## 7. What needs wiring elsewhere (not edited here)

- **`snd_trophy`**: decoded and measured but no native action fires it. It
  needs a trophy/achievement event in the shell or Steam integration. There is
  no such action today, which is why it is not in the `Cue` enum.
- **`snd_category_decide`**: horizontal category changes currently emit
  `Cue::Move` / `snd_cursor`. If firmware uses `snd_category_decide` for the
  category icon move, the shell needs a distinct cue call; the waveform decodes
  identically to `snd_cursor`, so there is no audible difference to measure
  either way, and no decompiled call site proves the assignment.
- **`snd_system_ok` on successful setting application**: the shell plays
  `Cue::Confirm` (`snd_decide`) for every activation, including writing a
  setting. Firmware's module-action path calls `snd_system_ok`. Wiring that
  needs an action-specific cue in `shell.cpp` after a successful write; the
  audio layer already exposes `Cue::Accept` for it.
- **`snd_error`**: must stay unmapped; its source is silent. It remains
  decoded only so the inventory is complete and any future firmware call site
  can be checked against real bytes.
- **Theme `.p3t` roles**: all five roles the native reader supports are already
  wired by `setThemeSounds`. No missing role was found.
- **Volume/key tone**: gated in `src/shell.cpp:1179-1180` and applied to the
  audio layer; no change needed there.
- **View**: no audio wiring is needed. `src/view.cpp` only generates the
  repeated move events; its 67 ms repeat interval is longer than the 39 ms
  Move sample, and the audio layer adds the 45 ms floor.

## 8. Honest limits and ISC-11 verdict

- Cue identity, source, sample rate, channel count, byte count, peak, RMS, and
  duration are measured from the firmware RCO and from the running native app.
- The mapping for `snd_cursor`, `snd_decide`, `snd_cancel`, and `snd_system_ok`
  has decompiled call-site evidence. The mapping for `snd_option` and
  `snd_system_ng` is label/role inference; no decompiled call site exists in
  the available dump. `snd_trophy` and `snd_category_decide` remain unwired.
- There is no recorded firmware audio to compare against, so cue start latency,
  device buffering, and final loudness on a real PS3 are unverified. The native
  app uses one `QAudioSink`, so it cannot mix a long cue with a second cue
  (the firmware may; the trophy cue is the obvious case).
- The retrigger guard uses the decoded clip duration (with the 45 ms Move
  floor), not a firmware-measured debounce interval. It is a conservative
  rule: the same cue is never restarted while its sample is still sounding.
  A long user theme cue therefore suppresses further Move ticks until it ends.
- The single firmware volume constant could not be read out of the packed PRX,
  so the absolute reference level is unknown; the relative per-cue levels are
  exact because they are in the PCM.
- ISC-11 remains open: this work establishes the cue inventory, the mapping,
  the silent-source finding, the retrigger behavior, and a reproducible trace,
  but not reference-correct timing/mixing against a recording.

## 9. Files touched

- `src/interaction_audio.cpp`
- `src/interaction_audio.h`
- `audit/INTERACTION-AUDIO.md` (this document)

No other repository file was edited by this work. Build/run logs and captures
were written under `C:\dev\out\native-audio` for evidence only.

## 10. Phase 14.4 / P8 first slice (2026-10-27)

Scope: turn the audio layer from a fit into a measured component where the local
4.93 modules carry the value, and give the gate a bounded way to assert a cue
decision without listening. The player files are the only code edited; the shell
call sites are reported as hunks in 10.3 for the passes that own `shell.cpp`.

Status: the firmware's single cue gain is now measured at **1.0 (unity)**, the
cue player exposes a bounded `playAt(cue, ms)` decision API, and the trace emits
one machine-readable line per decision (including retrigger and no-device
decisions). ISC-11 stays open only for what still needs a firmware recording or a
decompiled paf sound-context: start latency, cross-cue mixing, and the debounce
rule.

### 10.1 The single firmware cue gain is measured: 1.0 (unity)

Section 2 said the wrapper's single volume constant "was not recovered from the
packed PRX". It is recovered now. The decrypted module set in
`C:\dev\xmb\fw493\vsh_module` is a plain big-endian ELF64 with
`file offset = VA + 0xF0` for the code segment.

`explore_plugin.prx`, wrapper `FUN_001fdf4c` (VA `0x1fdf4c`):

```
VA 0x1fdf68: 3d 20 00 27   lis r9, 0x27
VA 0x1fdf6c: c0 29 20 00   lfs f1, 0x2000(r9)     ; f1 = *(float*)0x272000
VA 0x1fdf70: <bl>          play(f1, context, name, ..., 0)
```

VA `0x272000` maps to file `0x2720f0` and contains `3f 80 00 00`, i.e.
**float 1.0**. `sysconf_plugin.prx` has its own wrapper at VA `0x6f8c` which does
`lis r9, 0x15` / `lfs f1, -21648(r9)` (`0x150000 - 0x5490 = 0x14ab70`); file
`0x14ac60` also contains `3f 80 00 00`. Two independent modules, one value:
unity.

Consequences for the native mix:

- The reference per-cue level is entirely the level baked into the VAG PCM; there
  is no per-cue gain or pan, which the 28-byte descriptor already showed.
- `interaction_audio.cpp` now names the constant
  `kFirmwareCueGain = 1.0f` with that provenance and applies it once per sink
  (`Impl::gain() = clamp(userVolume * kFirmwareCueGain)`). At the default 60 %
  user volume the sink gain is 0.600000, so the audible result is unchanged from
  the previous build; what changed is that the constant is now a measurement
  rather than an unstated 1.0.
- The XMB volume preference (default 60, `src/preferences.cpp:65`) is the only
  remaining user scale. It is a native UI control, not a firmware cue constant.

### 10.2 Player internals changed

`src/interaction_audio.h`:

1. **`Cue::Trophy` appended** (seventh slot). Existing indices keep their
   meaning. `snd_trophy` was already decoded but unreachable; it now has a cue
   value for the shell's trophy-notification hook (10.3, H5).
2. **`Decision`** enum: `Played`, `RetriggerSkipped`, `Disabled`, `Silent`,
   `SourceUnavailable`, `OutputUnavailable`, `Rejected`.
3. **`playAt(Cue, qint64 atMs)`** — the bounded, testable entry point. Rejects a
   negative or greater-than-24 h timestamp (`kMaximumTimeMs`) and an invalid cue
   value without touching the retrigger history. `play(cue)` is exactly
   `playAt(cue, clock.elapsed())`. Time is monotonic by contract; a backwards
   timestamp reads as age 0, so it can only be suppressed, never used to bypass
   the guard.
4. **`cueName()` / `decisionName()`** static names so a gate does not hard-code
   strings.

`src/interaction_audio.cpp`:

5. **One trace line per decision**, not only per play. The trace now covers
   `disabled`, `silent`, `source-unavailable`, `output-unavailable`, `rejected`,
   `retrigger-skip`, and `play`. This is what makes the decision path assertable
   on a machine with no audio device: the decision is logged before output is
   opened.
6. **Trace grammar** (Qt log, i.e. stderr; parse with
   `^interaction audio: cue (\w+) t=(\d+)ms decision=([a-z-]+)`):
   ```
   interaction audio: cue <Name> t=<ms>ms decision=<name>[ <key=value>...]
   ```
   `play` carries `source= rate= channels= bytes= peak= rms= duration= guard=
   output=`; `retrigger-skip` carries `age= guard= source=`; the refusals carry
   `reason=` or `volume=`.
7. **`XMB_AUDIO_TRACE` now requires the value `1`** (and `XMB_AUDIO_LOG=1` still
   works). Previously any set value enabled tracing, so `XMB_AUDIO_TRACE=0`
   switched it *on*. Verified off below.
8. **Clip duration rounds up** (`ceil(frames*1000/rate)`): the 1 876-frame
   tick is 39.08 ms, so the guard is 40 ms, never 39. This only ever delays a
   retrigger by the sub-millisecond remainder of the sample.
9. **Same-cue retrigger guard re-verified**, now through the decision API:
   guard = clip duration for Confirm/Back/Error/Options/Accept/Trophy, and
   `max(45 ms, clip duration)` for Move (45 ms floor remains a fit; see 10.5).
   A different cue still interrupts the current one. History is cleared on a
   theme-sound change. The startup line reports the gain, and every decision
   reports its own guard.
10. **All decoded cue measurements are unchanged**: cursor/decide/option
    7 504 PCM bytes, peak 0.093811, RMS 0.007040; cancel 7 504 bytes, peak
    0.041595; system_ok 57 792 bytes, 301 ms; system_ng 153 664 bytes, 801 ms;
    trophy 96 040 bytes mono, 1 001 ms; snd_error still silent and unplayed.

What was deliberately **not** changed: no per-cue gain, no normalization, no
pan, and no software mixer. The RCO has no gain/pan to restore and the firmware
gain is unity, so adding any would be a change away from the reference. Cross-cue
mixing remains unimplemented because no artifact yet proves the firmware
overlaps cues (10.5).

### 10.3 Cue call-site hunks for `src/shell.cpp` (owned elsewhere)

Line numbers are this tree's (`shell.cpp` is 2 780 lines); apply by context if
the in-flight shell pass has moved them. `Cue::Trophy` exists only after this
slice, so H5 must land with (or after) this change.

#### Existing call sites (no diff; verdict per firmware evidence)

| # | file:function | line | action | cue | evidence | verdict |
|---|---------------|------|--------|-----|----------|---------|
| 1 | `shell.cpp:Shell::horizontal` | 1405 | category move | Move (`snd_cursor`) | `snd_cursor` in focus handlers (explore 9245/9318); `snd_category_decide` is not referenced by `explore_plugin.prx` at all | keep |
| 2 | `shell.cpp:Shell::vertical` | 1412 | row move | Move (`snd_cursor`) | same | keep |
| 3 | `shell.cpp:Shell::activate` | 1422 | message box confirm | Confirm (`snd_decide`) | `snd_decide` at explore 33256/34446/34899-34914 | keep |
| 4 | `shell.cpp:Shell::activate` | 1428 | dismiss a message | Accept (`snd_system_ok`) | `snd_system_ok` on module-action completion (explore 22617/38936); dismiss is a guess | keep, guess |
| 5 | `shell.cpp:Shell::activate` | 1432-1433 | entry press | Error / Options / Confirm | Error `snd_system_ng` has code refs (10.4); Options `snd_option` has code refs in sysconf (10.4); Confirm traced | H1 + H2 |
| 6 | `shell.cpp:Shell::toggleOptions` | 1724 | open in-player panel | Options (`snd_option`) | label/role + sysconf code refs | keep |
| 7 | `shell.cpp:Shell::toggleOptions` | 1739 | open item panel | Options | same | keep |
| 8 | `shell.cpp:Shell::optionsMove` | 1750 | panel cursor | Move | `snd_cursor` focus evidence | keep |
| 9 | `shell.cpp:Shell::optionsActivate` | 1761 | unavailable option | Error | `snd_system_ng` code refs | keep |
| 10 | `shell.cpp:Shell::optionsActivate` | 1776 | Information row | Confirm | generic decide | keep, guess |
| 11 | `shell.cpp:Shell::optionsActivate` | 1785 | open Sort By submenu | Options | open-menu role | keep |
| 12 | `shell.cpp:Shell::optionsActivate` | 1794 | sort applied | Confirm | generic decide | keep, guess |
| 13 | `shell.cpp:Shell::optionsActivate` | 1808 | open remove-confirm box | Options | open-surface guess | keep, guess |
| 14 | `shell.cpp:Shell::optionsActivate` | 1815 | Jellyfin restart | Confirm | generic decide | keep, guess |
| 15 | `shell.cpp:Shell::optionsClose` | 1846 | back out of submenu | Back (`snd_cancel`) | `snd_cancel` on close (explore 26546) | keep |
| 16 | `shell.cpp:Shell::dismissOptions` | 1856 | close panel | Back | same | keep |
| 17 | `shell.cpp:Shell::chooseSubtitleTrack` | 2050 | track off / change | Back / Confirm | no decoded call site | keep, guess |
| 18 | `shell.cpp:Shell::chooseAudioTrack` | 2089 | track switch | Confirm | no decoded call site | keep, guess |
| 19 | `shell.cpp:Shell::back` | 2201 | page / media / message back | Back (`snd_cancel`) | `snd_cancel` on close | keep |
| 20 | `shell.cpp:Shell::promoteNotification` | 1343 | trophy popup appears | **Trophy (`snd_trophy`)** | np_trophy_util notification carries `sound=snd_trophy` (10.4) | **H5** |
| 21 | `shell.cpp:Shell::activate` (`Action::SetSetting`) | 1437-1449 | commit a stored value | **Accept (`snd_system_ok`)** | module-action completion (explore 22617/38936) | **H1/H2/H3/H4** |

Already-correct-but-unwired slots:

- `snd_category_decide`: stdout the same PCM as `snd_cursor` (section 1), so
  whatever the firmware uses it for, the native Move cue is audibly identical.
  No reference in `explore_plugin.prx`; references live in
  `category_setting_plugin.prx`, `basic_plugins.prx`, `sysconf_plugin.prx` and
  `vshmain_ext.prx`, which fits a category-*setting* change rather than the XMB
  icon move. No hunk; a separate `Cue` would be indistinguishable.
- `snd_error`: must stay unmapped (silent source, section 3).
- `snd_trophy`: H5 gives it the only native surface that exists; nothing raises
  a trophy notification today (no `Shell::notify` caller passes a trophy icon),
  so the hook is dormant but correct when a trophy source lands.
- The in-player panel's Repeat and Shuffle toggles
  (`OptionCommand::RepeatMode`, `ToggleShuffle`) currently play no cue. They are
  outside the audit's six XMB slots and there is no decoded call site for a
  repeat/shuffle sound, so no hunk is proposed; adding one would be a guess.

#### H1 — a value write plays Accept after it commits

```diff
--- a/native-xmb/src/shell.cpp
+++ b/native-xmb/src/shell.cpp
@@ -1447,7 +1447,11 @@ void Shell::activate() {
-        if(preferences_.setValue(entry.setting,entry.option) && !pages_.isEmpty())pages_.removeLast();break;
+        if(preferences_.setValue(entry.setting,entry.option)) {
+            interactionAudio_.play(InteractionAudio::Cue::Accept);
+            if(!pages_.isEmpty())pages_.removeLast();
+        } else interactionAudio_.play(InteractionAudio::Cue::Error); // the write was rejected: a guess
+        break;
```

The up-front `Confirm` must not also fire, or the 301 ms `snd_system_ok` would
cut off the decide tick in the same activation. H2 is the other half:

```diff
@@ -1430,8 +1430,12 @@ void Shell::activate() {
     if(entries().isEmpty() || loading_)return;
     const auto entry=entries()[selection()];
-    interactionAudio_.play(entry.action==Action::Unavailable?InteractionAudio::Cue::Error:
-        entry.action==Action::SettingChoices?InteractionAudio::Cue::Options:InteractionAudio::Cue::Confirm);
+    // Committing a value is the firmware's module-action path: snd_system_ok when
+    // the action completes (explore_plugin.prx FUN_00030954 and FUN_00062e6c both
+    // call it there). SetSetting is therefore silent here and plays Cue::Accept
+    // after its write below.
+    if(entry.action!=Action::SetSetting)
+        interactionAudio_.play(entry.action==Action::Unavailable?InteractionAudio::Cue::Error:
+            entry.action==Action::SettingChoices?InteractionAudio::Cue::Options:InteractionAudio::Cue::Confirm);
     switch(entry.action) {
```

#### H3 — host (computer) settings use the same commit rule

```diff
@@ -1444,7 +1444,9 @@ void Shell::activate() {
             const bool ok=entry.setting=="host/volume"?host::setMasterVolume(entry.option.toInt()):host::setMasterMuted(entry.option.toBool());
-            if(!ok)showMessage("The system volume could not be changed.",MessageKind::Page);else if(!pages_.isEmpty())pages_.removeLast();
+            if(!ok){interactionAudio_.play(InteractionAudio::Cue::Error);showMessage("The system volume could not be changed.",MessageKind::Page);}
+            else {interactionAudio_.play(InteractionAudio::Cue::Accept);if(!pages_.isEmpty())pages_.removeLast();}
             break;
         }
```

The failure branch is a guess: no failed host write was decoded, so `snd_system_ng`
is chosen for symmetry with the unavailable-action cue.

#### H4 — clearing back to the system theme is also a commit

```diff
@@ -1439,7 +1439,10 @@ void Shell::activate() {
         if(entry.setting=="appearance/theme" && entry.option.toString().isEmpty() && backgroundMode()==2 && wallpaper().isEmpty()) {
             // Back to the system's theme with no wallpaper of the user's own: the original background returns.
-            if(preferences_.setValues({{"appearance/theme",QString{}},{"appearance/background",0}}) && !pages_.isEmpty())pages_.removeLast();
+            if(preferences_.setValues({{"appearance/theme",QString{}},{"appearance/background",0}})) {
+                interactionAudio_.play(InteractionAudio::Cue::Accept);
+                if(!pages_.isEmpty())pages_.removeLast();
+            } else interactionAudio_.play(InteractionAudio::Cue::Error);
             break;
         }
```

#### H5 — trophy notification plays `snd_trophy`

```diff
@@ -1345,6 +1345,10 @@ void Shell::promoteNotification() {
     auto& front=notifications_.first();
     front.expiresAt=notificationClock_.elapsed()+notificationLifetimeMs;
     if(!notificationTimer_.isActive())notificationTimer_.start();
+    // Firmware: the trophy notification carries sound "snd_trophy"
+    // (np_trophy_util.prx builds plugin=system_plugin, sound=snd_trophy).
+    if(front.icon.startsWith("tex_notification_trophy_"))
+        interactionAudio_.play(InteractionAudio::Cue::Trophy);
     if(notificationLog_)qInfo().noquote()<<"notify: show"<<front.title<<front.icon<<notificationLifetimeMs<<"ms";
```

`promoteNotification()` is the "popup actually appears" moment, not enqueue, so a
queued notification cannot sound early. `Shell::notify` already accepts the four
`tex_notification_trophy_*` icons, which is what this matches.

### 10.4 Firmware evidence for the two cues that were label-only

The decrypted modules resolve the open questions from section 2 without a
disassembler: strings live in the module's own `.rodata`, and the code that uses
them materialises their VA with `lis` + `addi`. `snd_option`, `snd_system_ng`,
`snd_trophy` and `snd_category_decide` all appear in decrypted modules:

- `snd_option` — `sysconf_plugin.prx` string VA `0x13d308`, materialised at code
  VAs `0x670cc`, `0x67188`, `0x6723c`, `0x6739c`; each passes it as the sound
  argument of the option-menu command builder (`bl 0x5d740`), i.e. the cue is
  attached to the option-menu action rather than played at that instruction.
  Also referenced by `vshmain_ext`, `webbrowser`, `photoupload`, `netconf`,
  `autoupdateconf`.
- `snd_system_ng` — `explore_plugin.prx` strings at VAs `0x247c50`, `0x24af30`,
  `0x253668`, `0x255608`, `0x25c240` with code references at `0x79dc4`,
  `0x9d25c`, `0xe8f10`, `0x1691b8`, `0x28fbc`, `0xf7c48`, `0xf7c60`,
  `0x185498`, `0x1854b4`, `0xa8060`, `0x12eeac`. At `0x79dc4` the code loads
  `0x247c50` into r3 and calls the play wrapper directly
  (`bl 0x1fdf4c`), so the XMB root module itself plays the negative cue.
  `sysconf_plugin.prx` strings at VAs `0x1384a8`, `0x139b80`, `0x13a278`,
  `0x13a578`, `0x13b980`, `0x144658`, `0x146c50` have code references at
  `0x265d8`, `0x36e8c`, `0x3f338`, `0x3f6ec`, `0x41874`, `0x4d1e8`, `0x4e1d4`,
  `0x4e2a8`, `0xad2b0`, `0x26864`; the site at `0x265d8` calls the module's
  sound wrapper (`0x6f8c`), which loads the unity gain and plays.
- `snd_trophy` — `np_trophy_util.prx` string VA `0x53380`, materialised at code
  VA `0x92c4` and passed as the sound-name argument of a notification descriptor
  call at `0x92e4`. The surrounding strings are the proof of role:
  `system_plugin`, `np_trophy_util_notification`, `msg_trophy_obtain`,
  `Trophy`, `snd_trophy`, `msg_error_trophy_obtain`, `tex_notification_info`.
  A trophy notification *is* the `snd_trophy` surface.
- `snd_category_decide` — referenced by `category_setting_plugin.prx`,
  `basic_plugins.prx`, `sysconf_plugin.prx` and `vshmain_ext.prx`, but **not** by
  `explore_plugin.prx`; no code reference in `vshmain_ext.prx` (address
  materialisation scan found none). It is a category-settings sound, not the
  XMB icon move, and it is bit-identical to `snd_cursor` anyway.

These are raw-instruction code references. `snd_system_ng` in
`explore_plugin.prx` reaches the same `FUN_001fdf4c` wrapper the decompiled dump
shows, so `Cue::Error` is call-site evidence equal to section 2's four cues.
`snd_option` (command-descriptor argument) and `snd_trophy` (notification-
descriptor argument) are one step below that: the string is provably used by the
module's code, but the play moment is a later consumer. Both are stronger than
the label-only inference they replace, and both are labelled in 10.3.

### 10.5 Scripted evidence (this machine has a real output device)

Build:

```
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-audio
```

Result: `Built C:\dev\out\native-audio\re-ps3-native.exe`. Only the pre-existing
`QtAudio::UnderrunError` deprecation warning remains in `interaction_audio.cpp`
(`:506`); no new warning.

Run (isolated data directory, key-tone default on, volume default 60):

```
$env:XMB_AUDIO_TRACE='1'
re-ps3-native.exe --dev-flash C:\dev\rpcs3\bin\dev_flash `
  --data-dir C:\dev\out\native-audio\run-data `
  --script "full,wait,RIGHT,wait,DOWN,wait,ENTER,ENTER,ENTER,ESC,wait,RIGHT,wait,RIGHT,wait,DOWN,wait,ENTER,wait,ENTER,wait,TRIANGLE,wait,ESC,wait,quit"
```

The script goes Game → Network (two Moves), activates the unavailable Remote
Play row (Error), dismisses its message (Accept), activates it again inside the
801 ms guard (retrigger skip), backs out (Back), moves to Friends (three Moves),
opens Players Met (Confirm), dismisses the information message (Accept), opens
the options panel (Options) and closes it (Back). Exit 0. Trace
(`C:\dev\out\native-audio\evidence\cue-trace.txt`):

```
interaction audio: firmware cue gain 1.000000 (measured: explore_plugin.prx VA 0x272000, sysconf_plugin.prx VA 0x14ab70), user volume 0.600000 -> sink gain 0.600000
interaction audio: cue Move t=5363ms decision=play source=snd_cursor rate=48000 channels=2 bytes=7504 peak=0.093811 rms=0.007040 duration=40ms guard=45ms output=Speakers (3- DualSense Wireless Controller)
interaction audio: cue Move t=5988ms decision=play source=snd_cursor rate=48000 channels=2 bytes=7504 peak=0.093811 rms=0.007040 duration=40ms guard=45ms output=Speakers (3- DualSense Wireless Controller)
interaction audio: cue Error t=6609ms decision=play source=snd_system_ng rate=48000 channels=2 bytes=153664 peak=0.071014 rms=0.007258 duration=801ms guard=801ms output=Speakers (3- DualSense Wireless Controller)
interaction audio: cue Accept t=6921ms decision=play source=snd_system_ok rate=48000 channels=2 bytes=57792 peak=0.027313 rms=0.005894 duration=301ms guard=301ms output=Speakers (3- DualSense Wireless Controller)
interaction audio: cue Error t=7238ms decision=retrigger-skip age=629ms guard=801ms source=snd_system_ng
interaction audio: cue Back t=7546ms decision=play source=snd_cancel rate=48000 channels=2 bytes=7504 peak=0.041595 rms=0.004050 duration=40ms guard=40ms output=Speakers (3- DualSense Wireless Controller)
interaction audio: cue Move t=8172ms decision=play source=snd_cursor rate=48000 channels=2 bytes=7504 peak=0.093811 rms=0.007040 duration=40ms guard=45ms output=Speakers (3- DualSense Wireless Controller)
interaction audio: cue Move t=8803ms decision=play source=snd_cursor rate=48000 channels=2 bytes=7504 peak=0.093811 rms=0.007040 duration=40ms guard=45ms output=Speakers (3- DualSense Wireless Controller)
interaction audio: cue Move t=9428ms decision=play source=snd_cursor rate=48000 channels=2 bytes=7504 peak=0.093811 rms=0.007040 duration=40ms guard=45ms output=Speakers (3- DualSense Wireless Controller)
interaction audio: cue Confirm t=10047ms decision=play source=snd_decide rate=48000 channels=2 bytes=7504 peak=0.093811 rms=0.007040 duration=40ms guard=40ms output=Speakers (3- DualSense Wireless Controller)
interaction audio: cue Accept t=10673ms decision=play source=snd_system_ok rate=48000 channels=2 bytes=57792 peak=0.027313 rms=0.005894 duration=301ms guard=301ms output=Speakers (3- DualSense Wireless Controller)
interaction audio: cue Options t=11297ms decision=play source=snd_option rate=48000 channels=2 bytes=7504 peak=0.093811 rms=0.007040 duration=40ms guard=40ms output=Speakers (3- DualSense Wireless Controller)
interaction audio: cue Back t=11922ms decision=play source=snd_cancel rate=48000 channels=2 bytes=7504 peak=0.041595 rms=0.004050 duration=40ms guard=40ms output=Speakers (3- DualSense Wireless Controller)
```

All six existing cues are present (Move, Confirm, Back, Error, Options, Accept)
plus one `retrigger-skip` (age 629 ms of the 801 ms `snd_system_ng` guard). This
machine reports a real default output (`Speakers (3- DualSense Wireless
Controller)`), so `decision=play` means `QAudioSink::start` ran, not just that
the guard passed. On a machine with no output device the same lines appear with
`decision=output-unavailable reason=no-device`, which still proves the cue
selection and guard; that is why the decision is logged before the device is
opened.

`XMB_AUDIO_TRACE=0` was also run with `RIGHT`: 0 trace lines, exit 0, so the
tightened env check is real.

The one cue not exercised is `Trophy`: nothing raises a trophy notification in
this build (see H5), so the hook is verified only by the startup decode of
`snd_trophy` and by `available(Cue::Trophy)` being true. That is the honest
limit of the reachable surface today.

### 10.6 Gate

```
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --capture --exe C:/dev/out/native-audio/re-ps3-native.exe
```

`PASS: 6/6 layers within budget` on the final binary (audio-only changes; the
pixel path is untouched). Frame PSNR 18.30 vs baseline 18.02, SSIM 0.8323 vs
0.8301; category icon row 20.92 vs 21.05, SSIM 0.8990 vs 0.8950; the other four
layers match exactly.

### 10.7 Remaining fits and what would close each

| Fit | Current value | Why it is still a fit | Artifact that closes it |
|-----|---------------|-----------------------|-------------------------|
| Move retrigger floor | 45 ms | no firmware debounce value traced | decrypted `paf.prx`/`libpaf.sprx` implementation of `paf_paf_B93AFE7E` (the sound context it plays through), or a PS3 audio recording of held navigation |
| Same-cue guard duration | decoded clip length (ceil) | firmware may queue rather than drop a retrigger | the same paf sound-context code, or a recording of a double press |
| Cross-cue mixing | none (one `QAudioSink`, previous cue stopped) | whether the firmware overlaps cues is unproven | a PS3 recording with a long cue (trophy/system_ng) plus navigation, or the paf context's voice limit |
| Start latency / device buffering | Qt default | no loopback measurement | WASAPI loopback capture of the native app running the same script |
| Trophy cue timing | popup shown | the notification player's play point is not decoded, only the descriptor | decompiled consumer of `np_trophy_util_notification` in `system_plugin`/`vshmain` |
| Failed host-write cue | Error (`snd_system_ng`) | no failed-write call site decoded | decompiled `sysconf_plugin.prx` write-failure branch (start at VAs `0x265d8`, `0x4d1e8`, `0xad2b0`) |
| Dismiss-a-message cue | Accept (`snd_system_ok`) | box cancel path unproven (section 2) | decompiled message-box cancel handler in `explore_plugin.prx` |
| Category-move cue | Move (`snd_cursor`) | `snd_category_decide` is bit-identical to `snd_cursor`, so there is nothing audible to measure | a decompiled `snd_category_decide` call site, only if the two waveforms ever diverge |
| Absolute loudness on hardware | unity cue gain × user volume | gain is measured, hardware chain is not | a calibrated PS3 analog capture at a known system volume |

No firmware duration needed implementing: durations are exact from the decoded
frame counts (`frames / 48000`), and the guard now uses the ceiling of that
value.

### 10.8 Files touched by this slice

- `src/interaction_audio.cpp`
- `src/interaction_audio.h`
- `audit/INTERACTION-AUDIO.md` (this section)

`src/shell.cpp` was **not** edited; H1–H5 are for the pass that owns it. Evidence
tooling and logs are outside the repository under
`C:\dev\out\native-audio` (`prx_probe.py`, `prx_load.py`, `scan_refs.py`,
`scan_call.py`, `dump2.py`, `evidence/cue-sequence.log`, `evidence/cue-trace.txt`).
