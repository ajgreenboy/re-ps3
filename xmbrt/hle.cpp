// HLE imports: the functions xmbld bound to synthetic descriptors instead of a module in the image.
// Each stub's code address gets its own host thunk in the indirect-call table, which routes to
// one dispatcher with the stub index. Libraries with a handler get it; the rest are logged and
// fail with CELL_ENOSYS.
#include "xmbrt.h"
#include "k_event.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <thread>

namespace
{
    constexpr size_t kMaxHle = 4096; // xmbld HLE_MAX
    std::atomic_uint8_t g_reported_binding[kMaxHle]{};

    struct HleImport
    {
        std::string library;
        uint32_t nid;
        uint32_t opd;
        uint32_t code;
    };

    std::vector<HleImport> g_imports;
    std::unordered_map<uint32_t, uint32_t> g_spurs_queues;
    std::unordered_map<uint32_t, uint32_t> g_job_chain_queues;
    std::unordered_map<uint32_t, uint32_t> g_job_chain_entries;
    unsigned g_job_reports = 0;
    void capture_job_descriptor(uint32_t address)
    {
        char name[64];
        snprintf(name, sizeof(name), "spurs-job-%08X.bin", address);
        if (FILE* file = fopen(name, "wb")) { fwrite(g_base + address, 1, 256, file); fclose(file); }
        const uint64_t binary = guest_load_u64(address);
        if (binary < 0x10000 || binary >= 0x40000000) return;
        log_line("spurs job binary: descriptor=%08X binary=%08X words=%08X %08X %08X %08X", address, uint32_t(binary),
            guest_load_u32(uint32_t(binary)), guest_load_u32(uint32_t(binary) + 4),
            guest_load_u32(uint32_t(binary) + 8), guest_load_u32(uint32_t(binary) + 12));
        snprintf(name, sizeof(name), "spurs-bin-%08X.bin", uint32_t(binary));
        if (FILE* file = fopen(name, "wb")) { fwrite(g_base + uint32_t(binary), 1, 4096, file); fclose(file); }
    }
    struct JobGuard
    {
        uint32_t chain;
        uint32_t count;
        uint32_t remaining;
        bool auto_reset;
    };
    std::unordered_map<uint32_t, JobGuard> g_job_guards;

    using HleFn = int64_t (*)(PPCContext& ctx);

    int64_t hle_ok(PPCContext&) { return 0; }

    // cellSpursJobChainGetError(jobChain, cause): successful job chains report
    // a null cause even when the library is handled by the boot shim.
    int64_t cell_spurs_job_chain_get_error(PPCContext& ctx)
    {
        if (ctx.r4.u32) guest_store_u32(ctx.r4.u32, 0);
        return 0;
    }

    int64_t cell_spurs_attach_queue(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        if (find_event_queue(ctx.r4.u32)) g_spurs_queues[ctx.r3.u32] = ctx.r4.u32;
        return 0;
    }

