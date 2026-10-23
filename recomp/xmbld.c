/*
 * xmbld: links a PS3 VSH executable and its PRX plugins into one guest memory image.
 *
 * Mirrors RPCS3's loader (Emu/Cell/PPUModule.cpp): PRX segments are placed at a module base,
 * SCE_PPURELA relocations are applied, and every import slot is written with the address of the
 * exporting module's function descriptor (OPD). Imports nobody in the set exports are bound to
 * synthetic HLE descriptors that the runtime services natively.
 *
 * usage: xmbld <out_dir> <vsh.elf> [plugin.prx ...]
 * writes: image.bin, modules.csv, exports.csv, imports.csv, hle.csv
 */
#include "elf_be.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GUEST_SIZE 0x20000000u /* 512 MB is far more than the boot set needs */
#define PRX_BASE 0x01000000u
/* HLE descriptors (and image.elf's .pdata) follow the last module, keeping the guest span compact */
#define HLE_MAX 4096
static uint32_t HLE_OPD_BASE, HLE_CODE_BASE, PDATA_ADDR;
#define MAX_MODULES 256
#define MAX_SEGS 1024

#define NID_MODULE_START 0xBC9A0086u
#define NID_MODULE_STOP 0xAB779874u

static uint8_t* g_mem;

typedef struct { uint32_t addr, size, flags; int module; } seg_t;
static seg_t g_segs[MAX_SEGS];
static int g_nsegs;

typedef struct {
    char name[64];
    char lib_name[32];
    elf_file elf;
    uint32_t seg_addr[32];
    uint32_t base, toc, start_opd, stop_opd;
    uint32_t exports_start, exports_end, imports_start, imports_end;
} module_t;
static module_t g_mods[MAX_MODULES];
static int g_nmods;

typedef struct { char lib[32]; uint32_t nid, opd; int module, var; } export_t; /* var: opd is the variable's address */
static export_t* g_exports;
static int g_nexports, g_cap_exports;

typedef struct { char lib[32]; uint32_t nid; } hle_t;
static hle_t* g_hle;
static int g_nhle, g_cap_hle;

static uint32_t rd32(uint32_t a) { return be32(g_mem + a); }
static int mapped(uint32_t a)
{
    for (int i = 0; i < g_nsegs; i++)
        if (a >= g_segs[i].addr && a - g_segs[i].addr < g_segs[i].size) return 1;
    return 0;
}

static void cstr(uint32_t a, char* out, size_t n)
{
    size_t i = 0;
    for (; i + 1 < n && mapped(a + (uint32_t)i) && g_mem[a + i]; i++) out[i] = (char)g_mem[a + i];
    out[i] = 0;
}

static int ident(const char* s)
{
    if (!*s) return 0;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_')) return 0;
    return 1;
}

static void add_seg(uint32_t addr, uint32_t size, uint32_t flags, int module)
{
    if (g_nsegs == MAX_SEGS) { fprintf(stderr, "too many segments\n"); exit(1); }
    g_segs[g_nsegs++] = (seg_t){addr, size, flags, module};
}

static void place_module(module_t* m, uint32_t base)
{
    m->base = base;
    for (int i = 0; i < m->elf.phnum; i++) {
        elf_phdr* ph = &m->elf.ph[i];
        if (ph->type != PT_LOAD || !ph->memsz) continue;
        uint32_t addr = base + (uint32_t)ph->vaddr;
        if ((uint64_t)addr + ph->memsz > GUEST_SIZE) { fprintf(stderr, "%s: segment beyond guest space\n", m->name); exit(1); }
        memcpy(g_mem + addr, m->elf.data + ph->offset, (size_t)ph->filesz);
        m->seg_addr[i] = addr;
        add_seg(addr, (uint32_t)ph->memsz, ph->flags, (int)(m - g_mods));
    }
}

