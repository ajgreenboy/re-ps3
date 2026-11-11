# Handoff: release-index reader (JSON → normalized release list)

Written for an agent picking this up cold. This is a **standalone Bun/TypeScript utility** that reads
a release-index JSON file — the kind the FitGirl dump on the Desktop represents, or any source with
the same shape (title + download URIs + date + size) — and turns it into a clean, normalized model it
can emit, summarize, and search. It is **not** wired into the XMB application and does **not** download
anything.

## 1. Goal

Read one release-index JSON document and produce a normalized list of releases, where each release has:

- a `title`
- a set of `downloads` (magnet / `.torrent` / direct links), classified and de-structured
- grouped direct-download mirrors
- a parsed `publishedAt` date
- a parsed size (bytes, with an optional range max)

The parser must be **generic**: a thin adapter per source shape, plus a heuristic fallback for unknown
JSON. The messy sub-field parsing (magnet links, size strings, dates) is shared and isolated.

## 2. Scope

**In scope**

- Read a JSON file from disk, detect its shape, and normalize it to `Release[]`.
- Classify and parse URIs (magnet, `.torrent`, direct HTTP).
- Parse magnet links into `{ infohash, displayName, trackers }`.
- Parse the varied `fileSize` strings and `uploadDate` values.
- Group direct-download mirrors by host and order the parts.
- Emit normalized output: JSONL, or a human summary (`--summary`), or a title search (`--search`).
- Unit tests for the normalizers and adapters.
- Tolerate broken UTF-8 (the real dump contains `�` mojibake) without crashing.

**Out of scope (do not build)**

- No network fetching, no tracker/DHT resolution, no downloading of any file, no mirror checking.
- No integration with `xmb-manager.exe` or `apps.json` (future question, see §10).
- No GUI.

## 3. Constraints and rules

Follow the repo's standing rules from `AGENT-HANDOFF.md` §4:

- **Bun/TypeScript only** for scripts (`~/.bun/bin/bun.exe`). Never `npm`. No new dependencies — use
  `bun:test` and the Node/Bun stdlib only.
- **Do not commit the source dump.** The real file stays local (currently
  `C:\Users\Albert\Desktop\{nameFitGirl,downloads[{titleSledde.txt`). Commit only a small **sanitized
  fixture** in `fixtures/`.
- **This tool reads and normalizes only.** It must not download, resolve, or automate obtaining the
  files the index points at. That boundary is intentional and stays out of scope (§2).
- Report only what you observe; no fabricated test results.

## 4. Data model (`src/model.ts`)

```ts
type DownloadKind = "magnet" | "torrent" | "direct" | "unknown";

interface Download {
  kind: DownloadKind;
  uri: string;
  infohash?: string;      // magnet xt=urn:btih:<hex>, lowercased
  displayName?: string;   // magnet dn=
  trackers?: string[];    // magnet tr=
}

interface MirrorGroup {
  host?: string;          // hostname derived from the first part's URL
  parts: Download[];      // ordered by part number
}

interface Release {
  source: string;         // adapter name
  title: string;
  downloads: Download[];  // flattened primary links
  mirrors: MirrorGroup[]; // grouped direct mirrors
  publishedAt?: Date;
  sizeBytes?: number;     // min of the range
  sizeBytesMax?: number;  // set when the source gave a range ("13.3/14.7 GB")
  sizeLabel?: string;     // original string, always kept
  raw: unknown;           // the original object, for provenance
}
```

## 5. File layout

```
work/release-reader/
  HANDOFF.md          this document
  README.md           quick usage
  src/
    model.ts          types from §4
    normalize.ts      parseMagnet, classifyUri, parseSize, parseDate, asStringArray
    adapters/
      fitgirl.ts      adapter for the { name, downloads: [...] } shape
      heuristic.ts    fallback: largest-array + guess title/uri keys
      index.ts        registry, detect, pick
    reader.ts         file -> bytes -> JSON -> adapter -> Release[]
    cli.ts            entry point (arg parsing)
  test/
    normalize.test.ts
    fitgirl.test.ts
    heuristic.test.ts
  fixtures/
    fitgirl-sample.json   small hand-made sample (NOT the real dump)
```

## 6. Adapters

