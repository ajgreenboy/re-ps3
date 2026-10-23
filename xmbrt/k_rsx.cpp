// sys_rsx (sys_rsx.cpp semantics) plus a null RSX: a FIFO thread that walks vsh's command buffer,
// honouring jumps/calls, references, semaphores and the GCM driver methods (flip/queue/user command),
// and a 59.94 Hz vblank thread. Drawing methods are skipped; phase 4 hands them to a renderer.
#include "kernel.h"

#include "k_event.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace
{
    constexpr uint32_t kContextBase = 0x40000000; // RPCS3 reserves vm::rsx_context on demand; first free 256 MB block
    constexpr uint32_t kLocalMemBase = 0xC0000000;
    constexpr uint32_t kContextId = 0x55555555;

    constexpr uint64_t SYS_RSX_EVENT_VBLANK = 1 << 1, SYS_RSX_EVENT_FLIP_BASE = 1 << 3, SYS_RSX_EVENT_QUEUE_BASE = 1 << 5;
    constexpr uint64_t SYS_RSX_EVENT_USER_CMD = 1 << 7, SYS_RSX_EVENT_SECOND_VBLANK_BASE = 1 << 10;

    // RsxDriverInfo offsets
    constexpr uint32_t DI_HEAD = 0x10B8, DI_HEAD_SIZE = 0x40, DI_HANDLERS = 0x12C0, DI_USER_CMD_PARAM = 0x12CC, DI_HANDLER_QUEUE = 0x12D0;
    constexpr uint32_t HEAD_LAST_FLIP_TIME = 0x0, HEAD_FLIP_FLAGS = 0x8, HEAD_FLIP_BUFFER_ID = 0x10, HEAD_LAST_QUEUED = 0x14;
    constexpr uint32_t HEAD_LAST_VTIME_LOW = 0x1C, HEAD_LAST_SECOND_VTIME = 0x20, HEAD_VBLANK_COUNT = 0x30, HEAD_LAST_VTIME_HIGH = 0x3C;

    struct DisplayBuffer
    {
        uint32_t width, height, pitch, offset;
    };

    struct Rsx
    {
        std::mutex m;
        uint32_t device_addr = 0, dma_address = 0, driver_info = 0, reports = 0;
        uint32_t local_mem_size = 0;
        uint32_t event_port = 0;
        uint32_t iomap_ea[512]; // io >> 20 -> ea, ~0 when unmapped
        DisplayBuffer display[8]{};
        uint32_t display_count = 0;
        bool second_vhandler = false;
        std::atomic<bool> started{ false };
        std::atomic<uint64_t> first_submit_logged{ 0 };
    } g_rsx;

    uint64_t timebase_now() { return ppc_mftb(); }

}

// rsx_gl.cpp
void rsx_gl_init();
void rsx_gl_method(uint32_t reg, uint32_t arg);
void rsx_gl_flip(uint32_t offset, uint32_t width, uint32_t height);
void rsx_gl_poll();
void rsx_gl_barrier();

namespace
{

    std::atomic<uint32_t> g_method_count[0x10000 / 4];
    std::atomic<uint64_t> g_draws{ 0 }, g_flips{ 0 };
    std::atomic<uint32_t> g_fifo_method{ 0 }, g_fifo_argument{ 0 };
    struct LabelCommand { uint32_t get, method, context, offset, address, value; };
    std::array<LabelCommand, 64> g_label_history{}; // FIFO thread only
    uint64_t g_label_sequence = 0;

    uint32_t load_label(uint32_t address)
    {
        return __builtin_bswap32(std::atomic_ref<uint32_t>(*reinterpret_cast<uint32_t*>(g_base + address)).load());
    }

    void store_label(uint32_t address, uint32_t value)
    {
        std::atomic_ref<uint32_t>(*reinterpret_cast<uint32_t*>(g_base + address)).store(__builtin_bswap32(value));
    }

    void remember_label(uint32_t get, uint32_t method, uint32_t context, uint32_t offset, uint32_t address, uint32_t value)
    {
        g_label_history[g_label_sequence++ % g_label_history.size()] = {get,method,context,offset,address,value};
    }

