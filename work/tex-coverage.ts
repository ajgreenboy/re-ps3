// tex-coverage.ts - coverage sweep for the native XMB asset decoder.
//
// Answers: of every named image in every dev_flash vsh/resource RCO, how many does
// Assets::image / Assets::decodeGim actually decode, with which GIM pixel format, and
// which ones fail and why. Two stages:
//
//   bun work/tex-coverage.ts scan  <dev_flash> <outdir>
//       Parse all RCO image tables (the same fields Assets::readContainer reads) and the
//       GIM image header of every format-5 entry. Writes <outdir>/plan.json. No pixels.
//
//   bun work/tex-coverage.ts sweep <dev_flash> <exe> <outdir>
//       Re-run scan, then feed every container's image names through the real app:
//       <exe> --export-image "container:name,name,...@<outdir>/png/<container>" with
//       XMB_GIM_TRACE=1 and XMB_LOG=<outdir>/logs/<container>.log. Parses the log and the
//       exit code into <outdir>/results.json, then prints the aggregate coverage tables.
//
//   bun work/tex-coverage.ts report <outdir>
//       Re-print the aggregate tables from an existing results.json.
//
// Analysis-only: writes under <outdir> (out/), never touches the repo or the firmware.
import {inflateSync} from 'node:zlib';
import {readdirSync, readFileSync, writeFileSync, mkdirSync, rmSync} from 'node:fs';
import {join} from 'node:path';

const u16 = (b: Buffer, p: number) => b.readUInt16BE(p);
const u32 = (b: Buffer, p: number) => b.readUInt32BE(p);

type GimBlock = {type: number; size: number; data: number};

type Image = {
    container: string;
    name: string;
    entryFormat: number;   // RCO image entry format: 5 = GIM, other = QImage::fromData
    compression: number;   // 0 stored, 1 zlib
    packed: number;
    unpacked: number;
    gim: null | {
        format: number; order: number; width: number; height: number;
        depth: number; align: number; palette: boolean;
        blocks: string;
    };
    scanError?: string;
};

type Result = Image & {
    decoded: boolean;
    outWidth?: number;
    outHeight?: number;
    error?: string;
    trace?: string;
    exit?: number;
};

function gimInfo(gim: Buffer) {
    let at = 0x10;
    let image: null | {format: number; order: number; width: number; height: number; depth: number; align: number; palette: boolean; blocks: string} = null;
    const blocks: string[] = [];
    for (let i = 0; i < 128; i++) {
        if (at + 16 > gim.length) break;
        const type = u16(gim, at), size = u32(gim, at + 4), data = u32(gim, at + 12);
        if (size < 16 || data < 16 || data >= size || at + size > gim.length) { blocks.push(`bad:${type}`); break; }
        blocks.push(`${type}:0x${size.toString(16)}`);
        if (type === 2 || type === 3) { at += data; continue; }
        if (type === 5) { image && (image.palette = true); at += size; continue; }
        if (type !== 4) { at += size; continue; }
        const info = at + data;
        if (info + 0x30 > gim.length) { blocks.push('trunc'); break; }
        image = {
            format: u16(gim, info + 4), order: u16(gim, info + 6),
            width: u16(gim, info + 8), height: u16(gim, info + 10),
            depth: u16(gim, info + 12), align: u16(gim, info + 14),
            palette: false, blocks: '',
        };
        // The image's enclosing picture block ends where the next sibling begins; the
        // decoder sizes the picture as [picture data, picture start + size).
        at += size;
        continue;
    }
    if (image) image.blocks = blocks.join(',');
    return image;
}

