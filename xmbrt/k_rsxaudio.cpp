// RSX audio DMA rings. Layout, retirement and event payloads follow RPCS3 sys_rsxaudio.cpp.
#include "kernel.h"
#include "k_event.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <thread>

namespace
{
    constexpr uint32_t SHMEM_SIZE = 0x20000, DMA_SILENCE = 0x18000, CTRL = 0x1C000, RING_SIZE = 0x230;
    constexpr uint32_t ACTIVE = 0, UNK2 = 4, READ = 8, WRITE = 12, MAX = 16, NOTIFY = 20, STEP = 24;
    constexpr uint32_t SILENCE = 32, NEXT_BLOCK = 40, ENTRIES = 0x30, ENTRY_SIZE = 0x20, BLOCK = 8, TIMESTAMP = 16, DMA = 28;
    constexpr uint32_t EQ[3] = { 0x694, 0x6A8, 0x6BC }, CHANNEL_UF = 0x7D0, DMA_BASE = 0x30000000;
    constexpr uint32_t REGIONS[3] = { 0, 0x10000, 0x14000 };
    constexpr uint64_t SOURCES[3] = { 0x8000000000400100ull, 0x8000000000400200ull, 0x8000000000400300ull };
    struct PortConfig
    {
        uint32_t rate = 48000, bytes = 2;
        bool enabled = false, muted = true, use_serial_buffer = false;
        bool configured = false; // UART AUDIO_MODE enables the hardware buffer-empty clock.
    };
    std::array<PortConfig, 3> g_ports = {{{48000, 2, false, true, false}, {44100, 2, false, true, true}, {44100, 2, false, true, true}}};
    struct KRsxAudio : KObject
    {
        uint32_t shmem = 0;
        std::shared_ptr<KEventQueue> queues[3];
        bool connected = false, prepared = false, running = false, invalid_ring_logged = false;
        bool dma_enabled[3]{};
        uint64_t period_index[3]{}, period_divisor[3]{}, retired[3]{};
    };
    std::shared_ptr<KRsxAudio> g_audio;
    std::condition_variable g_audio_cv;
    bool g_thread_started = false;

