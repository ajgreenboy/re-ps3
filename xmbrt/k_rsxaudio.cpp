// sys_rsxaudio (sys_rsxaudio.cpp semantics): the shared page between sys_audio and the audio hardware.
// Layout and initial values match RPCS3. Not yet modelled: the periodic DMA reader that consumes the
// ring buffers, posts the per-period events, and plays the samples (phase 5, WASAPI).
#include "kernel.h"

#include <cstring>

namespace
{
    // rsxaudio_shmem (0x20000 bytes)
    constexpr uint32_t SHMEM_SIZE = 0x20000, DMA_SERIAL = 0x0, DMA_SPDIF0 = 0x10000, DMA_SPDIF1 = 0x14000, DMA_SILENCE = 0x18000;
    constexpr uint32_t CTRL = 0x1C000;
    // ctrl_t
    constexpr uint32_t RINGBUF_SIZE = 0x230, RB_DMA_SILENCE = 0x20, RB_UNK2 = 0x04, RB_ENTRIES = 0x30, ENTRY_SIZE = 0x20, ENTRY_DMA = 0x1C;
    constexpr uint32_t EQ1 = 0x694, EQ2 = 0x6A8, EQ3 = 0x6BC, UNK4 = 0x6CC, INTR_PRIO = 0x6D0, UNK5 = 0x6D4, CHANNEL_UF = 0x7D0;
    constexpr uint32_t BLK_SERIAL = 0x1000, BLK_SPDIF = 0x400, RINGBUF_ENTRIES = 16;
    constexpr uint32_t kDmaIoBase = 0x30000000, kDmaIoId = 1;

    struct KRsxAudio : KObject
    {
        uint32_t shmem = 0;
        uint32_t queues[3]{};
    };
    bool g_allocated = false;

    // sys_rsxaudio_initialize(handle*)
    int64_t sys_rsxaudio_initialize(PPCContext& ctx)
    {
        if (g_allocated) return CELL_EINVAL;
        if (!ctx.r3.u32) return CELL_EFAULT;
        auto a = std::make_shared<KRsxAudio>();
        a->shmem = user_alloc(SHMEM_SIZE, 0x10000);
        if (!a->shmem) return CELL_ENOMEM;
        const uint32_t c = a->shmem + CTRL;
        memset(g_base + c, 0, 0x4000);
        guest_store_u32(c + UNK4, 0x8000);
        guest_store_u32(c + INTR_PRIO, 0xDEADBEEF);
        guest_store_u32(c + UNK5, 0);
        g_allocated = true;
        guest_store_u32(ctx.r3.u32, kobj_add(0x60, a));
        return CELL_OK;
    }

    // sys_rsxaudio_import_shared_memory(handle, addr*)
    int64_t sys_rsxaudio_import_shared_memory(PPCContext& ctx)
    {
        auto a = kobj<KRsxAudio>(ctx.r3.u32);
        if (!a) return CELL_ESRCH;
        if (!ctx.r4.u32) return CELL_EFAULT;
        guest_store_u64(ctx.r4.u32, a->shmem);
        return CELL_OK;
    }

    // sys_rsxaudio_create_connection(handle): binds the three event queues named in the shared page
    int64_t sys_rsxaudio_create_connection(PPCContext& ctx)
    {
        auto a = kobj<KRsxAudio>(ctx.r3.u32);
        if (!a) return CELL_ESRCH;
        const uint32_t c = a->shmem + CTRL;
        const uint32_t eq[3] = { guest_load_u32(c + EQ1), guest_load_u32(c + EQ2), guest_load_u32(c + EQ3) };
        for (int i = 0; i < 3; i++)
        {
            if (!kobj_get(eq[i])) return CELL_ESRCH;
            a->queues[i] = eq[i];
        }
        for (uint32_t rb = 0; rb < 3; rb++)
        {
            guest_store_u32(c + rb * RINGBUF_SIZE + RB_DMA_SILENCE, kDmaIoBase + DMA_SILENCE);
            guest_store_u32(c + rb * RINGBUF_SIZE + RB_UNK2, 100);
        }
        for (uint32_t i = 0; i < RINGBUF_ENTRIES; i++)
        {
            guest_store_u32(c + 0 * RINGBUF_SIZE + RB_ENTRIES + i * ENTRY_SIZE + ENTRY_DMA, kDmaIoBase + DMA_SERIAL + BLK_SERIAL * i);
            guest_store_u32(c + 1 * RINGBUF_SIZE + RB_ENTRIES + i * ENTRY_SIZE + ENTRY_DMA, kDmaIoBase + DMA_SPDIF0 + BLK_SPDIF * i);
            guest_store_u32(c + 2 * RINGBUF_SIZE + RB_ENTRIES + i * ENTRY_SIZE + ENTRY_DMA, kDmaIoBase + DMA_SPDIF1 + BLK_SPDIF * i);
        }
        return CELL_OK;
    }

    int64_t sys_rsxaudio_handle_ok(PPCContext& ctx)
    {
        return kobj<KRsxAudio>(ctx.r3.u32) ? CELL_OK : CELL_ESRCH;
    }

    // sys_rsxaudio_start_process(handle): clears the underflow counters
    int64_t sys_rsxaudio_start_process(PPCContext& ctx)
    {
        auto a = kobj<KRsxAudio>(ctx.r3.u32);
        if (!a) return CELL_ESRCH;
        for (uint32_t i = 0; i < 3; i++)
        {
            guest_store_u64(a->shmem + CTRL + CHANNEL_UF + i * 0x100, 0);
            guest_store_u32(a->shmem + CTRL + CHANNEL_UF + i * 0x100 + 8, 0);
        }
        static bool logged;
        if (!logged)
        {
            logged = true;
            log_line("rsxaudio: started; ring buffers are not consumed yet (no audio output)");
        }
        return CELL_OK;
    }

    // sys_rsxaudio_get_dma_param(handle, flag, out*): 0 = IO base, 1 = IO id
    int64_t sys_rsxaudio_get_dma_param(PPCContext& ctx)
    {
        if (!kobj<KRsxAudio>(ctx.r3.u32)) return CELL_ESRCH;
        const uint32_t out = ctx.r5.u32;
        if (!out) return CELL_EFAULT;
        if (ctx.r4.u32 == 0) guest_store_u64(out, kDmaIoBase);
        else if (ctx.r4.u32 == 1) guest_store_u64(out, kDmaIoId);
        else return CELL_EINVAL;
        return CELL_OK;
    }

    int64_t sys_rsxaudio_finalize(PPCContext& ctx)
    {
        if (!kobj_remove(ctx.r3.u32)) return CELL_ESRCH;
        g_allocated = false;
        return CELL_OK;
    }
}

void register_rsxaudio_syscalls()
{
    register_syscall(650, sys_rsxaudio_initialize);
    register_syscall(651, sys_rsxaudio_finalize);
    register_syscall(652, sys_rsxaudio_import_shared_memory);
    register_syscall(653, sys_rsxaudio_handle_ok); // unimport
    register_syscall(654, sys_rsxaudio_create_connection);
    register_syscall(655, sys_rsxaudio_handle_ok); // close_connection
    register_syscall(656, sys_rsxaudio_handle_ok); // prepare_process
    register_syscall(657, sys_rsxaudio_start_process);
    register_syscall(658, sys_rsxaudio_handle_ok); // stop_process
    register_syscall(659, sys_rsxaudio_get_dma_param);
}
