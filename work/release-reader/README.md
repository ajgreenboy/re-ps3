# Release-index reader

Standalone, dependency-free Bun/TypeScript utility. From this directory run `~/.bun/bin/bun.exe src/cli.ts fixtures/fitgirl-sample.json --summary`, or supply a local JSON path. Default output is JSONL (including original `raw` objects); `--search <query>` filters titles case-insensitively and can be combined with `--summary`. `--adapter fitgirl|heuristic` overrides detection. Run tests with `~/.bun/bin/bun.exe test` (PowerShell: `& "$HOME/.bun/bin/bun.exe" ...`).

Sizes use binary multipliers (KB = 1024 bytes), rounded to whole bytes; the exact original label is retained. HTTP `.torrent` links are classified as torrents before the generic HTTP rule. Each mirror input group stays separate. Heuristic results are lower confidence (`source: "heuristic"`): the largest array is used, titles and links are guessed, and raw records are retained for remapping. UTF-8 decoding replaces invalid bytes without attempting text repair. Files are read whole, not streamed.

Local reading and normalization only: no fetching, downloading, tracker/DHT resolution, mirror checks, GUI, or XMB integration. Bun/TypeScript and standard library only; no npm or added dependencies. Never commit the real dump or its raw/normalized records; the fixture is hand-made with reserved `.invalid` hosts. Aggregate smoke-test observations are recorded in `HANDOFF.md`.