/* SCE_PPURELA records: be64 offset, be16 unk, u8 index_value, u8 index_addr, be32 type, be64 ptr */
static int apply_relocs(module_t* m)
{
    int n = 0;
    for (int i = 0; i < m->elf.phnum; i++) {
        elf_phdr* ph = &m->elf.ph[i];
        if (ph->type != PT_SCE_PPURELA) continue;
        for (uint64_t o = 0; o + 24 <= ph->filesz; o += 24) {
            const uint8_t* r = m->elf.data + ph->offset + o;
            uint64_t off = be64(r);
            uint8_t index_value = r[10], index_addr = r[11];
            uint32_t type = be32(r + 12);
            uint64_t ptr = be64(r + 16);
            uint32_t raddr = m->seg_addr[index_addr] + (uint32_t)off;
            uint64_t rdata = (index_value == 0xFF ? 0 : m->seg_addr[index_value]) + ptr;
            uint8_t* p = g_mem + raddr;
            switch (type) {
            case 1: put_be32(p, (uint32_t)rdata); break;                                   /* ADDR32 */
            case 4: put_be16(p, (uint16_t)rdata); break;                                   /* ADDR16_LO */
            case 5: put_be16(p, (uint16_t)(rdata >> 16)); break;                           /* ADDR16_HI */
            case 6: put_be16(p, (uint16_t)((rdata >> 16) + ((rdata & 0x8000) ? 1 : 0))); break; /* ADDR16_HA */
            case 10: put_be32(p, (be32(p) & 0xFC000003u) | (((uint32_t)(rdata - raddr)) & 0x03FFFFFCu)); break; /* REL24 */
            case 11: put_be32(p, (be32(p) & 0xFFFF0003u) | (((uint32_t)(rdata - raddr)) & 0x0000FFFCu)); break; /* REL14 */
            case 38: put_be64(p, rdata); break;                                            /* ADDR64 */
            case 44: put_be64(p, rdata - raddr); break;                                    /* REL64 */
            case 57: put_be16(p, (uint16_t)((be16(p) & 3) | ((uint16_t)rdata & 0xFFFC))); break; /* ADDR16_LO_DS */
            default: fprintf(stderr, "%s: unknown relocation type %u at 0x%x\n", m->name, type, raddr); break;
            }
            n++;
        }
    }
    return n;
}

static void add_export(const char* lib, uint32_t nid, uint32_t opd, int module, int var)
{
    if (g_nexports == g_cap_exports) {
        g_cap_exports = g_cap_exports ? g_cap_exports * 2 : 4096;
        g_exports = (export_t*)realloc(g_exports, sizeof(export_t) * (size_t)g_cap_exports);
    }
    export_t* e = &g_exports[g_nexports++];
    snprintf(e->lib, sizeof(e->lib), "%s", lib);
    e->nid = nid;
    e->opd = opd;
    e->module = module;
    e->var = var;
}

static const export_t* find_export(const char* lib, uint32_t nid, int var)
{
    for (int i = 0; i < g_nexports; i++)
        if (g_exports[i].nid == nid && g_exports[i].var == var && strcmp(g_exports[i].lib, lib) == 0) return &g_exports[i];
    return NULL;
}

static uint32_t hle_opd(const char* lib, uint32_t nid)
{
    for (int i = 0; i < g_nhle; i++)
        if (g_hle[i].nid == nid && strcmp(g_hle[i].lib, lib) == 0) return HLE_OPD_BASE + (uint32_t)i * 8;
    if (g_nhle == HLE_MAX) { fprintf(stderr, "more than %d HLE imports\n", HLE_MAX); exit(1); }
    if (g_nhle == g_cap_hle) {
        g_cap_hle = g_cap_hle ? g_cap_hle * 2 : 1024;
        g_hle = (hle_t*)realloc(g_hle, sizeof(hle_t) * (size_t)g_cap_hle);
    }
    hle_t* h = &g_hle[g_nhle];
    snprintf(h->lib, sizeof(h->lib), "%s", lib);
    h->nid = nid;
    uint32_t opd = HLE_OPD_BASE + (uint32_t)g_nhle * 8;
    put_be32(g_mem + opd, HLE_CODE_BASE + (uint32_t)g_nhle * 4);
    put_be32(g_mem + opd + 4, 0);
    g_nhle++;
    return opd;
}

