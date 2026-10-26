# Category icons across all colours — 2026-10-25

## Latest correction: Original and all built-in colours

The Steam integration screenshots used a fresh Original profile and exposed the remaining gray/dark interiors.
Original had still selected the refractive firmware program; fixed colours already used the opaque material.
Normal presentation now uses the opaque material for every built-in colour, including over wallpaper. Original's
tint follows the current/next original monthly RGB textures; fixed colours retain their subtle palette tint.
Shape alpha, normal-map orientation, focus lighting, geometry and explicit page fades remain unchanged.
Custom `.p3t` icon images retain their supplied colours. Firmware/debug shaders remain explicit diagnostic options.

All six colours were captured at 00:00 and 12:00, 960x540, with the same animation/focus clocks. The sampled
8x6-pixel interior of the focused controller had these day RGB values, identical at night:

| Colour | Interior RGB | Luma |
|---|---|---:|
| Original (October) | 204, 176, 128 | 178.49 |
| Blue | 184, 192.81, 204 | 191.75 |
| Purple | 196, 188, 204 | 190.86 |
| Red | 204, 186, 187 | 189.90 |
| Green | 189, 204, 198 | 200.38 |
| Silver | 201, 202, 204 | 201.93 |

The Original wallpaper replay also exited zero. Evidence is local under
`out/native-reference/steam-integration/theme-check/` and `theme-interiors.json`. This verifies opaque, tinted
interiors across the built-in colours; it does not establish exact firmware material parity. The material and
Original tint strength remain an appearance reconstruction.

## Earlier fixed-colour investigation

The user reported visibly wrong icons. Computer Use reproduced near-black metallic interiors on the running
Green theme. The normal maps were loading; PSN's full-colour image was unaffected.

## Diagnosis and change

Local scripted captures isolated normal/diffuse/environment textures and replayed existing captured fragment
constants, without controlling or capturing RPCS3. Captured night constants made the icons lighter, but the
derived day constants still darkened the reconstructed fixed-colour background. Existing day captures in
fp-dump2 corroborate the day light/luminance values; replacing those with night values would be incorrect.

A separate native bug coupled the selected background month to the calendar used by amb.bmp. Fixed colours
now choose only the background palette. The icon material keeps the actual/diagnostic date. That correction
alone did not resolve the dark fixed-colour interiors.

Fixed colours now use a native opaque GL material on the original firmware vertex program and original
normal maps. Surface lighting and a subtle colour tint affect RGB; alpha comes only from shape coverage
and explicit page-transition opacity. The material skips background-cut blits because it is opaque.
This is an appearance reconstruction, not a newly verified firmware material port.

At this earlier stage Original still used the firmware glass shader; the latest correction above supersedes that
selection. PSN and custom theme images retain their own colours.
`XMB_ICON_FIRMWARE=1` selects the firmware path for fixed-colour diagnosis; existing DEBUG/UCODE diagnostics
also retain that path. `XMB_ICON_LOG_CONSTANTS=1` reports the first draw's named inputs.

## Evidence and remaining limits

- Default and isolated Release builds succeeded. Local Green captures before/after are
  out/native-reference/icon-fix/normal.png and opaque.png.
- Computer Use inspected the rebuilt default launcher with the user's Green preference preserved. The icons
  were visibly light, opaque and subtly green; the normal launcher was left open.
- The stable material has not been measured against a matched Green reference. The firmware path remains
  available for diagnosis; full material parity and exact colour composition remain open.
- Two pre-existing debug edits in icons.cpp were preserved separately from this change.

Use these facts rather than marking category-icon visual parity complete.
