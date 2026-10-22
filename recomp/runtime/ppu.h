/*
 * Guest CPU state and helpers shared by the recompiled code and the native runtime.
 * Semantics follow RPCS3's PPU interpreter (rpcs3/Emu/Cell/PPUInterpreter.cpp).
 *
 * Guest memory is a flat 4 GB reservation at g_base; guest addresses are 32-bit and big-endian.
 * Vector registers are kept in guest byte order (element 0 first) so lvx/stvx are plain copies.
 */
#ifndef XMB_PPU_H
#define XMB_PPU_H

#include <stdint.h>
#include <string.h>
#include <math.h>

#ifdef _MSC_VER
#include <intrin.h>
#include <stdlib.h>
#define BSWAP16 _byteswap_ushort
#define BSWAP32 _byteswap_ulong
#define BSWAP64 _byteswap_uint64
#else
#define BSWAP16 __builtin_bswap16
#define BSWAP32 __builtin_bswap32
#define BSWAP64 __builtin_bswap64
#endif

typedef struct ppu_t {
    uint64_t gpr[32];
    double fpr[32];
    uint8_t vr[32][16];
    uint8_t cr[32]; /* one byte per CR bit, ISA numbering: field f = bits 4f..4f+3 (LT GT EQ SO) */
    uint64_t lr, ctr;
    uint8_t xer_so, xer_ov, xer_ca, xer_cnt;
    uint32_t fpscr, vscr;
    uint32_t reserve_addr; /* lwarx/ldarx reservation */
    uint64_t reserve_value;
    int reserve_valid;
    uint32_t cia; /* address of the instruction that called into the runtime, for diagnostics */
} ppu_t;

typedef void (*ppu_fn)(ppu_t*);

extern uint8_t* g_base;

static inline uint8_t* gptr(uint64_t a) { return g_base + (uint32_t)a; }
static inline uint8_t ld8(uint64_t a) { return *gptr(a); }
static inline uint16_t ld16(uint64_t a) { uint16_t v; memcpy(&v, gptr(a), 2); return BSWAP16(v); }
static inline uint32_t ld32(uint64_t a) { uint32_t v; memcpy(&v, gptr(a), 4); return BSWAP32(v); }
static inline uint64_t ld64(uint64_t a) { uint64_t v; memcpy(&v, gptr(a), 8); return BSWAP64(v); }
static inline void st8(uint64_t a, uint8_t v) { *gptr(a) = v; }
static inline void st16(uint64_t a, uint16_t v) { v = BSWAP16(v); memcpy(gptr(a), &v, 2); }
static inline void st32(uint64_t a, uint32_t v) { v = BSWAP32(v); memcpy(gptr(a), &v, 4); }
static inline void st64(uint64_t a, uint64_t v) { v = BSWAP64(v); memcpy(gptr(a), &v, 8); }
static inline float ldf32(uint64_t a) { uint32_t u = ld32(a); float f; memcpy(&f, &u, 4); return f; }
static inline double ldf64(uint64_t a) { uint64_t u = ld64(a); double d; memcpy(&d, &u, 8); return d; }
static inline void stf32(uint64_t a, float f) { uint32_t u; memcpy(&u, &f, 4); st32(a, u); }
static inline void stf64(uint64_t a, double d) { uint64_t u; memcpy(&u, &d, 8); st64(a, u); }
static inline uint64_t dbits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static inline double bitsd(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }

static inline uint32_t rotl32(uint32_t x, unsigned n) { n &= 31; return n ? (x << n) | (x >> (32 - n)) : x; }
static inline uint64_t rotl64(uint64_t x, unsigned n) { n &= 63; return n ? (x << n) | (x >> (64 - n)) : x; }
static inline uint64_t dup32(uint32_t x) { return (uint64_t)x << 32 | x; }

/* Mask with ISA bits mb..me set (bit 0 = MSB), wrapping when mb > me. */
static inline uint64_t rotmask(unsigned mb, unsigned me)
{
    uint64_t a = ~0ull >> (mb & 63);
    uint64_t b = ~0ull << (63 - (me & 63));
    return (mb <= me) ? (a & b) : (a | b);
}

static inline unsigned clz32(uint32_t x)
{
#ifdef _MSC_VER
    unsigned long i;
    return _BitScanReverse(&i, x) ? 31 - i : 32;
#else
    return x ? __builtin_clz(x) : 32;
#endif
}

static inline unsigned clz64(uint64_t x)
{
#ifdef _MSC_VER
    unsigned long i;
    return _BitScanReverse64(&i, x) ? 63 - i : 64;
#else
    return x ? __builtin_clzll(x) : 64;
#endif
}

