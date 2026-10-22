/* Big-endian ELF64 reading for PS3 PPU executables (ET_EXEC) and PRX modules (ET 0xFFA4). */
#ifndef XMB_ELF_BE_H
#define XMB_ELF_BE_H

#include <stdint.h>
#include <stddef.h>

static inline uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline uint64_t be64(const uint8_t* p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }
static inline void put_be16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void put_be32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static inline void put_be64(uint8_t* p, uint64_t v) { put_be32(p, (uint32_t)(v >> 32)); put_be32(p + 4, (uint32_t)v); }

#define ELF_ET_EXEC 2
#define ELF_ET_SCE_PPURELA 0xFFA4
#define ELF_EM_PPC64 0x15
#define PT_LOAD 1
#define PT_SCE_PPURELA 0x700000A4u

typedef struct {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz;
} elf_phdr;

typedef struct {
    const char* path;
    uint8_t* data;
    size_t size;
    uint16_t type, machine;
    int phnum;
    elf_phdr ph[32];
} elf_file;

/* Returns 0 on success; fills err on failure. */
int elf_load(elf_file* f, const char* path, char* err, size_t errsz);
void elf_free(elf_file* f);

#endif