function scanContainer(dir: string, file: string): {images: Image[]; error?: string} {
    const container = file.replace(/\.rco$/i, '');
    const rco = readFileSync(join(dir, file));
    let tables: Buffer;
    try {
        if (u32(rco, 0) !== 0x46525000) throw new Error('not a PS3 big-endian RCO');
        const compression = u32(rco, 12) >>> 4;
        if (compression === 0) tables = rco;
        else if (compression === 1) tables = Buffer.concat([
            rco.subarray(0, 0xa4),
            inflateSync(rco.subarray(0xb0, 0xb0 + u32(rco, 0xa4)), {maxOutputLength: 32 * 1024 * 1024}),
        ]);
        else throw new Error(`unsupported RCO header compression ${compression}`);
    } catch (e) {
        return {images: [], error: String((e as Error).message)};
    }
    const imageRoot = u32(rco, 0x24);
    if (imageRoot === 0xffffffff) return {images: []};
    const labels = u32(rco, 0x40), labelLength = u32(rco, 0x44), imageData = u32(rco, 0x80);
    const label = (off: number) => {
        let end = labels + off;
        while (end < labels + labelLength && tables[end] !== 0) end++;
        return tables.toString('utf8', labels + off, end);
    };
    const images: Image[] = [];
    const count = u32(tables, imageRoot + 16);
    let at = imageRoot + 0x28;
    try {
        for (let i = 0; i < count; i++) {
            if (u16(tables, at) !== 0x0401) throw new Error(`unexpected RCO image entry at 0x${at.toString(16)}`);
            const name = label(u32(tables, at + 4));
            const extra = at + 0x28;
            const entryFormat = u16(tables, extra), compression = u16(tables, extra + 2) & 255;
            const packed = u32(tables, extra + 4), offset = u32(tables, extra + 8), unpacked = u32(tables, extra + 16);
            const image: Image = {container, name, entryFormat, compression, packed, unpacked, gim: null};
            if (entryFormat === 5) {
                try {
                    const start = imageData + offset;
                    const data = compression === 1
                        ? inflateSync(rco.subarray(start, start + packed), {maxOutputLength: 32 * 1024 * 1024})
                        : rco.subarray(start, start + packed);
                    if (data.subarray(0, 8).toString('latin1') !== '.GIM1.00') throw new Error('not a GIM: ' + data.subarray(0, 8).toString('latin1'));
                    const info = gimInfo(data);
                    if (!info) throw new Error('no GIM image block');
                    image.gim = info;
                } catch (e) {
                    image.scanError = String((e as Error).message);
                }
            }
            images.push(image);
            const next = u32(tables, at + 20);
            if (i + 1 < count && next < 0x38) throw new Error('invalid RCO image sibling offset');
            if (!next && i + 1 < count) throw new Error('truncated RCO image table');
            at += next;
        }
    } catch (e) {
        return {images, error: String((e as Error).message)};
    }
    return {images};
}

function scanAll(flash: string) {
    const dir = join(flash, 'vsh', 'resource');
    const files = readdirSync(dir).filter(f => /\.rco$/i.test(f)).sort();
    const images: Image[] = [];
    const bad: {container: string; error: string}[] = [];
    for (const file of files) {
        const {images: got, error} = scanContainer(dir, file);
        images.push(...got);
        if (error) bad.push({container: file.replace(/\.rco$/i, ''), error});
    }
    return {containers: files.length, images, bad};
}

function parseLog(text: string, container: string) {
    const decoded = new Map<string, {w: number; h: number}>();
    const failed = new Map<string, string>();
    const trace = new Map<string, string>();
    for (const line of text.split(/\r?\n/)) {
        let m = /^gim: (.+?) format=(.*)$/.exec(line);
        if (m) { trace.set(m[1], 'format=' + m[2]); continue; }
        m = /^assets: decoded ([^/\s]+)\/(\S+) QSize\((\d+), (\d+)\)$/.exec(line);
        if (m) { decoded.set(m[2], {w: +m[3], h: +m[4]}); continue; }
        m = /^assets: ([^/\s]+)\/(\S+) (.*)$/.exec(line);
        if (m && m[1] === container) failed.set(m[2], m[3]);
    }
    return {decoded, failed, trace};
}

function sweep(flash: string, exe: string, outdir: string) {
    mkdirSync(join(outdir, 'logs'), {recursive: true});
    mkdirSync(join(outdir, 'png'), {recursive: true});
    const {containers, images, bad} = scanAll(flash);
    writeFileSync(join(outdir, 'plan.json'), JSON.stringify({containers, images, bad}, null, 1));
    const results: Result[] = [];
    const byContainer = new Map<string, Image[]>();
    for (const image of images) {
        if (!byContainer.has(image.container)) byContainer.set(image.container, []);
        byContainer.get(image.container)!.push(image);
    }
    let runs = 0;
    const runErrors: {container: string; detail: string}[] = [];
    for (const [container, list] of byContainer) {
        const dir = join(outdir, 'png', container);
        mkdirSync(dir, {recursive: true});
        const log = join(outdir, 'logs', container + '.log');
        rmSync(log, {force: true});
        // Every item needs its own container prefix: main.cpp splits each comma-separated
        // item on ':' and skips anything that is not exactly container:name.
        const spec = `${list.map(i => `${container}:${i.name}`).join(',')}@${dir}`;
        const spawned = Bun.spawnSync({
            cmd: [exe, '--dev-flash', flash, '--export-image', spec],
            stdout: 'pipe', stderr: 'pipe',
            env: {...process.env, XMB_GIM_TRACE: '1', XMB_LOG: log},
        });
        runs++;
        const text = readFileSync(log, 'utf8');
        const parsed = parseLog(text, container);
        if (spawned.exitCode !== 0 && parsed.failed.size === 0) {
            runErrors.push({container, detail: `exit ${spawned.exitCode}; ${spawned.stderr.toString().trim().slice(0, 300)}`});
        }
        for (const image of list) {
            const ok = parsed.decoded.get(image.name);
            const err = parsed.failed.get(image.name);
            const result: Result = {...image, decoded: !!ok, exit: spawned.exitCode ?? -1};
            if (ok) { result.outWidth = ok.w; result.outHeight = ok.h; }
            if (err) result.error = err;
            const tr = parsed.trace.get(image.name);
            if (tr) result.trace = tr;
            if (!ok && !err) result.error = 'no decode line and no warning in log';
            results.push(result);
        }
    }
    writeFileSync(join(outdir, 'results.json'), JSON.stringify({containers, runs, bad, runErrors, results}, null, 1));
    return {containers, runs, bad, runErrors, results};
}

