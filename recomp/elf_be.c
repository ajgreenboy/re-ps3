#include "elf_be.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int elf_load(elf_file* f, const char* path, char* err, size_t errsz)
{
    memset(f, 0, sizeof(*f));
    f->path = path;
    FILE* fp = fopen(path, "rb");
    if (!fp) { snprintf(err, errsz, "cannot open %s", path); return -1; }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    f->data = (uint8_t*)malloc((size_t)sz);
    f->size = (size_t)sz;
    if (!f->data || fread(f->data, 1, f->size, fp) != f->size) { fclose(fp); snprintf(err, errsz, "cannot read %s", path); return -1; }
    fclose(fp);

    const uint8_t* h = f->data;
    if (f->size < 0x40 || memcmp(h, "\x7f" "ELF", 4) != 0 || h[4] != 2 || h[5] != 2) {
        snprintf(err, errsz, "%s: not a big-endian ELF64", path);
        return -1;
    }
    f->type = be16(h + 0x10);
    f->machine = be16(h + 0x12);
    if (f->machine != ELF_EM_PPC64) { snprintf(err, errsz, "%s: e_machine 0x%x is not PPC64", path, f->machine); return -1; }
    uint64_t phoff = be64(h + 0x20);
    uint16_t phentsize = be16(h + 0x36);
    f->phnum = be16(h + 0x38);
    if (f->phnum > 32 || phoff + (uint64_t)f->phnum * phentsize > f->size) { snprintf(err, errsz, "%s: bad program headers", path); return -1; }
    for (int i = 0; i < f->phnum; i++) {
        const uint8_t* p = h + phoff + (uint64_t)i * phentsize;
        elf_phdr* ph = &f->ph[i];
        ph->type = be32(p);
        ph->flags = be32(p + 4);
        ph->offset = be64(p + 8);
        ph->vaddr = be64(p + 16);
        ph->paddr = be64(p + 24);
        ph->filesz = be64(p + 32);
        ph->memsz = be64(p + 40);
        if (ph->filesz && ph->offset + ph->filesz > f->size) { snprintf(err, errsz, "%s: segment %d past EOF", path, i); return -1; }
    }
    return 0;
}

void elf_free(elf_file* f)
{
    free(f->data);
    f->data = NULL;
}