/* Export records (ppu_prx_module_info, 0x1c bytes). attr bit 0 = library, else management. */
static int read_exports(module_t* m, uint32_t start, uint32_t end)
{
    int n = 0, mi = (int)(m - g_mods);
    for (uint32_t a = start; a < end;) {
        uint8_t size = g_mem[a];
        uint16_t attr = be16(g_mem + a + 4);
        uint16_t num_func = be16(g_mem + a + 6), num_var = be16(g_mem + a + 8);
        uint32_t name = rd32(a + 0x10), nids = rd32(a + 0x14), addrs = rd32(a + 0x18);
        if (attr & 1) {
            char lib[32];
            cstr(name, lib, sizeof(lib));
            for (uint32_t i = 0; i < num_func; i++) add_export(lib, rd32(nids + 4 * i), rd32(addrs + 4 * i), mi, 0);
            /* variable exports follow the functions in the same nid/addr arrays */
            for (uint32_t i = num_func; i < num_func + num_var; i++) add_export(lib, rd32(nids + 4 * i), rd32(addrs + 4 * i), mi, 1);
            n += num_func + num_var;
        } else {
            for (uint32_t i = 0; i < num_func; i++) {
                uint32_t nid = rd32(nids + 4 * i), opd = rd32(addrs + 4 * i);
                if (nid == NID_MODULE_START) m->start_opd = opd;
                if (nid == NID_MODULE_STOP) m->stop_opd = opd;
            }
        }
        a += size ? size : 0x1c;
    }
    return n;
}

/* Executables carry no usable prx param here, so validated records are located by scanning. */
static void scan_tables(module_t* m)
{
    uint32_t lo = 0xFFFFFFFFu, hi = 0;
    int mi = (int)(m - g_mods);
    for (int i = 0; i < g_nsegs; i++) {
        if (g_segs[i].module != mi) continue;
        if (g_segs[i].addr < lo) lo = g_segs[i].addr;
        if (g_segs[i].addr + g_segs[i].size > hi) hi = g_segs[i].addr + g_segs[i].size;
    }
    m->exports_start = m->imports_start = 0;
    for (uint32_t a = lo; a + 0x2c <= hi; a += 4) {
        uint8_t size = g_mem[a];
        if ((size != 0x1c && size != 0x2c) || g_mem[a + 1] != 0) continue;
        uint16_t num_func = be16(g_mem + a + 6), num_var = be16(g_mem + a + 8);
        uint32_t name = rd32(a + 0x10), nids = rd32(a + 0x14), addrs = rd32(a + 0x18);
        if (!(num_func || num_var) || num_func > 8192 || num_var > 8192 || !mapped(name) || !mapped(nids) || !mapped(addrs)) continue;
        char lib[32];
        cstr(name, lib, sizeof(lib));
        if (!ident(lib)) continue;
        if (size == 0x1c && (be16(g_mem + a + 4) & 1)) {
            if (!m->exports_start) m->exports_start = a;
            m->exports_end = a + 0x1c;
        } else if (size == 0x2c && be16(g_mem + a + 2) <= 1) {
            if (!m->imports_start) m->imports_start = a;
            m->imports_end = a + 0x2c;
        }
    }
}

/* Reference lists ({be32 type, addr, addend}, type 0 ends) patched with an import's address, as RPCS3
   (0b0fa08) ppu_patch_refs does for imported variables and for functions whose library sets attribute 0x2000. */
static void patch_refs(uint32_t ref, uint32_t target)
{
    for (; ref && mapped(ref) && rd32(ref); ref += 12) {
        uint32_t type = rd32(ref), addr = rd32(ref + 4), data = target + rd32(ref + 8);
        uint8_t* p = g_mem + addr;
        switch (type) {
        case 1: put_be32(p, data); break;
        case 4: put_be16(p, (uint16_t)data); break;
        case 6: put_be16(p, (uint16_t)((data >> 16) + ((data & 0x8000) ? 1 : 0))); break;
        case 57: put_be16(p, (uint16_t)((be16(p) & 3) | ((uint16_t)data & 0xFFFC))); break;
        default: fprintf(stderr, "unknown ref type %u at 0x%x\n", type, addr); break;
        }
    }
}

