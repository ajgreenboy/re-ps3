import { describe, expect, test } from "bun:test";
import { asStringArray, classifyUri, groupToMirrors, parseDate, parseMagnet, parseSize } from "../src/normalize";

describe("parseSize", () => {
  const mb = 1024 ** 2, gb = 1024 ** 3;
  const cases: [string, number, number?][] = [
    ["2 GB", 2 * gb], ["23.3 GB\n", 23.3 * gb],
    ["13.3/14.7 GB", 13.3 * gb, 14.7 * gb], ["4~4.3 GB", 4 * gb, 4.3 * gb],
    ["13,3 GB", 13.3 * gb], ["2,4 GB", 2.4 * gb], ["19,9 GB", 19.9 * gb],
    ["13,38 GB", 13.38 * gb], ["215/374 MB", 215 * mb, 374 * mb],
    ["8,8~9,5 GB SELECTIVE DOWNLOAD", 8.8 * gb, 9.5 * gb],
    ["980 MB/1,3 GB (Selective Download)", 980 * mb, 1.3 * gb], ["69 MB", 69 * mb],
    ["1 tb", 1024 ** 4], ["12 KB", 12 * 1024], ["3 B", 3],
    ["2 GB/980 MB", 980 * mb, 2 * gb],
  ];
  for (const [label, min, max] of cases) test(label, () => {
    expect(parseSize(label)).toEqual({ sizeLabel: label, sizeBytes: Math.round(min),
      ...(max === undefined ? {} : { sizeBytesMax: Math.round(max) }) });
  });
  test("absent or unparseable labels", () => {
    for (const input of [undefined, null, 42, {}]) expect(parseSize(input)).toEqual({});
    for (const input of ["", "unknown", "123"]) expect(parseSize(input)).toEqual({ sizeLabel: input });
  });
});

describe("parseMagnet", () => {
  test("bare uppercase hash is lowercased without inventing metadata", () => {
    const uri = "magnet:?xt=urn:btih:0D07D3FD0123456789ABCDEF0123456789ABCDEF";
    expect(parseMagnet(uri)).toEqual({ kind: "magnet", uri,
      infohash: "0d07d3fd0123456789abcdef0123456789abcdef", displayName: undefined, trackers: [] });
  });
  test("full link with repeated xt, percent-encoding and trackers", () => {
    const parsed = parseMagnet("magnet:?xt=unrecognized&xt=urn%3Abtih%3AABCDEF&dn=An+Example%20Name&tr=udp%3A%2F%2Fa.invalid%3A80&tr=https%3A%2F%2Fb.invalid%2Fannounce");
    expect(parsed.infohash).toBe("abcdef");
    expect(parsed.displayName).toBe("An Example Name");
    expect(parsed.trackers).toEqual(["udp://a.invalid:80", "https://b.invalid/announce"]);
  });
  test("btmh v2 base32 payload is preserved verbatim", () => {
    expect(parseMagnet("magnet:?xt=urn:btmh:BCDEFG234567").infohash).toBe("BCDEFG234567");
  });
  test("missing and malformed parameters do not throw", () => {
    expect(parseMagnet("magnet:?").infohash).toBeUndefined();
    expect(parseMagnet("magnet:?xt=%ZZ&dn=%FF").trackers).toEqual([]);
  });
});

test("URI classification", () => {
  for (const [uri, kind] of [
    ["magnet:?xt=urn:btih:abc", "magnet"], ["file.torrent", "torrent"],
    ["https://example.invalid/a.TORRENT?download=1", "torrent"],
    ["http://example.invalid/a.zip", "direct"], ["https://example.invalid/a", "direct"],
    ["ftp://example.invalid/a", "unknown"], ["nonsense", "unknown"],
  ] as const) expect(classifyUri(uri)).toBe(kind);
});

test("date parsing: ISO, RFC2822, milliseconds, Date and invalid input", () => {
  const date = new Date("2026-01-01T00:00:00Z");
  expect(parseDate(date)).toBe(date);
  expect(parseDate(date.toISOString())?.getTime()).toBe(date.getTime());
  expect(parseDate("Thu, 01 Jan 2026 00:00:00 GMT")?.getTime()).toBe(date.getTime());
  expect(parseDate(date.getTime())?.getTime()).toBe(date.getTime());
  expect(parseDate(0)?.getTime()).toBe(0);
  for (const value of ["invalid", undefined, null, {}, NaN, Infinity, new Date(NaN)]) {
    expect(parseDate(value)).toBeUndefined();
  }
});

test("asStringArray accepts single, array and missing values", () => {
  expect(asStringArray("a")).toEqual(["a"]);
  expect(asStringArray(["a", "b"])).toEqual(["a", "b"]);
  for (const value of [undefined, null, {}, 1]) expect(asStringArray(value)).toEqual([]);
  expect(asStringArray(["a", null, 1])).toEqual(["a"]);
});

test("mirror groups sort numerically, keep fallback order and derive host", () => {
  const result = groupToMirrors([
    ["https://mirror.invalid/a.part10.rar", "https://mirror.invalid/a.part2.rar", "https://mirror.invalid/a.part1.rar"],
    ["https://other.invalid/z", "https://other.invalid/a"], ["not a URL"], [],
  ]);
  expect(result).toHaveLength(3);
  expect(result[0]!.host).toBe("mirror.invalid");
  expect(result[0]!.parts.map((p) => p.uri.split("/").pop())).toEqual(["a.part1.rar", "a.part2.rar", "a.part10.rar"]);
  expect(result[1]!.parts.map((p) => p.uri)).toEqual(["https://other.invalid/z", "https://other.invalid/a"]);
  expect(result[2]!.host).toBeUndefined();
  expect(groupToMirrors(undefined)).toEqual([]);
});
