// xmbrt: native runtime for the XenonRecomp-translated PS3 XMB (vsh.elf + 13 boot plugins, fw 4.93).
// Built against the generated ppc_context.h so the PPCContext layout always matches the objects.
#pragma once

#include "ppc_config.h"
#include "ppc_context.h"

#include <cstdint>
#include <cstdio>

// Guest address map. The image span comes from xmbld; the rest follows RPCS3's vm areas
// so addresses in xmbrt logs line up with RPCS3 reference traces.
constexpr uint32_t kImageEnd = uint32_t(PPC_IMAGE_BASE + PPC_IMAGE_SIZE);
// XenonRecomp's indirect-call table: one 8-byte host pointer per 4-byte guest word from
// PPC_CODE_BASE. It must reach the HLE stubs, which sit past PPC_CODE_BASE + PPC_CODE_SIZE.
constexpr uint64_t kFuncTableBytes = (uint64_t(kImageEnd) - PPC_CODE_BASE) * 2;
constexpr uint32_t kUserMemoryBase = 0x30000000; // sys_memory_* / sys_mmapper_* (RPCS3 vm::user64k)
constexpr uint32_t kStackBase = 0xD0000000;      // PPU thread stacks (RPCS3 vm::stack)
constexpr uint32_t kMainThreadId = 0x01000000;   // RPCS3's id for main_thread

constexpr int32_t CELL_ENOSYS = int32_t(0x80010003);

extern uint8_t* g_base;

// Guest memory is big-endian; every runtime access goes through these.
inline uint32_t guest_load_u32(uint32_t addr) { return __builtin_bswap32(*(uint32_t*)(g_base + addr)); }
inline uint64_t guest_load_u64(uint32_t addr) { return __builtin_bswap64(*(uint64_t*)(g_base + addr)); }
inline void guest_store_u32(uint32_t addr, uint32_t v) { *(uint32_t*)(g_base + addr) = __builtin_bswap32(v); }
inline void guest_store_u64(uint32_t addr, uint64_t v) { *(uint64_t*)(g_base + addr) = __builtin_bswap64(v); }

inline PPCFunc*& func_slot(uint32_t guest_addr)
{
    return *(PPCFunc**)(g_base + kImageEnd + (uint64_t(guest_addr) - PPC_CODE_BASE) * 2);
}

// Call log: one line per syscall / HLE call, in order, for diffing against RPCS3 traces.
void log_line(const char* fmt, ...);
// Counts a call and stops the process once XMBRT_MAX_CALLS (default 10000) is reached.
uint64_t next_call_seq();
uint32_t current_thread_id();
[[noreturn]] void fatal_exit(int code);

// lv2.cpp
void lv2_syscall(PPCContext& ctx, uint64_t number);
void lv2_trap(PPCContext& ctx, uint64_t address);

// hle.cpp
bool hle_load(const char* hle_csv);
void hle_install();