static int g_var_imports, g_var_unbound;

static int bind_imports(module_t* m, FILE* out, int* hle_count)
{
    int n = 0;
    for (uint32_t a = m->imports_start; a && a < m->imports_end;) {
        uint8_t size = g_mem[a];
        uint16_t attr = be16(g_mem + a + 4), num_func = be16(g_mem + a + 6), num_var = be16(g_mem + a + 8);
        uint32_t name = rd32(a + 0x10), nids = rd32(a + 0x14), addrs = rd32(a + 0x18);
        uint32_t vnids = rd32(a + 0x1c), vstubs = rd32(a + 0x20);
        char lib[32];
        cstr(name, lib, sizeof(lib));
        if (size == 0x2c && ident(lib)) {
            for (uint32_t i = 0; i < num_func; i++) {
                uint32_t nid = rd32(nids + 4 * i), slot = addrs + 4 * i;
                const export_t* e = find_export(lib, nid, 0);
                uint32_t target = e ? e->opd : hle_opd(lib, nid);
                if (!e) (*hle_count)++;
                put_be32(g_mem + slot, target);
                if (attr & 0x2000) patch_refs(rd32(nids + 4 * (i + num_func)), target);
                fprintf(out, "%s,%s,0x%08X,0x%08X,0x%08X,%s\n", m->name, lib, nid, slot, target, e ? g_mods[e->module].name : "HLE");
                n++;
            }
            for (uint32_t i = 0; i < num_var; i++) {
                const export_t* e = find_export(lib, rd32(vnids + 4 * i), 1);
                g_var_imports++;
                if (!e) { g_var_unbound++; fprintf(stderr, "%s: unbound variable import %s 0x%08X\n", m->name, lib, rd32(vnids + 4 * i)); }
                patch_refs(rd32(vstubs + 4 * i), e ? e->opd : 0);
            }
        }
        a += size ? size : 0x2c;
    }
    return n;
}

static void basename_of(const char* path, char* out, size_t n)
{
    const char* b = path;
    for (const char* p = path; *p; p++)
        if (*p == '/' || *p == '\\') b = p + 1;
    snprintf(out, n, "%s", b);
}

static FILE* open_out(const char* dir, const char* name)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    return f;
}

/* .pdata entries (IMAGE_CE_RUNTIME_FUNCTION) from Ghidra boundaries: entry + its contiguous range */
typedef struct { uint32_t begin, insns; } pdata_t;
static pdata_t* g_pdata;
static int g_npdata, g_cap_pdata;

static int cmp_pdata(const void* a, const void* b)
{
    uint32_t x = ((const pdata_t*)a)->begin, y = ((const pdata_t*)b)->begin;
    return x < y ? -1 : x > y;
}

/* Every function body range (half-open), merged into runs: only these bytes are presented as code. */
typedef struct { uint32_t start, end; } run_t;
static run_t* g_code;
static int g_ncode, g_cap_code;

static int cmp_run(const void* a, const void* b)
{
    uint32_t x = ((const run_t*)a)->start, y = ((const run_t*)b)->start;
    return x < y ? -1 : x > y;
}

static void add_code(uint32_t start, uint32_t end)
{
    if (g_ncode == g_cap_code) {
        g_cap_code = g_cap_code ? g_cap_code * 2 : 65536;
        g_code = (run_t*)realloc(g_code, sizeof(run_t) * (size_t)g_cap_code);
    }
    g_code[g_ncode++] = (run_t){start, end};
}

static void merge_code(void)
{
    qsort(g_code, (size_t)g_ncode, sizeof(run_t), cmp_run);
    int n = 0;
    for (int i = 0; i < g_ncode; i++) {
        if (n && g_code[i].start <= g_code[n - 1].end) {
            if (g_code[i].end > g_code[n - 1].end) g_code[n - 1].end = g_code[i].end;
        } else {
            g_code[n++] = g_code[i];
        }
    }
    g_ncode = n;
}