    int64_t cell_spurs_create_job_chain(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        if (const auto it = g_spurs_queues.find(ctx.r3.u32); it != g_spurs_queues.end()) g_job_chain_queues[ctx.r4.u32] = it->second;
        const uint32_t chain = ctx.r4.u32, attr = ctx.r5.u32;
        if (!chain || !attr) return int32_t(0x80410a11);
        const uint32_t entry = guest_load_u32(attr + 8);
        g_job_chain_entries[chain] = entry;
        memset(g_base + chain, 0, 256);
        guest_store_u64(chain, entry);
        guest_store_u16(chain + 0x72, guest_load_u16(attr + 12));
        guest_store_u64(chain + 0x78, ctx.r3.u32);
        log_line("spurs: job chain=%08X entry=%08X descriptor size=%u", chain, entry, guest_load_u16(attr + 12));
        return 0;
    }
    int64_t cell_spurs_job_chain_attribute_initialize(PPCContext& ctx)
    {
        const uint32_t attr = ctx.r5.u32, stack = ctx.r1.u32;
        if (!attr || !ctx.r6.u32 || !ctx.r9.u32) return int32_t(0x80410a11);
        memset(g_base + attr, 0, 64);
        guest_store_u32(attr, ctx.r3.u32);
        guest_store_u32(attr + 4, ctx.r4.u32);
        guest_store_u32(attr + 8, ctx.r6.u32);
        guest_store_u16(attr + 12, uint16_t(ctx.r7.u32));
        guest_store_u16(attr + 14, uint16_t(ctx.r8.u32));
        memcpy(g_base + attr + 16, g_base + ctx.r9.u32, 8);
        guest_store_u32(attr + 24, ctx.r10.u32);
        guest_store_u8(attr + 28, uint8_t(guest_load_u64(stack + 0x70)));
        guest_store_u32(attr + 32, uint32_t(guest_load_u64(stack + 0x78)));
        guest_store_u32(attr + 36, uint32_t(guest_load_u64(stack + 0x80)));
        guest_store_u8(attr + 40, uint8_t(guest_load_u64(stack + 0x88)));
        guest_store_u32(attr + 44, uint32_t(guest_load_u64(stack + 0x90)));
        guest_store_u32(attr + 48, uint32_t(guest_load_u64(stack + 0x98)));
        return 0;
    }

    int64_t cell_spurs_run_job_chain(PPCContext& ctx)
    {
        // Starting a chain does not complete any work. RAF submits work later
        // through its guards; the boot shim completes each notified batch there.
        return 0;
    }

    int64_t cell_spurs_job_guard_initialize(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        const uint32_t count = ctx.r5.u32;
        g_job_guards[ctx.r4.u32] = { ctx.r3.u32, count, count, ctx.r7.u32 != 0 };
        memset(g_base + ctx.r4.u32, 0, 128);
        guest_store_u32(ctx.r4.u32, count);
        guest_store_u32(ctx.r4.u32 + 4, count);
        guest_store_u32(ctx.r4.u32 + 12, ctx.r3.u32);
        guest_store_u32(ctx.r4.u32 + 16, ctx.r6.u32);
        guest_store_u32(ctx.r4.u32 + 32, ctx.r7.u32 != 0);
        return 0;
    }

    int64_t cell_spurs_job_guard_notify(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        const auto guard_it = g_job_guards.find(ctx.r3.u32);
        if (guard_it == g_job_guards.end()) return int32_t(0x80410A02); // JOB_ERROR_INVAL
        JobGuard& guard = guard_it->second;
        if (!guard.remaining) return int32_t(0x80410A0A); // JOB_ERROR_BUSY
        if (--guard.remaining) return 0;
        if (getenv("XMBRT_SPU_JOB_TRACE") && g_job_reports++ < 2)
        {
            const auto entry = g_job_chain_entries.find(guard.chain);
            if (entry != g_job_chain_entries.end())
                for (unsigned i = 0; i < 32; ++i)
                {
                    const uint64_t command = guest_load_u64(entry->second + i * 8);
                    log_line("spurs job command: guard=%08X pc=%08X value=%016llX", ctx.r3.u32, entry->second + i * 8,
                        (unsigned long long)command);
                    const uint32_t address = uint32_t(command) & ~15u, opcode = uint32_t(command) & 15;
                    if (command >> 32 || address < 0x10000 || address >= 0x40000000 || (opcode != 0 && opcode != 6)) continue;
                    log_line("spurs job payload: addr=%08X %016llX %016llX %016llX %016llX", address,
                        (unsigned long long)guest_load_u64(address), (unsigned long long)guest_load_u64(address + 8),
                        (unsigned long long)guest_load_u64(address + 16), (unsigned long long)guest_load_u64(address + 24));
                    if (opcode == 0) capture_job_descriptor(address);
                    else
                    {
                        const uint32_t jobs = guest_load_u32(address), size = guest_load_u32(address + 4);
                        const uint64_t list = guest_load_u64(address + 8);
                        if (jobs <= 4 && size == 256 && list >= 0x10000 && list < 0x40000000)
                            for (unsigned job = 0; job < jobs; ++job) capture_job_descriptor(uint32_t(list) + job * size);
                    }
                }
        }
        const auto chain_it = g_job_chain_queues.find(guard.chain);
        if (chain_it == g_job_chain_queues.end()) return int32_t(0x80410A02);
        const auto q = find_event_queue(chain_it->second);
        if (!q) return CELL_ESRCH;
        // Synthetic batch completion only: the SPU jobs themselves are not yet
        // executed by this shim. RAF consumes one queue entry per submitted guard.
        if (!event_queue_send(*q, 0xFFFFFFFF53505552ull, guard.chain, ctx.r3.u64, 0))
        {
            guard.remaining = 1;
            return CELL_EBUSY;
        }
        if (guard.auto_reset) guard.remaining = guard.count;
        return 0;
    }

