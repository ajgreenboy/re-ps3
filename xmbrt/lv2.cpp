// LV2 syscall dispatch. Implemented syscalls live in k_*.cpp and register here; the rest are logged
// and fail with CELL_ENOSYS, so the first missing one is obvious in the log.
#include "kernel.h"

#include <iterator>

#include "syscall_names.inc"

namespace
{
    SyscallFn g_table[1024];
}

static const char* syscall_name(uint64_t number)
{
    if (number < std::size(kSyscallNames) && kSyscallNames[number])
        return kSyscallNames[number];
    thread_local char unnamed[32];
    snprintf(unnamed, sizeof(unnamed), "syscall_%llu", (unsigned long long)number); // RPCS3's name for these
    return unnamed;
}

void register_syscall(uint32_t number, SyscallFn fn)
{
    g_table[number] = fn;
}

void register_config_syscalls();
void register_rsx_syscalls();
void register_uart_syscalls();
void register_timer_syscalls();
void register_rsxaudio_syscalls();
void register_extra_syscalls();

void lv2_init()
{
    register_thread_syscalls();
    register_sync_syscalls();
    register_event_syscalls();
    register_memory_syscalls();
    register_fs_syscalls();
    register_prx_syscalls();
    register_misc_syscalls();
    register_config_syscalls();
    register_rsx_syscalls();
    register_uart_syscalls(); // after misc: replaces its placeholders
    register_timer_syscalls();
    register_rsxaudio_syscalls(); // after misc: replaces its OK stubs
    register_extra_syscalls(); // after misc: real hid/sm handlers replace its OK stubs
    pad_init();
    size_t n = 0;
    for (auto fn : g_table) n += fn != nullptr;
    log_line("lv2: %zu syscalls implemented", n);
}

void lv2_syscall(PPCContext& ctx, uint64_t number)
{
    const uint64_t seq = next_call_seq();
    // sc doesn't pass its own address; lr is the return address of the enclosing syscall wrapper.
    const uint32_t lr = uint32_t(ctx.lr);
    const uint64_t r3 = ctx.r3.u64, r4 = ctx.r4.u64, r5 = ctx.r5.u64, r6 = ctx.r6.u64;
    const SyscallFn fn = number < 1024 ? g_table[number] : nullptr;
    const bool null_ok = !fn && number < std::size(kSyscallNullOk) && kSyscallNullOk[number];
    const int64_t result = fn ? fn(ctx) : null_ok ? CELL_OK : CELL_ENOSYS;
    ctx.r3.s64 = result;
    log_line("%llu T%08X sys %llu %s lr=%08X r3=%llX r4=%llX r5=%llX r6=%llX -> %s%llX",
        (unsigned long long)seq, current_thread_id(), (unsigned long long)number, syscall_name(number), lr,
        (unsigned long long)r3, (unsigned long long)r4, (unsigned long long)r5, (unsigned long long)r6,
        fn ? "" : null_ok ? "null_func " : "ENOSYS ", (unsigned long long)uint64_t(result));
}

void lv2_trap(PPCContext& ctx, uint64_t address)
{
    log_line("trap at %08X lr=%08X r3=%llX", uint32_t(address), uint32_t(ctx.lr), (unsigned long long)ctx.r3.u64);
    fatal_exit(3);
}