// Format names for the GIM picture formats the decoder knows about.
const GIM_NAMES: Record<number, string> = {
    0x0: 'RGBA5650', 0x1: 'RGBA5551', 0x2: 'RGBA4444', 0x3: 'RGBA8888',
    0x4: 'INDEX4', 0x5: 'INDEX8', 0x6: 'INDEX16', 0x7: 'INDEX32',
    0x8: 'DXT1', 0x9: 'DXT3', 0xa: 'DXT5',
};
const fmt = (f: number | null | undefined) => f === null || f === undefined ? 'n/a' : `0x${f.toString(16)}${GIM_NAMES[f] ? ' ' + GIM_NAMES[f] : ''}`;

function report(outdir: string) {
    const data = JSON.parse(readFileSync(join(outdir, 'results.json'), 'utf8'));
    const results: Result[] = data.results;
    const withImages = new Set(results.map(r => r.container));
    const ok = results.filter(r => r.decoded);
    const no = results.filter(r => !r.decoded);
    console.log(`containers swept        ${data.containers}`);
    console.log(`containers with images  ${withImages.size}`);
    console.log(`named images            ${results.length}`);
    console.log(`decoded                 ${ok.length}`);
    console.log(`failed                  ${no.length}`);
    console.log(`app runs                ${data.runs}`);
    if (data.bad?.length) {
        console.log(`containers unreadable   ${data.bad.length}`);
        for (const b of data.bad) console.log(`  ${b.container}: ${b.error}`);
    }
    if (data.runErrors?.length) {
        console.log(`runs with an unexplained non-zero exit: ${data.runErrors.length}`);
        for (const r of data.runErrors) console.log(`  ${r.container}: ${r.detail}`);
    }
    console.log('\n-- by GIM pixel format --');
    const keys = new Map<string, {total: number; ok: number; mpx: number}>();
    for (const r of results) {
        const label = r.entryFormat === 5
            ? (r.gim ? fmt(r.gim.format) : 'GIM unparsed')
            : `RCO entry format 0x${r.entryFormat.toString(16)} (QImage)`;
        if (!keys.has(label)) keys.set(label, {total: 0, ok: 0, mpx: 0});
        const k = keys.get(label)!;
        k.total++; if (r.decoded) k.ok++; k.mpx += ((r.outWidth || 0) * (r.outHeight || 0)) / 1e6;
    }
    for (const [label, k] of [...keys].sort((a, b) => b[1].total - a[1].total)) {
        console.log(`${label.padEnd(44)} ${String(k.ok).padStart(5)}/${String(k.total).padEnd(5)} decoded  ${k.mpx.toFixed(1)} Mpx decoded`);
    }
    console.log('\n-- failures by format --');
    const fails = new Map<string, Result[]>();
    for (const r of no) {
        const label = r.entryFormat === 5
            ? (r.gim ? fmt(r.gim.format) : 'GIM unparsed')
            : `RCO entry format 0x${r.entryFormat.toString(16)}`;
        if (!fails.has(label)) fails.set(label, []);
        fails.get(label)!.push(r);
    }
    if (!fails.size) console.log('  none');
    for (const [label, list] of fails) console.log(`  ${label}: ${list.length}`);
    // The indexed pictures are the interesting class: say where they live, whether any carries a
    // palette block, and which size classes exist, because the decoder treats a palette-less picture
    // as a coverage mask rather than as palette indices.
    const f5 = results.filter(r => r.gim && r.gim.format === 5);
    if (f5.length) {
        console.log('\n-- format 0x5 (indexed) detail --');
        const byContainer = new Map<string, {n: number; palette: number}>();
        for (const r of f5) {
            if (!byContainer.has(r.container)) byContainer.set(r.container, {n: 0, palette: 0});
            const c = byContainer.get(r.container)!;
            c.n++; if (r.gim!.palette) c.palette++;
        }
        for (const [c, v] of byContainer) console.log(`  ${c}: ${v.n} pictures, ${v.palette} with a palette block`);
        const sizes = [...new Set(f5.map(r => `${r.gim!.width}x${r.gim!.height}`))].sort();
        console.log(`  size classes: ${sizes.join(', ')}`);
    }
    console.log('\n-- failure list --');
    const reasons = new Map<string, number>();
    for (const r of no) reasons.set(r.error || '(no message)', (reasons.get(r.error || '(no message)') || 0) + 1);
    for (const [reason, n] of [...reasons].sort((a, b) => b[1] - a[1])) console.log(`  ${String(n).padStart(4)}  ${reason}`);
    const limit = Number(process.env.TEX_LIST_LIMIT || 0);
    const shown = limit ? no.slice(0, limit) : no;
    for (const r of shown) console.log(`  FAIL ${r.container}:${r.name} ${r.entryFormat === 5 && r.gim ? fmt(r.gim.format) : 'entry=0x' + r.entryFormat.toString(16)} ${r.gim ? r.gim.width + 'x' + r.gim.height : ''} -- ${r.error}`);
    if (limit && no.length > limit) console.log(`  (${no.length - limit} more; set TEX_LIST_LIMIT=0 for all)`);
}

