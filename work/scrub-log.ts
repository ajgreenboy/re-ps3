// scrub-log.ts - make an XMB log safe to share (best effort; read the result before sending it).
//
//   bun work/scrub-log.ts <in.log> [out.log]       scrub a file (stdout when no out.log)
//   bun work/scrub-log.ts < in.log > out.log       scrub stdin to stdout
//
// Follows the rules in native-xmb/audit/DIAGNOSE.md section 3 (fail closed: a line that looks like a secret is
// dropped whole, not edited) for the log lines the XMB writes today:
//   - credential-looking lines (Authorization, Bearer, X-Emby-Token, api_key, password, private-key blocks) are dropped
//   - Windows profile paths and the user name become %USERPROFILE%
//   - "autologon: on for <user>", "tailscale: status ... self <host>" and Steam ids/account names are masked
//   - private IPv4 addresses, MAC addresses and tailnet addresses are masked
// It cannot know your library or media paths or audio device names; skim the output for those, and for anything
// else personal, before attaching it. The summary line goes to stderr.
import { readFileSync, writeFileSync } from 'node:fs';

const DROP: RegExp[] = [
  /Authorization\s*[:=]/i, /\bBearer\s+\S/i, /X-Emby-Token|X-MediaBrowser-Token|AccessToken/i,
  /\b(api[_-]?key|apikey|password|passwd|pwd|secret|refresh[_-]?token|npsso)\b\s*[:=]/i,
  /[?&](api_key|token|apikey)=/i, /-----BEGIN [A-Z ]*PRIVATE KEY-----/, /\beyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\./,
];

const MASK: Array<[RegExp, string]> = [
  [/[A-Za-z]:[\\/]+Users[\\/]+[^\\/\s"'`]+/g, '%USERPROFILE%'],
  [/(autologon: on for )(\S+)/i, '$1<user>'],
  [/(autologon: on for a different user \()([^)]*)\)/i, '$1<user>)'],
  [/(tailscale: status \S+ self )(\S+)/i, '$1<host>'],
  [/\b7656119\d{10}\b/g, '<steam-id>'],
  [/(account(?:name)?[=:]\s*)[^\s,;"]+/gi, '$1<account>'],
  [/\b([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}\b/g, '<mac>'],
  [/\b(?:10|127)\.\d{1,3}\.\d{1,3}\.\d{1,3}\b/g, '<ip>'],
  [/\b192\.168\.\d{1,3}\.\d{1,3}\b/g, '<ip>'],
  [/\b172\.(?:1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3}\b/g, '<ip>'],
  [/\b100\.(?:6[4-9]|[7-9]\d|1[01]\d|12[0-7])\.\d{1,3}\.\d{1,3}\b/g, '<tailnet-ip>'],
];

function scrub(text: string): { out: string; dropped: number; masked: number } {
  const lines = text.split(/\r?\n/);
  const kept: string[] = [];
  let dropped = 0, masked = 0, inKey = false;
  for (const line of lines) {
    if (/-----BEGIN [A-Z ]*PRIVATE KEY-----/.test(line)) { inKey = true; dropped++; continue; }
    if (inKey) { dropped++; if (/-----END [A-Z ]*PRIVATE KEY-----/.test(line)) inKey = false; continue; }
    if (DROP.some(re => re.test(line))) { dropped++; continue; }
    let next = line;
    for (const [re, to] of MASK) next = next.replace(re, to);
    if (next !== line) masked++;
    kept.push(next);
  }
  kept.push(`[scrub-log: ${dropped} line(s) withheld, ${masked} line(s) masked]`);
  return { out: kept.join('\n') + '\n', dropped, masked };
}

const [input, output] = process.argv.slice(2);
if (input === '--help' || input === '-h') {
  console.error('usage: bun work/scrub-log.ts <in.log> [out.log]   (or stdin to stdout)');
  process.exit(0);
}
const text = input ? readFileSync(input, 'utf8') : await Bun.stdin.text();
const result = scrub(text);
if (output) writeFileSync(output, result.out); else process.stdout.write(result.out);
console.error(`scrub-log: ${result.dropped} line(s) withheld, ${result.masked} line(s) masked`);
