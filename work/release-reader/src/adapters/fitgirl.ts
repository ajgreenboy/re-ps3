import type { SourceAdapter } from "./index";
import { asStringArray, groupToMirrors, isRecord, normalizeDownload, parseDate, parseSize } from "../normalize";

export const fitgirl: SourceAdapter = {
  name: "fitgirl",
  detect: (doc) => isRecord(doc) && Array.isArray(doc.downloads),
  parse(doc) {
    if (!isRecord(doc) || !Array.isArray(doc.downloads)) return [];
    return doc.downloads.filter(isRecord).map((raw) => ({
      source: "fitgirl",
      title: typeof raw.title === "string" ? raw.title : "",
      downloads: asStringArray(raw.uris).map(normalizeDownload),
      mirrors: groupToMirrors(raw.uriGroups),
      publishedAt: parseDate(raw.uploadDate),
      ...parseSize(raw.fileSize),
      raw,
    }));
  },
};