static inline uint64_t mulhu64(uint64_t a, uint64_t b)
{
#ifdef _MSC_VER
    return __umulh(a, b);
#else
    return (uint64_t)(((unsigned __int128)a * b) >> 64);
#endif
}

static inline int64_t mulhs64(int64_t a, int64_t b)
{
#ifdef _MSC_VER
    return __mulh(a, b);
#else
    return (int64_t)(((__int128)a * b) >> 64);
#endif
}

/* CR helpers */
static inline void cr_set(ppu_t* c, unsigned f, int lt, int gt, int eq)
{
    c->cr[4 * f + 0] = (uint8_t)lt;
    c->cr[4 * f + 1] = (uint8_t)gt;
    c->cr[4 * f + 2] = (uint8_t)eq;
    c->cr[4 * f + 3] = c->xer_so;
}
static inline void cr0_s64(ppu_t* c, uint64_t v) { int64_t s = (int64_t)v; cr_set(c, 0, s < 0, s > 0, s == 0); }

static inline uint32_t cr_get(const ppu_t* c)
{
    uint32_t v = 0;
    for (int i = 0; i < 32; i++) v |= (uint32_t)c->cr[i] << (31 - i);
    return v;
}
static inline void cr_put_field(ppu_t* c, unsigned f, uint32_t word)
{
    for (int b = 0; b < 4; b++) c->cr[4 * f + b] = (word >> (31 - (4 * f + b))) & 1;
}

/* Overflow (OE=1) bookkeeping */
static inline void ov_set(ppu_t* c, int ov) { c->xer_ov = (uint8_t)ov; c->xer_so |= (uint8_t)ov; }

/* add with carry out: returns sum, writes carry */
static inline uint64_t add_ca(uint64_t a, uint64_t b, unsigned cin, uint8_t* ca)
{
    uint64_t s = a + b;
    unsigned c1 = s < a;
    uint64_t r = s + cin;
    unsigned c2 = r < s;
    *ca = (uint8_t)(c1 | c2);
    return r;
}

/* float -> int conversions with PPU saturation */
static inline int64_t cvt_f64_s32_trunc(double b)
{
    if (b != b) return INT32_MIN;
    if (b >= 2147483648.0) return INT32_MAX;
    if (b < -2147483648.0) return INT32_MIN;
    return (int32_t)b;
}
static inline int64_t cvt_f64_s64_trunc(double b)
{
    if (b != b) return INT64_MIN;
    if (b >= 9223372036854775808.0) return INT64_MAX;
    if (b < -9223372036854775808.0) return INT64_MIN;
    return (int64_t)b;
}
static inline int64_t cvt_f64_s32_round(double b) { return cvt_f64_s32_trunc(nearbyint(b)); }
static inline int64_t cvt_f64_s64_round(double b) { return cvt_f64_s64_trunc(nearbyint(b)); }

static inline void fcmp(ppu_t* c, unsigned f, double a, double b)
{
    int un = (a != a) || (b != b);
    c->cr[4 * f + 0] = (uint8_t)(!un && a < b);
    c->cr[4 * f + 1] = (uint8_t)(!un && a > b);
    c->cr[4 * f + 2] = (uint8_t)(!un && a == b);
    c->cr[4 * f + 3] = (uint8_t)un;
    c->fpscr = (c->fpscr & ~0xF000u) | ((uint32_t)c->cr[4 * f] << 15) | ((uint32_t)c->cr[4 * f + 1] << 14) | ((uint32_t)c->cr[4 * f + 2] << 13) | ((uint32_t)un << 12);
}

static inline double round_single(double d) { return (double)(float)d; }

/* Runtime entry points (runtime.c) */
void ppu_call(ppu_t* c, uint64_t code_addr);           /* indirect call/jump to guest code */
void ppu_syscall(ppu_t* c);                            /* sc: LV2 syscall number in r11 */
void ppu_interp(ppu_t* c, uint32_t cia, uint32_t op);  /* rare instructions, executed one at a time */
void ppu_trap(ppu_t* c, uint32_t cia);
uint64_t ppu_mfspr(ppu_t* c, uint32_t spr);
void ppu_mtspr(ppu_t* c, uint32_t spr, uint64_t v);
uint64_t ppu_timebase(void);
int ppu_stwcx(ppu_t* c, uint64_t ea, uint32_t v);
int ppu_stdcx(ppu_t* c, uint64_t ea, uint64_t v);
uint32_t ppu_lwarx(ppu_t* c, uint64_t ea);
uint64_t ppu_ldarx(ppu_t* c, uint64_t ea);

#endif
