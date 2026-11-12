# Network > Wake on LAN and Home Assistant (2026-11-07)

Two native pages after Tailscale in the Network column (not firmware surfaces; plan items H6 and H7 in
[PHASES-2026-11-07.md](PHASES-2026-11-07.md)). Code: `src/wol_client.*`, `src/homeassistant_client.*`,
`src/home_shell.cpp`.

## Wake on LAN

- **Machines** come from `<data>/wol.json`:

      {"devices": [{"name": "Server", "mac": "AA:BB:CC:DD:EE:FF", "host": "192.168.1.10"}]}

  Each row shows Online / Offline from a TCP probe of its host (3389, 22, 445, 80, or `probePort`). Cross on an
  offline machine sends the magic packet (3 times, ports 9 and 7, from every private interface on the broadcast's
  subnet) and reports when the machine answers (polls every 3 s, up to 120 s) or that it did not.
- **Add from This Network** lists the neighbours Windows has seen (IP + MAC from `GetIpNetTable2`; Hyper-V/WSL virtual
  adapters hidden). Cross appends one to `wol.json` (the file is re-validated before it is written).
- Rules: MACs and addresses are validated; only private/LAN broadcasts are ever used; no process or shell.

## Home Assistant

- **Setup**: create a long-lived access token in Home Assistant (Profile > Security) and put
  `{"url": "http://<HA address>", "token": "<token>"}` in `<data>/homeassistant.json`. On the next start the token
  moves into `<data>/homeassistant/session.bin` (DPAPI, this Windows user) and the json keeps only the address. The
  sealed token is bound to that address. On this network HA answers on **port 80** (`http://<ha-ip>`), not 8123.
- **Page**: rooms (HA areas, when the token's user may read them) or kinds as folders; inside, scenes and scripts run
  on Cross, lights / switches / fans / input booleans / covers toggle, media players play/pause, sensors and climate
  show their state. States are read back after every action.
- **Icon**: the server's own `/static/icons/favicon-1024x1024.png` (no sign-in needed), cached under
  `<data>/homeassistant/`; a drawn house until then.

## Verification (2026-11-07)

- Wake on LAN, real LAN: <server> (<server-ip>) shown Online; the add page listed the router, the TV, the HA VM
  and others. No magic packet was sent to the LAN (the client's own tests used loopback only: 3 x 102-byte packets,
  69/69 checks).
- Home Assistant, real server: reachable, answers 401 without a token; the page shows Set Up Home Assistant with the
  server's icon. With the client's mock server (`HA_URL`/`HA_TOKEN`): 14 entities in 4 rooms, Cross on a fan sent
  `homeassistant.toggle` and the state was read back.
- **Not verified**: a real wake; a real HA action (needs the token).
