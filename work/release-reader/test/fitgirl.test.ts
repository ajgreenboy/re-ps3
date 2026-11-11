import { expect, test } from "bun:test";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import fixture from "../fixtures/fitgirl-sample.json";
import { fitgirl } from "../src/adapters/fitgirl";
import { pickAdapter } from "../src/adapters";
import { readReleases } from "../src/reader";

test("FitGirl fixture normalizes all six records and retains provenance", () => {
  expect(fitgirl.detect(fixture)).toBe(true);
  expect(fitgirl.detect(null)).toBe(false);
  const releases = fitgirl.parse(fixture);
  expect(releases).toHaveLength(6);
  expect(releases.map((r) => r.title)).toEqual(fixture.downloads.map((r) => r.title));
  for (const [i, release] of releases.entries()) {
    expect(release.source).toBe("fitgirl");
    expect(release.raw).toBe(fixture.downloads[i]);
    expect(release.sizeLabel).toBe(fixture.downloads[i]!.fileSize);
  }
  expect(releases[0]!.downloads[0]!.infohash).toBe("abcdef0123456789abcdef0123456789abcdef01");
  expect(releases[0]!.publishedAt?.toISOString()).toBe("2026-01-01T00:00:00.000Z");
  expect(releases[0]!.mirrors).toEqual([]);
  expect(releases[1]!.downloads[0]!.displayName).toBe("Example Full");
  expect(releases[1]!.downloads[0]!.trackers).toHaveLength(2);
  expect(releases[1]!.sizeBytes).toBe(Math.round(13.38 * 1024 ** 3));
  expect(releases[2]!.downloads[0]!.kind).toBe("torrent");
  expect(releases[2]!.mirrors[0]!.host).toBe("mirror.invalid");
  expect(releases[2]!.mirrors[0]!.parts[0]!.uri).toEndWith("part1.rar");
  expect(releases[2]!.sizeBytesMax).toBe(Math.round(14.7 * 1024 ** 3));
  expect(releases[3]!.title).toContain("�");
  expect(releases[3]!.publishedAt).toBeUndefined();
  expect(releases[4]!.sizeBytes).toBe(980 * 1024 ** 2);
  expect(releases[5]!.downloads[0]!.infohash).toBe("BCDEFG234567");
});

test("registry detection and explicit hint", () => {
  expect(pickAdapter(fixture)).toBe(fitgirl);
  expect(pickAdapter(fixture, "heuristic").name).toBe("heuristic");
  expect(pickAdapter({}, "fitgirl")).toBe(fitgirl);
  expect(pickAdapter({}).name).toBe("heuristic");
  expect(() => pickAdapter({}, "missing")).toThrow("Unknown adapter");
  expect(fitgirl.parse(null)).toEqual([]);
  expect(fitgirl.parse({ downloads: [null, 1, {}] })).toHaveLength(1);
});

test("reader reads the fixture", async () => {
  expect(await readReleases(fileURLToPath(new URL("../fixtures/fitgirl-sample.json", import.meta.url))))
    .toEqual(fitgirl.parse(fixture));
});

test("reader replaces invalid UTF-8 bytes without repairing titles", async () => {
  const temp = process.env.LOCALAPPDATA ? join(process.env.LOCALAPPDATA, "Temp", "opencode") : tmpdir();
  const dir = await mkdtemp(join(temp, "release-reader-"));
  try {
    const path = join(dir, "invalid.json");
    await writeFile(path, Buffer.concat([Buffer.from('{"downloads":[{"title":"Example '),
      Buffer.from([0xff]), Buffer.from(' Adventure"}]}')]));
    expect((await readReleases(path))[0]!.title).toBe("Example � Adventure");
    await writeFile(path, "not JSON");
    await expect(readReleases(path)).rejects.toThrow();
  } finally { await rm(dir, { recursive: true, force: true }); }
});

test("CLI emits JSONL, filters titles, summarizes, and reports invalid arguments", () => {
  const cli = fileURLToPath(new URL("../src/cli.ts", import.meta.url));
  const sample = fileURLToPath(new URL("../fixtures/fitgirl-sample.json", import.meta.url));
  const run = (...args: string[]) => Bun.spawnSync([process.execPath, cli, ...args]);
  const jsonl = run(sample);
  expect(jsonl.exitCode).toBe(0);
  const records = jsonl.stdout.toString().trim().split("\n").map((line) => JSON.parse(line));
  expect(records).toHaveLength(6);
  expect(records[0].source).toBe("fitgirl");
  const search = run(sample, "--search", "mIxEd");
  expect(search.exitCode).toBe(0);
  expect(JSON.parse(search.stdout.toString()).title).toBe("Example Mixed");
  const summary = run(sample, "--summary");
  expect(summary.exitCode).toBe(0);
  expect(summary.stdout.toString()).toContain("Releases: 6 (total: 6)");
  expect(summary.stdout.toString()).toContain("Example Mirrors | 13.3/14.7 GB | 1 downloads | 1 mirrors");
  const missing = run(sample, "--search");
  expect(missing.exitCode).toBe(1);
  expect(missing.stderr.toString()).toContain("Missing value");
  expect(run(sample, "--unknown").exitCode).toBe(1);
});
