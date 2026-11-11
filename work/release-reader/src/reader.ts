import { readFile } from "node:fs/promises";
import { pickAdapter } from "./adapters";
import type { Release } from "./model";

export async function readReleases(path: string, hint?: string): Promise<Release[]> {
  const bytes = await readFile(path);
  const doc: unknown = JSON.parse(new TextDecoder("utf-8", { fatal: false }).decode(bytes));
  return pickAdapter(doc, hint).parse(doc);
}
