import { readReleases } from "./reader";

const usage = "Usage: bun src/cli.ts <file> [--summary] [--search <query>] [--adapter <name>]";

export async function main(args: string[]): Promise<void> {
  let path: string | undefined;
  let summary = false;
  let search: string | undefined;
  let hint: string | undefined;
  for (let i = 0; i < args.length; i++) {
    const arg = args[i]!;
    if (arg === "--help" || arg === "-h") { console.log(usage); return; }
    if (arg === "--summary") summary = true;
    else if (arg === "--search" || arg === "--adapter") {
      const value = args[++i];
      if (value === undefined || value.startsWith("--")) throw new Error(`Missing value for ${arg}`);
      if (arg === "--search") search = value; else hint = value;
    } else if (arg.startsWith("-") || path !== undefined) throw new Error(`Unexpected argument: ${arg}`);
    else path = arg;
  }
  if (!path) throw new Error(usage);
  const all = await readReleases(path, hint);
  const releases = search === undefined ? all : all.filter((r) => r.title.toLowerCase().includes(search!.toLowerCase()));
  if (summary) console.log(`Releases: ${releases.length} (total: ${all.length})`);
  for (const release of releases) {
    console.log(summary
      ? `${release.title} | ${release.sizeLabel?.trim() ?? "unknown size"} | ${release.downloads.length} downloads | ${release.mirrors.length} mirrors`
      : JSON.stringify(release));
  }
}

if (import.meta.main) {
  main(Bun.argv.slice(2)).catch((error) => {
    console.error(error instanceof Error ? error.message : String(error));
    process.exitCode = 1;
  });
}
