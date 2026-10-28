// work/parity-import.ts - the reference corpus importer (native-xmb/PARITY-PHASES.md P0.3).
//
// work/parity.ts reports which cells have no RPCS3 reference, but nothing put a frame *into* the
// reference set: the contract (work/parity-reference.json) has a name, a provenance sidecar and a
// "never rescale" rule, and the captures have a manifest that says what each frame shows. This tool is
// the missing join. It takes a batch MANIFEST.md plus an authored mapping table
//
//   <state> <theme> <background> <resolution> <source frame> <note>
//
// and files each frame under the contract name, writing the .state.json provenance sidecar Tier B
// requires. Every value in the sidecar that came from the manifest is re-read from the manifest, not
// retyped, so a mapping cannot invent a build number.
//
//   bun work/parity-import.ts --map work/parity-references-map.json \
//     --manifest C:/dev/out/rpcs3-ref-2026-10-28/MANIFEST.md
//
// Refuses (exit 2, nothing written - the batch is validated before a single byte is copied):
//   * a state id work/parity-catalogue.json does not define;
//   * a theme/background/resolution token work/parity-reference.json does not define;
//   * two mapping rows for one cell, or a cell that already has a reference (its PNG, its sidecar, or a
//     `registered` entry) unless --replace. A *registered* cell is refused even with --replace:
//     retiring a registered entry is a contract change, not an import;
//   * a missing manifest, or an incomplete one (no capture date, RPCS3 build, firmware version,
//     renderer/window preset or expected frame size);
//   * a frame the manifest does not name (neither the file nor its directory appears in it), and a
//     source file that is missing or is not a PNG;
//   * a frame whose own dimensions differ from the manifest's expected frame size.
// Skips (exit 1, recorded in the map, that cell untouched):
//   * a frame whose dimensions differ from the *cell's* resolution. A 720p cell needs a 1280x720 frame;
//     nothing is ever rescaled, here or downstream.
//
// The map file is both the input and the audit record: the importer rewrites each entry's applied /
// skipped block with the cell id, the destination reference and provenance paths, the source and
// destination sha256, and whether the manifest names the frame exactly or only its group. The images
// stay under out/ (gitignored); the map is the committed list of paths and hashes.
//
// Exit codes: 0 every entry applied; 1 one or more entries skipped (dimension/cell mismatch); 2 an
// input error or a refusal. Nothing in this tool launches RPCS3: the rig belongs to the capture agent.

import { createHash } from 'node:crypto';
import {
  copyFileSync, existsSync, linkSync, mkdirSync, readFileSync, statSync, unlinkSync, writeFileSync,
} from 'node:fs';
import { basename, dirname, isAbsolute, join, resolve } from 'node:path';

type Axis = { id: string };
type CatalogueState = { id: string; class: string; title?: string };
type Catalogue = { schema: string; states: CatalogueState[] };
type ReferenceDoc = {
  schema: string;
  root: string;
  matrix: { themes: Axis[]; backgrounds: Axis[]; resolutions: Axis[] };
  registered?: Array<{ cell: string; reference: string; kind: string; note?: string }>;
};
type MapApplied = {
  cell: string;
  reference: string;
  provenance: string;
  source: string;
  sourceSha256: string;
  destinationSha256: string;
  manifest: string;
  manifestRef: 'exact' | 'group';
  imported: string;
  replaced?: string;
};
type MapEntry = {
  state: string;
  theme: string;
  background: string;
  resolution: string;
  source: string;
  note: string;
  clock?: string;
  manifest?: string;
  applied?: MapApplied | null;
  skipped?: string | null;
};
type MapDoc = {
  schema: string;
  updated?: string;
  note?: string;
  [key: string]: unknown;
  entries: MapEntry[];
};

const repo = resolve(import.meta.dir, '..');
const cataloguePath = join(repo, 'work', 'parity-catalogue.json');
const referencePath = join(repo, 'work', 'parity-reference.json');

