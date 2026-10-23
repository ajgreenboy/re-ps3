import { resolve } from 'node:path';
const [address, length, output] = process.argv.slice(2);
if (!output) throw new Error('usage: extract-image.ts address-hex size-hex output');
const wanted = parseInt(address, 16), size = parseInt(length, 16);
const image = await Bun.file(resolve('out/boot493/image.bin')).arrayBuffer(), view = new DataView(image);
for (let i=0,pos=12;i<view.getUint32(8);++i) {
  const addr=view.getUint32(pos), len=view.getUint32(pos+4);
  if (wanted>=addr && wanted+size<=addr+len) {
    await Bun.write(output,new Uint8Array(image,pos+12+wanted-addr,size));
    console.log(`${size} bytes -> ${output}`);
    process.exit(0);
  }
  pos+=12+len;
}
throw new Error('range not present in one linked image segment');
