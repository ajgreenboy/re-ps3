// LV2 syscall dispatch. Implemented syscalls live in k_*.cpp and register here; the rest are logged
// and fail with CELL_ENOSYS, so the first missing one is obvious in the log.
#include "kernel.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iterator>
#include <map>
#include <mutex>
#include <tuple>
#include <vector>

#include "syscall_names.inc"

namespace
{
    SyscallFn g_table[1024];
    std::atomic_bool g_reported_unsupported[1024]{};
    std::atomic_bool g_reported_out_of_range{};

    // XMBRT_SYSCALL_TIMING=1: wall time the main thread spends inside each syscall call site, reported
    // every 10 s, to find what paces frame production. =all times every guest thread (top sites overall).
    struct SiteTime { double ms = 0; uint64_t calls = 0; };
    struct MainTiming
    {
        bool on = getenv("XMBRT_SYSCALL_TIMING") != nullptr;
        bool all = [] { const char* v = getenv("XMBRT_SYSCALL_TIMING"); return v && !strcmp(v, "all"); }();
        std::mutex m;
        std::map<std::tuple<uint32_t, uint64_t, uint32_t>, SiteTime> thread_sites; // (thread, syscall, lr)
        std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
        double in_syscalls_ms = 0;
        std::map<std::pair<uint64_t, uint32_t>, SiteTime> sites;
    } g_main_timing;
}

static const char* syscall_name(uint64_t number);

static void report_main_timing()
{
    auto& m = g_main_timing;
    const double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m.since).count();
    if (wall < 10000) return;
    std::vector<std::pair<std::pair<uint64_t, uint32_t>, SiteTime>> top(m.sites.begin(), m.sites.end());
    std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.second.ms > b.second.ms; });
    log_line("main timing: %.0f ms wall, %.0f ms in syscalls (%.0f%%)", wall, m.in_syscalls_ms, 100 * m.in_syscalls_ms / wall);
    for (size_t i = 0; i < top.size() && i < 8; i++)
        log_line("main timing:   %8.0f ms %7llu calls  sys %llu %s lr=%08X", top[i].second.ms, (unsigned long long)top[i].second.calls,
            (unsigned long long)top[i].first.first, syscall_name(top[i].first.first), top[i].first.second);
    m.sites.clear();
    if (m.all)
    {
        std::lock_guard lock(m.m);
        std::vector<std::pair<std::tuple<uint32_t, uint64_t, uint32_t>, SiteTime>> t(m.thread_sites.begin(), m.thread_sites.end());
        std::sort(t.begin(), t.end(), [](const auto& a, const auto& b) { return a.second.ms > b.second.ms; });
        for (size_t i = 0; i < t.size() && i < 16; i++)
            log_line("thread timing: T%08X %8.0f ms %7llu calls  sys %llu %s lr=%08X", std::get<0>(t[i].first), t[i].second.ms,
                (unsigned long long)t[i].second.calls, (unsigned long long)std::get<1>(t[i].first), syscall_name(std::get<1>(t[i].first)), std::get<2>(t[i].first));
        m.thread_sites.clear();
    }
    m.in_syscalls_ms = 0;
    m.since = std::chrono::steady_clock::now();
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
void register_spu_syscalls();
void register_extra_syscalls();
void register_net_syscalls();

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
    register_spu_syscalls();
    register_extra_syscalls(); // after misc: real hid/sm handlers replace its OK stubs
    register_security_syscalls();
    register_net_syscalls();
    pad_init();
    size_t n = 0;
    for (auto fn : g_table) n += fn != nullptr;
    log_line("lv2: %zu syscalls registered (includes compatibility handlers)", n);
}

void lv2_syscall(PPCContext& ctx, uint64_t number)
{
    const uint64_t seq = next_call_seq();
    // sc doesn't pass its own address; lr is the return address of the enclosing syscall wrapper.
    const uint32_t lr = uint32_t(ctx.lr);
    const uint64_t r3 = ctx.r3.u64, r4 = ctx.r4.u64, r5 = ctx.r5.u64, r6 = ctx.r6.u64;
    const SyscallFn fn = number < 1024 ? g_table[number] : nullptr;
    const bool null_ok = !fn && number < std::size(kSyscallNullOk) && kSyscallNullOk[number];
    const bool main_thread = current_thread_id() == kMainThreadId;
    const bool timed = g_main_timing.on && (main_thread || g_main_timing.all);
    const auto t0 = timed ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const int64_t result = fn ? fn(ctx) : null_ok ? CELL_OK : CELL_ENOSYS;
    if (timed && !main_thread)
    {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::lock_guard lock(g_main_timing.m);
        auto& site = g_main_timing.thread_sites[{ current_thread_id(), number, lr }];
        site.ms += ms;
        site.calls++;
    }
    if (timed && main_thread)
    {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        auto& site = g_main_timing.sites[{ number, lr }];
        site.ms += ms;
        site.calls++;
        g_main_timing.in_syscalls_ms += ms;
        report_main_timing();
    }
    ctx.r3.s64 = result;
    if (result == CELL_ENOSYS)
    {
        auto& reported = number < 1024 ? g_reported_unsupported[number] : g_reported_out_of_range;
        if (!reported.exchange(true))
            log_line("unsupported service: sys %llu %s lr=%08X r3=%llX r4=%llX r5=%llX r6=%llX (%s)",
                (unsigned long long)number, syscall_name(number), lr,
                (unsigned long long)r3, (unsigned long long)r4, (unsigned long long)r5, (unsigned long long)r6,
                fn ? "registered handler returned ENOSYS" : "no registered handler");
    }
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