static void add_pdata(uint32_t begin, uint32_t insns)
{
    if (g_npdata == g_cap_pdata) {
        g_cap_pdata = g_cap_pdata ? g_cap_pdata * 2 : 65536;
        g_pdata = (pdata_t*)realloc(g_pdata, sizeof(pdata_t) * (size_t)g_cap_pdata);
    }
    g_pdata[g_npdata++] = (pdata_t){begin, insns};
}

static void load_pdata(const char* functions_dir)
{
    static char line[1 << 16];
    for (int mi = 0; mi < g_nmods; mi++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s_functions.csv", functions_dir, g_mods[mi].name);
        FILE* f = fopen(path, "r");
        if (!f) { fprintf(stderr, "warning: no function list %s\n", path); continue; }
        fgets(line, sizeof(line), f);
        while (fgets(line, sizeof(line), f)) {
            char* p = line;
            uint32_t entry = (uint32_t)strtoul(p, &p, 16);
            if (*p++ != ',') continue;
            /*
             * Every range is marked as code. The .pdata length spans from the entry to the end of the
             * function's last range: XenonRecomp analyses unclaimed code as new functions, and starting
             * mid-function it cannot follow branches back to before its start. Ranges below the entry
             * get their own .pdata entry. Overlapping neighbours are just translated twice.
             */
            uint32_t base = g_mods[mi].base, max_end = entry;
            while (*p && *p != ',') {
                uint32_t s = (uint32_t)strtoul(p, &p, 16);
                if (*p != '-') break;
                uint32_t e = (uint32_t)strtoul(p + 1, &p, 16);
                add_code(base + s, base + e + 1);
                if (s < entry) add_pdata(base + s, (e + 1 - s) / 4);
                /* a range that starts below the entry but runs past it still belongs to the entry's span */
                if (e + 1 > max_end) max_end = e + 1;
                if (*p == ';') p++;
            }
            if (max_end > entry) add_pdata(base + entry, (max_end - entry) / 4);
        }
        fclose(f);
    }
    qsort(g_pdata, (size_t)g_npdata, sizeof(pdata_t), cmp_pdata);
    /* one function per start address (a range below one entry can start at another function's entry): keep the longest */
    int n = 0;
    for (int i = 0; i < g_npdata; i++) {
        if (n && g_pdata[n - 1].begin == g_pdata[i].begin) {
            if (g_pdata[i].insns > g_pdata[n - 1].insns) g_pdata[n - 1].insns = g_pdata[i].insns;
        } else {
            g_pdata[n++] = g_pdata[i];
        }
    }
    g_npdata = n;
    /* Ghidra reports some entries (export thunks) as a 1-byte range: give them code up to the next function */
    for (int i = 0; i < g_npdata; i++) {
        if (g_pdata[i].insns) continue;
        const uint32_t next = i + 1 < g_npdata ? g_pdata[i + 1].begin : g_pdata[i].begin + 4;
        g_pdata[i].insns = (next - g_pdata[i].begin) / 4;
        add_code(g_pdata[i].begin, next);
    }
    merge_code();
}

/*
 * image.elf: the linked image as a big-endian ELF32 with named sections, the input format
 * XenonRecomp's ElfLoadImage() accepts. Inside executable segments only the merged Ghidra function
 * ranges are flagged as code (XenonRecomp treats every unclaimed code byte as a new function, and
 * these segments also hold read-only data); the gaps become data sections. The function
 * boundaries are supplied as a synthetic .pdata (Xbox 360 runtime function table).
 */
typedef struct { uint32_t addr, size; int exec; } osec_t;

