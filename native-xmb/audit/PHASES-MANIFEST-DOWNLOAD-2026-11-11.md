# Manifest-driven game download & install (self-hosted source), 2026-11-11

A new capability in `xmb-manager`: download a game from a **user-supplied JSON manifest** (a repo the
owner controls, typically on the LAN or a local file share), verify it by hash, and install it into the
same `apps.json` library the XMB already reads. It is a package manager, not a store: it fetches only
what a manifest the owner wrote points at, and it never discovers, scrapes or bundles any third-party
source. This is the manager's first network module, so it reverses the "uses no network" boundary of
[MANAGER.md](../MANAGER.md) §1 — deliberately, and only when a manifest is explicitly given.

Read with [MANAGER.md](../MANAGER.md) (the registry, the virtual package, hashing, backup) and
[audit/CONTENT-REGISTRY.md](CONTENT-REGISTRY.md) (the `apps.json` the XMB consumes).

## 1. What already exists to reuse

| Piece | Where | What it gives the downloader |
|---|---|---|
| Registry | `apps.json`, `xmb` version 1 (MANAGER.md §5) | the row shape the XMB reads; a downloaded game is just a row with a real `executable` |
| Virtual package | `pkg/<id>/` + `pkg.json` `xmb-virtual-pkg/1` (MANAGER.md §5.1) | where an installed game's metadata and art live |
| Id derivation | `pkg-<platform slug>-<16 hex>`, FNV-1a over the canonical path (MANAGER.md §5.1) | stable ids without a random component |
| Hash infra | `romHash`/`romHashLabel`: `full-sha256-v1`, `partial-sha256-v1`, `folder-v1` (MANAGER.md §11) | integrity check on every downloaded file |
| Platform map | `platform-map.json` + built-in profiles (MANAGER.md §6.5) | platform → emulator resolution, reused as-is |
| Import path | `--import`, the package writer (MANAGER.md §7.1) | the install step is this same code with a downloaded file instead of a scanned one |
| Verify | `--verify-hashes`, `--validate` (MANAGER.md §7.1, §11) | post-download integrity re-check and relink |

## 2. Scope, and the line it won't cross

It does:

- Read one manifest file the owner points at (a path or URL), schema `xmb-source/1` (section 4).
- Download each entry's file(s) from `baseUrl` over HTTP/HTTPS with bounded memory, resume support, and a
  SHA-256 check against the manifest before anything is registered.
- Install into `pkg/<id>/` and write the `apps.json` row through the existing import/package path, so the
  XMB picks it up on the next refresh exactly like a scanned ROM.
- Work offline-first: a manifest whose `baseUrl` is a `file://` or UNC path needs no internet at all.
- List what is on the source: browse the repo's file tree (an `index.json` or a plain directory listing) so
  the owner can see what a manifest points at before downloading anything.

It deliberately does **not**:

- Ship, discover, or default to any source. There is no built-in catalogue and no "add community source"
  button. A manifest is an explicit argument or an explicit file the owner put on disk.
- Include a torrent client, repack support, or any downloader other than plain HTTP(S) of the files the
  manifest lists.
- Decrypt, patch, strip or bypass any DRM, NPDRM or licence check. An encrypted PS3 package stays refused,
  exactly as `--import-pkg` refuses it today (MANAGER.md §8).
- Store credentials. A private repo that needs auth is out of scope; the owner exposes it unauthenticated
  on a trusted LAN or as local files.

## 3. Ranked backlog

Size: S under a day, M one to three days. "New-only" = new files plus a one-time registration (a
`CMakeLists.txt` line and a verb/row), the pattern `tailscale_shell.cpp` / `dlna_shell.cpp` used. "Hybrid"
= real edits inside the existing `manager/` core.

