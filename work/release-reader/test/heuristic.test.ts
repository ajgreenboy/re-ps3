import { expect, test } from "bun:test";
import { heuristic } from "../src/adapters/heuristic";

test("renamed keys in the largest nested array are ingestible", () => {
  const items = [
    { label: "Example Renamed One", links: ["https://example.invalid/one.zip"], size: "2,4 GB" },
    { label: "Example Renamed Two", links: "magnet:?xt=urn:btih:ABCDEF", size: "215/374 MB" },
    { name: "Example Named", nested: { url: "https://example.invalid/a.torrent?x=1" }, date: "2026-01-01" },
  ];
  const releases = heuristic.parse({ metadata: { small: [0] }, nested: { items } });
  expect(releases).toHaveLength(3);
  expect(releases.map((r) => r.title)).toEqual(["Example Renamed One", "Example Renamed Two", "Example Named"]);
  expect(releases[0]!.sizeBytes).toBe(Math.round(2.4 * 1024 ** 3));
  expect(releases[1]!.downloads[0]!.infohash).toBe("abcdef");
  expect(releases[1]!.sizeBytesMax).toBe(374 * 1024 ** 2);
  expect(releases[2]!.downloads[0]!.kind).toBe("torrent");
  expect(releases[2]!.publishedAt).toBeInstanceOf(Date);
  for (const [i, release] of releases.entries()) {
    expect(release.source).toBe("heuristic");
    expect(release.raw).toBe(items[i]);
    expect(release.mirrors).toEqual([]);
  }
});

test("title keys win, all nested URI strings are collected and deduplicated", () => {
  const [release] = heuristic.parse([{ description: "A long description", title: "Short",
    links: { first: "magnet:?xt=urn:btih:AB", again: "magnet:?xt=urn:btih:AB", other: "local.torrent" } }]);
  expect(release!.title).toBe("Short");
  expect(release!.downloads.map((d) => d.kind)).toEqual(["magnet", "torrent"]);
});

test("empty or malformed documents are safe", () => {
  for (const doc of [null, {}, "text", [], [null, 1]]) expect(heuristic.parse(doc)).toEqual([]);
});

test("large arrays do not exceed function argument limits", () => {
  expect(heuristic.parse({ items: Array(150_000).fill(null) })).toEqual([]);
});
