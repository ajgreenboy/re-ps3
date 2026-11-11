export type DownloadKind = "magnet" | "torrent" | "direct" | "unknown";

export interface Download {
  kind: DownloadKind;
  uri: string;
  infohash?: string;
  displayName?: string;
  trackers?: string[];
}

export interface MirrorGroup {
  host?: string;
  parts: Download[];
}

export interface Release {
  source: string;
  title: string;
  downloads: Download[];
  mirrors: MirrorGroup[];
  publishedAt?: Date;
  sizeBytes?: number;
  sizeBytesMax?: number;
  sizeLabel?: string;
  raw: unknown;
}
