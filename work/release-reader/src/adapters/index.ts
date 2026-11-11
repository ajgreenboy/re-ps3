import type { Release } from "../model";
import { fitgirl } from "./fitgirl";
import { heuristic } from "./heuristic";

export interface SourceAdapter {
  name: string;
  detect(doc: unknown): boolean;
  parse(doc: unknown): Release[];
}

export const adapters: SourceAdapter[] = [fitgirl, heuristic];

export function pickAdapter(doc: unknown, hint?: string): SourceAdapter {
  if (hint !== undefined) {
    const adapter = adapters.find((a) => a.name === hint);
    if (!adapter) throw new Error(`Unknown adapter: ${hint}`);
    return adapter;
  }
  return adapters.find((a) => a.detect(doc)) ?? heuristic;
}