    // Dynamic WinMM binding needs no new import library. Hardware DMA remains independent
    // of host output availability. Currently outputs serial stream zero as stereo PCM16.
    class StereoSink
    {
        struct Buffer { WAVEHDR header{}; std::array<int16_t, 512> pcm{}; bool prepared = false; };
        HMODULE module = nullptr;
        HWAVEOUT device = nullptr;
        uint32_t rate = 0;
        bool unavailable_logged = false;
        std::array<Buffer, 16> buffers{};
        decltype(&waveOutOpen) open_fn = nullptr;
        decltype(&waveOutClose) close_fn = nullptr;
        decltype(&waveOutReset) reset_fn = nullptr;
        decltype(&waveOutPrepareHeader) prepare_fn = nullptr;
        decltype(&waveOutUnprepareHeader) unprepare_fn = nullptr;
        decltype(&waveOutWrite) write_fn = nullptr;
    public:
        void stop()
        {
            if (!device) return;
            reset_fn(device);
            for (auto& b : buffers)
            {
                if (b.prepared) unprepare_fn(device, &b.header, sizeof(b.header));
                b.prepared = false;
                b.header = {};
            }
            close_fn(device);
            device = nullptr;
            rate = 0;
        }
        bool open(uint32_t sample_rate)
        {
            if (device && rate == sample_rate) return true;
            stop();
            if (!module)
            {
                module = LoadLibraryW(L"winmm.dll");
                if (module)
                {
                    open_fn = reinterpret_cast<decltype(open_fn)>(GetProcAddress(module, "waveOutOpen"));
                    close_fn = reinterpret_cast<decltype(close_fn)>(GetProcAddress(module, "waveOutClose"));
                    reset_fn = reinterpret_cast<decltype(reset_fn)>(GetProcAddress(module, "waveOutReset"));
                    prepare_fn = reinterpret_cast<decltype(prepare_fn)>(GetProcAddress(module, "waveOutPrepareHeader"));
                    unprepare_fn = reinterpret_cast<decltype(unprepare_fn)>(GetProcAddress(module, "waveOutUnprepareHeader"));
                    write_fn = reinterpret_cast<decltype(write_fn)>(GetProcAddress(module, "waveOutWrite"));
                }
            }
            if (!open_fn || !close_fn || !reset_fn || !prepare_fn || !unprepare_fn || !write_fn) return false;
            WAVEFORMATEX fmt{};
            fmt.wFormatTag = WAVE_FORMAT_PCM;
            fmt.nChannels = 2;
            fmt.nSamplesPerSec = sample_rate;
            fmt.wBitsPerSample = 16;
            fmt.nBlockAlign = 4;
            fmt.nAvgBytesPerSec = sample_rate * 4;
            const MMRESULT result = open_fn(&device, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL);
            if (result != MMSYSERR_NOERROR)
            {
                device = nullptr;
                if (!unavailable_logged) log_line("rsxaudio: Windows stereo output unavailable (%u); DMA remains active", result);
                unavailable_logged = true;
                return false;
            }
            rate = sample_rate;
            log_line("rsxaudio: Windows stereo PCM output %u Hz", rate);
            return true;
        }
        void submit(uint32_t addr, const PortConfig& cfg)
        {
            if (!open(cfg.rate)) return;
            auto it = std::find_if(buffers.begin(), buffers.end(), [](const Buffer& b) { return !b.prepared || (b.header.dwFlags & WHDR_DONE); });
            if (it == buffers.end()) return; // Host backlog must not stall the DMA clock.
            auto& b = *it;
            if (b.prepared) unprepare_fn(device, &b.header, sizeof(b.header));
            b.prepared = false;
            b.header = {};
            const uint32_t frames = 128 / cfg.bytes;
            // Four 256-byte chunks per stream, with separate left/right halves.
            for (uint32_t block = 0; block < 4; ++block)
                for (uint32_t frame = 0; frame < frames; ++frame)
                    for (uint32_t ch = 0; ch < 2; ++ch)
                    {
                        const uint32_t src = addr + block * 1024 + ch * 128 + frame * cfg.bytes;
                        b.pcm[(block * frames + frame) * 2 + ch] = cfg.muted ? 0 : cfg.bytes == 2 ?
                            static_cast<int16_t>(guest_load_u16(src)) : static_cast<int16_t>(static_cast<int32_t>(guest_load_u32(src)) >> 16);
                    }
            b.header.lpData = reinterpret_cast<LPSTR>(b.pcm.data());
            b.header.dwBufferLength = 4 * frames * 2 * sizeof(int16_t);
            if (prepare_fn(device, &b.header, sizeof(b.header)) != MMSYSERR_NOERROR) return;
            b.prepared = true;
            write_fn(device, &b.header, sizeof(b.header));
        }
    };