    void dump_label_history()
    {
        const uint64_t begin = g_label_sequence > 16 ? g_label_sequence - 16 : 0;
        for (uint64_t i = begin; i < g_label_sequence; ++i)
        {
            const auto& h = g_label_history[i % g_label_history.size()];
            log_line("rsx label history: seq=%llu get=%08X method=%04X context=%08X offset=%08X address=%08X value=%08X",
                (unsigned long long)i, h.get, h.method, h.context, h.offset, h.address, h.value);
        }
    }

    uint32_t head(uint32_t index, uint32_t field) { return g_rsx.driver_info + DI_HEAD + index * DI_HEAD_SIZE + field; }

    // Sends an RSX interrupt event if the guest registered a handler for it. Caller holds g_kernel.
    void send_event(uint64_t d1, uint64_t flags, uint64_t d3)
    {
        flags &= guest_load_u32(g_rsx.driver_info + DI_HANDLERS) | (0xFFFFFFFFull << 32);
        if (!flags) return;
        event_port_send_internal(g_rsx.event_port, d1, flags, d3);
    }

    // Completes a flip the way RPCS3's renderer does once the frame is presented (package 0xFEC).
    void complete_flip(uint32_t buffer)
    {
        g_flips++;
        if (buffer < g_rsx.display_count)
        {
            const DisplayBuffer& d = g_rsx.display[buffer];
            rsx_gl_flip(d.offset, d.width, d.height);
        }
        const uint32_t ff = head(1, HEAD_FLIP_FLAGS);
        guest_store_u32(ff, guest_load_u32(ff) | 0x80000000);
        guest_store_u64(head(1, HEAD_LAST_FLIP_TIME), timebase_now());
        guest_store_u32(head(1, HEAD_FLIP_BUFFER_ID), buffer);
        memset(g_base + g_rsx.reports + 0x10, 0, 16);
        send_event(0, SYS_RSX_EVENT_FLIP_BASE << 1, 0);
    }

    // Translates an RSX address (offset + context DMA / location) to a guest address, 0 if unmapped.
    uint32_t rsx_address(uint32_t offset, uint32_t location)
    {
        switch (location)
        {
        case 0: case 0xFEED0000: // local memory
            return offset < g_rsx.local_mem_size ? kLocalMemBase + offset : 0;
        case 1: case 0xFEED0001: // main memory through the IO map
        {
            if (offset >= 0x10000000) return 0;
            const uint32_t ea = g_rsx.iomap_ea[(offset >> 20) & 511];
            return ea == ~0u ? 0 : ea + (offset & 0xFFFFF);
        }
        case 0x66606660: case 0x66616661: // semaphores
            return offset < 0x1000 ? g_rsx.reports + offset : 0;
        case 0x66626660: // reports (local)
            return offset < 0x8000 ? g_rsx.reports + 0x1400 + offset : 0;
        case 0xBAD68000: // reports (main memory through the IO map)
            return offset < 0x1000000 ? rsx_address(0x0E000000 + offset, 1) : 0;
        case 0x56616660: case 0x56616661: // device synchronization area
            return offset < 0x100000 ? g_rsx.device_addr + offset : 0;
        default:
            return 0;
        }
    }

