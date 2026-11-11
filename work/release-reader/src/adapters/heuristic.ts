import type { SourceAdapter } from "./index";
import { isRecord, normalizeDownload, parseDate, parseSize } from "../normalize";

const uriLike = (s: string) => /^(?:magnet:|https?:\/\/)|\.torrent$/i.test(s);

function largestArray(doc: unknown): unknown[] {
  let largest: unknown[] = [];
  const pending = [doc];
  const visited = new Set<object>();
  while (pending.length) {
    const value = pending.pop();
    if (!value || typeof value !== "object" || visited.has(value)) continue;
    visited.add(value);
    if (Array.isArray(value) && value.length > largest.length) largest = value;
    for (const child of Object.values(value)) pending.push(child);
  }
  return largest;
}

function collectUris(doc: unknown): string[] {
  const found = new Set<string>();
  const pending = [doc];
  const visited = new Set<object>();
  while (pending.length) {
    const value = pending.pop();
    if (typeof value === "string" && uriLike(value)) found.add(value);
    else if (value && typeof value === "object" && !visited.has(value)) {
      visited.add(value);
      for (const child of Object.values(value).reverse()) pending.push(child);
    }
  }
  return [...found];
}

// Lower-confidence best effort only: raw is retained for a dedicated adapter later.
export const heuristic: SourceAdapter = {
  name: "heuristic",
  detect: () => true,
  parse(doc) {
    return largestArray(doc).filter(isRecord).map((raw) => {
      const entries = Object.entries(raw);
      const title = entries.find(([k, v]) => /^(title|name)$/i.test(k) && typeof v === "string")?.[1]
        ?? entries.find(([, v]) => typeof v === "string" && v.length >= 8 && !uriLike(v))?.[1];
      const size = entries.find(([k]) => /^(size|fileSize)$/i.test(k))?.[1];
      const date = entries.find(([k]) => /^(date|uploadDate|publishedAt)$/i.test(k))?.[1];
      return { source: "heuristic", title: typeof title === "string" ? title : "",
        downloads: collectUris(raw).map(normalizeDownload), mirrors: [],
        publishedAt: parseDate(date), ...parseSize(size), raw };
    });
  },
};