    void clean_ring(uint32_t rb)
    {
        guest_store_u32(rb + UNK2, 100);
        guest_store_u32(rb + READ, 0);
        guest_store_u32(rb + WRITE, 0);
        guest_store_u32(rb + NOTIFY, 0);
        guest_store_u64(rb + NEXT_BLOCK, 0);
        for (uint32_t i = 0; i < 16; ++i)
        {
            const uint32_t e = rb + ENTRIES + i * ENTRY_SIZE;
            guest_store_u32(e, 0);
            guest_store_u64(e + BLOCK, 0);
            guest_store_u64(e + TIMESTAMP, 0);
        }
    }
    void send_event(KRsxAudio& a, uint32_t port, uint64_t block, uint64_t timestamp)
    {
        const auto& q = a.queues[port];
        if (q && !q->destroyed) event_queue_send(*q, SOURCES[port], port, block, timestamp);
    }
    void consume_period(KRsxAudio& a, uint32_t port, uint64_t scheduled_us, StereoSink& sink)
    {
        const uint32_t rb = a.shmem + CTRL + port * RING_SIZE;
        const int32_t max = static_cast<int32_t>(guest_load_u32(rb + MAX));
        const int32_t step = static_cast<int32_t>(guest_load_u32(rb + STEP));
        const int32_t read = static_cast<int32_t>(guest_load_u32(rb + READ));
        if (max <= 0 || max > 16 || step <= 0 || read < 0 || read >= max)
        {
            if (!a.invalid_ring_logged) log_line("rsxaudio: invalid ring port=%u read=%d max=%d notify_step=%d", port, read, max, step);
            a.invalid_ring_logged = true;
            return;
        }
        const uint32_t stamp = rb + ENTRIES + ((read + max - (max > 2) - 1) % max) * ENTRY_SIZE;
        guest_store_u64(stamp + TIMESTAMP, scheduled_us * 80); // PS3 80 MHz timebase.
        const uint32_t current = rb + ENTRIES + read * ENTRY_SIZE;
        const bool valid = (guest_load_u32(current) & 1) != 0;
        const uint32_t dma = guest_load_u32(valid ? current + DMA : rb + SILENCE);
        const uint32_t size = port == 0 ? 0x1000 : 0x400;
        if (port == 0 && dma >= DMA_BASE && uint64_t(dma - DMA_BASE) + size <= SHMEM_SIZE)
            sink.submit(a.shmem + dma - DMA_BASE, g_ports[port]);
        // lv1 disables underflow events. An empty ring plays its silence block without
        // retiring an entry or inventing a completion event.
        if (!valid) return;
        const uint32_t e = rb + ENTRIES + ((read + max - (max > 2)) % max) * ENTRY_SIZE;
        const uint64_t block = guest_load_u64(e + BLOCK), timestamp = guest_load_u64(e + TIMESTAMP);
        const uint32_t notify = (guest_load_u32(rb + NOTIFY) + 1) % step;
        guest_store_u32(current, 0);
        guest_store_u32(rb + NOTIFY, notify);
        guest_store_u32(rb + READ, (read + 1) % max);
        if (((max > 2) ^ notify) == 0) send_event(a, port, block, timestamp);
        if (++a.retired[port] == 1) log_line("rsxaudio: first DMA completion port=%u block=%llu read=%d", port, static_cast<unsigned long long>(block), read);
    }
    void audio_thread()
    {
        StereoSink sink;
        KLock lock(g_kernel);
        for (;;)
        {
            uint64_t wait_us = 1000000;
            bool serial_output = false;
            if (g_audio && g_audio->connected && g_audio->prepared && g_audio->running)
            {
                auto& a = *g_audio;
                const uint64_t now = system_time_us();
                for (uint32_t port = 0; port < 3; ++port)
                {
                    const auto& cfg = g_ports[port];
                    if (!cfg.enabled || !cfg.configured || !a.dma_enabled[port] || (port && cfg.use_serial_buffer))
                    {
                        a.period_divisor[port] = 0;
                        continue;
                    }
                    serial_output |= port == 0;
                    const uint64_t divisor = uint64_t(cfg.rate) * cfg.bytes * 2;
                    constexpr uint64_t numerator = 1024ull * 1000000;
                    if (a.period_divisor[port] != divisor)
                    {
                        a.period_divisor[port] = divisor;
                        a.period_index[port] = now * divisor / numerator + 1;
                    }
                    uint64_t due = a.period_index[port] * numerator / divisor;
                    if (due <= now)
                    {
                        consume_period(a, port, due, sink);
                        // Bound catch-up after host scheduling delays.
                        a.period_index[port] = std::max(a.period_index[port] + 1, now * divisor / numerator);
                        due = a.period_index[port] * numerator / divisor;
                    }
                    wait_us = std::min(wait_us, due > now ? due - now : 1);
                }
            }
            if (!serial_output) sink.stop();
            g_audio_cv.wait_for(lock, std::chrono::microseconds(wait_us));
        }
    }
    int64_t sys_rsxaudio_initialize(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        if (g_audio) return CELL_EINVAL;
        if (!ctx.r3.u32) return CELL_EFAULT;
        auto a = std::make_shared<KRsxAudio>();
        a->shmem = user_alloc(SHMEM_SIZE, 0x10000);
        if (!a->shmem) return CELL_ENOMEM;
        memset(g_base + a->shmem, 0, SHMEM_SIZE);
        guest_store_u32(a->shmem + CTRL + 0x6CC, 0x8000);
        guest_store_u32(a->shmem + CTRL + 0x6D0, 0xDEADBEEF);
        g_audio = a;
        guest_store_u32(ctx.r3.u32, kobj_add(0x60, a));
        if (!g_thread_started) { g_thread_started = true; std::thread(audio_thread).detach(); }
        return CELL_OK;
    }
    int64_t sys_rsxaudio_import_shared_memory(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto a = kobj<KRsxAudio>(ctx.r3.u32);
        if (!a) return CELL_ESRCH;
        if (!ctx.r4.u32) return CELL_EFAULT;
        guest_store_u64(ctx.r4.u32, a->shmem);
        return CELL_OK;
    }
    int64_t sys_rsxaudio_create_connection(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto a = kobj<KRsxAudio>(ctx.r3.u32);
        if (!a) return CELL_ESRCH;
        std::shared_ptr<KEventQueue> queues[3];
        for (uint32_t i = 0; i < 3; ++i)
        {
            queues[i] = find_event_queue(guest_load_u32(a->shmem + CTRL + EQ[i]));
            if (!queues[i] || queues[i]->destroyed) return CELL_ESRCH;
        }
        for (uint32_t port = 0; port < 3; ++port)
        {
            a->queues[port] = queues[port];
            const uint32_t rb = a->shmem + CTRL + port * RING_SIZE;
            guest_store_u32(rb + SILENCE, DMA_BASE + DMA_SILENCE);
            guest_store_u32(rb + UNK2, 100);
            for (uint32_t i = 0; i < 16; ++i)
                guest_store_u32(rb + ENTRIES + i * ENTRY_SIZE + DMA, DMA_BASE + REGIONS[port] + (port == 0 ? 0x1000 : 0x400) * i);
        }
        a->connected = true;
        return CELL_OK;
    }
    int64_t sys_rsxaudio_handle_ok(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        return kobj<KRsxAudio>(ctx.r3.u32) ? CELL_OK : CELL_ESRCH;
    }
    int64_t sys_rsxaudio_prepare_process(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto a = kobj<KRsxAudio>(ctx.r3.u32);
        if (!a) return CELL_ESRCH;
        if (a->prepared) return -1;
        a->prepared = true;
        g_audio_cv.notify_one();
        return CELL_OK;
    }
    int64_t sys_rsxaudio_start_process(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto a = kobj<KRsxAudio>(ctx.r3.u32);
        if (!a) return CELL_ESRCH;
        for (uint32_t port = 0; port < 3; ++port)
        {
            const uint32_t rb = a->shmem + CTRL + port * RING_SIZE;
            a->dma_enabled[port] = guest_load_u32(rb + ACTIVE) != 0;
            if (a->dma_enabled[port]) clean_ring(rb);
            guest_store_u64(a->shmem + CTRL + CHANNEL_UF + port * 0x100, 0);
            guest_store_u32(a->shmem + CTRL + CHANNEL_UF + port * 0x100 + 8, 0);
            a->period_divisor[port] = 0;
            if (a->dma_enabled[port]) send_event(*a, port, 0, 0);
        }
        a->running = true;
        g_audio_cv.notify_one();
        log_line("rsxaudio: started with firmware ring notifications and DMA retirement");
        return CELL_OK;
    }
    int64_t sys_rsxaudio_stop_process(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto a = kobj<KRsxAudio>(ctx.r3.u32);
        if (!a) return CELL_ESRCH;
        a->running = false;
        for (uint32_t port = 0; port < 3; ++port)
        {
            a->dma_enabled[port] = false;
            const uint32_t rb = a->shmem + CTRL + port * RING_SIZE;
            if (guest_load_u32(rb + ACTIVE)) clean_ring(rb);
        }
        g_ports[0].enabled = false;
        g_ports[0].muted = true;
        for (uint32_t port = 1; port < 3; ++port) if (!g_ports[port].use_serial_buffer) g_ports[port].enabled = false;
        g_ports[2].muted = true;
        g_audio_cv.notify_one();
        return CELL_OK;
    }
    int64_t sys_rsxaudio_close_connection(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto a = kobj<KRsxAudio>(ctx.r3.u32);
        if (!a) return CELL_ESRCH;
        a->connected = a->prepared = a->running = false;
        for (auto& q : a->queues) q.reset();
        g_audio_cv.notify_one();
        return CELL_OK;
    }
    int64_t sys_rsxaudio_get_dma_param(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        if (!kobj<KRsxAudio>(ctx.r3.u32)) return CELL_ESRCH;
        if (!ctx.r5.u32) return CELL_EFAULT;
        if (ctx.r4.u32 == 0) guest_store_u64(ctx.r5.u32, DMA_BASE);
        else if (ctx.r4.u32 == 1) guest_store_u64(ctx.r5.u32, 1);
        else return CELL_EINVAL;
        return CELL_OK;
    }
    int64_t sys_rsxaudio_finalize(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto a = kobj<KRsxAudio>(ctx.r3.u32);
        if (!a) return CELL_ESRCH;
        a->running = a->prepared = a->connected = false;
        for (auto& q : a->queues) q.reset();
        kobj_remove(ctx.r3.u32);
        if (g_audio == a) g_audio.reset();
        g_audio_cv.notify_one();
        return CELL_OK;
    }
}

