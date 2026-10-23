/*
 * ppc2c: static recompiler from PS3 PPU (PowerPC64 + VMX) machine code to C.
 *
 * Input: an xmbld image directory (image.bin, modules.csv) and Ghidra function boundary CSVs
 * (<module>_functions.csv, addresses relative to the module's load base).
 * Output: one C function per guest function, chunked into files, plus a dispatch table.
 *
 * Each guest function F becomes `void F_<addr>(ppu_t* c)`. Branches inside F become gotos,
 * direct calls become C calls, blr becomes return, and indirect transfers go through ppu_call().
 * Common instructions are emitted inline with RPCS3 interpreter semantics; rare ones (VMX,
 * FPSCR control, string ops, traps) are delegated to ppu_interp() one instruction at a time.
 *
 * usage: ppc2c <image_dir> <functions_dir> <out_dir>
 */
#include "elf_be.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GUEST_SIZE 0x20000000u
#define FUNCS_PER_FILE 1500

static uint8_t* g_mem;

typedef struct { uint32_t start, end; } range_t; /* inclusive end, like Ghidra */
typedef struct {
    uint32_t entry;
    int module;
    int nranges;
    range_t* ranges;
} func_t;

static func_t* g_funcs;
static int g_nfuncs, g_cap_funcs;

typedef struct { char name[64]; uint32_t base; } mod_t;
static mod_t g_mods[256];
static int g_nmods;

/* stats */
static long g_insns, g_interp, g_unknown;
static long g_interp_by_op[64];

/* ---------- string buffer ---------- */
typedef struct { char* p; size_t n, cap; } sb_t;
static void sb_printf(sb_t* s, const char* fmt, ...)
{
    va_list ap;
    for (;;) {
        size_t room = s->cap - s->n;
        va_start(ap, fmt);
        int w = vsnprintf(s->p ? s->p + s->n : NULL, s->p ? room : 0, fmt, ap);
        va_end(ap);
        if (w >= 0 && s->p && (size_t)w < room) { s->n += (size_t)w; return; }
        s->cap = (s->cap ? s->cap * 2 : 1 << 20) + (size_t)(w > 0 ? w : 0);
        s->p = (char*)realloc(s->p, s->cap);
    }
}

/* ---------- inputs ---------- */
static void load_image(const char* dir)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/image.bin", dir);
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
    uint8_t hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "XMBIMG01", 8)) { fprintf(stderr, "bad image\n"); exit(1); }
    uint32_t n = be32(hdr + 8);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t sh[12];
        if (fread(sh, 1, 12, f) != 12) { fprintf(stderr, "truncated image\n"); exit(1); }
        uint32_t addr = be32(sh), size = be32(sh + 4);
        if ((uint64_t)addr + size > GUEST_SIZE || fread(g_mem + addr, 1, size, f) != size) { fprintf(stderr, "bad segment\n"); exit(1); }
    }
    fclose(f);

    snprintf(path, sizeof(path), "%s/modules.csv", dir);
    f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
    char line[512];
    fgets(line, sizeof(line), f);
    while (fgets(line, sizeof(line), f)) {
        mod_t* m = &g_mods[g_nmods];
        char* comma = strchr(line, ',');
        if (!comma) continue;
        *comma = 0;
        snprintf(m->name, sizeof(m->name), "%s", line);
        char* p = strchr(comma + 1, ',');
        if (!p) continue;
        m->base = (uint32_t)strtoul(p + 1, NULL, 16);
        g_nmods++;
    }
    fclose(f);
}

static void add_func(uint32_t entry, int module, range_t* r, int nr)
{
    if (g_nfuncs == g_cap_funcs) {
        g_cap_funcs = g_cap_funcs ? g_cap_funcs * 2 : 65536;
        g_funcs = (func_t*)realloc(g_funcs, sizeof(func_t) * (size_t)g_cap_funcs);
    }
    func_t* fn = &g_funcs[g_nfuncs++];
    fn->entry = entry;
    fn->module = module;
    fn->nranges = nr;
    fn->ranges = (range_t*)malloc(sizeof(range_t) * (size_t)nr);
    memcpy(fn->ranges, r, sizeof(range_t) * (size_t)nr);
}

static void load_functions(const char* dir)
{
    for (int mi = 0; mi < g_nmods; mi++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s_functions.csv", dir, g_mods[mi].name);
        FILE* f = fopen(path, "r");
        if (!f) { fprintf(stderr, "warning: no function list for %s\n", g_mods[mi].name); continue; }
        static char line[1 << 16];
        fgets(line, sizeof(line), f);
        uint32_t base = g_mods[mi].base;
        while (fgets(line, sizeof(line), f)) {
            char* p = line;
            uint32_t entry = (uint32_t)strtoul(p, &p, 16) + base;
            if (*p != ',') continue;
            p++;
            range_t r[512];
            int nr = 0;
            while (*p && *p != ',' && nr < 512) {
                uint32_t s = (uint32_t)strtoul(p, &p, 16);
                if (*p != '-') break;
                uint32_t e = (uint32_t)strtoul(p + 1, &p, 16);
                r[nr++] = (range_t){s + base, e + base};
                if (*p == ';') p++;
            }
            if (nr) add_func(entry, mi, r, nr);
        }
        fclose(f);
    }
}