    int64_t cell_spurs_job_guard_reset(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        const auto it = g_job_guards.find(ctx.r3.u32);
        if (it == g_job_guards.end()) return int32_t(0x80410A02);
        it->second.remaining = it->second.count;
        return 0;
    }

    int64_t cell_spurs_create_task(PPCContext& ctx)
    {
        // Capture is optional; execution uses the same guest ELF directly.
        const uint32_t elf = ctx.r5.u32;
        log_line("spurs: create task taskset=%08X elf=%08X context=%08X size=%08X args=%08X",
            ctx.r3.u32, elf, ctx.r6.u32, ctx.r7.u32, ctx.r9.u32);
        if (ctx.r9.u32)
            log_line("spurs: task argument=%016llX %016llX",
                (unsigned long long)guest_load_u64(ctx.r9.u32), (unsigned long long)guest_load_u64(ctx.r9.u32 + 8));
        if (getenv("XMBRT_CAPTURE_SPU_TASKS") && elf && guest_load_u32(elf) == 0x7F454C46)
        {
            const uint32_t phoff = guest_load_u32(elf + 28);
            const uint16_t phsize = guest_load_u16(elf + 42), count = guest_load_u16(elf + 44);
            uint64_t size = uint64_t(phoff) + uint64_t(phsize) * count;
            if (phsize >= 32 && count <= 64 && size < 0x100000)
            {
                for (uint32_t i = 0; i < count; ++i)
                {
                    const uint32_t ph = elf + phoff + i * phsize;
                    const uint64_t end = uint64_t(guest_load_u32(ph + 4)) + guest_load_u32(ph + 16);
                    if (end > size) size = end;
                }
                if (size <= 0x1000000 && uint64_t(elf) + size <= 0x100000000ull)
                {
                    char name[64];
                    snprintf(name, sizeof(name), "spurs-task-%08X.elf", elf);
                    if (FILE* file = fopen(name, "wb"))
                    {
                        const size_t written = fwrite(g_base + elf, 1, size_t(size), file);
                        fclose(file);
                        log_line("spurs: captured %zu bytes to %s", written, name);
                    }
                }
            }
        }
        return spurs_create_task(ctx);
    }
    int64_t cell_spurs_create_taskset(PPCContext& ctx)
    {
        if (!ctx.r3.u32 || !ctx.r4.u32) return int32_t(0x80410902);
        guest_store_u64(ctx.r4.u32 + 0x60, ctx.r3.u32);
        guest_store_u64(ctx.r4.u32 + 0x68, ctx.r5.u64);
        return 0;
    }
    int64_t cell_spurs_create_taskset_attribute(PPCContext& ctx)
    {
        const uint64_t args = ctx.r5.u32 ? guest_load_u64(ctx.r5.u32 + 8) : 0;
        ctx.r5.u64 = args;
        return cell_spurs_create_taskset(ctx);
    }
    int64_t cell_spurs_event_flag_initialize(PPCContext& ctx)
    {
        const uint32_t flag = ctx.r5.u32;
        if (!flag || (!ctx.r3.u32 && !ctx.r4.u32)) return int32_t(0x80410911);
        if ((flag | ctx.r3.u32 | ctx.r4.u32) & 127) return int32_t(0x80410910);
        if (ctx.r6.u32 > 1 || ctx.r7.u32 > 3) return int32_t(0x80410902);
        KLock lock(g_kernel);
        memset(g_base + flag, 0, 128);
        guest_store_u8(flag + 12, 0xff);
        guest_store_u8(flag + 13, ctx.r4.u32 ? 0 : 1);
        guest_store_u8(flag + 14, ctx.r7.u32);
        guest_store_u8(flag + 15, ctx.r6.u32);
        guest_store_u64(flag + 0x70, ctx.r4.u32 ? ctx.r4.u32 : ctx.r3.u32);
        return 0;
    }
    int64_t cell_spurs_event_flag_attach(PPCContext& ctx)
    {
        const uint32_t flag = ctx.r3.u32;
        if (!flag) return int32_t(0x80410911);
        KLock lock(g_kernel);
        std::shared_ptr<KEventQueue> q;
        const uint32_t queue = create_event_queue_internal(16, &q);
        const uint32_t taskset = uint32_t(guest_load_u64(flag + 0x70));
        // XMB has one attached SPU-to-PPU flag per taskset.
        spurs_connect_task_event_queue(taskset, 0, queue);
        guest_store_u8(flag + 12, 0);
        guest_store_u32(flag + 0x78, create_local_port_internal(q));
        guest_store_u32(flag + 0x7c, queue);
        return 0;
    }
    int64_t cell_spurs_event_flag_clear(PPCContext& ctx)
    {
        if (!ctx.r3.u32) return int32_t(0x80410911);
        KLock lock(g_kernel);
        guest_store_u16(ctx.r3.u32, guest_load_u16(ctx.r3.u32) & ~ctx.r4.u32);
        return 0;
    }
    int64_t cell_spurs_event_flag_set(PPCContext& ctx)
    {
        const uint32_t flag = ctx.r3.u32;
        if (!flag) return int32_t(0x80410911);
        std::vector<uint32_t> tasks;
        uint32_t taskset;
        {
            KLock lock(g_kernel);
            const uint16_t events = guest_load_u16(flag) | uint16_t(ctx.r4.u32);
            const uint16_t used = guest_load_u16(flag + 8), modes = guest_load_u16(flag + 10);
            uint16_t pending = guest_load_u16(flag + 2), consumed = 0;
            for (unsigned i = 0; i < 16; ++i)
            {
                const uint16_t slot = uint16_t(0x8000u >> i);
                if (!(used & slot) || (pending & slot)) continue;
                const uint16_t mask = guest_load_u16(flag + 0x10 + i * 2), relevant = events & mask;
                if ((modes & slot) ? relevant != mask : relevant == 0) continue;
                pending |= slot;
                consumed |= relevant;
                guest_store_u16(flag + 0x30 + i * 2, relevant);
                tasks.push_back(guest_load_u8(flag + 0x50 + i));
            }
            guest_store_u16(flag + 2, pending);
            guest_store_u16(flag, guest_load_u8(flag + 15) == 0 ? events & ~consumed : events);
            taskset = uint32_t(guest_load_u64(flag + 0x70));
        }
        for (uint32_t task : tasks)
        {
            PPCContext signal{};
            signal.r3.u32 = taskset;
            signal.r4.u32 = task;
            const int64_t result = spurs_send_signal(signal);
            if (result) return result;
        }
        return 0;
    }
    int64_t cell_spurs_event_flag_wait(PPCContext& ctx, bool block)
    {
        const uint32_t flag = ctx.r3.u32, out = ctx.r4.u32;
        if (!flag || !out) return int32_t(0x80410911);
        if (ctx.r5.u32 > 1) return int32_t(0x80410902);
        const uint16_t mask = guest_load_u16(out);
        bool registered = false;
        for (;;)
        {
            {
                KLock lock(g_kernel);
                const uint16_t events = guest_load_u16(flag), relevant = events & mask;
                if (guest_load_u8(flag + 7))
                {
                    const uint32_t slot = guest_load_u8(flag + 6) >> 4;
                    guest_store_u16(out, guest_load_u16(flag + 0x30 + slot * 2));
                    guest_store_u8(flag + 7, 0);
                    return 0;
                }
                if (ctx.r5.u32 ? relevant == mask : relevant != 0)
                {
                    guest_store_u16(out, relevant);
                    if (!guest_load_u8(flag + 15)) guest_store_u16(flag, events & ~relevant);
                    if (registered) guest_store_u16(flag + 4, 0);
                    return 0;
                }
                if (!block) return int32_t(0x8041090a);
                if (!registered)
                {
                    guest_store_u16(flag + 4, mask);
                    guest_store_u8(flag + 6, ctx.r5.u32);
                    registered = true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    // Per-library handlers. Swap rule from the project plan: cellSpurs is stubbed (succeeds, no SPU work)
    // until something visibly needs SPU work; media decoders go to ffmpeg later, so cellAdec reports no data.
    const std::unordered_map<std::string, HleFn> kLibraryHandlers = {
        { "cellSpurs", hle_ok },
        { "cellAdec", hle_ok },
        { "fs_utility_init", hle_ok }, // RPCS3 HLEs libfs_utility_init.sprx; every function but 6B5896B0 returns OK
    };

    // fs_utility_init_6B5896B0(u64* dest): writes the HDD partition count (RPCS3 Modules/libfs_utility_init.cpp)
    int64_t fs_utility_init_6B5896B0(PPCContext& ctx)
    {
        if (!ctx.r3.u32) return int32_t(0x8001000D); // CELL_EFAULT
        guest_store_u64(ctx.r3.u32, 2);
        return 0;
    }

    // Functions that need more than their library's handler, by (library, nid)
    const std::unordered_map<uint64_t, HleFn> kFunctionHandlers = {
        { 0x6B5896B0, fs_utility_init_6B5896B0 },
        { 0x161DA6A7, cell_spurs_job_chain_get_error },
        { 0xB9BC6207, cell_spurs_attach_queue },
        { 0x303C19CD, cell_spurs_create_job_chain },
        { 0x3548F483, cell_spurs_job_chain_attribute_initialize },
        { 0xBB68D76E, [](PPCContext& ctx)->int64_t { guest_store_u32(ctx.r3.u32 + 0x34, 1); return 0; } },
        { 0xF31731BB, cell_spurs_run_job_chain },
        { 0x68AAEBA9, cell_spurs_job_guard_initialize },
        { 0xD5D0B256, cell_spurs_job_guard_notify },
        { 0x00AF2519, cell_spurs_job_guard_reset },
        { 0xBEB600AC, cell_spurs_create_task },
        { 0x52CC6C82, cell_spurs_create_taskset },
        { 0xC10931CB, cell_spurs_create_taskset_attribute },
        { 0x4A6465E3, cell_spurs_create_taskset_attribute },
        { 0xE0A6DBE4, spurs_send_signal },
        { 0x5EF96465, cell_spurs_event_flag_initialize },
        { 0x87630976, cell_spurs_event_flag_attach },
        { 0x4AC7BAE4, cell_spurs_event_flag_clear },
        { 0xF5507729, cell_spurs_event_flag_set },
        { 0x373523D4, [](PPCContext& ctx) { return cell_spurs_event_flag_wait(ctx, true); } },
        { 0x6D2D9339, [](PPCContext& ctx) { return cell_spurs_event_flag_wait(ctx, false); } },
    };

    HleFn find_handler(const HleImport& imp)
    {
        if (const auto f = kFunctionHandlers.find(imp.nid); f != kFunctionHandlers.end())
        {
            if ((imp.nid == 0x6B5896B0 && imp.library == "fs_utility_init") ||
                (imp.nid != 0x6B5896B0 && imp.library == "cellSpurs")) return f->second;
        }
        const auto l = kLibraryHandlers.find(imp.library);
        return l != kLibraryHandlers.end() ? l->second : nullptr;
    }

    void hle_dispatch(PPCContext& ctx, size_t index)
    {
        const HleImport& imp = g_imports[index];
        const uint64_t seq = next_call_seq();
        const uint32_t lr = uint32_t(ctx.lr);
        const uint64_t r3 = ctx.r3.u64, r4 = ctx.r4.u64, r5 = ctx.r5.u64, r6 = ctx.r6.u64;
        const HleFn fn = find_handler(imp);
        const int64_t result = fn ? fn(ctx) : CELL_ENOSYS;
        ctx.r3.s64 = result;
        const uint8_t diagnostic = !fn || result == CELL_ENOSYS ? 1 : fn == hle_ok ? 2 : 0;
        if (diagnostic && !(g_reported_binding[index].fetch_or(diagnostic) & diagnostic))
            log_line("%s: hle %s 0x%08X lr=%08X r3=%llX r4=%llX r5=%llX r6=%llX",
                diagnostic == 1 ? "unsupported service" : "success placeholder",
                imp.library.c_str(), imp.nid, lr,
                (unsigned long long)r3, (unsigned long long)r4, (unsigned long long)r5, (unsigned long long)r6);
        log_line("%llu T%08X hle %s 0x%08X lr=%08X r3=%llX r4=%llX r5=%llX r6=%llX -> %s%llX",
            (unsigned long long)seq, current_thread_id(), imp.library.c_str(), imp.nid, lr,
            (unsigned long long)r3, (unsigned long long)r4, (unsigned long long)r5, (unsigned long long)r6,
            fn ? "" : "ENOSYS ", (unsigned long long)uint64_t(result));
    }

    template <size_t N>
    void hle_thunk(PPCContext& __restrict__ ctx, uint8_t*)
    {
        hle_dispatch(ctx, N);
    }

    template <size_t... I>
    constexpr std::array<PPCFunc*, sizeof...(I)> make_thunks(std::index_sequence<I...>)
    {
        return { &hle_thunk<I>... };
    }

    constexpr auto kThunks = make_thunks(std::make_index_sequence<kMaxHle>{});
}

bool hle_load(const char* hle_csv)
{
    FILE* f = fopen(hle_csv, "r");
    if (!f)
    {
        log_line("cannot open %s", hle_csv);
        return false;
    }
    char line[256];
    fgets(line, sizeof(line), f); // header: index,library,nid,opd,code
    while (fgets(line, sizeof(line), f))
    {
        unsigned index, nid, opd, code;
        char library[64];
        if (sscanf(line, "%u,%63[^,],0x%X,0x%X,0x%X", &index, library, &nid, &opd, &code) != 5)
            continue;
        if (index != g_imports.size() || index >= kMaxHle)
        {
            log_line("hle.csv: unexpected index %u", index);
            fclose(f);
            return false;
        }
        g_imports.push_back({ library, nid, opd, code });
    }
    fclose(f);
    log_line("hle: %zu imports from %s", g_imports.size(), hle_csv);
    return !g_imports.empty();
}

void hle_install()
{
    for (size_t i = 0; i < g_imports.size(); i++)
    {
        const HleImport& imp = g_imports[i];
        // The descriptor xmbld wrote must point at this stub's code word.
        if (guest_load_u32(imp.opd) != imp.code)
            log_line("hle: OPD %08X points to %08X, expected %08X", imp.opd, guest_load_u32(imp.opd), imp.code);
        func_slot(imp.code) = kThunks[i];
    }
}
