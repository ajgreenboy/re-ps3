import type { Download, DownloadKind, MirrorGroup, Release } from "./model";

export function asStringArray(value: unknown): string[] {
  return typeof value === "string" ? [value]
    : Array.isArray(value) ? value.filter((v): v is string => typeof v === "string") : [];
}

export function classifyUri(uri: string): DownloadKind {
  if (/^magnet:/i.test(uri)) return "magnet";
  // Check the extension before HTTP so web-hosted torrent files stay torrents.
  if (/\.torrent(\?|$)/i.test(uri)) return "torrent";
  return /^https?:\/\//i.test(uri) ? "direct" : "unknown";
}

export function parseMagnet(uri: string): Download {
  const params = new URLSearchParams(uri.replace(/^magnet:\??/i, ""));
  let infohash: string | undefined;
  for (const xt of params.getAll("xt")) {
    const v1 = /^urn:btih:([0-9a-f]+)$/i.exec(xt);
    const v2 = /^urn:btmh:(.+)$/i.exec(xt);
    if (v1) { infohash = v1[1]!.toLowerCase(); break; }
    if (v2 && infohash === undefined) infohash = v2[1];
  }
  return { kind: "magnet", uri, infohash, displayName: params.get("dn") ?? undefined,
    trackers: params.getAll("tr") };
}

export function normalizeDownload(uri: string): Download {
  const kind = classifyUri(uri);
  return kind === "magnet" ? parseMagnet(uri) : { kind, uri };
}

type Size = Pick<Release, "sizeLabel" | "sizeBytes" | "sizeBytesMax">;
const units: Record<string, number> = { B: 1, KB: 1024, MB: 1024 ** 2, GB: 1024 ** 3, TB: 1024 ** 4 };

export function parseSize(label: unknown): Size {
  if (typeof label !== "string") return {};
  const result: Size = { sizeLabel: label };
  const text = label.trim().replace(/\(?selective\s+download\)?\s*$/i, "").replace(/,/g, ".");
  const tokens = [...text.matchAll(/(\d+(?:\.\d+)?)\s*(TB|GB|MB|KB|B)?/gi)];
  const fallback = tokens.find((t) => t[2])?.[2]?.toUpperCase();
  if (!fallback) return result;
  const values = tokens.map((t) => Number(t[1]) * units[(t[2] ?? fallback).toUpperCase()]!);
  if (!values.length || values.some((v) => !Number.isFinite(v))) return result;
  result.sizeBytes = Math.round(Math.min(...values));
  if (values.length > 1) result.sizeBytesMax = Math.round(Math.max(...values));
  return result;
}

export function parseDate(value: unknown): Date | undefined {
  if (!(value instanceof Date) && typeof value !== "string" && typeof value !== "number") return undefined;
  const date = value instanceof Date ? value : new Date(value);
  return Number.isNaN(date.getTime()) ? undefined : date;
}

export function groupToMirrors(value: unknown): MirrorGroup[] {
  if (!Array.isArray(value)) return [];
  return value.map((group) => {
    const parts = asStringArray(group).map(normalizeDownload);
    // Sort numbered parts in their existing slots; unnumbered links keep insertion order.
    const partNumber = (d: Download) => /\.part(\d+)\.rar$/i.exec(d.uri)?.[1];
    const numbered = parts.filter((d) => partNumber(d) !== undefined)
      .sort((a, b) => Number(partNumber(a)) - Number(partNumber(b)));
    let index = 0;
    const ordered = parts.map((d) => partNumber(d) === undefined ? d : numbered[index++]!);
    let host: string | undefined;
    try { host = new URL(ordered[0]!.uri).hostname || undefined; } catch { /* Non-URL parts have no host. */ }
    return { host, parts: ordered };
  }).filter((group) => group.parts.length > 0);
}

export function isRecord(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}
