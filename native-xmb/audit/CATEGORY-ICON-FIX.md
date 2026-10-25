# Category icons on fixed colours — 2026-10-25

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

Original colour still uses the firmware glass shader. PSN and custom theme images retain their own colours.
`XMB_ICON_FIRMWARE=1` selects the firmware path for fixed-colour diagnosis; existing DEBUG/UCODE diagnostics
also retain that path. `XMB_ICON_LOG_CONSTANTS=1` reports the first draw's named inputs.

## Evidence and remaining limits

- Default and isolated Release builds succeeded. Local Green captures before/after are
  out/native-reference/icon-fix/normal.png and opaque.png.
- Computer Use inspected the rebuilt default launcher with the user's Green preference preserved. The icons
  were visibly light, opaque and subtly green; the normal launcher was left open.
- The stable fixed-colour path has not been measured against a matched Green reference. Original's firmware
  path remains available; full material parity and exact fixed-colour composition remain open.
- Two pre-existing debug edits in icons.cpp were preserved separately from this change.

Use these facts rather than marking category-icon visual parity complete.