static int cmp_func(const void* a, const void* b)
{
    uint32_t x = ((const func_t*)a)->entry, y = ((const func_t*)b)->entry;
    return x < y ? -1 : x > y;
}
static int cmp_range(const void* a, const void* b)
{
    uint32_t x = ((const range_t*)a)->start, y = ((const range_t*)b)->start;
    return x < y ? -1 : x > y;
}

static const func_t* find_entry(uint32_t a)
{
    int lo = 0, hi = g_nfuncs - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_funcs[mid].entry == a) return &g_funcs[mid];
        if (g_funcs[mid].entry < a) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/* ---------- per-function state ---------- */
static uint32_t* g_addrs;     /* instruction addresses of current function, sorted */
static int g_naddrs, g_cap_addrs;
static uint8_t* g_label;      /* parallel to g_addrs */

static int idx_of(uint32_t a)
{
    int lo = 0, hi = g_naddrs - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_addrs[mid] == a) return mid;
        if (g_addrs[mid] < a) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

/* referenced functions in the current output file */
static uint32_t* g_refs;
static int g_nrefs, g_cap_refs;
static void add_ref(uint32_t a)
{
    if (g_nrefs == g_cap_refs) {
        g_cap_refs = g_cap_refs ? g_cap_refs * 2 : 4096;
        g_refs = (uint32_t*)realloc(g_refs, sizeof(uint32_t) * (size_t)g_cap_refs);
    }
    g_refs[g_nrefs++] = a;
}

/* ---------- emission helpers ---------- */
#define RD ((op >> 21) & 31)
#define RA ((op >> 16) & 31)
#define RB ((op >> 11) & 31)
#define RC_ ((op >> 6) & 31)
#define RCBIT (op & 1)
#define SIMM ((int32_t)(int16_t)(op & 0xFFFF))
#define UIMM (op & 0xFFFF)

static void ea_d(sb_t* s, uint32_t op) /* D-form EA with ra==0 meaning 0 */
{
    if (RA) sb_printf(s, "uint64_t ea = c->gpr[%u] + (int64_t)%d;", RA, SIMM);
    else sb_printf(s, "uint64_t ea = (uint64_t)(int64_t)%d;", SIMM);
}
static void ea_ds(sb_t* s, uint32_t op)
{
    int32_t ds = (int32_t)(int16_t)(op & 0xFFFC);
    if (RA) sb_printf(s, "uint64_t ea = c->gpr[%u] + (int64_t)%d;", RA, ds);
    else sb_printf(s, "uint64_t ea = (uint64_t)(int64_t)%d;", ds);
}
static void ea_x(sb_t* s, uint32_t op)
{
    if (RA) sb_printf(s, "uint64_t ea = c->gpr[%u] + c->gpr[%u];", RA, RB);
    else sb_printf(s, "uint64_t ea = c->gpr[%u];", RB);
}

static void emit_interp(sb_t* s, uint32_t a, uint32_t op)
{
    sb_printf(s, "ppu_interp(c, 0x%08XU, 0x%08XU);", a, op);
    g_interp++;
    g_interp_by_op[op >> 26]++;
}

static void emit_call(sb_t* s, uint32_t target)
{
    if (find_entry(target)) { sb_printf(s, "F_%08X(c);", target); add_ref(target); }
    else sb_printf(s, "ppu_call(c, 0x%08XU);", target);
}

/* Transfer control to target without link (b / taken bc). */
static void emit_jump(sb_t* s, uint32_t target)
{
    int i = idx_of(target);
    if (i >= 0) { sb_printf(s, "goto L_%08X;", target); g_label[i] = 1; }
    else { sb_printf(s, "{ "); emit_call(s, target); sb_printf(s, " return; }"); }
}

/* Condition for bc/bclr/bcctr. Returns 1 if unconditional. */
static int emit_cond(sb_t* s, uint32_t bo, uint32_t bi, int allow_ctr)
{
    if (!(bo & 4) && allow_ctr) sb_printf(s, "c->ctr--; ");
    int ctr_test = !(bo & 4) && allow_ctr;
    int cr_test = !(bo & 0x10);
    if (!ctr_test && !cr_test) return 1;
    sb_printf(s, "if (");
    if (ctr_test) sb_printf(s, "c->ctr %s 0", (bo & 2) ? "==" : "!=");
    if (ctr_test && cr_test) sb_printf(s, " && ");
    if (cr_test) sb_printf(s, "%sc->cr[%u]", (bo & 8) ? "" : "!", bi);
    sb_printf(s, ") ");
    return 0;
}