static void write_elf32(const char* out_dir, uint32_t entry)
{
    /* split segments into code runs and data gaps */
    int cap = g_nsegs * 2 + g_ncode * 2 + 8, nos = 0;
    osec_t* os = (osec_t*)malloc(sizeof(osec_t) * (size_t)cap);
    for (int i = 0; i < g_nsegs; i++) {
        uint32_t s = g_segs[i].addr, e = s + g_segs[i].size;
        if (!(g_segs[i].flags & 1)) { os[nos++] = (osec_t){s, e - s, 0}; continue; }
        uint32_t cur = s;
        for (int r = 0; r < g_ncode; r++) {
            uint32_t rs = g_code[r].start, re = g_code[r].end;
            if (re <= cur || rs >= e) continue;
            if (rs < cur) rs = cur;
            if (re > e) re = e;
            if (rs > cur) os[nos++] = (osec_t){cur, rs - cur, 0};
            os[nos++] = (osec_t){rs, re - rs, 1};
            cur = re;
        }
        if (cur < e) os[nos++] = (osec_t){cur, e - cur, 0};
    }
    if (nos + 3 > 0xFF00) { fprintf(stderr, "too many sections (%d)\n", nos); exit(1); }

    int nsec = nos + 3; /* null, sections..., .pdata, .shstrtab */
    size_t ncap = (size_t)nsec * 24 + 64, nlen = 1;
    char* names = (char*)malloc(ncap);
    names[0] = 0;
    uint32_t* name_off = (uint32_t*)calloc((size_t)nsec, sizeof(uint32_t));
    for (int i = 0; i < nos; i++) {
        name_off[i + 1] = (uint32_t)nlen;
        nlen += (size_t)sprintf(names + nlen, "%s.%08X", os[i].exec ? ".text" : ".data", os[i].addr) + 1;
    }
    name_off[nos + 1] = (uint32_t)nlen;
    nlen += (size_t)sprintf(names + nlen, ".pdata") + 1;
    name_off[nos + 2] = (uint32_t)nlen;
    nlen += (size_t)sprintf(names + nlen, ".shstrtab") + 1;

    uint32_t lo = 0xFFFFFFFFu, hi = 0, code_bytes = 0;
    for (int i = 0; i < nos; i++) {
        if (os[i].addr < lo) lo = os[i].addr;
        if (os[i].addr + os[i].size > hi) hi = os[i].addr + os[i].size;
        if (os[i].exec) code_bytes += os[i].size;
    }

    FILE* f = open_out(out_dir, "image.elf");
    const uint32_t ehsize = 52, phsize = 32, shsize = 40;
    uint32_t off = ehsize + phsize;
    uint32_t* data_off = (uint32_t*)calloc((size_t)nsec, sizeof(uint32_t));
    for (int i = 0; i < nos; i++) { data_off[i + 1] = off; off += (os[i].size + 3) & ~3u; }
    data_off[nos + 1] = off; off += (uint32_t)g_npdata * 8;
    data_off[nos + 2] = off; off += ((uint32_t)nlen + 3) & ~3u;
    uint32_t shoff = off;

    uint8_t eh[52] = {0x7f, 'E', 'L', 'F', 1, 2, 1};
    put_be16(eh + 16, 2);    /* ET_EXEC */
    put_be16(eh + 18, 20);   /* EM_PPC */
    put_be32(eh + 20, 1);
    put_be32(eh + 24, entry);
    put_be32(eh + 28, ehsize);
    put_be32(eh + 32, shoff);
    put_be16(eh + 40, (uint16_t)ehsize);
    put_be16(eh + 42, (uint16_t)phsize);
    put_be16(eh + 44, 1);
    put_be16(eh + 46, (uint16_t)shsize);
    put_be16(eh + 48, (uint16_t)nsec);
    put_be16(eh + 50, (uint16_t)(nsec - 1));
    fwrite(eh, 1, sizeof(eh), f);

    uint8_t ph[32] = {0};
    put_be32(ph, 1); /* PT_LOAD: XenonRecomp takes the image base from the first one */
    put_be32(ph + 8, lo);
    put_be32(ph + 12, lo);
    put_be32(ph + 20, hi - lo);
    put_be32(ph + 24, 7);
    fwrite(ph, 1, sizeof(ph), f);

    static const uint8_t pad[4] = {0};
    for (int i = 0; i < nos; i++) {
        fwrite(g_mem + os[i].addr, 1, os[i].size, f);
        fwrite(pad, 1, ((os[i].size + 3) & ~3u) - os[i].size, f);
    }
    for (int i = 0; i < g_npdata; i++) {
        uint8_t r[8];
        uint32_t len = g_pdata[i].insns > 0x3FFFFF ? 0x3FFFFF : g_pdata[i].insns;
        put_be32(r, g_pdata[i].begin);
        put_be32(r + 4, len << 8);
        fwrite(r, 1, 8, f);
    }
    fwrite(names, 1, nlen, f);
    fwrite(pad, 1, ((nlen + 3) & ~3u) - nlen, f);

    for (int i = 0; i < nsec; i++) {
        uint8_t sh[40] = {0};
        if (i > 0) {
            int last = i == nsec - 1, pd = i == nsec - 2;
            put_be32(sh, name_off[i]);
            put_be32(sh + 4, last ? 3 : 1);                    /* SHT_STRTAB / SHT_PROGBITS */
            uint32_t flags = last ? 0 : 2;                     /* SHF_ALLOC */
            if (!last && !pd && os[i - 1].exec) flags |= 4;    /* SHF_EXECINSTR */
            put_be32(sh + 8, flags);
            put_be32(sh + 12, last ? 0 : pd ? PDATA_ADDR : os[i - 1].addr);
            put_be32(sh + 16, data_off[i]);
            put_be32(sh + 20, last ? (uint32_t)nlen : pd ? (uint32_t)g_npdata * 8 : os[i - 1].size);
            put_be32(sh + 32, 4);
        }
        fwrite(sh, 1, sizeof(sh), f);
    }
    /* XenonRecomp emits PPC_IMAGE_SIZE from the file length and places its function lookup table
       after base + size, so the file must cover the whole guest span including HLE and .pdata. */
    uint32_t span = PDATA_ADDR + (uint32_t)g_npdata * 8 - lo;
    for (long pos = ftell(f); pos < (long)span; pos++) fputc(0, f);
    fclose(f);
    free(name_off);
    free(data_off);
    free(names);
    free(os);
    printf("image.elf: %d sections (%d code runs, %u code bytes), %d .pdata functions, base 0x%08X\n",
        nsec, g_ncode, code_bytes, g_npdata, lo);
}

