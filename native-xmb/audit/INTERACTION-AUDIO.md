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
