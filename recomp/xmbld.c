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
#define HLE_OPD_BASE 0x0F000000u
#define HLE_CODE_BASE 0x0F800000u
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

typedef struct { char lib[32]; uint32_t nid, opd; int module; } export_t;
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

static void add_export(const char* lib, uint32_t nid, uint32_t opd, int module)
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
}

static const export_t* find_export(const char* lib, uint32_t nid)
{
    for (int i = 0; i < g_nexports; i++)
        if (g_exports[i].nid == nid && strcmp(g_exports[i].lib, lib) == 0) return &g_exports[i];
    return NULL;
}

static uint32_t hle_opd(const char* lib, uint32_t nid)
{
    for (int i = 0; i < g_nhle; i++)
        if (g_hle[i].nid == nid && strcmp(g_hle[i].lib, lib) == 0) return HLE_OPD_BASE + (uint32_t)i * 8;
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
            for (uint32_t i = 0; i < num_func; i++) add_export(lib, rd32(nids + 4 * i), rd32(addrs + 4 * i), mi);
            n += num_func;
        } else {
            for (uint32_t i = 0; i < num_func; i++) {
                uint32_t nid = rd32(nids + 4 * i), opd = rd32(addrs + 4 * i);
                if (nid == NID_MODULE_START) m->start_opd = opd;
                if (nid == NID_MODULE_STOP) m->stop_opd = opd;
            }
        }
        (void)num_var;
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
        uint16_t num_func = be16(g_mem + a + 6);
        uint32_t name = rd32(a + 0x10), nids = rd32(a + 0x14), addrs = rd32(a + 0x18);
        if (!num_func || num_func > 8192 || !mapped(name) || !mapped(nids) || !mapped(addrs)) continue;
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

static int bind_imports(module_t* m, FILE* out, int* hle_count)
{
    int n = 0;
    for (uint32_t a = m->imports_start; a && a < m->imports_end;) {
        uint8_t size = g_mem[a];
        uint16_t num_func = be16(g_mem + a + 6);
        uint32_t name = rd32(a + 0x10), nids = rd32(a + 0x14), addrs = rd32(a + 0x18);
        char lib[32];
        cstr(name, lib, sizeof(lib));
        if (size == 0x2c && ident(lib)) {
            for (uint32_t i = 0; i < num_func; i++) {
                uint32_t nid = rd32(nids + 4 * i), slot = addrs + 4 * i;
                const export_t* e = find_export(lib, nid);
                uint32_t target = e ? e->opd : hle_opd(lib, nid);
                if (!e) (*hle_count)++;
                put_be32(g_mem + slot, target);
                fprintf(out, "%s,%s,0x%08X,0x%08X,0x%08X,%s\n", m->name, lib, nid, slot, target, e ? g_mods[e->module].name : "HLE");
                n++;
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

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "usage: xmbld <out_dir> <vsh.elf> [plugin.prx ...]\n"); return 2; }
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

    printf("modules=%d segments=%d exports=%d imports=%d bound_to_modules=%d bound_to_hle=%d (distinct hle functions=%d)\n",
        g_nmods, g_nsegs, g_nexports, total, total - hle, hle, g_nhle);
    return 0;
}
