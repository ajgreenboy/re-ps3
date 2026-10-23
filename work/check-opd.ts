import { readFileSync } from "node:fs";
const img = new Uint8Array(readFileSync("C:/dev/xmb/out/boot493/image.bin"));
const code = 0x018EDE00, toc = 0x01977D70;
let pairs = 0;
for (let i = 0; i + 7 < img.length; i++) {
  const a = (img[i] << 24) | (img[i+1] << 16) | (img[i+2] << 8) | img[i+3];
  const b = (img[i+4] << 24) | (img[i+5] << 16) | (img[i+6] << 8) | img[i+7];
  if ((a >>> 0) === code && (b >>> 0) === toc) { console.log(`descriptor at image offset 0x${i.toString(16)}`); pairs++; }
}
console.log(`matching descriptors: ${pairs}`);