int main(int argc, char** argv)
{
    const char* functions_dir = NULL;
    if (argc > 2 && strcmp(argv[1], "--functions") == 0) {
        functions_dir = argv[2];
        argv += 2;
        argc -= 2;
    }
    if (argc < 3) { fprintf(stderr, "usage: xmbld [--functions <dir>] <out_dir> <vsh.elf> [plugin.prx ...]\n"); return 2; }
    const char* out_dir = argv[1];
    g_mem = (uint8_t*)calloc(1, GUEST_SIZE);
    if (!g_mem) { fprintf(stderr, "cannot allocate guest memory\n"); return 1; }

    char err[512];
    uint32_t next_base = PRX_BASE;
    for (int i = 2; i < argc; i++) {
        module_t* m = &g_mods[g_nmods++];
        basename_of(argv[i], m->name, sizeof(m->name));
        if (elf_load(&m->elf, argv[i], err, sizeof(err))) { fprintf(stderr, "%s\n", err); return 1; }
        if (m->elf.type == ELF_ET_EXEC) {
            place_module(m, 0);
        } else if (m->elf.type == ELF_ET_SCE_PPURELA) {
            uint32_t span = 0;
            for (int s = 0; s < m->elf.phnum; s++)
                if (m->elf.ph[s].type == PT_LOAD && m->elf.ph[s].vaddr + m->elf.ph[s].memsz > span)
                    span = (uint32_t)(m->elf.ph[s].vaddr + m->elf.ph[s].memsz);
            place_module(m, next_base);
            next_base = (next_base + span + 0xFFFFu) & ~0xFFFFu;
        } else {
            fprintf(stderr, "%s: unsupported ELF type 0x%x\n", m->name, m->elf.type);
            return 1;
        }
    }
    HLE_OPD_BASE = next_base;
    HLE_CODE_BASE = HLE_OPD_BASE + HLE_MAX * 8;
    PDATA_ADDR = (HLE_CODE_BASE + HLE_MAX * 4 + 0xFFFFu) & ~0xFFFFu;

    for (int i = 0; i < g_nmods; i++) {
        module_t* m = &g_mods[i];
        int relocs = apply_relocs(m);
        const elf_phdr* p0 = &m->elf.ph[0];
        if (m->elf.type == ELF_ET_SCE_PPURELA && p0->paddr) {
            uint32_t li = m->seg_addr[0] + (uint32_t)(p0->paddr - p0->offset);
            cstr(li + 4, m->lib_name, sizeof(m->lib_name));
            m->toc = rd32(li + 0x20);
            m->exports_start = rd32(li + 0x24);
            m->exports_end = rd32(li + 0x28);
            m->imports_start = rd32(li + 0x2c);
            m->imports_end = rd32(li + 0x30);
        } else {
            scan_tables(m);
        }
        int exports = m->exports_start ? read_exports(m, m->exports_start, m->exports_end) : 0;
        printf("%-28s base=0x%08X relocs=%6d exports=%5d imports=0x%08X-0x%08X\n", m->name, m->base, relocs, exports, m->imports_start, m->imports_end);
    }

    FILE* fi = open_out(out_dir, "imports.csv");
    fprintf(fi, "module,library,nid,slot,target_opd,provider\n");
    int total = 0, hle = 0;
    for (int i = 0; i < g_nmods; i++) total += bind_imports(&g_mods[i], fi, &hle);
    fclose(fi);
    if (g_nhle) add_seg(HLE_OPD_BASE, (uint32_t)g_nhle * 8, 4, -1);

    FILE* fe = open_out(out_dir, "exports.csv");
    fprintf(fe, "module,library,nid,opd,code,toc\n");
    for (int i = 0; i < g_nexports; i++) {
        export_t* e = &g_exports[i];
        fprintf(fe, "%s,%s,0x%08X,0x%08X,0x%08X,0x%08X\n", g_mods[e->module].name, e->lib, e->nid, e->opd, rd32(e->opd), rd32(e->opd + 4));
    }
    fclose(fe);

    FILE* fh = open_out(out_dir, "hle.csv");
    fprintf(fh, "index,library,nid,opd,code\n");
    for (int i = 0; i < g_nhle; i++)
        fprintf(fh, "%d,%s,0x%08X,0x%08X,0x%08X\n", i, g_hle[i].lib, g_hle[i].nid, HLE_OPD_BASE + (uint32_t)i * 8, HLE_CODE_BASE + (uint32_t)i * 4);
    fclose(fh);

    FILE* fm = open_out(out_dir, "modules.csv");
    fprintf(fm, "module,lib_name,base,toc,module_start_opd,module_stop_opd\n");
    for (int i = 0; i < g_nmods; i++) {
        module_t* m = &g_mods[i];
        fprintf(fm, "%s,%s,0x%08X,0x%08X,0x%08X,0x%08X\n", m->name, m->lib_name, m->base, m->toc, m->start_opd, m->stop_opd);
    }
    fclose(fm);

    /* image.bin: "XMBIMG01", be32 count, then {be32 addr, be32 size, be32 flags, bytes[size]} */
    FILE* fo = open_out(out_dir, "image.bin");
    uint8_t hdr[12];
    memcpy(hdr, "XMBIMG01", 8);
    put_be32(hdr + 8, (uint32_t)g_nsegs);
    fwrite(hdr, 1, 12, fo);
    for (int i = 0; i < g_nsegs; i++) {
        uint8_t sh[12];
        put_be32(sh, g_segs[i].addr);
        put_be32(sh + 4, g_segs[i].size);
        put_be32(sh + 8, g_segs[i].flags);
        fwrite(sh, 1, 12, fo);
        fwrite(g_mem + g_segs[i].addr, 1, g_segs[i].size, fo);
    }
    fclose(fo);

    printf("variable imports=%d unbound=%d\n", g_var_imports, g_var_unbound);
    printf("modules=%d segments=%d exports=%d imports=%d bound_to_modules=%d bound_to_hle=%d (distinct hle functions=%d)\n",
        g_nmods, g_nsegs, g_nexports, total, total - hle, hle, g_nhle);

    if (functions_dir) {
        load_pdata(functions_dir);
        write_elf32(out_dir, (uint32_t)be64(g_mods[0].elf.data + 0x18));
    }
    return 0;
}
