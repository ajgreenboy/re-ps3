# XMB proof of concept and phase handoffs

Updated: October 23, 2026. Repository root: the existing XMB project.

## Start here

The active direction changed to a native C++ recreation on October 23, evening.
Start with [the native application handoff](../native-xmb/HANDOFF.md) and
[its build instructions](../native-xmb/README.md). The documents below preserve
the firmware-execution proof of concept as reference evidence.

The [full XMB feature/presentation audit](../native-xmb/audit/README.md) and
[October24 handoff](../native-xmb/audit/HANDOFF.md) cover the current native work,
including shading, animation timing, all indexed categories and the orientation fix.

1. [Proof of concept: run, build, evidence](POC.md).
2. [Phase 4: stability and visual fidelity](phases/04-stability-rendering.md) — active.
3. [Phase 5: OS services and media](phases/05-services-media.md) — future phase.
4. [Phase 6: desktop launcher](phases/06-desktop-launcher.md) — future phase.
5. [Handoff template](HANDOFF-TEMPLATE.md) — copy at a phase boundary.
6. [Screenshots of the observed boot stages](SCREENSHOTS.md).

`ISA.md` holds the project criteria. `HANDOFF.md` holds the detailed investigation
history; start at its newest section 3a. These shorter docs describe the current
proof of concept and the work required to move forward.

## Current baseline

| Item | Latest built checkpoint |
|---|---|
| Firmware | PS3 4.93 |
| Native host | Windows, clang-cl/lld, OpenGL |
| Linked firmware | 46 modules |
| Build | 503 generated objects + 23 runtime objects |
| Observed UI | Startup notices; Game, Video and Music navigation; advancing clock |
| Still incomplete | Reliable recovery/boot, rendering fidelity, SPURS stability, wider media/process services |

These counts describe a dated checkpoint. Rebuild logs and the generated inventory
are authoritative for later builds. A module binding or syscall registration is
not evidence that its semantics are complete.
