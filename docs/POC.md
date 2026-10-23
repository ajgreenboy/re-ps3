# Native XMB proof of concept

## What this demonstrates

PS3 firmware code can run through static PPC recompilation on Windows, call native
OS-service implementations, submit OpenGL work and render interactive XMB menus.
The current build is a development proof of concept, with incomplete visuals and
intermittent failure paths.

## Run

Open PowerShell in the repository root:

```powershell
& .\run-xmb.ps1
```

The script starts `out/xmbrt/xmbrt.exe` and restarts it when the guest requests a
supported reboot. Each launch records logs under `out/xmbrt/sessions/`.

Keyboard: arrows = D-pad; Enter = Cross; Escape = Circle; Home = PS button.
F12 records a live PPU/SPU/RSX snapshot without exiting the runtime.

The dirty-shutdown notice may appear. Return acknowledges its first page; Escape
returns from the optional filesystem-repair page. This bypasses the offered repair;
it does not demonstrate that filesystem recovery works. Format/delete/system-restore
actions require separate authorization.

## Build

For runtime-only changes, with generated objects already present:

```powershell
& .\xmbrt\build-xmbrt.ps1
```

Close the existing runtime before linking over its executable.

For module/function metadata or generator changes, use the current component pipeline:

```powershell
& .\work\rebuild-components.ps1
```

That script links the image, discovers descriptors/branches/switches, translates,
compiles and builds the runtime. It is currently a local orchestration script under
`work/`; preserve it when moving the workspace. Native tools and firmware must
already be installed in the existing dependency layout.

## Diagnose

```powershell
& .\run-xmb.ps1 -TraceCalls
& (Join-Path $env:USERPROFILE '.bun\bin\bun.exe') .\xenon\inventory-os.ts
```

Detailed tracing changes timing and produces large logs. Reproduce important
results in normal mode as well. The inventory tool reads the adjacent RPCS3 source
checkout and writes `out/os-inventory/`.

## Evidence and limits

See the [stage screenshot gallery](SCREENSHOTS.md) for the current native build.
These captures document visible states; they do not establish filesystem repair,
media playback or completion of future phases.

- Build: `work/component-body-refresh-build.log`, `out/compile.log`, `out/build-xmbrt.log`.
- Positive boot: session `20261023-162424-995`; Game -> Video -> Music; clock21:25 ->21:29.
- Live snapshots:48,382draws/696flips, then105,548draws/1,488flips.
- Normal-mode follow-up: session `20261023-162941-020` reached Game at21:31.
- Counterexample: session `20261023-162149-565` unloaded UI modules and became black.

Host-backed random services and persistent emulated PSID were observed working.
Full crypto-engine support, process spawning, all media modules, exact rendering and
long-run stability are not established. Firmware/derived binaries are local inputs,
not source-repository deliverables.