    void fifo_thread()
    {
        const uint32_t ctrl = g_rsx.dma_address;
        uint32_t sem_ctx_406e = 0x66606660, sem_off_406e = 0, sem_ctx_4097 = 0x66606660, sem_off_4097 = 0;
        uint32_t call_stack[8], call_depth = 0;
        for (;;)
        {
            const uint32_t put = guest_load_u32(ctrl + 0x40);
            uint32_t get = guest_load_u32(ctrl + 0x44);
            rsx_gl_poll();
            if (get == put)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(200));
                continue;
            }
            if (!g_rsx.first_submit_logged.exchange(1))
                log_line("rsx: first command buffer submit (get=%08X put=%08X)", get, put);
            const uint32_t addr = rsx_address(get, 1);
            if (!addr)
            {
                log_line("rsx: FIFO get %08X is not IO-mapped; stopping FIFO", get);
                return;
            }
            const uint32_t cmd = guest_load_u32(addr);
            if ((cmd & 0xE0000003) == 0x20000000) // old jump
            {
                guest_store_u32(ctrl + 0x44, cmd & 0x1FFFFFFC);
                continue;
            }
            if ((cmd & 0xE0000003) == 0x00000001) // new jump
            {
                guest_store_u32(ctrl + 0x44, cmd & 0xFFFFFFFC);
                continue;
            }
            if ((cmd & 3) == 2) // call
            {
                if (call_depth < 8) call_stack[call_depth++] = get + 4;
                guest_store_u32(ctrl + 0x44, cmd & 0x1FFFFFFC);
                continue;
            }
            if ((cmd & 0xFFFF0003) == 0x00020000) // return
            {
                guest_store_u32(ctrl + 0x44, call_depth ? call_stack[--call_depth] : get + 4);
                continue;
            }
            const uint32_t count = (cmd >> 18) & 0x7FF;
            const uint32_t method = cmd & 0xFFFC;
            const bool non_inc = (cmd & 0xE0030003) == 0x40000000;
            for (uint32_t i = 0; i < count; i++)
            {
                const uint32_t reg = non_inc ? method : method + i * 4;
                const uint32_t arg = guest_load_u32(addr + 4 + i * 4);
                g_fifo_method = reg;
                g_fifo_argument = arg;
                g_method_count[(reg & 0xFFFF) >> 2]++;
                if (reg == 0x1808 && arg) g_draws++; // NV4097_SET_BEGIN_END (begin)
                switch (reg)
                {
                case 0x0050: // NV406E_SET_REFERENCE
                    rsx_gl_barrier();
                    guest_store_u32(ctrl + 0x48, arg);
                    break;
                case 0x0060: sem_ctx_406e = arg; break;
                case 0x0064: sem_off_406e = arg; break;
                case 0x0068: // NV406E_SEMAPHORE_ACQUIRE: wait for the value (bounded, so a missing release can't hang us)
                {
                    const uint32_t a = rsx_address(sem_off_406e, sem_ctx_406e);
                    remember_label(get,reg,sem_ctx_406e,sem_off_406e,a,arg);
                    if (a & 3) { log_line("rsx: unaligned semaphore acquire %08X",a); break; }
                    for (int spin = 0; a && load_label(a) != arg && spin < 5000; spin++)
                    {
                        rsx_gl_poll();
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    }
                    static uint32_t timeouts = 0;
                    if (a && load_label(a) != arg && timeouts++ < 8)
                    {
                        log_line("rsx: semaphore timeout at %08X (value=%08X expected=%08X context=%08X offset=%08X get=%08X)",
                            a,load_label(a),arg,sem_ctx_406e,sem_off_406e,get);
                        dump_label_history();
                    }
                    break;
                }
                case 0x006C: // NV406E_SEMAPHORE_RELEASE
                    if (const uint32_t a = rsx_address(sem_off_406e, sem_ctx_406e); a && !(a & 3))
                    {
                        // RPCS3 nv406e.cpp: the device flip-sync register latches 1 on a zero write.
                        const uint32_t value = a == g_rsx.device_addr + 0x30 && !arg ? 1 : arg;
                        remember_label(get,reg,sem_ctx_406e,sem_off_406e,a,value);
                        rsx_gl_barrier();
                        store_label(a,value);
                    }
                    break;
                case 0x01A4: sem_ctx_4097 = arg; break;
                case 0x1D6C: sem_off_4097 = arg; break;
                case 0x1D70: // NV4097_BACK_END_WRITE_SEMAPHORE_RELEASE (byte-swizzled value)
                    if (const uint32_t a = rsx_address(sem_off_4097, sem_ctx_4097))
                    {
                        if (sem_off_4097 & 15) { log_line("rsx: unaligned backend semaphore release %08X",a); break; }
                        const uint32_t value = (arg & 0xFF00FF00) | ((arg & 0xFF) << 16) | ((arg >> 16) & 0xFF);
                        remember_label(get,reg,sem_ctx_4097,sem_off_4097,a,value);
                        rsx_gl_barrier();
                        store_label(a,value);
                    }
                    break;
                case 0x1D74: // NV4097_TEXTURE_READ_SEMAPHORE_RELEASE
                    if (const uint32_t a = rsx_address(sem_off_4097, sem_ctx_4097))
                    {
                        if (sem_off_4097 & 15) { log_line("rsx: unaligned texture semaphore release %08X",a); break; }
                        remember_label(get,reg,sem_ctx_4097,sem_off_4097,a,arg);
                        rsx_gl_barrier();
                        store_label(a,arg);
                    }
                    break;
                case 0xE920: case 0xE924: // GCM_FLIP_HEAD: display flip (package 0x102)
                {
                    KLock lock(g_kernel);
                    complete_flip(arg & 7);
                    break;
                }
                case 0xEB00: case 0xEB04: // GCM_SET_USER_COMMAND (package 0xFEF)
                {
                    KLock lock(g_kernel);
                    guest_store_u32(g_rsx.driver_info + DI_USER_CMD_PARAM, arg);
                    send_event(0, SYS_RSX_EVENT_USER_CMD, 0);
                    break;
                }
                default:
                    if (reg < 0xE000) rsx_gl_method(reg, arg);
                    if (reg >= 0xE940 && reg <= 0xE95C) // GCM_DRIVER_QUEUE (package 0x103)
                    {
                        KLock lock(g_kernel);
                        const uint32_t h = 1;
                        guest_store_u32(head(h, HEAD_LAST_QUEUED), arg);
                        guest_store_u32(head(h, HEAD_FLIP_FLAGS), guest_load_u32(head(h, HEAD_FLIP_FLAGS)) | 0x40000000 | (1u << (arg & 31)));
                        send_event(0, SYS_RSX_EVENT_QUEUE_BASE << h, 0);
                    }
                    break;
                }
            }
            guest_store_u32(ctrl + 0x44, get + 4 + count * 4);
        }
    }

    void vblank_thread()
    {
        using namespace std::chrono;
        const auto period = nanoseconds(1001000000ull / 60); // 59.94 Hz
        auto next = steady_clock::now() + period;
        for (;;)
        {
            std::this_thread::sleep_until(next);
            next += period;
            KLock lock(g_kernel);
            for (uint32_t h = 0; h < 2; h++)
            {
                const uint64_t now = timebase_now();
                guest_store_u64(head(h, HEAD_LAST_SECOND_VTIME), now);
                guest_store_u32(head(h, HEAD_LAST_VTIME_LOW), uint32_t(now));
                guest_store_u32(head(h, HEAD_LAST_VTIME_HIGH), uint32_t(now >> 32));
                guest_store_u64(head(h, HEAD_VBLANK_COUNT), guest_load_u64(head(h, HEAD_VBLANK_COUNT)) + 1);
            }
            send_event(0, SYS_RSX_EVENT_VBLANK | (g_rsx.second_vhandler ? SYS_RSX_EVENT_SECOND_VBLANK_BASE << 1 : 0), 0);
        }
    }

    int64_t sys_rsx_ok(PPCContext&) { return CELL_OK; }

    // sys_rsx_memory_allocate(mem_handle*, mem_addr*, size, flags, a5, a6, a7)
    int64_t sys_rsx_memory_allocate(PPCContext& ctx)
    {
        const uint64_t size = ctx.r5.u64 % (0x100000ull * 0x100000000ull);
        if ((size & 0xFFFFF) || size > 0x10000000) return int32_t(-17); // LV1_ILLEGAL_PARAMETER_VALUE
        std::lock_guard lock(g_rsx.m);
        if (!guest_commit(kLocalMemBase, size)) return CELL_ENOMEM;
        g_rsx.local_mem_size = uint32_t(size);
        if (g_rsx.driver_info) guest_store_u32(g_rsx.driver_info + 8, g_rsx.local_mem_size);
        guest_store_u64(ctx.r4.u32, kLocalMemBase);
        guest_store_u32(ctx.r3.u32, 0x5A5A5A5B);
        return CELL_OK;
    }

    // sys_rsx_device_map(dev_addr*, a2*, dev_id): the first 1 MB of the RSX context area
    int64_t sys_rsx_device_map(PPCContext& ctx)
    {
        if (ctx.r5.u32 != 8) return CELL_EINVAL;
        std::lock_guard lock(g_rsx.m);
        if (!g_rsx.device_addr)
        {
            if (!guest_commit(kContextBase, 0x100000)) return CELL_ENOMEM;
            g_rsx.device_addr = kContextBase;
        }
        guest_store_u64(ctx.r3.u32, g_rsx.device_addr);
        return CELL_OK;
    }

    // sys_rsx_context_allocate(context_id*, lpar_dma_control*, lpar_driver_info*, lpar_reports*, mem_ctx, system_mode)
    int64_t sys_rsx_context_allocate(PPCContext& ctx)
    {
        std::lock_guard lock(g_rsx.m);
        if (g_rsx.dma_address) return CELL_EINVAL; // one context, as in RPCS3
        const uint32_t dma = kContextBase + 0x100000;
        if (!guest_commit(dma, 0x300000)) return CELL_ENOMEM;
        g_rsx.dma_address = dma;
        g_rsx.driver_info = dma + 0x100000;
        g_rsx.reports = dma + 0x200000;
        guest_store_u64(ctx.r4.u32, dma);
        guest_store_u64(ctx.r5.u32, g_rsx.driver_info);
        guest_store_u64(ctx.r6.u32, g_rsx.reports);

        // RsxReports: semaphore[1024], notify[64] {timestamp=-1}, report[2048] {timestamp=-1, val=0, pad=-1}
        const uint32_t rep = g_rsx.reports;
        memset(g_base + rep, 0, 0x1000 + 0x400 + 0x8000);
        static const uint32_t pattern[4] = { 0x1337C0D3, 0x1337BABE, 0x1337BEEF, 0x1337F001 };
        for (uint32_t i = 0; i < 1024; i++) guest_store_u32(rep + i * 4, pattern[i % 4]);
        for (uint32_t i = 0; i < 64; i++) guest_store_u64(rep + 0x1000 + i * 16, ~0ull);
        for (uint32_t i = 0; i < 2048; i++)
        {
            guest_store_u64(rep + 0x1400 + i * 16, ~0ull);
            guest_store_u32(rep + 0x1400 + i * 16 + 12, ~0u);
        }

        const uint32_t di = g_rsx.driver_info;
        memset(g_base + di, 0, 0x1400);
        guest_store_u32(di + 0x00, 0x211);      // version_driver
        guest_store_u32(di + 0x04, 0x5C);       // version_gpu
        guest_store_u32(di + 0x08, g_rsx.local_mem_size);
        guest_store_u32(di + 0x0C, 1);          // hardware_channel
        guest_store_u32(di + 0x10, 500000000);  // nvcore_frequency
        guest_store_u32(di + 0x14, 650000000);  // memory_frequency
        guest_store_u32(di + 0x2C, 0x1000);     // reportsNotifyOffset
        guest_store_u32(di + 0x30, 0);          // reportsOffset
        guest_store_u32(di + 0x34, 0x1400);     // reportsReportOffset
        guest_store_u32(di + 0x54, ctx.r8.u32); // systemModeFlags

        memset(g_base + dma, 0, 0x60); // RsxDmaControl: put/get/ref zero
        for (auto& e : g_rsx.iomap_ea) e = ~0u;
        {
            KLock klock(g_kernel);
            std::shared_ptr<KEventQueue> q;
            const uint32_t qid = create_event_queue_internal(0x20, &q);
            g_rsx.event_port = create_local_port_internal(q);
            guest_store_u32(di + DI_HANDLER_QUEUE, qid);
        }
        guest_store_u32(ctx.r3.u32, kContextId);
        if (!g_rsx.started.exchange(true))
        {
            rsx_gl_init();
            std::thread(fifo_thread).detach();
            std::thread(vblank_thread).detach();
        }
        log_line("rsx: context allocated: dma %08X driver_info %08X reports %08X local %08X+%X", dma, g_rsx.driver_info, g_rsx.reports, kLocalMemBase, g_rsx.local_mem_size);
        return CELL_OK;
    }

    // sys_rsx_context_iomap(context_id, io, ea, size, flags)
    int64_t sys_rsx_context_iomap(PPCContext& ctx)
    {
        const uint64_t io = ctx.r4.u64, ea = ctx.r5.u64, size = ctx.r6.u64;
        if (!size || (io & 0xFFFFF) || size > 0x20000000 || (ea & 0xFFFFF) || (size & 0xFFFFF)) return CELL_EINVAL;
        if (ctx.r3.u32 != kContextId || io + size > 0x10000000) return CELL_EINVAL; // 256 MB main memory window
        std::lock_guard lock(g_rsx.m);
        for (uint64_t i = 0; i < (size >> 20); i++) g_rsx.iomap_ea[(io >> 20) + i] = uint32_t(ea + (i << 20));
        return CELL_OK;
    }

    // sys_rsx_context_iounmap(context_id, io, size)
    int64_t sys_rsx_context_iounmap(PPCContext& ctx)
    {
        std::lock_guard lock(g_rsx.m);
        for (uint64_t i = 0; i < (ctx.r5.u64 >> 20); i++) g_rsx.iomap_ea[((ctx.r4.u64 >> 20) + i) & 511] = ~0u;
        return CELL_OK;
    }

    // sys_rsx_context_attribute(context_id, package_id, a3, a4, a5, a6)
    int64_t sys_rsx_context_attribute(PPCContext& ctx)
    {
        const uint32_t package = ctx.r4.u32;
        const uint64_t a3 = ctx.r5.u64, a4 = ctx.r6.u64, a5 = ctx.r7.u64;
        if (!g_rsx.dma_address || ctx.r3.u32 != kContextId) return CELL_EINVAL;
        switch (package)
        {
        case 0x001: // FIFO: set get/put
            guest_store_u32(g_rsx.dma_address + 0x44, uint32_t(a3));
            guest_store_u32(g_rsx.dma_address + 0x40, uint32_t(a4));
            break;
        case 0x102: // display flip: a queued buffer (high bit) or a display buffer offset
        {
            uint32_t idx = 0;
            if (a4 & 0x80000000)
            {
                idx = (a4 & 0xF) > 7 ? guest_load_u32(head(uint32_t(a3) & 1, HEAD_LAST_QUEUED)) : uint32_t(a4 & 0xF);
            }
            else
            {
                std::lock_guard lock(g_rsx.m);
                for (uint32_t i = 0; i < g_rsx.display_count; i++)
                    if (g_rsx.display[i].offset == uint32_t(a4)) idx = i;
            }
            KLock lock(g_kernel);
            complete_flip(idx);
            break;
        }
        case 0x103: // display queue
        {
            if (a3 >= 2) return CELL_EINVAL;
            KLock lock(g_kernel);
            guest_store_u32(head(uint32_t(a3), HEAD_LAST_QUEUED), uint32_t(a4));
            guest_store_u32(head(uint32_t(a3), HEAD_FLIP_FLAGS), guest_load_u32(head(uint32_t(a3), HEAD_FLIP_FLAGS)) | 0x40000000 | (1u << (a4 & 31)));
            send_event(0, SYS_RSX_EVENT_QUEUE_BASE << a3, 0);
            break;
        }
        case 0x104: // display buffer
        {
            const uint32_t id = uint32_t(a3 & 0xFF);
            if (id > 7) return int32_t(-17);
            std::lock_guard lock(g_rsx.m);
            g_rsx.display[id] = { uint32_t(a4 >> 32), uint32_t(a4), uint32_t(a5 >> 32), uint32_t(a5) };
            g_rsx.display_count = std::max(g_rsx.display_count, id + 1);
            log_line("rsx: display buffer %u: %ux%u pitch %u offset %08X", id, uint32_t(a4 >> 32), uint32_t(a4), uint32_t(a5 >> 32), uint32_t(a5));
            break;
        }
        case 0x108: // vblank frequency (a5 == 2: second vhandler)
            if (a5 == 2) g_rsx.second_vhandler = a4 != 4;
            break;
        case 0x10A: // flip status reset
        {
            if (a3 > 7) return int32_t(-17);
            KLock lock(g_kernel);
            const uint32_t ff = head(uint32_t(a3), HEAD_FLIP_FLAGS);
            guest_store_u32(ff, (guest_load_u32(ff) & uint32_t(a4)) | uint32_t(a5));
            break;
        }
        case 0xFEF: // user command
        {
            KLock lock(g_kernel);
            guest_store_u32(g_rsx.driver_info + DI_USER_CMD_PARAM, uint32_t(a4));
            send_event(0, SYS_RSX_EVENT_USER_CMD, 0);
            break;
        }
        case 0x100: case 0x101: case 0x105: case 0x106: case 0x10B: case 0x10C: case 0x10D:
        case 0x300: case 0x301: case 0x302: case 0x600: case 0x601: case 0x602: case 0x603:
            break;
        default:
            log_line("rsx: unsupported context attribute package %X", package);
            return CELL_EINVAL;
        }
        return CELL_OK;
    }
}