**Interface** (in `adapters/index.ts`):

```ts
interface SourceAdapter {
  name: string;
  detect(doc: unknown): boolean;
  parse(doc: unknown): Release[];
}
```

**`fitgirl.ts`** — detects `typeof doc === "object"` and `Array.isArray(doc.downloads)`; maps each
entry: `title`, `uris` → magnets (via `asStringArray` + `parseMagnet`), `uriGroups` → `mirrors`
(via `groupToMirrors`), `uploadDate` → `parseDate`, `fileSize` → `parseSize`.

**`heuristic.ts`** — for unknown JSON: find the largest array anywhere in the doc; per item, pick the
`title`-like key (a key named `title`/`name`, else the first long string value), collect every URI-like
string (`^magnet:|^https?://|\.torrent$`), and emit `source: "heuristic"` releases. Mark them lower
confidence (keep `raw` so a human can re-map). This exists so an unknown source is *ingestible*
immediately and gets a dedicated adapter later.

**`index.ts`** — `pickAdapter(doc, hint?)`: explicit `hint` wins, else first adapter whose `detect`
returns true, else `heuristic`.

## 7. Normalizers and the edge cases they must handle (`src/normalize.ts`)

These are the source of most bugs. The real dump exercises every one of these.

**`asStringArray(v)`** — accept `string` or `string[]`; return `string[]` (empty if absent). Other
sources sometimes give a single string where this one gives an array.

**`classifyUri(uri)`** — decision tree:
`magnet:` → magnet · `^https?://` → direct · `/\.torrent(\?|$)/i` → torrent · else unknown.

**`parseMagnet(uri)`** — strip `magnet:?`, parse as query params (`URLSearchParams`), take all `xt`,
extract the hash via `/urn:bt[ih]h:([0-9a-fA-F]+)/`, lower-case it. Handle:

- bare hash only: `magnet:?xt=urn:btih:0D07D3FD...` (no `dn`, no `tr`) → `trackers: []`, `displayName: undefined`
- full link with `dn` and many `tr`
- uppercase hashes (older entries) → normalize to lowercase
- `xt` may be a v2 `btmh` (base32) — capture as `infohash` verbatim rather than rejecting.

**`parseSize(label)`** — must not throw on any of these observed values:

| Input | Meaning |
|---|---|
| `"2 GB"` | 2 GB |
| `"23.3 GB\n"` | trailing newline → strip |
| `"13.3/14.7 GB"` | range, `/` separator → min 13.3, max 14.7 |
| `"4~4.3 GB"` | range, `~` separator |
| `"13,3 GB"`, `"2,4 GB"`, `"19,9 GB"` | **EU decimal comma** → treat `,` as `.` |
| `"13,38 GB"` | still a decimal comma (13.38), not thousands |
| `"215/374 MB"` | range with unit on one side |
| `"8,8~9,5 GB SELECTIVE DOWNLOAD"` | range + noise suffix |
| `"980 MB/1,3 GB (Selective Download)"` | mixed-unit range (treat units independently) |
| `"69 MB"` | small |

Algorithm: strip whitespace; strip trailing `SELECTIVE DOWNLOAD` / `(Selective Download)` noise;
normalize `,` → `.`; detect unit (B/KB/MB/GB/TB, case-insensitive); pull all numbers via
`/\d+(?:\.\d+)?/g`; multiply by the unit multiplier; `sizeBytes = min`, `sizeBytesMax = max` when more
than one number. Always return `sizeLabel` (the original string) for provenance.

**`parseDate(v)`** — `Date` → itself · `number` → `new Date(ms)` · ISO/RFC2822 string → `new Date`;
return `undefined` if `isNaN(getTime())`. (This dump is ISO `Z`, but the general reader must not assume.)

**`groupToMirrors(uriGroups)`** — each inner array is one host's set of parts. Sort each group by the
part number (match `/\.part(\d+)\.rar$/i`, fall back to insertion order), and derive `host` from the
first URL's hostname.