| ID | Title | Why it matters | Evidence | Size | Depends on | Files |
|---|---|---|---|---|---|---|
| M1 | Manifest schema and reader (`xmb-source/1`) | The manifest is the whole trust boundary; it has to be small, validated, and versioned like the other schemas | `apps.json`/`pkg.json` validation patterns, MANAGER.md §5; schema versioning precedent `xmb-virtual-pkg/1` | S | none | New-only: `manager_source.h/.cpp` |
| M2 | Bounded HTTP(S) downloader with SHA-256 verify + resume | The first network code in `manager/`; must be memory-bounded and hash-checked before anything is registered | Hash labels MANAGER.md §11; bounded streaming precedent (1 MiB read buffer, MANAGER.md §11) | M | M1 | New-only: `manager_download.h/.cpp` (Qt Network) |
| M3 | Install + register through the existing import path | A downloaded file must land in `pkg/<id>/` and `apps.json` identically to a scanned ROM, so the XMB treats it the same | Import/package writer MANAGER.md §7.1, §5.1 | M | M1, M2 | Hybrid: `manager_core.*`, `manager_package.*` |
| M4 | `--install-from <manifest>` verb + Sources tab | The only way to trigger a fetch, and the only place a manifest is chosen; keeps the no-network default intact | CLI verb pattern MANAGER.md §7.1; five-tab UI MANAGER.md §3 | M | M3 | Hybrid: `manager_cli.cpp`, `manager_ui.*` |
| M5 | Offline/LAN source support (`file://`, UNC) and re-verify | Airgapped HTPCs; a LAN repo is the normal case here, and a hash re-check closes the loop | `--verify-hashes`/`--relink` MANAGER.md §11 | S | M3 | `manager_source.*`, reuse `--verify-hashes` |
| M6 | Source file browser (list the repo's tree) | The manifest says what is *installable*; a browser shows what is *there*, so the owner can pick from the server without leaving the couch | `xmb-source/1` `baseUrl` is already the root; an `index.json` or a plain directory listing | M | M1 | New-only: `manager_source.*` listing path, a Sources-tab browser, a `--list-source` verb |

Order: M1 → M2 → M3 (each strictly depends on the one before). M4 and M5 are parallel once M3 lands;
M5 is a thin variant of M2's source handling. M6 only needs M1 (the reader) and is independent of the
downloader, so it can start as soon as M1 lands.

## 4. Manifest schema (`xmb-source/1`)

```json
{
  "schema": "xmb-source/1",
  "name": "LAN repo",
  "baseUrl": "http://192.168.1.10/repo/",
  "entries": [
    {
      "id": "pkg-snes-5d1f0f6f0f6b0c21",
      "title": "Chrono Test",
      "platform": "SNES",
      "emulatorId": "retroarch",
      "files": [
        { "path": "snes/chrono-test.sfc", "sha256": "9f2c…64 hex…", "size": 4194304 }
      ],
      "art": "snes/chrono-test.png"
    }
  ]
}
```

Rules:

- `id` is optional and, when absent, derived the same way as import (`pkg-<platform slug>-<16 hex>`, FNV-1a
  over the canonical `baseUrl`+`path`). Re-importing the same source is therefore a duplicate, not a new id.
- `platform` maps through the existing platform map (MANAGER.md §6.5); `emulatorId` overrides it, same as
  `--emulator` does today.
- `files` is one or more entries; `path` is relative to `baseUrl` and joined the same canonical way paths are
  resolved in `apps.json` (absolute after join). `sha256` is the `full-sha256-v1` digest; `size` is optional
  and used only for a pre-flight length check.
- `art` is optional and relative to `baseUrl`; it is scaled to the same at-most-512x288 `ICON0.PNG` the import
  path already writes (MANAGER.md §4).
- Limits mirror the registry: at most 1000 entries, ids `[A-Za-z0-9_.-]`, a bounded manifest size (1 MiB,
  like `apps.json`). An unknown field is ignored per field, not fatal; a wrong `schema` or a malformed
  entry aborts that entry with its reason, the same "one bad entry doesn't break the rest" rule as
  `pkg.json` (MANAGER.md §5.1).

## 5. Verification each item needs

| Item | Verification |
|---|---|
| M1 | Scripted run on a manifest fixture: valid, wrong-schema, oversized, unknown-field, one-bad-entry. Assert the same validation exits as `apps.json` (0/1/2). |
| M2 | A local HTTP fixture (loopback) serving a file with a known SHA-256: correct, corrupt (hash mismatch), truncated (resume), and a missing file. No real network; the listener is a scratch process the test starts. |
| M3 | Import the downloaded fixture into a scratch `dataDir` and diff the resulting `apps.json`/`pkg.json` against an identical `--import` of the same file. The XMB load proof reuses MANAGER.md §14's method. |
| M4 | CLI verb runs headless against a scratch manifest; the Sources tab lists, installs and reports without a window. Exit codes match §7.3 of MANAGER.md. |
| M5 | A `file://`/UNC manifest installs with no network; `--verify-hashes` passes on the installed package, then fails after the file is changed (the existing mismatch path). |
| M6 | Scripted run on a scratch source: the browser lists a `file://` tree and an `index.json`-backed HTTP fixture, empty and populated; a `--list-source` run prints the same list headlessly. |

Gate for every item: build passes, scripted run exits 0, no new `qWarning` lines, and the affected audit
file is updated with date, command and result.

## 6. Collisions and the network-boundary change

- `manager/` has been network-free by design (MANAGER.md §1: "No module in manager/ includes Qt Network").
  M2 is the first Qt Network module there. It is opt-in by construction: nothing fetches unless a manifest
  is passed to `--install-from` or chosen in the Sources tab; detection, scan and import stay local.
- `manager_core.cpp` and `manager_cli.cpp` are shared hot files (the import/package writer). M3 and M4 edit
  them; treat the current owner of `xmb-manager` work as the single writer, per the collision map in
  `PHASES-2026-11-07.md` §3.
- The XMB itself needs no change: it already reads `apps.json` on refresh (MANAGER.md §9.1), so an installed
  manifest entry appears in the Game column with no new XMB code.