// For the renderer: location 0 = local memory, 1 = main memory through the IO map.
uint32_t rsx_guest_address(uint32_t offset, uint32_t location)
{
    return rsx_address(offset, location);
}

// Logs which RSX methods vsh has issued (most used first), with draw and flip counts.
void rsx_dump_stats()
{
    std::vector<std::pair<uint32_t, uint32_t>> used;
    for (uint32_t i = 0; i < 0x4000; i++)
        if (const uint32_t n = g_method_count[i]) used.push_back({ n, i * 4 });
    std::sort(used.rbegin(), used.rend());
    log_line("rsx: %llu draws, %llu flips, %zu distinct methods", (unsigned long long)g_draws.load(), (unsigned long long)g_flips.load(), used.size());
    if (g_rsx.dma_address)
    {
        const uint32_t get = guest_load_u32(g_rsx.dma_address + 0x44);
        log_line("rsx FIFO: get=%08X put=%08X reference=%08X last method=%04X argument=%08X",
            get, guest_load_u32(g_rsx.dma_address + 0x40),
            guest_load_u32(g_rsx.dma_address + 0x48), g_fifo_method.load(), g_fifo_argument.load());
        if (const uint32_t a = rsx_address(get, 1))
            log_line("rsx FIFO words: ea=%08X %08X %08X %08X %08X", a, guest_load_u32(a),
                guest_load_u32(a + 4), guest_load_u32(a + 8), guest_load_u32(a + 12));
    }
    std::string line;
    for (size_t i = 0; i < used.size(); i++)
    {
        char item[32];
        snprintf(item, sizeof(item), " %04X:%u", used[i].second, used[i].first);
        line += item;
        if ((i + 1) % 12 == 0 || i + 1 == used.size())
        {
            log_line("rsx methods:%s", line.c_str());
            line.clear();
        }
    }
}

void register_rsx_syscalls()
{
    register_syscall(666, sys_rsx_ok); // device_open
    register_syscall(667, sys_rsx_ok); // device_close
    register_syscall(668, sys_rsx_memory_allocate);
    register_syscall(669, sys_rsx_ok); // memory_free
    register_syscall(670, sys_rsx_context_allocate);
    register_syscall(671, sys_rsx_ok); // context_free
    register_syscall(672, sys_rsx_context_iomap);
    register_syscall(673, sys_rsx_context_iounmap);
    register_syscall(674, sys_rsx_context_attribute);
    register_syscall(675, sys_rsx_device_map);
    register_syscall(676, sys_rsx_ok); // device_unmap
    register_syscall(677, sys_rsx_ok); // attribute
}