// UART hardware state is independent of DMA start. Callers must not hold g_kernel.
void rsxaudio_configure_port(uint32_t port, uint32_t sample_rate, uint32_t sample_bytes, bool use_serial_buffer)
{
    if (port >= 3 || !sample_rate || (sample_bytes != 2 && sample_bytes != 4)) return;
    KLock lock(g_kernel);
    g_ports[port].rate = sample_rate;
    g_ports[port].bytes = sample_bytes;
    g_ports[port].use_serial_buffer = port && use_serial_buffer;
    g_ports[port].configured = true;
    g_audio_cv.notify_one();
}
void rsxaudio_set_port_active(uint32_t port, bool active)
{
    if (port >= 3) return;
    KLock lock(g_kernel);
    g_ports[port].enabled = active;
    if (g_audio) g_audio->period_divisor[port] = 0;
    g_audio_cv.notify_one();
}
void rsxaudio_set_port_muted(uint32_t port, bool muted)
{
    if (port >= 3) return;
    KLock lock(g_kernel);
    g_ports[port].muted = muted;
}
void rsxaudio_reset_hardware()
{
    KLock lock(g_kernel);
    g_ports = {{{48000, 2, false, true, false}, {44100, 2, false, true, true}, {44100, 2, false, true, true}}};
    if (g_audio) for (auto& divisor : g_audio->period_divisor) divisor = 0;
    g_audio_cv.notify_one();
}
void register_rsxaudio_syscalls()
{
    register_syscall(650, sys_rsxaudio_initialize);
    register_syscall(651, sys_rsxaudio_finalize);
    register_syscall(652, sys_rsxaudio_import_shared_memory);
    register_syscall(653, sys_rsxaudio_handle_ok);
    register_syscall(654, sys_rsxaudio_create_connection);
    register_syscall(655, sys_rsxaudio_close_connection);
    register_syscall(656, sys_rsxaudio_prepare_process);
    register_syscall(657, sys_rsxaudio_start_process);
    register_syscall(658, sys_rsxaudio_stop_process);
    register_syscall(659, sys_rsxaudio_get_dma_param);
}
