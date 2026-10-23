// LV2 syscall dispatch. Phase 1: every syscall is logged and fails with CELL_ENOSYS.
#include "xmbrt.h"

#include <iterator>

#include "syscall_names.inc"

static const char* syscall_name(uint64_t number)
{
    if (number < std::size(kSyscallNames) && kSyscallNames[number])
        return kSyscallNames[number];
    return "?";
}

void lv2_syscall(PPCContext& ctx, uint64_t number)
{
    const uint64_t seq = next_call_seq();
    // sc doesn't pass its own address; lr is the return address of the enclosing syscall wrapper.
    log_line("%llu T%08X sys %llu %s lr=%08X r3=%llX r4=%llX r5=%llX r6=%llX -> ENOSYS",
        (unsigned long long)seq, current_thread_id(), (unsigned long long)number, syscall_name(number),
        uint32_t(ctx.lr), (unsigned long long)ctx.r3.u64, (unsigned long long)ctx.r4.u64,
        (unsigned long long)ctx.r5.u64, (unsigned long long)ctx.r6.u64);
    ctx.r3.s64 = CELL_ENOSYS;
}

void lv2_trap(PPCContext& ctx, uint64_t address)
{
    log_line("trap at %08X lr=%08X r3=%llX", uint32_t(address), uint32_t(ctx.lr), (unsigned long long)ctx.r3.u64);
    fatal_exit(3);
}
