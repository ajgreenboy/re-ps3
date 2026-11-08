# Network > Tailscale (2026-11-07)

A native page for this PC's Tailscale connection. This is not a firmware surface: the 4.93 Network column has no
VPN row. The row sits after Download Management and wears the Windows client's own icon (taken at runtime from
`tailscale-ipn.exe`, `src/service_icons.cpp`; a drawn nine-dot mark is the fallback).

## What it shows and does

- **Network column row**: "Connected as <host> - <tailnet IP>" (plus "via <exit node>"), "Off", "Sign-in needed" or
  "Not installed".
- **Tailscale page**:
  - **Tailscale Is On / Off**: Cross turns it on (`tailscale up`); turning it off (`tailscale down`) asks first,
    because devices on the tailnet lose this PC until it is back on.
  - **Exit Node**: None, then every device offering itself as an exit node (offline ones dimmed). Choosing one runs
    `tailscale set --exit-node=<ip>` (empty clears it).
  - **Devices**: this tailnet's devices, online first, with OS, tailnet IP, "last seen" for offline ones, and an
    exit-node mark. Cross pings the device over the tailnet (`tailscale ping -c 1`) and shows the answer.
  - **Sign In to Tailscale** (NeedsLogin only): `tailscale up` returns Tailscale's login URL, which opens in the
    browser.
- The status refreshes every 10 s while a Tailscale page is open.

## Implementation

- `src/tailscale_client.*`: the CLI over QProcess with argument arrays (no shell); `status --json` (8 s timeout),
  `up`/`down`/`set` (30 s), `ping` (15 s); 4 MB output cap; peer arguments limited to IP literals and
  `[A-Za-z0-9.-]` names, never starting with `-`; never logs in, out or switches accounts; stores nothing.
- `src/tailscale_shell.cpp`: the row and pages.

## Verification

- Standalone client: 61/61 self-test checks against a fake CLI (parsing, argument validation, timeouts, output cap,
  login-URL early stop).
- Real tailnet on this PC (Tailscale 1.104.5 installed with winget, already signed in): the row read "Connected as
  al-workstation - 100.118.251.42"; the page listed 8 devices; Cross on homelab-node2 answered
  "pong from homelab-node2 (100.108.36.91) via 192.168.1.122:41641 in 1ms"; the Exit Node page opened.
- **Not exercised on purpose** (they change this PC's live network): turning Tailscale off/on and choosing an exit
  node. They run the client calls above, which the fake-CLI tests cover.
