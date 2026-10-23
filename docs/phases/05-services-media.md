# Phase 5 handoff — OS services and media

Status: future phase; some service work is already implemented.

## Entry checkpoint

Use a documented Phase 4 baseline. Regenerate `out/os-inventory/` and preserve the
reference boot trace. Track three separate facts: a module is present, its imports
resolve, and its behavior has actually been observed.

## Current support and gaps

Implemented pieces include filesystem overlays, USB shutdown events, audio-ring
retirement, IPv4 sockets, firmware SPURS execution, Windows CNG random generation,
and a persistent emulated console identity. These subsystems still have limitations.

Recorded reference modules absent from the current image:

`libat3dec`, `libavcdec`, `libatrac3plus`, `libsmvd4`, `libasfparser2`, `sacd`,
`libmtp`, `libtiffdec`.

Important gaps: process-spawn syscall27, crypto-engine create976, IO-error listener524,
startup-mode decoding of LPAR1 reboot packets, complete SPU semantics and media paths.
Crypto RNG984 is implemented; that does not implement the whole crypto engine.

## Work next

1. Select one concrete feature and identify its actual caller/module/service chain.
2. Preserve the failing call arguments, return values and firmware version.
3. Analyze the required firmware or implement the host behavior with explicit errors.
4. Observe completion through the existing boot or feature interaction.
5. Record unsupported paths and any fallback behavior before adding the next feature.

Use `xmbrt/k_prx.cpp` for module lifecycle, `k_fs.cpp` for overlays, `k_rsxaudio.cpp`
for audio, `k_net.cpp` for networking, and `k_security.cpp` for RNG/identity.
Missing plugin interfaces must fail instead of reporting successful stand-in loads.

## State to preserve

- The filesystem upper layer, its metadata and whiteouts; retain the RPCS3 lower tree.
- `out/xmbrt/console-psid.bin`:16-byte emulated identity. Do not regenerate it during
  routine rebuilds or print its contents in logs. `XMBRT_CONSOLE_PSID` is an optional override.
- Ghidra projects, patched function CSVs and local compiler changes/patches.

## Exit criteria and handoff

Each supported feature completes with recorded evidence and persists relevant state
across restart. Audio teardown, USB teardown and guest reboot complete. Any process
dependency has a real implementation or a demonstrated supported fallback. Media,
network and peripheral limits are listed explicitly. Hand Phase 6 a reproducible
normal launch, a supported-feature matrix, and stable state locations.
