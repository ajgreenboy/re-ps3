// Lists the compiled Cg programs (.vpo/.fpo) in a Sony QGL archive (QRCF/QRCC): profile, instruction counts and
// every parameter (name, semantic, resource, variability, default). Also extracts .mnu presets, Cg objects and
// SPU ELFs into an output folder for local analysis. Firmware-derived output: keep it out of Git.
// Cg binary layout per RPCS3 (0b0fa08) Emu/RSX/Program/CgBinaryProgram.h.
// usage: bun work/cg-inspect.ts <archive.qrc> <out dir> [name filter]
import { inflateSync } from "node:zlib";
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";

const [path, outDir, filter] = process.argv.slice(2);
if (!outDir) throw new Error("usage: bun work/cg-inspect.ts <archive.qrc> <out dir> [name filter]");
let bytes = Buffer.from(await Bun.file(path).arrayBuffer());
if (bytes.toString("ascii", 0, 4) === "QRCC") bytes = inflateSync(bytes.subarray(8));
const u32 = (at: number) => bytes.readUInt32BE(at);
const toc = u32(8), tocSize = u32(12), ids = u32(16), idsSize = u32(20), strings = u32(24), stringsSize = u32(28), data = u32(48);
const str = (o: number) => (o < stringsSize ? bytes.toString("utf8", strings + o, bytes.indexOf(0, strings + o)) : "?");
const id = (o: number) => (o + 4 < idsSize ? bytes.toString("utf8", ids + o + 4, bytes.indexOf(0, ids + o + 4)) : "?");

const files: { name: string; file: Buffer }[] = [];
for (let offset = 0; offset < tocSize; ) {
  const at = toc + offset, count = u32(at + 4);
  const attrs = Array.from({ length: count }, (_, i) => {
    const p = at + 28 + i * 16;
    return { name: str(u32(p)), type: u32(p + 4), a: u32(p + 8), b: u32(p + 12) };
  });
  const source = attrs.find((a) => a.type === 6), nameAttr = attrs.find((a) => a.type === 7), size = attrs.find((a) => a.name === "size")?.a;
  if (source && nameAttr) {
    let file = bytes.subarray(data + source.a, data + source.a + source.b);
    if (size) file = inflateSync(file);
    files.push({ name: id(nameAttr.a), file });
  }
  offset += 28 + count * 16;
}

// CGtype / CGresource names for the values that appear in RSX Cg objects (Cg 2.x headers)
const typeName = (t: number) =>
  ({ 1045: "float", 1046: "float2", 1047: "float3", 1048: "float4", 1061: "float4x4", 1056: "float3x3", 1066: "sampler2D", 1069: "samplerCUBE", 1065: "sampler1D", 1067: "sampler3D", 1091: "bool", 1093: "int" } as Record<number, string>)[t] ?? `type${t}`;
const varName = (v: number) => ({ 4096: "varying", 4097: "uniform", 4098: "constant" } as Record<number, string>)[v] ?? `var${v}`;
const dirName = (d: number) => ({ 4097: "in", 4098: "out", 4099: "inout" } as Record<number, string>)[d] ?? `dir${d}`;

const report: string[] = [];
for (const { name, file } of files) {
  if (filter && !name.includes(filter)) continue;
  const target = join(outDir, name);
  mkdirSync(dirname(target), { recursive: true });
  if (name.endsWith(".mnu") || name.endsWith(".elf") || name.endsWith(".vpo") || name.endsWith(".fpo")) writeFileSync(target, file);
  if (!name.endsWith(".vpo") && !name.endsWith(".fpo")) continue;
  const r = (o: number) => file.readUInt32BE(o);
  const profile = r(0), paramCount = r(12), paramArray = r(16), program = r(20), ucodeSize = r(24);
  const cstr = (o: number) => (o ? file.toString("utf8", o, file.indexOf(0, o)) : "");
  const vertex = profile === 7003;
  const info = vertex
    ? `vertex: ${r(program)} instructions, slot ${r(program + 4)}, ${r(program + 8)} regs, inputs 0x${r(program + 12).toString(16)}, outputs 0x${r(program + 16).toString(16)}`
    : `fragment: ${r(program)} instructions, inputs 0x${r(program + 4).toString(16)}, texcoords 0x${file.readUInt16BE(program + 12).toString(16)}, regs ${file[program + 18]}, outputFromH0 ${file[program + 19]}, depthReplace ${file[program + 20]}, kill ${file[program + 21]}`;
  report.push(`== ${name} (profile ${profile}, ucode ${ucodeSize} bytes)\n   ${info}`);
  for (let i = 0; i < paramCount; i++) {
    const p = paramArray + i * 48;
    const [type, res, variability, resIndex, nameOff, defOff, embOff, semOff, direction, paramno, referenced] = Array.from({ length: 11 }, (_, k) => r(p + k * 4));
    if (!referenced) continue;
    let def = "";
    if (defOff) {
      const n = typeName(type) === "float4x4" ? 16 : typeName(type) === "float3" ? 3 : typeName(type) === "float2" ? 2 : typeName(type) === "float" ? 1 : 4;
      def = " = [" + Array.from({ length: n }, (_, k) => file.readFloatBE(defOff + k * 4).toFixed(4)).join(",") + "]";
    }
    const emb = embOff ? ` embedded@${Array.from({ length: r(embOff) }, (_, k) => r(embOff + 4 + k * 4)).join(",")}` : "";
    report.push(`   ${dirName(direction).padEnd(3)} ${varName(variability).padEnd(8)} ${typeName(type).padEnd(9)} ${cstr(nameOff).padEnd(28)} res=${res} idx=${resIndex}${cstr(semOff) ? " :" + cstr(semOff) : ""}${def}${emb}`);
  }
}
writeFileSync(join(outDir, "cg-programs.txt"), report.join("\n") + "\n");
console.log(`${files.length} archive entries; Cg report and extracted files in ${outDir}`);
