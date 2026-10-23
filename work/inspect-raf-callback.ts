import { readFileSync } from "node:fs";
const img = new Uint8Array(readFileSync("C:/dev/xmb/out/boot493/image.bin"));
const target = 0x01DA7694;
for (let i = 0; i + 3 < img.length; i++) {
  const v = ((img[i]<<24)|(img[i+1]<<16)|(img[i+2]<<8)|img[i+3]) >>> 0;
  if (v !== target) continue;
  console.log(`occurrence at image offset 0x${i.toString(16)}`);
  for(let p=i-32;p<=i+48;p+=4) { const w=((img[p]<<24)|(img[p+1]<<16)|(img[p+2]<<8)|img[p+3])>>>0; console.log(`  ${p===i?'>' :' '} 0x${w.toString(16).padStart(8,'0')}`); }
}