/* Arithmetic XO-form, op 31. Returns 0 if not handled. */
static int emit_xo_arith(sb_t* s, uint32_t a, uint32_t op, uint32_t xo9)
{
    unsigned d = RD, ra = RA, rb = RB;
    switch (xo9) {
    case 266: sb_printf(s, "c->gpr[%u] = c->gpr[%u] + c->gpr[%u];", d, ra, rb); break;
    case 40: sb_printf(s, "c->gpr[%u] = c->gpr[%u] - c->gpr[%u];", d, rb, ra); break;
    case 104: sb_printf(s, "c->gpr[%u] = 0 - c->gpr[%u];", d, ra); break;
    case 10: sb_printf(s, "c->gpr[%u] = add_ca(c->gpr[%u], c->gpr[%u], 0, &c->xer_ca);", d, ra, rb); break;
    case 8: sb_printf(s, "c->gpr[%u] = add_ca(~c->gpr[%u], c->gpr[%u], 1, &c->xer_ca);", d, ra, rb); break;
    case 138: sb_printf(s, "c->gpr[%u] = add_ca(c->gpr[%u], c->gpr[%u], c->xer_ca, &c->xer_ca);", d, ra, rb); break;
    case 136: sb_printf(s, "c->gpr[%u] = add_ca(~c->gpr[%u], c->gpr[%u], c->xer_ca, &c->xer_ca);", d, ra, rb); break;
    case 202: sb_printf(s, "c->gpr[%u] = add_ca(c->gpr[%u], 0, c->xer_ca, &c->xer_ca);", d, ra); break;
    case 200: sb_printf(s, "c->gpr[%u] = add_ca(~c->gpr[%u], 0, c->xer_ca, &c->xer_ca);", d, ra); break;
    case 234: sb_printf(s, "c->gpr[%u] = add_ca(c->gpr[%u], ~0ull, c->xer_ca, &c->xer_ca);", d, ra); break;
    case 232: sb_printf(s, "c->gpr[%u] = add_ca(~c->gpr[%u], ~0ull, c->xer_ca, &c->xer_ca);", d, ra); break;
    case 235: sb_printf(s, "c->gpr[%u] = (uint64_t)((int64_t)(int32_t)c->gpr[%u] * (int64_t)(int32_t)c->gpr[%u]);", d, ra, rb); break;
    case 233: sb_printf(s, "c->gpr[%u] = c->gpr[%u] * c->gpr[%u];", d, ra, rb); break;
    case 75: sb_printf(s, "c->gpr[%u] = (uint64_t)(((int64_t)(int32_t)c->gpr[%u] * (int64_t)(int32_t)c->gpr[%u]) >> 32);", d, ra, rb); break;
    case 11: sb_printf(s, "c->gpr[%u] = ((uint64_t)(uint32_t)c->gpr[%u] * (uint32_t)c->gpr[%u]) >> 32;", d, ra, rb); break;
    case 73: sb_printf(s, "c->gpr[%u] = (uint64_t)mulhs64((int64_t)c->gpr[%u], (int64_t)c->gpr[%u]);", d, ra, rb); break;
    case 9: sb_printf(s, "c->gpr[%u] = mulhu64(c->gpr[%u], c->gpr[%u]);", d, ra, rb); break;
    case 491: sb_printf(s, "{ int32_t x = (int32_t)c->gpr[%u], y = (int32_t)c->gpr[%u]; c->gpr[%u] = (y == 0 || (x == INT32_MIN && y == -1)) ? 0 : (uint64_t)(uint32_t)(x / y); }", ra, rb, d); break;
    case 459: sb_printf(s, "{ uint32_t x = (uint32_t)c->gpr[%u], y = (uint32_t)c->gpr[%u]; c->gpr[%u] = y == 0 ? 0 : x / y; }", ra, rb, d); break;
    case 489: sb_printf(s, "{ int64_t x = (int64_t)c->gpr[%u], y = (int64_t)c->gpr[%u]; c->gpr[%u] = (y == 0 || (x == INT64_MIN && y == -1)) ? 0 : (uint64_t)(x / y); }", ra, rb, d); break;
    case 457: sb_printf(s, "{ uint64_t x = c->gpr[%u], y = c->gpr[%u]; c->gpr[%u] = y == 0 ? 0 : x / y; }", ra, rb, d); break;
    default: return 0;
    }
    if (RCBIT) sb_printf(s, " cr0_s64(c, c->gpr[%u]);", d);
    (void)a;
    return 1;
}

static void emit_op31(sb_t* s, uint32_t a, uint32_t op)
{
    uint32_t xo10 = (op >> 1) & 0x3FF;
    unsigned d = RD, ra = RA, rb = RB;
    int rc = RCBIT;

    /* XO-form arithmetic: OE forms go to the interpreter (none occur in the boot set