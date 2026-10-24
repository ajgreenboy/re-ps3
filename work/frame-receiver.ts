// Local-only receiver for reference video frames grabbed in the browser: POST /save?name=<file> with a PNG/JPEG body.
// Frames land in out/native-reference/video-frames/ (gitignored, never published). Usage: bun work/frame-receiver.ts
import { mkdirSync, writeFileSync } from "node:fs";
import { join, basename } from "node:path";

const dir = join(import.meta.dir, "..", "out", "native-reference", "video-frames");
mkdirSync(dir, { recursive: true });
const cors = { "Access-Control-Allow-Origin": "*", "Access-Control-Allow-Methods": "POST, OPTIONS", "Access-Control-Allow-Headers": "*", "Access-Control-Allow-Private-Network": "true" };
Bun.serve({
  hostname: "127.0.0.1",
  port: 8765,
  async fetch(request) {
    if (request.method === "OPTIONS") return new Response(null, { headers: cors });
    const url = new URL(request.url);
    const name = basename(url.searchParams.get("name") ?? "");
    if (request.method !== "POST" || url.pathname !== "/save" || !/^[\w.-]+\.(png|jpg)$/.test(name)) return new Response("bad request", { status: 400, headers: cors });
    const bytes = new Uint8Array(await request.arrayBuffer());
    writeFileSync(join(dir, name), bytes);
    console.log(`saved ${name} ${bytes.length} bytes`);
    return new Response("ok", { headers: cors });
  },
});
console.log(`frame receiver on http://127.0.0.1:8765, saving to ${dir}`);