// The (container, name) pairs the recreation's own sources name: the category and item icons in
// src/shell.cpp, the information-panel/notification/message-box art in src/view.cpp and
// src/dialogs.cpp, the qgl normal maps in src/icons.cpp/src/view.cpp, and the 52 names in
// src/osk_layout.cpp kTextures. This is the cross-reference list the audit reports on: for each
// pair, does the container actually ship it and does it decode today.
const CONSUMED: Record<string, string[]> = {
    'qgl/icontex': ['1n', '2n', '3n', '4n', '5n', '6n', '7n', '8n', '35n', 'dif'],
    'explore_plugin_full': ['item_tex_NewUser', 'item_tex_ps12util', 'item_tex_ps2game_folder', 'item_tex_ps3game_folder',
        'item_tex_pspgame_folder', 'item_tex_ps3sd_folder', 'item_tex_ps_store', 'item_tex_trophy',
        'item_tex_plain_folder', 'item_tex_vitaformat', 'item_tex_ps3format', 'item_tex_ps2format',
        'item_tex_pspformat', 'item_tex_ps4format', 'game_tex_default_ps3', 'tex_video', 'tex_photo',
        'tex_music', 'tex_Sign_In', 'video_tex_album_default', 'photo_tex_album_pic', 'music_tex_album_pic',
        'user_tex_load', 'user_tex_login', 'tex_Avatar_Default', 'tex_Sign_Up'],
    'explore_category_sysconf': ['tex_update', 'tex_game', 'tex_bddvd', 'tex_music', 'tex_chat', 'tex_console',
        'tex_thema', 'tex_datetime', 'tex_power', 'tex_pd', 'tex_printer', 'tex_display', 'tex_sound',
        'tex_security', 'tex_remote', 'tex_network'],
    'explore_category_network': ['tex_onlinemanual', 'tex_premo', 'tex_browser', 'tex_kensaku', 'tex_dl_manage'],
    'explore_category_friend': ['item_tex_InBox', 'item_tex_OutBox', 'item_tex_NewMessage', 'item_tex_NewFriend',
        'item_tex_Players', 'item_tex_ChatRoomText', 'item_tex_NewAvc', 'item_tex_Messages', 'item_tex_IgnoreList',
        'tex_Avatar_Default', 'item_tex_Profile_Onlines'],
    'xmb_plugin_normal': ['tex_psn', 'tex_indi_bg', 'tex_indi_chat', 'tex_indi_message', 'tex_indi_online', 'tex_indi_plus'],
    'xmb_ingame': ['tex_user', 'tex_sysconf', 'tex_photo', 'tex_music', 'tex_video', 'tex_tv', 'tex_game',
        'tex_network', 'tex_psn', 'tex_friend', 'tex_loading_icon'],
    'system_plugin': ['tex_infobar', 'tex_msgdialog_bg', 'tex_msgdialog_bg2', 'tex_msgdialog_bar', 'tex_notification',
        'tex_cross', 'tex_circle', 'tex_triangle', 'tex_rectangle', 'tex_busy', 'tex_busy_s', 'tex_busy_l',
        'tex_busy_shadow', 'tex_common_dialog', 'tex_battery_indicator', 'tex_notification_music',
        'tex_notification_settings', 'tex_notification_friend', 'tex_notification_info', 'tex_notification_caution',
        'tex_notification_mediasever', 'tex_notification_trophy_platinum', 'tex_notification_trophy_gold',
        'tex_notification_trophy_silver', 'tex_notification_trophy_bronze',
        'tex_notification_psbutton_insensitive', 'tex_notification_headset', 'tex_notification_keypad'],
};

