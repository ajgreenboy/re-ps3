// Inspect the embedded boot SPU using the checked-out RPCS3 opcode table.
import { resolve } from 'node:path';
const [root = '.', reference = '../rpcs3', input] = process.argv.slice(2);
const opcodeSource = await Bun.file(resolve(reference, 'rpcs3/Emu/Cell/SPUOpcodes.h')).text();
const table = Array<string>(2048).fill('UNKNOWN');
for (const match of opcodeSource.matchAll(/\{\s*(\d+),\s*(0x[\da-f]+),\s*GET\((\w+)\)/gi)) {
  const magnitude = Number(match[1]), key = Number(match[2]);
  for (let i = 0; i < 1 << magnitude; ++i) table[(key << magnitude) + i] = match[3];
}
const image = await Bun.file(resolve(root, 'out/boot493/image.bin')).arrayBuffer();
const view = new DataView(image);
let elf: DataView | undefined;
for (let i = 0, pos = 12; i < view.getUint32(8); ++i) {
  const address = view.getUint32(pos), size = view.getUint32(pos + 4);
  const source = 0x6b9980;
  if (source >= address && source + 0x8d4 <= address + size)
    elf = new DataView(image, pos + 12 + source - address, 0x8d4);
  pos += 12 + size;
}
if (input) {
  elf = new DataView(await Bun.file(input).arrayBuffer());
  if (elf.getUint32(0) !== 0x7f454c46) {
    for (let i=0;i+4<=elf.byteLength;i+=4) {
      const word=elf.getUint32(i), op=table[word>>>21];
      console.log(`${i.toString(16).padStart(5,'0')} ${word.toString(16).padStart(8,'0')} ${op.padEnd(10)} rt=${word&127} ra=${word>>>7&127} rb=${word>>>14&127} i16=${word>>>7&65535}`);
    }
    process.exit(0);
  }
}
if (!elf) throw new Error('embedded SPU ELF not found');
console.log(`entry=${elf.getUint32(24).toString(16)}`);
const phoff = elf.getUint32(28), entsize = elf.getUint16(42), count = elf.getUint16(44);
const operations = new Map<string, number>();
for (let p = 0; p < count; ++p) {
  const ph = phoff + p * entsize;
  if (elf.getUint32(ph) !== 1 || !(elf.getUint32(ph + 24) & 1)) continue;
  const offset = elf.getUint32(ph + 4), address = elf.getUint32(ph + 8), size = elf.getUint32(ph + 16);
  for (let i = 0; i + 4 <= size; i += 4) {
    const word = elf.getUint32(offset + i), op = table[word >>> 21];
    operations.set(op, (operations.get(op) ?? 0) + 1);
    console.log(`${(address + i).toString(16).padStart(5, '0')} ${word.toString(16).padStart(8, '0')} ${op.padEnd(10)} rt=${word & 127} ra=${word >>> 7 & 127} rb=${word >>> 14 & 127} i16=${word >>> 7 & 65535}`);
  }
}
console.log([...operations].sort((a,b) => a[0].localeCompare(b[0])).map(([name,count]) => `${name}: ${count}`).join('\n'));