const usage = `usage: bun work/parity-import.ts --map <file> [--manifest <file>]... [options]

  --map <file>        the mapping table (required). It is also the audit record: the importer rewrites
                      each entry's applied/skipped block with the cell, paths and sha256 values.
  --manifest <file>   a capture batch MANIFEST.md (repeatable). When given, every mapped entry must name
                      one of these; when omitted, the manifest each entry declares is used.
  --root <dir>        reference root (default: work/parity-reference.json root, out/parity-references)
  --replace           overwrite a cell that already has a reference (never a registered cell)
  --link              hard-link the frame instead of copying it (same volume only)
  --dry-run           validate and report, write no image, sidecar or map
  --json              machine-readable result
  --help              this text

The reference name comes from the cell, not the source file:
  <state>__<theme>__<background>__<resolution>.png  +  .state.json
Nothing is rescaled: a frame that is not the cell's resolution is skipped with a message.`;

function fail(message: string): never {
  console.error(`parity-import: ${message}`);
  process.exit(2);
}

function readJson(path: string, label: string): any {
  try {
    return JSON.parse(readFileSync(path, 'utf8'));
  } catch (error) {
    fail(`${label} ${path}: ${error instanceof Error ? error.message : String(error)}`);
  }
}

function escapeRegExp(text: string) {
  return text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function sha256File(path: string) {
  return createHash('sha256').update(readFileSync(path)).digest('hex');
}

// The repository is ASCII. Manifest prose carries typography (section signs, arrows, trademark marks),
// so anything lifted from a manifest is transliterated before it reaches a sidecar or the map.
function ascii(text: string) {
  return text
    .replace(/\u00a7\s*/g, 'section ')
    .replace(/\u2192/g, '->')
    .replace(/[\u2018\u2019]/g, "'")
    .replace(/[\u201c\u201d]/g, '"')
    .replace(/[\u2013\u2014]/g, '-')
    .replace(/\u00ae/g, '(R)')
    .replace(/\u2122/g, '(TM)')
    .replace(/\u00a9/g, '(C)')
    .replace(/[^\x00-\x7f]/g, '?');
}

// PNG dimensions straight from IHDR: no decode, no resampling, no dependency on the metric module.
function pngSize(path: string) {
  const bytes = readFileSync(path);
  if (bytes.length < 24 || bytes.toString('latin1', 1, 4) !== 'PNG' || bytes.toString('latin1', 12, 16) !== 'IHDR')
    throw new Refusal(`${path} is not a PNG (no IHDR): a reference must be the captured PNG`);
  return { width: bytes.readUInt32BE(16), height: bytes.readUInt32BE(20) };
}

class Refusal extends Error {}

type ManifestInfo = {
  path: string;
  sha256: string;
  bytes: number;
  captured: string;
  session: string;
  rpcs3Build: string;
  firmware: string;
  preset: string;
  config: string;
  width: number;
  height: number;
  text: string;
};

// A markdown table row's second cell: "| RPCS3 build | value |". Emphasis and code ticks are stripped.
function tableCell(text: string, label: string): string | undefined {
  const match = new RegExp(`^\\|\\s*${escapeRegExp(label)}\\s*\\|\\s*(.+?)\\s*\\|\\s*$`, 'im').exec(text);
  if (!match) return undefined;
  const value = match[1].replace(/\*\*/g, '').replace(/`/g, '').replace(/\\\|/g, '|').trim();
  return value || undefined;
}

const manifestCache = new Map<string, ManifestInfo>();

// The manifest is the authority for build, firmware, capture date, preset and frame size. A batch that
// does not state them cannot produce a provenance sidecar, so it is refused rather than half-imported.
function readManifest(path: string): ManifestInfo {
  const cached = manifestCache.get(path);
  if (cached) return cached;
  if (!existsSync(path)) throw new Refusal(`manifest missing: ${path}`);
  const stat = statSync(path);
  if (!stat.isFile()) throw new Refusal(`manifest is not a file: ${path}`);
  if (stat.size < 64) throw new Refusal(`manifest is too small to be a capture manifest (${stat.size} bytes): ${path}`);
  let text: string;
  try {
    text = readFileSync(path, 'utf8');
  } catch (error) {
    throw new Refusal(`manifest unreadable: ${path}: ${error instanceof Error ? error.message : error}`);
  }
  const sessionMatch = /Session:[^\n]*?(\d{4}-\d{2}-\d{2})[ T](\d{2}:\d{2})/.exec(text);
  const captured = sessionMatch?.[1] ?? '';
  const session = sessionMatch ? `${sessionMatch[1]} ${sessionMatch[2]}` : '';
  const rpcs3Build = tableCell(text, 'RPCS3 build') ?? /RPCS3 v[\d.]+-[\w]+[^\n]*/.exec(text)?.[0] ?? '';
  const firmware = tableCell(text, 'Firmware') ?? /PS3\s+(\d\.\d{2})\s+XMB/i.exec(text)?.[1]
    ?? /firmware[^\n.]{0,40}?(\d\.\d{2})/i.exec(text)?.[1] ?? '';
  const sizeMatch = /every image is (\d+)x(\d+)/i.exec(text) ?? /all (?:images|frames)[^\n]{0,24}?(\d+)x(\d+)/i.exec(text);
  const videoRow = tableCell(text, 'Video');
  const videoSize = videoRow ? /(\d{3,4})x(\d{3,4})/.exec(videoRow) : undefined;
  const width = Number(sizeMatch?.[1] ?? videoSize?.[1] ?? 0);
  const height = Number(sizeMatch?.[2] ?? videoSize?.[2] ?? 0);
  const windowRow = tableCell(text, 'Window');
  const preset = [videoRow, windowRow].filter(Boolean).join('; ');
  const bootRow = tableCell(text, 'Boot');
  const scratchRow = tableCell(text, 'Scratch config');
  const captureRow = tableCell(text, 'Capture');
  const config = [scratchRow ? `scratch config ${scratchRow}` : '', bootRow ? `boot ${bootRow}` : '',
    captureRow ? `capture ${captureRow}` : ''].filter(Boolean).join('; ');

  const missing: string[] = [];
  if (!captured) missing.push('capture date (a "- Session:" line)');
  if (!rpcs3Build) missing.push('RPCS3 build (a "| RPCS3 build |" row)');
  if (!firmware) missing.push('firmware version (a "PS3 x.xx XMB" mention)');
  if (!preset) missing.push('renderer preset (a "| Video |" row)');
  if (!width || !height) missing.push('expected frame size ("every image is WxH" or a "| Video |" row)');
  if (missing.length) throw new Refusal(`manifest incomplete: ${path} has no ${missing.join(', ')}`);

  const info: ManifestInfo = {
    path, sha256: sha256File(path), bytes: stat.size, captured, session, rpcs3Build, firmware,
    preset, config, width, height, text,
  };
  manifestCache.set(path, info);
  return info;
}

// ------------------------------------------------------------------------------------------------
// Arguments
// ------------------------------------------------------------------------------------------------

type Options = {
  map: string;
  manifests: string[];
  root: string;
  replace: boolean;
  link: boolean;
  dryRun: boolean;
  json: boolean;
};

function parseArgs(argv: string[]): Options {
  const opts: Options = {
    map: '', manifests: [], root: '', replace: false, link: false, dryRun: false, json: false,
  };
  for (let index = 0; index < argv.length; index++) {
    const arg = argv[index];
    if (arg === '--help' || arg === '-h') { console.log(usage); process.exit(0); }
    if (arg === '--json') { opts.json = true; continue; }
    if (arg === '--replace') { opts.replace = true; continue; }
    if (arg === '--link') { opts.link = true; continue; }
    if (arg === '--dry-run') { opts.dryRun = true; continue; }
    if (!['--map', '--manifest', '--root'].includes(arg)) fail(`unknown option ${JSON.stringify(arg)};\n${usage}`);
    const value = argv[++index];
    if (value === undefined || value.startsWith('--')) fail(`${arg} needs a value`);
    if (arg === '--map') opts.map = resolve(value);
    else if (arg === '--root') opts.root = resolve(value);
    else opts.manifests.push(resolve(value));
  }
  if (!opts.map) fail(`no mapping table given;\n${usage}`);
  return opts;
}

// ------------------------------------------------------------------------------------------------
// Preflight: validate every entry before anything is written. A refusal aborts the whole batch, so a
// contract violation never leaves a half-imported corpus.
// ------------------------------------------------------------------------------------------------

type Prepared = {
  index: number;
  entry: MapEntry;
  cell: string;
  source: string;
  sourceSha256: string;
  manifest: ManifestInfo;
  manifestRef: 'exact' | 'group';
  reference: string;
  provenance: string;
  replaced: string;
};

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  const mapPath = opts.map;
  const doc = readJson(mapPath, 'mapping table') as MapDoc;
  if (doc.schema !== 'xmb-parity-references-map/1')
    fail(`mapping table ${mapPath}: unsupported schema ${JSON.stringify(doc.schema)}`);
  if (!Array.isArray(doc.entries) || !doc.entries.length) fail(`mapping table ${mapPath}: entries must be a non-empty array`);

  const catalogue = readJson(cataloguePath, 'catalogue') as Catalogue;
  const refs = readJson(referencePath, 'reference') as ReferenceDoc;
  const root = opts.root || resolve(repo, refs.root);
  const states = new Map(catalogue.states.map(state => [state.id, state]));
  const themes = new Set(refs.matrix.themes.map(axis => axis.id));
  const backgrounds = new Set(refs.matrix.backgrounds.map(axis => axis.id));
  const resolutions = new Map(refs.matrix.resolutions.map(axis => [axis.id, axis as Axis & { width: number; height: number }]));
  const registered = new Map((refs.registered ?? []).map(entry => [entry.cell, entry]));

  const prepared: Prepared[] = [];
  const skipped: Array<{ cell: string; reason: string }> = [];
  const refusals: string[] = [];
  const seenCells = new Map<string, number>();

  const refuse = (index: number, message: string) => {
    refusals.push(`entry ${index}${doc.entries[index]?.state ? ` (${doc.entries[index].state})` : ''}: ${message}`);
  };

  for (const [index, entry] of doc.entries.entries()) {
    try {
      if (!entry || typeof entry !== 'object') throw new Refusal('the entry is not an object');
      for (const key of ['state', 'theme', 'background', 'resolution', 'source', 'note'] as const)
        if (typeof entry[key] !== 'string' || !(entry[key] as string).trim())
          throw new Refusal(`${key} must be a non-empty string`);
      if (!states.has(entry.state)) throw new Refusal(`state ${JSON.stringify(entry.state)} is not in work/parity-catalogue.json`);
      if (!themes.has(entry.theme)) throw new Refusal(`theme ${JSON.stringify(entry.theme)} is not in the reference matrix`);
      if (!backgrounds.has(entry.background)) throw new Refusal(`background ${JSON.stringify(entry.background)} is not in the reference matrix`);
      const resolution = resolutions.get(entry.resolution);
      if (!resolution) throw new Refusal(`resolution ${JSON.stringify(entry.resolution)} is not in the reference matrix`);
      if (entry.state.includes('__')) throw new Refusal('a state id may not contain "__" (it is the cell separator)');

      const cell = `${entry.state}__${entry.theme}__${entry.background}__${entry.resolution}`;
      const firstIndex = seenCells.get(cell);
      if (firstIndex !== undefined) throw new Refusal(`cell ${cell} is already mapped by entry ${firstIndex}: one file is one cell`);
      seenCells.set(cell, index);

      // The manifest: declared by the entry, or the single one given on the command line.
      const declared = typeof entry.manifest === 'string' && entry.manifest.trim()
        ? (isAbsolute(entry.manifest) ? resolve(entry.manifest) : resolve(repo, entry.manifest))
        : (opts.manifests.length === 1 ? opts.manifests[0] : '');
      if (!declared) throw new Refusal('no manifest named (add "manifest" to the entry or pass one --manifest)');
      if (opts.manifests.length && !opts.manifests.includes(declared))
        throw new Refusal(`declares manifest ${declared}, which was not passed with --manifest`);
      const manifest = readManifest(declared);

      // The source frame: absolute, or relative to the manifest's own directory.
      const source = isAbsolute(entry.source) ? resolve(entry.source) : resolve(dirname(manifest.path), entry.source);
      if (!existsSync(source)) throw new Refusal(`source frame missing: ${source}`);
      const stat = statSync(source);
      if (!stat.isFile()) throw new Refusal(`source frame is not a file: ${source}`);
      if (!/\.png$/i.test(source)) throw new Refusal(`source frame is not a .png: ${source}`);
      const size = pngSize(source);

      // The manifest says what size the batch's frames are. A frame that contradicts it is a corrupt or
      // wrong batch: refuse, do not guess.
      if (size.width !== manifest.width || size.height !== manifest.height)
        throw new Refusal(`frame ${size.width}x${size.height} does not match the manifest's ${manifest.width}x${manifest.height} `
          + `(${source}); the manifest and the frame disagree`);

      // The cell's own resolution. Never rescaled: skip with a message.
      if (size.width !== resolution.width || size.height !== resolution.height) {
        const reason = `frame is ${size.width}x${size.height}, the ${entry.resolution} cell is `
          + `${resolution.width}x${resolution.height}: skipped, nothing is rescaled`;
        skipped.push({ cell, reason });
        entry.skipped = reason;
        entry.applied = null;
        continue;
      }

      // Provenance of the *frame*: the manifest must name it, or at least the group it belongs to.
      const file = basename(source);
      const group = basename(dirname(source));
      const namedFile = manifest.text.includes(file);
      const namedGroup = Boolean(group) && manifest.text.includes(`${group}/`);
      if (!namedFile && !namedGroup)
        throw new Refusal(`the manifest does not name ${file} or its directory ${group}/: the frame cannot be attributed`);
      const manifestRef: 'exact' | 'group' = namedFile ? 'exact' : 'group';

      // An occupied cell. The registered list is checked first because parity.ts pairs a registered cell
      // by that path, so writing the contract name would be a file nothing reads.
      const reference = join(root, `${cell}.png`);
      const provenance = join(root, `${cell}.state.json`);
      const registeredEntry = registered.get(cell);
      if (registeredEntry)
        throw new Refusal(`cell ${cell} is backed by the registered reference ${registeredEntry.reference} `
          + `(${registeredEntry.kind}); retiring a registered entry is a contract change in work/parity-reference.json, `
          + `not an import, so this frame cannot take the canonical cell`);
      const occupied: string[] = [];
      if (existsSync(reference)) occupied.push(reference);
      if (existsSync(provenance)) occupied.push(provenance);
      if (occupied.length && !opts.replace)
        throw new Refusal(`cell ${cell} already has a reference (${occupied.join(', ')}); pass --replace to replace it deliberately`);

      prepared.push({
        index, entry, cell, source, sourceSha256: sha256File(source), manifest, manifestRef,
        reference, provenance, replaced: opts.replace ? occupied.join(', ') : '', count: 0,
      });
    } catch (error) {
      if (error instanceof Refusal) refuse(index, error.message);
      else refuse(index, error instanceof Error ? error.message : String(error));
    }
  }

  if (refusals.length) {
    console.error(`parity-import: refused ${refusals.length} of ${doc.entries.length} entr${doc.entries.length === 1 ? 'y' : 'ies'}; nothing was written.`);
    for (const refusal of refusals) console.error(`  - ${refusal}`);
    process.exit(2);
  }

  // ----------------------------------------------------------------------------------------------
  // Commit
  // ----------------------------------------------------------------------------------------------

  const imported: MapApplied[] = [];
  for (const item of prepared) {
    const { entry, manifest } = item;
    const clock = entry.clock && entry.clock.trim() ? entry.clock.trim()
      : `console session ${manifest.session} local, per-frame clock not recorded in the manifest`;
    if (!opts.dryRun) {
      mkdirSync(dirname(item.reference), { recursive: true });
      if (opts.link) {
        if (existsSync(item.reference)) unlinkSync(item.reference);
        linkSync(item.source, item.reference);
      } else {
        copyFileSync(item.source, item.reference);
      }
      const destinationSha256 = sha256File(item.reference);
      if (destinationSha256 !== item.sourceSha256)
        throw new Refusal(`destination sha256 ${destinationSha256} differs from source ${item.sourceSha256} for ${item.cell}: `
          + 'a conversion happened, and this importer only copies or links');
      const applied: MapApplied = {
        cell: item.cell,
        reference: item.reference,
        provenance: item.provenance,
        source: item.source,
        sourceSha256: item.sourceSha256,
        destinationSha256,
        manifest: manifest.path,
        manifestRef: item.manifestRef,
        imported: new Date().toISOString(),
        ...(item.replaced ? { replaced: item.replaced } : {}),
      };
      // The contract's fields first, then the provenance-of-the-provenance the audit needs.
      const sidecar = {
        cell: item.cell,
        kind: 'rpcs3',
        captured: manifest.captured,
        firmware: ascii(manifest.firmware),
        rpcs3Build: ascii(manifest.rpcs3Build),
        resolution: entry.resolution,
        preset: ascii(manifest.preset),
        clock: ascii(clock),
        sha256: destinationSha256,
        note: ascii(entry.note.trim()),
        source: { manifest: item.manifest.path, file: item.source, sha256: item.sourceSha256 },
        destination: { path: item.reference, sha256: destinationSha256 },
        config: ascii(manifest.config),
        conversion: 'none',
        manifestRef: item.manifestRef,
        mapping: { file: mapPath, entry: item.index },
        imported: applied.imported,
        tool: 'work/parity-import.ts',
      };
      writeFileSync(item.provenance, `${JSON.stringify(sidecar, null, 2)}\n`);
      entry.applied = applied;
      entry.skipped = null;
      imported.push(applied);
    }
  }

  if (!opts.dryRun) {
    const today = new Date();
    const two = (n: number) => String(n).padStart(2, '0');
    doc.updated = `${today.getFullYear()}-${two(today.getMonth() + 1)}-${two(today.getDate())}`;
    doc.summary = {
      entries: doc.entries.length,
      applied: doc.entries.filter(entry => entry.applied).length,
      skipped: doc.entries.filter(entry => entry.skipped).length,
      referenceRoot: root,
    };
    writeFileSync(mapPath, `${JSON.stringify(doc, null, 2)}\n`);
  }

  // ----------------------------------------------------------------------------------------------
  // Report
  // ----------------------------------------------------------------------------------------------

  if (opts.json) {
    console.log(JSON.stringify({
      schema: 'xmb-parity-import-result/1',
      map: mapPath,
      referenceRoot: root,
      dryRun: opts.dryRun,
      imported: opts.dryRun ? prepared.map(item => ({ cell: item.cell, reference: item.reference, source: item.source })) : imported,
      skipped,
      manifests: [...new Set(prepared.map(item => item.manifest.path))].map(path => {
        const info = manifestCache.get(path)!;
        return { path, sha256: info.sha256, captured: info.captured, rpcs3Build: info.rpcs3Build, firmware: info.firmware };
      }),
    }, null, 2));
  } else {
    console.log(`parity-import: ${opts.dryRun ? 'dry run' : 'imported'} ${prepared.length} cell(s) into ${root}`);
    const manifests = [...new Set(prepared.map(item => item.manifest.path))];
    for (const path of manifests) {
      const info = manifestCache.get(path)!;
      console.log(`  manifest   ${path}`);
      console.log(`             ${info.rpcs3Build}`);
      console.log(`             firmware ${info.firmware}, captured ${info.captured}, ${info.width}x${info.height}, sha256 ${info.sha256.slice(0, 12)}`);
    }
    console.log('');
    const width = Math.max(4, ...prepared.map(item => item.cell.length));
    for (const item of prepared) {
      console.log(`  ${item.cell.padEnd(width)}  ${item.manifestRef.padEnd(5)}  ${basename(item.source)}`
        + `${item.replaced ? `  (replaced ${item.replaced})` : ''}`);
    }
    if (skipped.length) {
      console.log('');
      console.log(`  skipped ${skipped.length} cell(s):`);
      for (const skip of skipped) console.log(`    ${skip.cell}: ${skip.reason}`);
    }
  }

  if (skipped.length) process.exitCode = 1;
}

try {
  await main();
} catch (error) {
  if (error instanceof Refusal) fail(error.message);
  fail(error instanceof Error ? `${error.message}\n${error.stack ?? ''}` : String(error));
}