// The 52 texture names declared by src/osk_layout.cpp kTextures, kept here so the check runs off the
// sweep data without a C++ parser. Re-checked against the source when the layout table changes.
const OSK_TEXTURES = ['BUTTON_BASE_WHITE', 'BUTTON_BASE_B', 'BUTTON_BASE_G', 'BUTTON_BASE_O', 'BUTTON_BASE_E',
    'BUTTON_BASE_P', 'BUTTON_BASE_B_S', 'BUTTON_BASE_SHORT', 'BUTTON_BASE_BLUE_GRAY', 'SBTN_FOCUS', 'LBTN_FOCUS',
    'BUTTON_FORCUS', 'LABEL_UP', 'LABEL_DOWN', 'LABEL_LEFT', 'LABEL_RIGHT', 'LABEL_L1', 'LABEL_R1', 'LABEL_L2',
    'LABEL_R2', 'LABEL_SQUARE', 'LABEL_TRIANGLE', 'LABEL_SELECT', 'LABEL_START', 'EN_LABEL_DELETE', 'EN_LABEL_SPACE',
    'EN_LABEL_RETURN', 'EN_LABEL_MODE', 'EN_LABEL_CLOSE', 'KEYTOP_COPY', 'KEYTOP_PASTE', 'MODE_BASE', 'KB_MODE_BASE',
    'MODE_GUIDE_ALPHABET', 'MODE_GUIDE_ALPHABET_CAP', 'CANDIDATE_WINDOW0x0', 'CANDIDATE_WINDOW1x1',
    'CANDIDATE_WINDOW2x2', 'SCROLL_BASE', 'SCROLL_KNOB', 'LIST_WINDOW_FOCUS', 'tbase1x0', 'tbase2x1', 'tbase3x2',
    'BASE_SYMBOL', 'FOCUS_SYMBOL',
    'SCROLL_BASE_SYMBOL', 'SCROLL_KNOB_SYMBOL', 'BASE_NOTIFY', 'MENU_BASE_0x0', 'MENU_FOCUS', 'POINTER'];

function consumed(outdir: string) {
    const data = JSON.parse(readFileSync(join(outdir, 'results.json'), 'utf8'));
    const results: Result[] = data.results;
    const have = new Map(results.map(r => [`${r.container}/${r.name}`, r]));
    CONSUMED['oskfullkeypanel_plugin'] = OSK_TEXTURES;
    let total = 0, present = 0;
    const missing: string[] = [];
    const rco = Object.keys(CONSUMED).filter(c => c !== 'qgl/icontex');
    for (const container of rco) {
        const names = [...new Set(CONSUMED[container])];
        let ok = 0;
        for (const name of names) {
            const got = have.get(`${container}/${name}`);
            if (got && got.decoded) ok++; else missing.push(`${container}:${name}`);
        }
        total += names.length; present += ok;
        console.log(`${container.padEnd(28)} ${ok}/${names.length} decoded`);
    }
    const qgl = CONSUMED['qgl/icontex'];
    console.log(`${'qgl/icontex'.padEnd(28)} ${qgl.length}/${qgl.length} decoded (DDS path, checked separately; not an RCO)`);
    console.log(`consumed RCO names ${total}, decoded ${present}, missing ${missing.length}`);
    for (const m of missing) console.log(`  MISSING ${m}`);
}

const [mode, ...rest] = process.argv.slice(2);
if (mode === 'scan') {
    const {containers, images, bad} = scanAll(rest[0]);
    mkdirSync(rest[1], {recursive: true});
    writeFileSync(join(rest[1], 'plan.json'), JSON.stringify({containers, images, bad}, null, 1));
    console.log(`containers ${containers} images ${images.length} unreadable ${bad.length}`);
} else if (mode === 'sweep') {
    const r = sweep(rest[0], rest[1], rest[2]);
    report(rest[2]);
} else if (mode === 'report') {
    report(rest[0]);
} else if (mode === 'consumed') {
    consumed(rest[0]);
} else {
    console.log('usage: tex-coverage.ts scan <dev_flash> <outdir> | sweep <dev_flash> <exe> <outdir> | report <outdir> | consumed <outdir>');
    process.exit(2);
}