**UTF-8 tolerance** — read the file as bytes and decode as UTF-8 with replacement. The dump has
mojibake like `Mount & Blade: Warband �?" Viking Conquest` and `Assassin�?Ts Creed`. The reader must
survive these; do not attempt to repair them (that's out of scope).

## 8. Implementation plan (ranked)

1. **`model.ts` + `normalize.ts`** — pure functions, no I/O. Acceptance: the `parseSize` / `parseMagnet`
   / `classifyUri` / `parseDate` unit tests in §9 pass against every value in §7's table.
2. **`adapters/fitgirl.ts` + `fixtures/fitgirl-sample.json`** — a hand-written ~6-entry fixture
   covering: bare-hash magnet, full magnet, an entry with `uriGroups`, an entry without, an EU-comma
   size, a range size, and a mojibake title. Acceptance: adapter returns the expected `Release[]`.
3. **`adapters/index.ts` + `adapters/heuristic.ts`** — registry + dispatch + fallback. Acceptance:
   heuristic parses a fixture whose keys are renamed (`label`, `links`, `size`).
4. **`reader.ts` + `cli.ts`** — read file bytes → decode → `JSON.parse` → `pickAdapter` → `Release[]`.
   `bun cli.ts <file>` prints JSONL; `--summary` prints per-release `title | size | #downloads | #mirrors`;
   `--search <q>` filters titles (case-insensitive substring). Acceptance: runs on the real dump and
   reports a release count without crashing.
5. **`README.md`** — one-paragraph usage + the §3 constraints restated.
6. **Smoke test against the real dump** — confirm: no crash, a sane count, and that mojibake titles and
   EU-comma sizes are handled. Record the observed count in the handoff.

## 9. Verification

`bun test` in `work/release-reader/`. Required cases:

- `parseSize`: each row of §7's table → correct `sizeBytes` / `sizeBytesMax`.
- `parseMagnet`: bare hash, full link, uppercase hash, `btmh` v2 hash.
- `classifyUri`: one of each kind.
- `parseDate`: ISO string, unix ms number, invalid string → `undefined`.
- `fitgirl` adapter: fixture round-trips to expected releases.
- `heuristic` adapter: renamed-key fixture still yields releases.
- `asStringArray`: string input, array input, missing input.

The real dump is **not** committed and is not a test fixture; it is only used for the §8.6 smoke run.

## 10. Open questions / future

- **Integration?** Whether these releases should ever flow into `xmb-manager.exe` / `apps.json` is a
  separate decision and deliberately deferred. If pursued, it would be a new handoff with its own
  scope — and would need to resolve the download/obtain boundary before anything is built.
- **Other source shapes.** The adapter registry is the extension point; a new source is a new ~30-line
  adapter, not a rewrite.
- **Incremental reads.** The real file is ~18.5 MB single-line JSON. If it grows, consider streaming
  (`JSONStream`-style) — out of scope now, but noted.

## 11. Definition of done

- `bun test` passes; `bun cli.ts <real-dump> --summary` runs clean and reports a sane release count.
- Normalizers handle every observed edge case in §7.
- Only `fixtures/fitgirl-sample.json` (sanitized) is committed; the real dump is not.
- No new dependencies; Bun/TypeScript throughout.

## 12. Observed verification (2026-11-11)

- `bun test` on Bun 1.3.14: **34 pass, 0 fail**, 143 assertions across three files.
- Real-dump smoke (§8.6): `bun src/cli.ts <local-desktop-dump> --summary` exited 0;
  **7,266 releases**. `--search Assassin --summary` exited 0; **14 matches**.
- Aggregate inspection: 21 EU-decimal size labels, all parsed; 984 range sizes;
  3,446 releases with mirrors (108,782 mirror parts).
- Eight nonempty size labels had no usable numeric value (` GB`, `DIGIT NOT FOUND MB`,
  `XX.VIII GB\n`); their original labels are preserved and byte sizes left undefined.
- This local copy contained **zero U+FFFD replacement-character titles**; the earlier mojibake
  examples were not observed here. Replacement decoding is verified separately by injecting an
  invalid UTF-8 byte in a temporary test file, not by altering the real dump.
- Sanitized fixture `--summary` exited 0 with six releases. No real records, links, or dump files
  were copied into the repository. HTTP `.torrent` classification precedes generic HTTP;
  sizes use 1024-based units rounded to whole bytes (both documented in README).
- This file's §8.6 smoke-run result is recorded.
