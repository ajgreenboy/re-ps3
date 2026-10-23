// Minimal Cell SPU runtime for the XMB boot path. The interpreter implements the scalar/vector
// instructions emitted by the embedded firmware SPU program and the LV2 channels it uses.
#include "kernel.h"
#include "k_event.h"
#include "spu_estimates.h"

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <immintrin.h>

namespace
{
    constexpr uint32_t kLsSize = 0x40000;
    constexpr uint32_t kImageKernel = 1, kImageUser = 0;
    constexpr uint32_t kSegCopy = 1, kSegFill = 2, kSegInfo = 4;
    constexpr uint32_t kStopGroupExit = 0x101, kStopThreadExit = 0x102;
    constexpr uint32_t kStopReceiveEvent = 0x110, kStopTryReceiveEvent = 0x111;
    constexpr uint64_t kEventSource = 0xFFFFFFFF53505501ull;
    constexpr uint32_t kEventLockLost = 0x400;
    std::mutex g_reservation_lock;

    uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
    uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
    void put_be32(uint8_t* p, uint32_t x) { p[0] = uint8_t(x >> 24); p[1] = uint8_t(x >> 16); p[2] = uint8_t(x >> 8); p[3] = uint8_t(x); }
    uint32_t sign_extend(uint32_t v, unsigned bits) { const uint32_t m = 1u << (bits - 1); return (v ^ m) - m; }
    void setup_spu_floats()
    {
        // Same host rounding and denormal mode as RPCS3's SPU interpreter.
        _mm_setcsr((_mm_getcsr() & ~_MM_ROUND_MASK) | _MM_ROUND_TOWARD_ZERO | _MM_FLUSH_ZERO_ON | _MM_DENORMALS_ZERO_ON);
    }

    struct Segment { uint32_t type, ls, size, source; };
    struct KSpuImage : KObject { uint32_t entry = 0; std::vector<Segment> segments; };
    struct KSpuGroup;
    struct KSpuThread : KObject
    {
        std::weak_ptr<KSpuGroup> group;
        uint32_t id = 0, spu_num = 0, entry = 0;
        uint64_t args[4]{};
        std::string name;
        std::array<uint8_t, kLsSize> ls{};
        std::array<std::array<uint32_t, 4>, 128> r{}; // lane 3 is the scalar/preferred word
        std::array<uint32_t, 4> fpscr{};
        uint32_t pc = 0, event_mask = 0, event_stat = 0, dec = 0;
        uint32_t mfc_lsa = 0, mfc_eah = 0, mfc_eal = 0, mfc_size = 0, mfc_tag = 0, mfc_tagmask = 0;
        uint32_t atomic_status = 0;
        uint32_t snr[2]{};
        uint32_t reserved_line = 0;
        std::array<uint8_t, 128> reserved{};
        std::deque<uint32_t> in_mbox, out_mbox;
        std::shared_ptr<KSpuImage> image;
        bool exited = false, group_exit = false;
        uint32_t exit_status = 0;
        uint32_t taskset = 0, task_id = 0;
        unsigned dma_reports = 0;
        unsigned error_reports = 0;
        unsigned stop_reports = 0;
        bool captured_stop = false;
        std::atomic<bool> task_signal{false};
        std::atomic<bool> stop_requested{false};
        std::atomic<uint32_t> observed_pc{0}, task_state{0};
        std::atomic<uint64_t> instructions{0};
        uint64_t executed_instructions=0; // written only by this SPU's host thread
    };
    struct KSpuGroup : KObject
    {
        uint32_t expected = 0;
        bool started = false, destroyed = false;
        uint32_t join_cause = 2, status = 0;
        std::vector<std::shared_ptr<KSpuThread>> threads;
        PpuThread* joiner = nullptr;
        std::array<uint32_t, 3> event_queues{}; // RUN, EXCEPTION, SYSTEM_MODULE
    };
    struct QueueBinding { uint32_t queue = 0, spuq = 0; };
    std::unordered_map<uint32_t, QueueBinding> g_queue_bindings; // SPU thread id -> queue binding
    std::unordered_map<uint32_t, std::shared_ptr<KSpuThread>> g_spu_threads;
    std::unordered_map<uint32_t, std::array<uint32_t, 64>> g_user_event_queues;
    std::unordered_map<uint32_t, std::vector<std::shared_ptr<KSpuThread>>> g_spurs_tasks;
    std::unordered_map<uint32_t, std::array<uint32_t, 64>> g_task_event_queues;
    std::shared_ptr<KSpuThread> spu_thread(uint32_t id)
    {
        auto it = g_spu_threads.find(id);
        return it == g_spu_threads.end() ? nullptr : it->second;
    }

    bool group_done(const KSpuGroup& g) { for (auto& t : g.threads) if (!t->exited) return false; return !g.threads.empty(); }
    uint32_t ls_word(const KSpuThread& t, uint32_t a) { return be32(t.ls.data() + (a & (kLsSize - 4))); }
    void ls_store_word(KSpuThread& t, uint32_t a, uint32_t v) { put_be32(t.ls.data() + (a & (kLsSize - 4)), v); }
    void load_quad(KSpuThread& t, uint32_t addr, std::array<uint32_t, 4>& out)
    {
        for (int i = 0; i < 4; ++i) out[3 - i] = ls_word(t, addr + i * 4);
    }
    void store_quad(KSpuThread& t, uint32_t addr, const std::array<uint32_t, 4>& value)
    {
        for (int i = 0; i < 4; ++i) ls_store_word(t, addr + i * 4, value[3 - i]);
    }
    void write_thread_loader(KSpuThread& t)
    {
        // LV2 installs this loader before deploying image segments. Firmware may
        // branch back to it to reload arguments or clear registers/local storage.
        t.ls.fill(0);
        uint32_t pc = 0;
        const auto emit = [&](uint32_t instruction) { ls_store_word(t, pc, instruction); pc += 4; };
        const auto rr = [](uint32_t op, uint32_t rt, uint32_t ra, uint32_t rb = 0) {
            return (op << 21) | (rb << 14) | (ra << 7) | rt;
        };
        const auto ri10 = [](uint32_t op, uint32_t rt, uint32_t ra, int32_t imm) {
            return (op << 24) | ((uint32_t(imm) & 0x3ff) << 14) | (ra << 7) | rt;
        };
        for (uint32_t i = 0; i < 4; ++i)
        {
            emit(0x42000000 | ((0x30 + i * 0x10) << 7)); // ILA r0, argument
            emit(ri10(0x34, 3 + i, 0, 0)); // LQD
        }
        emit(rr(0x241, 0, 0));
        emit(rr(0x10d, 0, 28));
        emit(rr(0x201, 0, 0));
        emit(0x32000000); // BR 0
        for (uint32_t i = 0; i < 4; ++i)
        {
            put_be32(t.ls.data() + 0x30 + i * 0x10, uint32_t(t.args[i] >> 32));
            put_be32(t.ls.data() + 0x34 + i * 0x10, uint32_t(t.args[i]));
        }
        pc = 0x70;
        for (uint32_t reg = 25; reg < 121; reg += 2)
        {
            emit(rr(0x241, reg, reg, reg));
            emit(rr(0x1f0, reg + 1, 0));
        }
        emit(0x42000000 | (0x300 << 7) | 127);
        emit(rr(0x1f0, 121, 0));
        emit(0x42000000 | (0x3ff00 << 7) | 1);
        emit(rr(0x1f0, 122, 0));
        emit(rr(0x241, 123, 123, 123));
        emit(rr(0x1f0, 124, 0));
        emit(rr(0x241, 125, 125, 125));
        emit(rr(0x1f0, 126, 0));
        emit(rr(0x040, 127, 127, 1));
        emit(0x10000000 | ((0x220 / 4) << 7) | (0x54 / 4)); // HBRA
        emit(rr(0x201, 0, 0));
        emit(rr(0x001, 0, 0));
        emit(ri10(0x4c, 1, 127, 0));
        for (uint32_t offset = 0x300; offset < 0x400; offset += 16)
            emit(ri10(0x24, 0, 127, offset / 16));
        emit(ri10(0x1c, 127, 127, -0x100));
        emit(0x21000000 | ((uint32_t(-0x48 / 4) & 0xffff) << 7) | 1);
        emit(rr(0x241, 127, 127, 127));
        emit(0x1004);
    }
    void capture_spu_stop(KSpuThread& t)
    {
        if (t.captured_stop || !getenv("XMBRT_CAPTURE_SPU_STATE")) return;
        t.captured_stop = true;
        char path[64];
        snprintf(path,sizeof(path),"spu-%08X-ls.bin",t.id);
        if(FILE* file=fopen(path,"wb")){fwrite(t.ls.data(),1,t.ls.size(),file);fclose(file);}
        snprintf(path,sizeof(path),"spu-%08X-regs.txt",t.id);
        if(FILE* file=fopen(path,"w"))
        {
            fprintf(file,"pc %05X\n",t.pc);
            for(unsigned reg=0;reg<128;++reg)fprintf(file,"r%u %08X %08X %08X %08X\n",reg,t.r[reg][3],t.r[reg][2],t.r[reg][1],t.r[reg][0]);
            fclose(file);
        }
    }

    uint32_t channel_count(KSpuThread& t, uint32_t ch)
    {
        switch (ch)
        {
        case 0: return (t.event_stat & t.event_mask) ? 1 : 0;
        case 3: return t.snr[0] ? 1 : 0;
        case 4: return t.snr[1] ? 1 : 0;
        case 8: return t.dec ? 1 : 0;
        case 11: return 1;
        case 13: return 1;
        case 16: case 17: case 18: case 19: case 20: case 22: case 23: return 1;
        case 21: return 16; // synchronous MFC command queue is always available
        case 28: return t.out_mbox.empty() ? 4 : 3;
        case 29: return uint32_t(t.in_mbox.size());
        case 30: return t.out_mbox.empty() ? 1 : 0;
        case 24: return 1; // MFC tag status, synchronous DMA
        case 27: return 1;
        default: return 0;
        }
    }
    uint32_t read_channel(KSpuThread& t, uint32_t ch)
    {
        switch (ch)
        {
        case 0: { const uint32_t v = t.event_stat & t.event_mask; t.event_stat &= ~v; return v; }
        case 3: case 4: { uint32_t& v = t.snr[ch - 3]; const uint32_t r = v; v = 0; return r; }
        case 8: return t.dec;
        case 11: return t.event_mask;
        case 13: return 1;
        case 15: return t.pc;
        case 29:
            if (t.in_mbox.empty()) return 0;
            { const uint32_t v = t.in_mbox.front(); t.in_mbox.pop_front(); return v; }
        case 24: return t.mfc_tagmask;
        case 27: return t.atomic_status;
        default: return 0;
        }
    }

    void dma(KSpuThread& t, uint32_t cmd)
    {
        const uint64_t ea64 = (uint64_t(t.mfc_eah) << 32) | t.mfc_eal;
        const uint32_t ea = uint32_t(ea64);
        uint32_t n = std::min(t.mfc_size & 0x7ffff, 0x40000u);
        const uint32_t op = cmd & 0xff;
        if (t.taskset && getenv("XMBRT_SPU_TASK_TRACE") && t.dma_reports++ < 16)
            log_line("spurs DMA: taskset=%08X pc=%05X cmd=%02X local=%05X external=%08X size=%X", t.taskset, t.pc, op, t.mfc_lsa, ea, n);
        if (op == 0xd0 || op == 0xb4 || op == 0xb0 || op == 0xb8)
        {
            const uint32_t line = ea & ~0x7fu, local = t.mfc_lsa & 0x3ff80;
            std::lock_guard<std::mutex> guard(g_reservation_lock);
            if (op == 0xd0)
            {
                memcpy(t.reserved.data(), g_base + line, 128);
                memcpy(t.ls.data() + local, t.reserved.data(), 128);
                t.reserved_line = line;
                t.atomic_status = 4;
            }
            else
            {
                const bool conditional = op == 0xb4;
                const bool success = !conditional || (line == t.reserved_line && memcmp(g_base + line, t.reserved.data(), 128) == 0);
                if (success) memcpy(g_base + line, t.ls.data() + local, 128);
                else t.event_stat |= kEventLockLost;
                t.atomic_status = conditional ? !success : 2;
                t.reserved_line = ~0u;
            }
            return;
        }
        const uint32_t transfer = op & ~0x1fu;
        if (transfer == 0x20 || transfer == 0x40)
        {
            const bool get = transfer == 0x40;
            auto copy = [&](uint32_t local, uint32_t external, uint32_t size)
            {
                if (uint64_t(local) + size > kLsSize || uint64_t(external) + size > 0x100000000ull) return;
                if (get) memcpy(t.ls.data() + local, g_base + external, size);
                else memcpy(g_base + external, t.ls.data() + local, size);
            };
            if (op & 4)
            {
                uint32_t local = t.mfc_lsa & 0x3fff0;
                for (uint32_t offset = 0; offset + 8 <= n; offset += 8)
                {
                    const uint32_t list = ((ea & 0x3fff8) + offset) & 0x3fff8;
                    const uint32_t size = be16(t.ls.data() + list + 2) & 0x7fff;
                    const uint32_t external = ls_word(t, list + 4);
                    copy(local + (external & 15), external, size);
                    local += (size + 15) & ~15u;
                    if (t.ls[list] & 0x80)
                        log_line("spu: DMA list stall requires acknowledgement pc=%05X tag=%u", t.pc, t.mfc_tag);
                }
            }
            else copy(t.mfc_lsa, ea, n);
        }
        else if (op != 0xc0 && op != 0xc8 && op != 0xcc)
        {
            log_line("spu: unsupported DMA command %02X pc=%05X", op, t.pc);
        }
        t.mfc_tagmask |= 1u << (t.mfc_tag & 31);
    }

    void write_channel(KSpuThread& t, uint32_t ch, uint32_t v)
    {
        switch (ch)
        {
        case 1: t.event_mask = v; break;
        case 2: t.event_stat &= ~v; break;
        case 7: t.dec = v; break;
        case 16: t.mfc_lsa = v; break;
        case 17: t.mfc_eah = v; break;
        case 18: t.mfc_eal = v; break;
        case 19: t.mfc_size = v; break;
        case 20: t.mfc_tag = v & 31; break;
        case 21: dma(t, v); break;
        case 22: t.mfc_tagmask = v; break;
        case 23: break; // all requested tags have completed synchronously
        case 28: t.out_mbox.push_back(v); break;
        case 30:
        {
            const uint32_t code = v >> 24;
            if (code < 128)
            {
                const uint32_t data = t.out_mbox.empty() ? 0 : t.out_mbox.front();
                if (!t.out_mbox.empty()) t.out_mbox.pop_front();
                KLock lock(g_kernel);
                auto it = g_user_event_queues.find(t.id);
                const uint32_t queue = t.taskset ? g_task_event_queues[t.taskset][code & 63] :
                    it != g_user_event_queues.end() ? it->second[code & 63] : 0;
                if (queue)
                {
                    auto q = find_event_queue(queue);
                    if (q)
                    {
                        const uint32_t spup = code & 63;
                        const uint64_t d2 = (uint64_t(spup) << 32) | (v & 0x00ffffff);
                        event_queue_send(*q, kEventSource, t.id, d2, data);
                        static std::atomic<unsigned> reported{0};
                        if (reported.fetch_add(1) < 4)
                            log_line("spu: firmware sent event id=%08X port=%u data=%08X", t.id, spup, data);
                        if (code < 64) t.in_mbox.push_back(CELL_OK);
                    }
                    else if (code < 64) t.in_mbox.push_back(CELL_ESRCH);
                }
                else if (code < 64) t.in_mbox.push_back(CELL_EINVAL);
            }
            break;
        }
        default: break;
        }
    }

    bool receive_event(KSpuThread& t, bool wait)
    {
        if (!t.in_mbox.empty()) { t.in_mbox.push_back(CELL_EBUSY); return true; }
        if(t.out_mbox.empty()){t.in_mbox.push_back(CELL_EINVAL);return true;}
        const uint32_t spuq=t.out_mbox.front();t.out_mbox.pop_front();
        for (;;)
        {
            if (t.stop_requested.load(std::memory_order_relaxed)) return false;
            std::array<uint64_t, 4> e{};
            bool found = false;
            {
                KLock lock(g_kernel);
                auto it = g_queue_bindings.find(t.id);
                if (it == g_queue_bindings.end() || it->second.spuq != spuq) { t.in_mbox.push_back(CELL_EINVAL); return true; }
                auto q = find_event_queue(it->second.queue);
                if (!q || q->destroyed) { t.in_mbox.push_back(CELL_EINVAL); return true; }
                if (!q->events.empty()) { e = q->events.front(); q->events.pop_front(); found = true; }
            }
            if (found)
            {
                t.in_mbox.push_back(CELL_OK);
                for (int i = 1; i < 4; ++i) t.in_mbox.push_back(uint32_t(e[i]));
                return true;
            }
            if (!wait) { t.in_mbox.push_back(CELL_EBUSY); return true; }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    bool execute_one(KSpuThread& t, uint32_t& stop_code)
    {
        if (t.stop_requested.load(std::memory_order_relaxed)) { stop_code=0x3fff; return false; }
        // Publish diagnostics in batches; a locked increment on every emulated
        // instruction needlessly consumes the firmware's frame budget.
        if((++t.executed_instructions&1023)==0)
        {
            t.observed_pc.store(t.pc,std::memory_order_relaxed);
            t.instructions.store(t.executed_instructions,std::memory_order_relaxed);
        }
        if (t.taskset && t.pc == 0xa70)
        {
            const uint32_t call = t.r[3][3] & 15;
            if (call != 3)
                log_line("spurs: task syscall=%u arg=%08X lr=%05X taskset=%08X task=%u", call, t.r[4][3], t.r[0][3], t.taskset, t.task_id);
            if (!call) { stop_code = kStopThreadExit; return false; }
            if (call == 2)
            {
                t.task_state = 1;
                while (!t.task_signal.exchange(false)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                t.task_state = 0;
            }
            else if (call == 1) std::this_thread::yield();
            else if (call != 3)
            {
                log_line("spurs: unsupported task syscall %u taskset=%08X task=%u", call, t.taskset, t.task_id);
                stop_code = 0x3fff;
                return false;
            }
            t.r[3].fill(0);
            t.pc = t.r[0][3] & 0x3fffc;
            return true;
        }
        if (t.pc >= kLsSize - 3) { stop_code = 0x3fff; return false; }
        const uint32_t old_pc = t.pc, ins = ls_word(t, old_pc), d = ins >> 21;
        const uint32_t rt = ins & 127, ra = (ins >> 7) & 127, rb = (ins >> 14) & 127;
        const uint32_t si10 = sign_extend((ins >> 14) & 0x3ff, 10), i16 = (ins >> 7) & 0xffff;
        const uint32_t si16 = sign_extend(i16, 16);
        auto& a = t.r[ra]; auto& b = t.r[rb]; auto& out = t.r[rt];
        uint32_t next = (old_pc + 4) & 0x3fffc;
        auto broadcast = [&](uint32_t v) { out.fill(v); };
        auto branchrel = [&](int32_t disp) { next = (old_pc + uint32_t(disp * 4)) & 0x3fffc; };
        if(d==0x10d){write_channel(t,ra,t.r[rt][3]);t.pc=next;return true;}
        if (d==0x128||d==0x129||d==0x12a||d==0x12b)
        {
            const uint16_t v=uint16_t(t.r[rt][3]);
            if((d==0x128&&t.r[rt][3]==0)||(d==0x129&&t.r[rt][3]!=0)||(d==0x12a&&v==0)||(d==0x12b&&v!=0))next=t.r[ra][3]&0x3fffc;
            t.pc=next;return true;
        }
        if(d==0x1a8)
        {
            if (t.taskset && (t.r[3][3] & 0xffff0000) == 0x80410000 && t.error_reports++ < 16)
                log_line("spurs: library error pc=%05X target=%05X result=%08X", old_pc, t.r[ra][3], t.r[3][3]);
            t.pc=t.r[ra][3]&0x3fffc;
            return true;
        }
        if(d==0x1a9){const uint32_t target=t.r[ra][3]&0x3fffc;out.fill(0);out[3]=next;t.pc=target;return true;}
        if (d >= 0x400 && d <= 0x47f)
        {
            const uint32_t dst=(ins>>21)&127; const auto c=t.r[ins&127];
            const auto left=a, right=b;
            for(int i=0;i<4;i++)t.r[dst][i]=(c[i]&right[i])|(~c[i]&left[i]);
            t.pc=next;return true;
        } // SELB
        if (d >= 0x580 && d <= 0x5ff)
        {
            const uint32_t dst=(ins>>21)&127; const auto c=t.r[ins&127]; uint8_t ab[32]{};
            for(int i=0;i<4;i++)for(int j=0;j<4;j++){ab[i*4+j]=uint8_t(a[3-i]>>(24-j*8));ab[16+i*4+j]=uint8_t(b[3-i]>>(24-j*8));}
            for(int i=0;i<4;i++)
            {
                uint32_t v=0;
                for(int j=0;j<4;j++)
                {
                    const uint32_t sh=24-j*8;const uint8_t ctrl=uint8_t(c[3-i]>>sh);
                    const uint8_t value=ctrl>=0xe0?0x80:ctrl>=0xc0?0xff:ctrl>=0x80?0:ab[ctrl&31];
                    v|=uint32_t(value)<<sh;
                }
                t.r[dst][3-i]=v;
            }
            t.pc=next;return true;
        } // SHUFB
        if (d >= 0x600 && d < 0x680) // MPYA
        {
            const uint32_t dst=(ins>>21)&127;
            const auto left=a,right=b,addend=t.r[rt];
            for(int i=0;i<4;++i)t.r[dst][i]=uint32_t(int32_t(int16_t(left[i]))*int32_t(int16_t(right[i])))+addend[i];
            t.pc=next;return true;
        }
        if (d >= 0x680)
        {
            const uint32_t dst = (ins >> 21) & 127;
            const __m128 exp = _mm_castsi128_ps(_mm_set1_epi32(0x7f800000));
            const __m128 x = _mm_castsi128_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a.data())));
            const __m128 y = _mm_castsi128_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(b.data())));
            const __m128 z = _mm_castsi128_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(t.r[rt].data())));
            const __m128 left = _mm_and_ps(x, _mm_cmpneq_ps(_mm_and_ps(x, exp), exp));
            const __m128 right = _mm_and_ps(y, _mm_cmpneq_ps(_mm_and_ps(y, exp), exp));
            const __m128 product = _mm_mul_ps(left, right);
            const __m128 result = d < 0x700 ? _mm_sub_ps(z, product) : d < 0x780 ? _mm_add_ps(product, z) : _mm_sub_ps(product, z);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(t.r[dst].data()), _mm_castps_si128(result));
            t.pc = next;
            return true;
        }
        if (d >= 0x3b0 && d <= 0x3b7)
        {
            const unsigned operation = (d - 0x3b0) >> 1, imm = (ins >> 14) & 255;
            const __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a.data()));
            __m128i result;
            if (operation < 2)
            {
                const __m128 scaled = _mm_mul_ps(_mm_castsi128_ps(input), _mm_set1_ps(std::ldexp(1.f, 173 - int(imm))));
                if (!operation)
                    result = _mm_xor_si128(_mm_cvttps_epi32(scaled), _mm_castps_si128(_mm_cmpge_ps(scaled, _mm_set1_ps(2147483648.f))));
                else
                {
                    const __m128 positive = _mm_max_ps(scaled, _mm_setzero_ps());
                    const __m128 high = _mm_and_ps(_mm_sub_ps(positive, _mm_set1_ps(2147483648.f)), _mm_cmpge_ps(positive, _mm_set1_ps(2147483648.f)));
                    result = _mm_or_si128(_mm_or_si128(_mm_cvttps_epi32(positive), _mm_cvttps_epi32(high)), _mm_castps_si128(_mm_cmpge_ps(positive, _mm_set1_ps(4294967296.f))));
                }
            }
            else
            {
                const __m128 value = operation == 2 ? _mm_cvtepi32_ps(input) :
                    _mm_add_ps(_mm_cvtepi32_ps(_mm_and_si128(input, _mm_set1_epi32(0x7fffffff))),
                        _mm_and_ps(_mm_castsi128_ps(_mm_srai_epi32(input, 31)), _mm_set1_ps(2147483648.f)));
                result = _mm_castps_si128(_mm_mul_ps(value, _mm_set1_ps(std::ldexp(1.f, int(imm) - 155))));
            }
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out.data()), result);
            t.pc = next;
            return true;
        }
        if (d == 0x2c4 || d == 0x2c5 || d == 0x2c6 || d == 0x2c2 || d == 0x2ca || d == 0x3c2 || d == 0x3ca || d == 0x3d4)
        {
            const __m128 x = _mm_castsi128_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a.data())));
            const __m128 y = _mm_castsi128_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(b.data())));
            const __m128 exp = _mm_castsi128_ps(_mm_set1_epi32(0x7f800000));
            __m128 result;
            if (d == 0x2c4) result = _mm_add_ps(x, y);
            else if (d == 0x2c5) result = _mm_sub_ps(x, y);
            else if (d == 0x3c2) result = _mm_cmpeq_ps(x, y);
            else if (d == 0x3ca)
            {
                const __m128 magnitude=_mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));
                result=_mm_cmpeq_ps(_mm_and_ps(x,magnitude),_mm_and_ps(y,magnitude));
            }
            else if (d == 0x2c6)
            {
                const __m128 denorm = _mm_or_ps(_mm_cmpeq_ps(_mm_setzero_ps(), _mm_and_ps(exp, x)), _mm_cmpeq_ps(_mm_setzero_ps(), _mm_and_ps(exp, y)));
                const __m128 product = _mm_mul_ps(x, y);
                const __m128 flushed = _mm_andnot_ps(_mm_or_ps(denorm, _mm_cmpeq_ps(_mm_setzero_ps(), _mm_and_ps(exp, product))), product);
                const __m128 extended = _mm_cmpeq_ps(_mm_and_ps(product, exp), exp);
                const __m128 sign = _mm_castsi128_ps(_mm_set1_epi32(int(0x80000000)));
                const __m128 fixed = _mm_andnot_ps(denorm, _mm_or_ps(_mm_xor_ps(_mm_and_ps(x, sign), _mm_and_ps(y, sign)), _mm_andnot_ps(sign, product)));
                result = _mm_or_ps(_mm_andnot_ps(extended, flushed), _mm_and_ps(extended, fixed));
            }
            else if (d == 0x3d4)
            {
                const __m128i yi = _mm_castps_si128(y), xi = _mm_castps_si128(x);
                const __m128 base = _mm_castsi128_ps(_mm_or_si128(_mm_and_si128(yi, _mm_set1_epi32(0x007ffc00)), _mm_set1_epi32(0x3f800000)));
                const __m128 step = _mm_mul_ps(_mm_cvtepi32_ps(_mm_and_si128(yi, _mm_set1_epi32(0x3ff))), _mm_set1_ps(0x1p-13f));
                const __m128 fraction = _mm_mul_ps(_mm_cvtepi32_ps(_mm_and_si128(xi, _mm_set1_epi32(0x7ffff))), _mm_set1_ps(0x1p-19f));
                const __m128 se = _mm_castsi128_ps(_mm_set1_epi32(int(0xff800000)));
                result = _mm_or_ps(_mm_and_ps(se, y), _mm_andnot_ps(se, _mm_sub_ps(base, _mm_mul_ps(step, fraction))));
            }
            else
            {
                const __m128 xf = _mm_andnot_ps(_mm_cmpeq_ps(_mm_setzero_ps(), _mm_and_ps(x, exp)), x);
                const __m128 yf = _mm_andnot_ps(_mm_cmpeq_ps(_mm_setzero_ps(), _mm_and_ps(y, exp)), y);
                if (d == 0x2c2)
                {
                    const __m128 lowered = _mm_castsi128_ps(_mm_set1_epi32(0x00800000));
                    result = _mm_cmpgt_ps(_mm_andnot_ps(_mm_and_ps(_mm_cmpunord_ps(xf, _mm_setzero_ps()), lowered), xf),
                        _mm_andnot_ps(_mm_and_ps(_mm_cmpunord_ps(yf, _mm_setzero_ps()), lowered), yf));
                }
                else
                {
                    const __m128 magnitude = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));
                    result = _mm_or_ps(_mm_cmpgt_ps(_mm_and_ps(xf, magnitude), _mm_and_ps(yf, magnitude)),
                        _mm_andnot_ps(_mm_cmpunord_ps(y, _mm_setzero_ps()), _mm_cmpunord_ps(x, _mm_setzero_ps())));
                }
            }
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out.data()), _mm_castps_si128(result));
            t.pc = next;
            return true;
        }
        if (d >= 0x80 && d <= 0x9f) { t.pc=next; return true; } // HBRA/HBRR branch hints
        if (d >= 0x204 && d <= 0x207) { broadcast(si16);t.pc=next;return true; } // IL
        if (d >= 0x208 && d <= 0x20b) { broadcast(i16<<16);t.pc=next;return true; } // ILHU
        if (d >= 0x20c && d <= 0x20f) { broadcast(uint16_t(i16)*0x10001u);t.pc=next;return true; } // ILH
        if (d >= 0x304 && d <= 0x307) { for(int i=0;i<4;i++)out[i]|=i16;t.pc=next;return true; } // IOHL
        if (d >= 0x210 && d <= 0x21f) { broadcast((ins >> 7) & 0x3ffff); t.pc=next; return true; } // ILA
        if (d >= 0x120 && d <= 0x127) { const uint32_t p=(t.r[ra][3]+si10*16)&0x3fff0;store_quad(t,p,t.r[rt]);t.pc=next;return true; }
        if (d >= 0x1a0 && d <= 0x1a7) { const uint32_t p=(t.r[ra][3]+si10*16)&0x3fff0;load_quad(t,p,out);t.pc=next;return true; }
        if ((d >= 0x78 && d <= 0x7f) || (d >= 0x58 && d <= 0x5f))
        {
            const uint32_t shift_op = d | 0x20;
            const auto counts = b;
            const auto input = a;
            for (int i = 0; i < 4; ++i)
            {
                const uint32_t sh = (d & 0x20) ? (ins >> 14) & 127 : counts[i];
                if (shift_op <= 0x7b)
                {
                    const uint32_t n = shift_op == 0x78 ? sh & 31 : shift_op == 0x7b ? sh & 63 : (0 - sh) & 63;
                    out[i] = shift_op == 0x78 ? (input[i] << n) | (input[i] >> ((32 - n) & 31)) :
                        shift_op == 0x7b ? (n < 32 ? input[i] << n : 0) :
                        shift_op == 0x7a ? uint32_t(int32_t(input[i]) >> std::min(n, 31u)) :
                        (n < 32 ? input[i] >> n : 0);
                }
                else
                {
                    auto shift = [&](uint16_t v, uint32_t count) -> uint16_t
                    {
                        const uint32_t n = shift_op == 0x7c ? count & 15 : shift_op == 0x7f ? count & 31 : (0 - count) & 31;
                        if (shift_op == 0x7c) return uint16_t((uint32_t(v) << n) | (v >> ((16 - n) & 15)));
                        if (shift_op == 0x7f) return n < 16 ? uint16_t(uint32_t(v) << n) : 0;
                        if (shift_op == 0x7e) return uint16_t(int16_t(v) >> std::min(n, 15u));
                        return n < 16 ? uint16_t(v >> n) : 0;
                    };
                    out[i] = uint32_t(shift(uint16_t(input[i] >> 16), (d & 0x20) ? sh : sh >> 16)) << 16 | shift(uint16_t(input[i]), sh);
                }
            }
            t.pc = next;
            return true;
        }
        // Quadword operations use architectural byte order (the preferred word
        // is at the left). Copy the input first because RT may alias RA or RB.
        const uint32_t quad = d & ~0x20u;
        if (quad >= 0x1d4 && quad <= 0x1d7)
        {
            const uint32_t width = 1u << (quad - 0x1d4);
            const uint32_t offset = (a[3] + ((d & 0x20) ? (ins >> 14) & 127 : b[3])) & (16 - width);
            out.fill(0);
            for (uint32_t byte = 0; byte < 16; ++byte)
            {
                const uint32_t control = byte >= offset && byte < offset + width ?
                    (width < 4 ? 4 - width : 0) + byte - offset : byte + 16;
                out[3 - byte / 4] |= control << (24 - (byte % 4) * 8);
            }
            t.pc = next;
            return true;
        }
        if (quad == 0x1cc || quad == 0x1cd || quad == 0x1cf ||
            quad == 0x1d8 || quad == 0x1d9 || quad == 0x1db ||
            quad == 0x1dc || quad == 0x1dd || quad == 0x1df)
        {
            const bool immediate = (d & 0x20) != 0;
            const uint32_t count = immediate ? (ins >> 14) & 127 : b[3];
            const bool bit_count_bytes = quad >= 0x1cc && quad <= 0x1cf;
            const bool bits = quad >= 0x1d8 && quad <= 0x1db;
            const bool rotate = (quad & 3) == 0;
            const bool right = (quad & 3) == 1;
            const uint32_t units = bit_count_bytes ? count >> 3 : count;
            const uint32_t n = bits ? (right ? (0 - units) & 7 : units & 7) :
                (right ? (0 - units) & 31 : units & (rotate ? 15 : 31)) * 8;
            const auto input = a;
            out.fill(0);
            for (uint32_t bit = 0; bit < 128; ++bit)
            {
                int source = right ? int(bit) - int(n) : int(bit + n);
                if (rotate) source &= 127;
                if (source >= 0 && source < 128)
                {
                    const uint32_t value = (input[3 - source / 32] >> (31 - source % 32)) & 1;
                    out[3 - bit / 32] |= value << (31 - bit % 32);
                }
            }
            t.pc = next;
            return true;
        }
        // Only these RI16 families use this decode. Register instructions such
        // as STQX/LQX and byte rotates occupy gaps between them.
        const uint32_t ri16 = d & ~3u;
        if (ri16 == 0x100 || ri16 == 0x104 || ri16 == 0x108 || ri16 == 0x110 ||
            ri16 == 0x118 || ri16 == 0x11c || ri16 == 0x180 || ri16 == 0x184 ||
            ri16 == 0x188 || ri16 == 0x190 || ri16 == 0x194 || ri16 == 0x198 || ri16 == 0x19c)
        {
            switch (d >> 2)
            {
            case 0x40: if (!t.r[rt][3]) branchrel(int32_t(si16)); break; // BRZ
            case 0x41: { uint32_t p=(i16<<2)&0x3fff0; store_quad(t,p,t.r[rt]); break; }
            case 0x42: if (t.r[rt][3]) branchrel(int32_t(si16)); break; // BRNZ
            case 0x44: if (uint16_t(t.r[rt][3])==0) branchrel(int32_t(si16)); break;
            case 0x46: if (uint16_t(t.r[rt][3])!=0) branchrel(int32_t(si16)); break;
            case 0x47: { uint32_t p=(old_pc+si16*4)&0x3fff0; store_quad(t,p,t.r[rt]); break; }
            case 0x60: next=(i16<<2)&0x3fffc; break;
            case 0x61: { uint32_t p=(i16<<2)&0x3fff0; load_quad(t,p,out); break; }
            case 0x62: out.fill(0);out[3]=next;next=(i16<<2)&0x3fffc;break;
            case 0x66: out.fill(0);out[3]=next;branchrel(int32_t(si16));break;
            case 0x64: branchrel(int32_t(si16)); break;
            case 0x65:
                for(int lane=0;lane<4;lane++)
                {
                    uint32_t v=0;
                    for(int byte=0;byte<4;byte++)
                    {
                        const int pos=lane*4+byte;
                        v=(v<<8)|((i16&(1u<<(15-pos)))?0xffu:0u);
                    }
                    out[3-lane]=v;
                }
                break;
            case 0x67: { uint32_t p=(old_pc+si16*4)&0x3fff0; load_quad(t,p,out); break; }
            default: log_line("spu: unsupported immediate16 opcode %03X ins=%08X pc=%05X",d,ins,old_pc);stop_code=0x3fff;t.pc=next;return false;
            }
            t.pc=next; return true;
        }
        // Ten-bit immediate encodings occupy eight decode slots each.
        if ((d >> 3)==0x04 || (d >> 3)==0x05 || (d >> 3)==0x06 || (d >> 3)==0x0c ||
            (d >> 3)==0x0d || (d >> 3)==0x14 || (d >> 3)==0x15 || (d >> 3)==0x16 ||
            (d >> 3)==0x1c || (d >> 3)==0x1d || (d >> 3)==0x44 || (d >> 3)==0x45 ||
            (d >> 3)==0x46 || (d >> 3)==0x4c || (d >> 3)==0x4d || (d >> 3)==0x4e ||
            (d >> 3)==0x5c || (d >> 3)==0x5d || (d >> 3)==0x5e || (d >> 3)==0x74 ||
            (d >> 3)==0x75 || (d >> 3)==0x7c || (d >> 3)==0x7d || (d >> 3)==0x7e ||
            (d >> 3)==0x4f || (d >> 3)==0x5f || (d >> 3)==0x7f)
        {
            const uint32_t family = d >> 3;
            // Halfword and byte immediates act on every element, not just
            // the low element of each 32-bit lane.
            if (family == 0x05 || family == 0x15 || family == 0x45 ||
                family == 0x06 || family == 0x16 || family == 0x46 ||
                family == 0x0d || family == 0x1d || family == 0x4d ||
                family == 0x5d || family == 0x7d || family == 0x4e ||
                family == 0x5e || family == 0x7e)
            {
                const bool bytes = (family & 3) == 2;
                const unsigned width = bytes ? 8 : 16;
                const uint32_t mask = (1u << width) - 1, imm = si10 & mask;
                const auto input = a;
                out.fill(0);
                for (int lane = 0; lane < 4; ++lane)
                    for (unsigned bit = 0; bit < 32; bit += width)
                    {
                        const uint32_t value = (input[lane] >> bit) & mask;
                        uint32_t result = 0;
                        switch (family >> 2)
                        {
                        case 1: result = value | imm; break;
                        case 3: result = imm - value; break;
                        case 5: result = value & imm; break;
                        case 7: result = value + imm; break;
                        case 17: result = value ^ imm; break;
                        case 19: result = int32_t(sign_extend(value, width)) > int32_t(sign_extend(imm, width)) ? mask : 0; break;
                        case 23: result = value > imm ? mask : 0; break;
                        case 31: result = value == imm ? mask : 0; break;
                        }
                        out[lane] |= (result & mask) << bit;
                    }
                t.pc = next;
                return true;
            }
            if (family == 0x4f || family == 0x5f || family == 0x7f)
            {
                const bool halt = family == 0x4f ? int32_t(a[3]) > int32_t(si10) :
                    family == 0x5f ? a[3] > si10 : a[3] == si10;
                if (halt)
                {
                    log_line("spu: firmware halt ins=%08X pc=%05X ra=%08X", ins, old_pc, a[3]);
                    capture_spu_stop(t);
                    stop_code = 0x3fff; t.pc = next; return false;
                }
                t.pc = next;
                return true;
            }
            switch(d>>3)
            {
            case 0x04: for(int i=0;i<4;i++)out[i]=a[i]|uint32_t(si10);break;
            case 0x0c: for(int i=0;i<4;i++)out[i]=uint32_t(si10)-a[i];break;
            case 0x14: for(int i=0;i<4;i++)out[i]=a[i]&uint32_t(si10);break;
            case 0x1c: for(int i=0;i<4;i++)out[i]=a[i]+si10;break;
            case 0x44: for(int i=0;i<4;i++)out[i]=a[i]^uint32_t(si10);break;
            case 0x4c: for(int i=0;i<4;i++)out[i]=int32_t(a[i])>int32_t(si10)?~0u:0;break;
            case 0x5c: for(int i=0;i<4;i++)out[i]=a[i]>uint32_t(si10)?~0u:0;break;
            case 0x7c: for(int i=0;i<4;i++)out[i]=a[i]==uint32_t(si10)?~0u:0;break;
            case 0x74: for(int i=0;i<4;i++)out[i]=uint32_t(int32_t(int16_t(a[i]))*int32_t(si10));break;
            case 0x75: for(int i=0;i<4;i++)out[i]=uint16_t(a[i])*uint32_t(uint16_t(si10));break;
            default: break;
            }
            t.pc=next; return true;
        }
        if (d == 0x248 || d == 0x2c8 || d == 0x3c8 || d == 0x250 || d == 0x2d0 || d == 0x3d0)
        {
            const unsigned width = (d & 0x18) == 0x10 ? 8 : 16;
            const uint32_t mask = (1u << width) - 1;
            const auto left = a, right = b;
            out.fill(0);
            for (int lane = 0; lane < 4; ++lane)
                for (unsigned bit = 0; bit < 32; bit += width)
                {
                    const uint32_t x = (left[lane] >> bit) & mask, y = (right[lane] >> bit) & mask;
                    const bool match = d >= 0x3c0 ? x == y : d >= 0x2c0 ? x > y :
                        int32_t(sign_extend(x, width)) > int32_t(sign_extend(y, width));
                    if (match) out[lane] |= mask << bit;
                }
            t.pc = next;
            return true;
        }
        switch (d)
        {
        case 0x000:
            stop_code = ins & 0x3fff;
            if (stop_code == kStopReceiveEvent || stop_code == kStopTryReceiveEvent)
            {
                receive_event(t, stop_code == kStopReceiveEvent);
                stop_code=0x3fff;
                t.pc=next; return true;
            }
            t.pc = next; return false;
        case 0x001: case 0x002: case 0x003: break;
        case 0x00d: { const uint32_t v=read_channel(t,ra);out.fill(0);out[3]=v;break; }
        case 0x00f: { const uint32_t v=channel_count(t,ra);out.fill(0);out[3]=v;break; }
        case 0x10d: write_channel(t, ra, t.r[rt][3]); break;
        case 0x040: for (int i=0;i<4;i++) out[i]=b[i]-a[i]; break; // SF
        case 0x041: for (int i=0;i<4;i++) out[i]=a[i]|b[i]; break;
        case 0x042: for (int i=0;i<4;i++) out[i]=uint32_t(b[i]>=a[i]); break;
        case 0x048: for (int i=0;i<4;i++) out[i]=uint32_t(uint16_t((b[i]>>16)-(a[i]>>16)))<<16 | uint16_t(b[i]-a[i]); break;
        case 0x049: for (int i=0;i<4;i++) out[i]=~(a[i]|b[i]); break;
        case 0x0c0: for (int i=0;i<4;i++) out[i]=a[i]+b[i]; break;
        case 0x0c1: for (int i=0;i<4;i++) out[i]=a[i]&b[i]; break;
        case 0x0c2: for (int i=0;i<4;i++) out[i]=uint32_t((uint64_t(a[i])+b[i])>>32); break;
        case 0x0c8: for (int i=0;i<4;i++) out[i]=uint32_t(int16_t(a[i]>>16)+int16_t(b[i]>>16))<<16 | uint16_t(int16_t(a[i])+int16_t(b[i])); break;
        case 0x0c9: for (int i=0;i<4;i++) out[i]=~(a[i]&b[i]); break;
        case 0x241: for (int i=0;i<4;i++) out[i]=a[i]^b[i]; break;
        case 0x240: for (int i=0;i<4;i++) out[i]=int32_t(a[i])>int32_t(b[i])?~0u:0; break;
        case 0x253:
            for (int i=0;i<4;++i)
            {
                const uint32_t left=a[i],right=b[i];
                uint32_t x=0,y=0;
                for(unsigned byte=0;byte<4;++byte){x+=uint8_t(left>>(byte*8));y+=uint8_t(right>>(byte*8));}
                out[i]=(y<<16)|x;
            }
            break;
        case 0x2a5: for (int i=0;i<4;i++) out[i]=std::countl_zero(a[i]); break;
        case 0x2b4:
            for (int i=0;i<4;++i)
            {
                const uint32_t value=a[i];
                out[i]=0;
                for (unsigned byte=0;byte<4;++byte) out[i]|=uint32_t(std::popcount(uint8_t(value>>(byte*8))))<<(byte*8);
            }
            break;
        case 0x2a6:
        {
            const auto input=a;
            out={input[0],uint32_t(int32_t(input[0])>>31),input[2],uint32_t(int32_t(input[2])>>31)};
            break;
        }
        case 0x00c: out.fill(0); break; // MFSPR
        case 0x10c: break; // MTSPR
        case 0x398: out=t.fpscr; break; // FSCRRD
        case 0x3ba: // FSCRWR: reserved bits read back as zero.
            for (int i=0;i<4;++i) t.fpscr[i]=a[i]&((i==1||i==2)?0x3f07u:0xf07u);
            break;
        case 0x2ae: for (int i=0;i<4;i++) out[i]=uint32_t(int32_t(int16_t(a[i]))); break;
        case 0x3c4: for (int i=0;i<4;i++) out[i]=uint32_t(int32_t(int16_t(a[i]))*int32_t(int16_t(b[i]))); break;
        case 0x3c5: for (int i=0;i<4;i++) out[i]=(uint32_t(a[i]>>16)*uint32_t(uint16_t(b[i])))<<16; break;
        case 0x3c6: for (int i=0;i<4;i++) out[i]=uint32_t(int32_t(int16_t(a[i]>>16))*int32_t(int16_t(b[i]>>16))); break;
        case 0x3c7: for (int i=0;i<4;i++) out[i]=uint32_t((int32_t(int16_t(a[i]))*int32_t(int16_t(b[i])))>>16); break;
        case 0x3cc: for (int i=0;i<4;i++) out[i]=uint32_t(uint16_t(a[i]))*uint16_t(b[i]); break;
        case 0x3ce: for (int i=0;i<4;i++) out[i]=(a[i]>>16)*(b[i]>>16); break;
        case 0x1f0: {const uint32_t value=a[0]|a[1]|a[2]|a[3];out.fill(0);out[3]=value;break;}
        case 0x1b8: case 0x1b9:
            for (int i=0;i<4;++i)
            {
                const uint32_t input=a[i], exponent=(input>>23)&255;
                if (d==0x1b8)
                    out[i]=spu_reciprocal_fraction[(input>>18)&31] | (input&0x80000000) |
                        (!exponent?0x7f800000:exponent<253?(253-exponent)<<23:0);
                else
                    out[i]=spu_rsqrt_fraction[(input>>18)&63] | (!exponent?0x7f800000:(190-((exponent+1)>>1))<<23);
            }
            break;
        case 0x1b0: case 0x1b1: case 0x1b2:
        {
            const unsigned width=d==0x1b0?32:d==0x1b1?16:8;
            const auto input=a;
            uint32_t value=0;
            for(unsigned i=0;i<128/width;++i)value|=((input[i*width/32]>>(i*width%32))&1)<<i;
            out.fill(0);out[3]=value;break;
        }
        case 0x1b4: case 0x1b5: case 0x1b6:
        {
            const unsigned width=d==0x1b4?32:d==0x1b5?16:8;
            const uint32_t bits=a[3],mask=width==32?~0u:(1u<<width)-1;
            out.fill(0);
            for(unsigned i=0;i<128/width;++i)if(bits&(1u<<i))out[i*width/32]|=mask<<(i*width%32);
            break;
        }
        case 0x3c0: for (int i=0;i<4;i++) out[i]=a[i]==b[i]?~0u:0; break;
        case 0x249: for (int i=0;i<4;i++) out[i]=~(a[i]^b[i]); break;
        case 0x2c1: for (int i=0;i<4;i++) out[i]=a[i]&~b[i]; break;
        case 0x2c0: for (int i=0;i<4;i++) out[i]=a[i]>b[i]?~0u:0; break;
        case 0x2c9: for (int i=0;i<4;i++) out[i]=a[i]|~b[i]; break;
        case 0x201: break;
        case 0x1ac: break; // HBR hint
        case 0x340: for (int i=0;i<4;i++) out[i]=a[i]+b[i]+(out[i]&1); break; // ADDX
        case 0x341: for (int i=0;i<4;i++) out[i]=b[i]-a[i]-(1-(out[i]&1)); break;
        case 0x342: for (int i=0;i<4;i++) out[i]=uint32_t((uint64_t(a[i])+b[i]+(out[i]&1))>>32); break;
        case 0x343: for (int i=0;i<4;i++) out[i]=uint64_t(b[i])>=uint64_t(a[i])+(1-(out[i]&1)); break;
        case 0x346: for (int i=0;i<4;i++) out[i]+=uint32_t(int32_t(int16_t(a[i]>>16))*int32_t(int16_t(b[i]>>16))); break;
        case 0x34e: for (int i=0;i<4;i++) out[i]+=(a[i]>>16)*(b[i]>>16); break;
        case 0x2b6: for (int i=0;i<4;i++) out[i]=uint32_t(uint16_t(int16_t(int8_t(a[i]>>16))))<<16 | uint16_t(int16_t(int8_t(a[i]))); break;
        case 0x128: if(t.r[rt][3]==0) next=t.r[ra][3]&0x3fffc; break;
        case 0x129: if(t.r[rt][3]!=0) next=t.r[ra][3]&0x3fffc; break;
        case 0x1c4: { uint32_t addr=(t.r[ra][3]+t.r[rb][3])&0x3fff0; load_quad(t,addr,out); break; }
        case 0x144: { uint32_t addr=(t.r[ra][3]+t.r[rb][3])&0x3fff0; store_quad(t,addr,t.r[rt]); break; }
        default:
            // Unknown instructions are surfaced with enough context to extend the interpreter.
            log_line("spu: unsupported opcode %03X ins=%08X pc=%05X id=%08X", d, ins, old_pc, t.id);
            capture_spu_stop(t);
            stop_code = 0x3fff; t.pc=next; return false;
        }
        t.pc = next;
        return true;
    }

    void check_instruction_semantics()
    {
        KSpuThread t;
        uint32_t stop = 0;
        auto run = [&](uint32_t instruction)
        {
            t.pc = 0x100;
            ls_store_word(t, t.pc, instruction);
            if (!execute_one(t, stop)) fatal_exit(8);
        };
        auto require = [&](bool valid, const char* name)
        {
            if (!valid) { log_line("spu: instruction check failed: %s", name); fatal_exit(8); }
        };
        t.r[4][3] = 0x2340;
        run((0x1a8u << 21) | (4 << 7));
        require(t.pc == 0x2340, "BI uses RA");
        t.r[4][3] = 0x200;
        t.r[5].fill(0);
        for (int i = 0; i < 4; ++i) ls_store_word(t, 0x200 + i * 4, 0x11223344u + i);
        run((0x1c4u << 21) | (5 << 14) | (4 << 7) | 7);
        require(t.r[7][3] == 0x11223344 && t.r[7][0] == 0x11223347, "LQX preferred word");
        t.r[4][3] = 0x220;
        run((0x144u << 21) | (5 << 14) | (4 << 7) | 7);
        require(ls_word(t, 0x220) == 0x11223344 && ls_word(t, 0x22c) == 0x11223347, "STQX lane order");
        t.r[4].fill(0xaaaaaaaa); t.r[5].fill(0x55555555); t.r[6].fill(0x00ff00ff);
        run((8u << 28) | (7 << 21) | (5 << 14) | (4 << 7) | 6);
        require(t.r[7][3] == 0xaa55aa55 && t.r[7][0] == 0xaa55aa55, "SELB mask and RT4");
        t.r[4] = {0x0c0d0e0f,0x08090a0b,0x04050607,0x00010203};
        t.r[5] = {0x8c8d8e8f,0x88898a8b,0x84858687,0x80818283};
        t.r[6].fill(0x000f101f);
        run((0xbu << 28) | (6 << 21) | (5 << 14) | (4 << 7) | 6);
        require(t.r[6][3] == 0x000f808f && t.r[6][0] == 0x000f808f, "SHUFB order and aliased control");
        t.r[8].fill(0x12340000);
        run((0x304u << 21) | (0xabcdu << 7) | 8);
        require(t.r[8][3] == 0x1234abcd, "IOHL retains RT");
        run((0x188u << 21) | (0x123u << 7) | 9);
        require(t.pc == 0x48c && t.r[9][3] == 0x104 && t.r[9][0] == 0, "BRASL absolute target");
        t.r[4].fill(0x12345678);
        run((0x7bu << 21) | (48 << 14) | (4 << 7) | 7);
        require(t.r[7][3] == 0, "SHLI shifts beyond word width");
        log_line("spu: instruction checks passed (8 cases)");
    }

    void spu_main(std::shared_ptr<KSpuThread> t)
    {
        setup_spu_floats();
        uint32_t stop = 0x3fff;
        t->pc=t->entry;
        // Firmware loader convention: entry point in r0's preferred word; four 64-bit args in r3-r6.
        t->r[0].fill(0);
        t->r[1].fill(0); t->r[1][3] = 0x3fff0; // RPCS3 cpu_init() initial SPU stack frame
        for (int i=0;i<4;i++)
        {
            // RPCS3's v128::from64(0, arg) places the high word in preferred lane 3.
            t->r[3+i][2]=uint32_t(t->args[i]); t->r[3+i][3]=uint32_t(t->args[i]>>32);
        }
        for (uint32_t steps=0;;++steps)
        {
            if (!execute_one(*t, stop)) break;
            if ((steps & 0xfffff) == 0xfffff) std::this_thread::yield();
        }
        auto g=t->group.lock();
        t->observed_pc=t->pc;t->instructions=t->executed_instructions;
        if (!g) return;
        {
            KLock lock(g_kernel);
            t->exited=true;
            t->exit_status=t->out_mbox.empty()?0:t->out_mbox.front();
            if (stop==kStopGroupExit || stop==kStopThreadExit)
            {
                if (!t->out_mbox.empty()) t->exit_status=t->out_mbox.front();
                if (stop==kStopGroupExit && g->join_cause!=1)
                {
                    g->join_cause=1;g->status=t->exit_status;t->group_exit=true;
                    // A group exit stops every peer, including one blocked in an
                    // SPU receive. Join completes only after all host threads stop.
                    for (auto& peer:g->threads) peer->stop_requested.store(true,std::memory_order_relaxed);
                }
            }
            if(t->stop_reports++<4||t->exit_status||(stop!=kStopGroupExit&&stop!=kStopThreadExit)||getenv("XMBRT_SPU_VERBOSE"))
                log_line("spu: thread %08X stopped code=%04X status=%08X pc=%05X",t->id,stop,t->exit_status,t->pc);
            if (group_done(*g) && g->joiner) { g->joiner->wake_data[0]=g->join_cause; g->joiner->wake_data[1]=g->status; wake_thread(g->joiner,CELL_OK); g->joiner=nullptr; }
        }
    }

    int64_t sys_spu_image_import(PPCContext& c)
    {
        const uint32_t out=c.r3.u32, src=c.r4.u32, size=c.r5.u32;
        if (!out || !src || size<52) return CELL_EFAULT;
        const uint8_t* elf=static_cast<uint8_t*>(guest_ptr(src));
        if (memcmp(elf,"\x7f" "ELF",4) || elf[4]!=1 || elf[5]!=2) return CELL_ENOEXEC;
        const uint32_t entry=be32(elf+24), phoff=be32(elf+28);
        const uint16_t phentsize=be16(elf+42), phnum=be16(elf+44);
        if (phentsize<32 || phnum>32 || uint64_t(phoff)+uint64_t(phentsize)*phnum>size) return CELL_ENOEXEC;
        auto image=std::make_shared<KSpuImage>(); image->entry=entry;
        for(uint32_t i=0;i<phnum;i++)
        {
            const uint8_t* p=elf+phoff+i*phentsize;
            const uint32_t type=be32(p), off=be32(p+4), vaddr=be32(p+8), filesz=be32(p+16), memsz=be32(p+20);
            if(type==1)
            {
                if(uint64_t(off)+filesz>size || uint64_t(vaddr)+memsz>kLsSize || filesz>memsz) return CELL_ENOEXEC;
                if(filesz) image->segments.push_back({kSegCopy,vaddr,filesz,src+off});
                if(memsz>filesz) image->segments.push_back({kSegFill,vaddr+filesz,memsz-filesz,0});
            }
            else if(type!=0 && type!=4) return CELL_ENOEXEC;
        }
        if(image->segments.empty()) return CELL_ENOEXEC;
        const uint32_t id=kobj_add(0x68,image);
        guest_store_u32(out,kImageKernel); guest_store_u32(out+4,id); guest_store_u32(out+8,0); guest_store_u32(out+12,0);
        log_line("spu: imported ELF src=%08X size=%X entry=%05X segments=%u image=%08X",src,size,entry,uint32_t(image->segments.size()),id);
        return CELL_OK;
    }
    int64_t sys_spu_image_close(PPCContext& c) { return kobj_remove(guest_load_u32(c.r3.u32+4))?CELL_OK:CELL_ESRCH; }

    int64_t sys_spu_group_create(PPCContext& c)
    {
        const uint32_t n=c.r4.u32;
        if(!c.r3.u32||!n||n>6)return CELL_EINVAL;
        auto g=std::make_shared<KSpuGroup>(); g->expected=n;
        guest_store_u32(c.r3.u32,kobj_add(0x04,g)); return CELL_OK;
    }
    int64_t sys_spu_group_destroy(PPCContext& c)
    {
        KLock lock(g_kernel); auto g=kobj<KSpuGroup>(c.r3.u32); if(!g)return CELL_ESRCH;
        if(g->started&&!group_done(*g))return CELL_EBUSY; g->destroyed=true; return kobj_remove(c.r3.u32)?CELL_OK:CELL_ESRCH;
    }
    int64_t sys_spu_thread_initialize(PPCContext& c)
    {
        const uint32_t tidptr=c.r3.u32,gid=c.r4.u32,n=c.r5.u32,img=c.r6.u32,attr=c.r7.u32,arg=c.r8.u32;
        if(!tidptr||!img||!attr||!arg)return CELL_EFAULT;
        KLock lock(g_kernel); auto g=kobj<KSpuGroup>(gid); if(!g)return CELL_ESRCH;
        if(g->started||n>=6||g->threads.size()>=g->expected)return CELL_EINVAL;
        const uint32_t type=guest_load_u32(img), entry=guest_load_u32(img+4), segptr=guest_load_u32(img+8), nseg=guest_load_u32(img+12);
        log_line("spu: initialize image type=%u entry=%08X segs=%08X count=%u group=%08X index=%u",type,entry,segptr,nseg,gid,n);
        auto t=std::make_shared<KSpuThread>(); t->group=g;t->spu_num=n;t->id=(n<<24)|(gid&0xffffff);t->entry=entry;
        const uint32_t name_ptr=guest_load_u32(attr), name_len=guest_load_u32(attr+4);
        if(name_ptr&&name_len)t->name=guest_string(name_ptr,std::min<uint32_t>(name_len,128));
        for(int i=0;i<4;i++)t->args[i]=guest_load_u64(arg+i*8);
        if(type==kImageKernel)
        {
            t->image=kobj<KSpuImage>(entry); if(!t->image)return CELL_ESRCH; t->entry=t->image->entry;
        }
        else if(type==kImageUser)
        {
            if(entry>=kLsSize||!segptr||!nseg||nseg>32)return CELL_EINVAL;
            t->image=std::make_shared<KSpuImage>(); t->image->entry=entry;
            for(uint32_t i=0;i<nseg;i++)
            {
                const uint32_t p=segptr+i*24, st=guest_load_u32(p), ls=guest_load_u32(p+4), sz=guest_load_u32(p+8), addr=guest_load_u32(p+16);
                log_line("spu: user segment %u type=%u ls=%05X size=%X addr=%08X",i,st,ls,sz,addr);
                if(st!=kSegCopy&&st!=kSegFill&&st!=kSegInfo)return CELL_EINVAL;
                if(st!=kSegInfo&&(uint64_t(ls)+sz>kLsSize||!sz))return CELL_EINVAL;
                t->image->segments.push_back({st,ls,sz,addr});
            }
        }
        else return CELL_EINVAL;
        for(const auto& s:t->image->segments)
        {
            if(s.type==kSegCopy){if(!guest_commit(s.source,s.size))return CELL_EFAULT;memcpy(t->ls.data()+s.ls,g_base+s.source,s.size);}
            else if(s.type==kSegFill)for(uint32_t offset=0;offset+4<=s.size;offset+=4)put_be32(t->ls.data()+s.ls+offset,s.source);
        }
        // The LV2 loader reserves one quadword per 64-bit thread argument at LS + 0x30.
        for(int i=0;i<4;i++)
        {
            put_be32(t->ls.data()+0x30+i*0x10,uint32_t(t->args[i]>>32));
            put_be32(t->ls.data()+0x34+i*0x10,uint32_t(t->args[i]));
        }
        g->threads.push_back(t); g_spu_threads[t->id]=t; guest_store_u32(tidptr,t->id); return CELL_OK;
    }
    int64_t sys_spu_group_start(PPCContext& c)
    {
        std::shared_ptr<KSpuGroup> g;
        {
            KLock lock(g_kernel);
            g=kobj<KSpuGroup>(c.r3.u32);
            if(!g)return CELL_ESRCH;
            if(g->started||g->threads.size()!=g->expected)return CELL_EINVAL;
            g->started=true;g->join_cause=2;g->status=0;
            for (auto& t:g->threads)
            {
                write_thread_loader(*t);t->r={};t->fpscr={};t->in_mbox.clear();t->out_mbox.clear();
                t->exited=false;t->group_exit=false;t->exit_status=0;
                t->stop_requested=false;
                t->event_mask=0;t->event_stat=0;t->mfc_tagmask=0;
                for (const auto& segment:t->image->segments)
                {
                    if(segment.type==kSegCopy)memcpy(t->ls.data()+segment.ls,g_base+segment.source,segment.size);
                    else if(segment.type==kSegFill)
                        for(uint32_t off=0;off+4<=segment.size;off+=4)put_be32(t->ls.data()+segment.ls+off,segment.source);
                }
            }
        }
        if(g->event_queues[0])
        {
            KLock lock(g_kernel); auto q=find_event_queue(g->event_queues[0]);
            if(q)event_queue_send(*q,0xFFFFFFFF53505500ull,c.r3.u32,0,0);
        }
        for(auto&t:g->threads)std::thread(spu_main,t).detach();
        return CELL_OK;
    }
    int64_t sys_spu_group_join(PPCContext& c)
    {
        KLock lock(g_kernel);auto g=kobj<KSpuGroup>(c.r3.u32);if(!g)return CELL_ESRCH;
        if(!g->started)return CELL_EINVAL;
        if(!group_done(*g))
        {
            if(g->joiner)return CELL_EBUSY;g->joiner=self();
            if(!sleep_thread(lock,0)){g->joiner=nullptr;return CELL_ECANCELED;}
        }
        if(c.r4.u32)guest_store_u32(c.r4.u32,g->join_cause);
        if(c.r5.u32)guest_store_u32(c.r5.u32,g->status);
        g->started=false;
        return CELL_OK;
    }
    int64_t sys_spu_group_connect_all(PPCContext& c)
    {
        KLock lock(g_kernel);auto g=kobj<KSpuGroup>(c.r3.u32);auto q=find_event_queue(c.r4.u32);if(!g||!q)return CELL_ESRCH;
        const uint64_t requested=c.r5.u64;
        for(unsigned port=0;port<64;++port)
        {
            if(!(requested&(uint64_t(1)<<port)))continue;
            bool available=true;
            for(const auto& thread:g->threads)if(g_user_event_queues[thread->id][port])available=false;
            if(!available)continue;
            for(const auto& thread:g->threads)g_user_event_queues[thread->id][port]=c.r4.u32;
            if(c.r6.u32)guest_store_u8(c.r6.u32,uint8_t(port));
            return CELL_OK;
        }
        return CELL_EBUSY;
    }
    int64_t sys_spu_group_connect_event(PPCContext& c)
    {
        KLock lock(g_kernel);auto g=kobj<KSpuGroup>(c.r3.u32);auto q=find_event_queue(c.r4.u32);if(!g||!q)return CELL_ESRCH;
        const uint32_t type=c.r5.u32;
        if(type!=1&&type!=2&&type!=4)return CELL_EINVAL;
        auto& binding=g->event_queues[type==1?0:type==2?1:2];
        if(binding)return CELL_EBUSY;
        binding=c.r4.u32;return CELL_OK;
    }
    int64_t sys_spu_group_disconnect_event(PPCContext& c)
    {
        KLock lock(g_kernel);auto g=kobj<KSpuGroup>(c.r3.u32);if(!g)return CELL_ESRCH;
        const uint32_t type=c.r4.u32;if(type!=1&&type!=2&&type!=4)return CELL_OK;
        auto& binding=g->event_queues[type==1?0:type==2?1:2];
        binding=0;return CELL_OK;
    }
    int64_t sys_spu_connect_event(PPCContext& c)
    {
        const uint32_t id=c.r3.u32,qid=c.r4.u32,et=c.r5.u32,spup=c.r6.u32;KLock lock(g_kernel);
        if(!spu_thread(id))return CELL_ESRCH;
        auto q=find_event_queue(qid);if(!q)return CELL_ESRCH;
        if(et!=1||spup>63)return CELL_EINVAL;g_user_event_queues[id][spup]=qid;return CELL_OK;
    }
    int64_t sys_spu_disconnect_event(PPCContext& c)
    {
        KLock lock(g_kernel);auto it=g_user_event_queues.find(c.r3.u32);if(it==g_user_event_queues.end()||c.r5.u32>63)return CELL_ESRCH;it->second[c.r5.u32]=0;return CELL_OK;
    }
    int64_t sys_spu_bind_queue(PPCContext& c)
    {
        const uint32_t id=c.r3.u32,qid=c.r4.u32,spuq=c.r5.u32;KLock lock(g_kernel);
        if(!spu_thread(id))return CELL_ESRCH;auto q=find_event_queue(qid);if(!q)return CELL_ESRCH;if(q->type!=2)return CELL_EINVAL;
        g_queue_bindings[id]={qid,spuq};return CELL_OK;
    }
    int64_t sys_spu_unbind_queue(PPCContext& c)
    {
        KLock lock(g_kernel);auto it=g_queue_bindings.find(c.r3.u32);if(it==g_queue_bindings.end())return CELL_ESRCH;g_queue_bindings.erase(it);return CELL_OK;
    }
    int64_t sys_spu_get_exit_status(PPCContext& c)
    {
        auto t=spu_thread(c.r3.u32);if(!t)return CELL_ESRCH;if(!t->exited)return CELL_ESTAT;
        if(c.r4.u32)guest_store_u32(c.r4.u32,t->exit_status);return CELL_OK;
    }
    int64_t sys_spu_thread_ls(PPCContext& c,bool write)
    {
        const uint32_t id=c.r3.u32,lsa=c.r4.u32,size=c.r6.u32;auto t=spu_thread(id);
        if(!t)return CELL_ESRCH;
        if((size!=1&&size!=2&&size!=4&&size!=8)||uint64_t(lsa)+size>kLsSize||(lsa&(size-1)))return CELL_EINVAL;
        uint64_t value=c.r5.u64;
        if(write)for(uint32_t byte=0;byte<size;++byte)t->ls[lsa+byte]=uint8_t(value>>((size-1-byte)*8));
        else
        {
            if(!c.r5.u32)return CELL_EFAULT;
            value=0;
            for(uint32_t byte=0;byte<size;++byte)value=(value<<8)|t->ls[lsa+byte];
            guest_store_u64(c.r5.u32,value);
        }
        return CELL_OK;
    }
    int64_t sys_spu_set_argument(PPCContext& c)
    {
        KLock lock(g_kernel);auto t=spu_thread(c.r3.u32);
        if(!t)return CELL_ESRCH;if(!c.r4.u32)return CELL_EFAULT;
        for(unsigned i=0;i<4;++i)t->args[i]=guest_load_u64(c.r4.u32+i*8);
        return CELL_OK;
    }
    int64_t sys_spu_write_snr(PPCContext& c)
    {
        auto t=spu_thread(c.r3.u32);if(!t)return CELL_ESRCH;const uint32_t n=c.r4.u32;if(n>1)return CELL_EINVAL;t->snr[n]=c.r5.u32;return CELL_OK;
    }
    int64_t sys_spu_write_mb(PPCContext& c)
    {
        auto t=spu_thread(c.r3.u32);if(!t)return CELL_ESRCH;std::lock_guard<std::mutex> lock(g_kernel);t->in_mbox.push_back(c.r4.u32);return CELL_OK;
    }
    int64_t sys_spu_thread_ok(PPCContext& c) { return spu_thread(c.r3.u32)?CELL_OK:CELL_ESRCH; }
    int64_t sys_spu_group_ok(PPCContext& c) { return kobj<KSpuGroup>(c.r3.u32)?CELL_OK:CELL_ESRCH; }
}

int64_t spurs_create_task(PPCContext& ctx)
{
    constexpr int32_t invalid = int32_t(0x80410902), noexec = int32_t(0x80410907);
    const uint32_t taskset = ctx.r3.u32, elf_addr = ctx.r5.u32;
    if (!taskset || !ctx.r4.u32 || !elf_addr || !ctx.r9.u32) return invalid;
    const uint8_t* elf = g_base + elf_addr;
    if (memcmp(elf, "\x7f" "ELF", 4) || elf[4] != 1 || elf[5] != 2 || be16(elf + 18) != 23) return noexec;
    const uint32_t phoff = be32(elf + 28), entry = be32(elf + 24);
    const uint16_t phsize = be16(elf + 42), count = be16(elf + 44);
    if (phsize < 32 || count > 64 || phoff > 0x100000 || entry < 0x3000 || entry >= kLsSize) return noexec;
    auto task = std::make_shared<KSpuThread>();
    task->taskset = taskset;
    task->entry = entry;
    for (unsigned i = 0; i < count; ++i)
    {
        const uint8_t* ph = elf + phoff + i * phsize;
        if (be32(ph) != 1) continue;
        const uint32_t off = be32(ph + 4), addr = be32(ph + 8), size = be32(ph + 16), memsize = be32(ph + 20);
        if (addr < 0x3000 || uint64_t(addr) + memsize > kLsSize || size > memsize || uint64_t(off) + size > 0x1000000) return noexec;
        memcpy(task->ls.data() + addr, elf + off, size);
    }
    {
        KLock lock(g_kernel);
        auto& tasks = g_spurs_tasks[taskset];
        if (tasks.size() >= 128) return int32_t(0x8041090a);
        task->task_id = uint32_t(tasks.size());
        tasks.push_back(task);
        guest_store_u32(ctx.r4.u32, task->task_id);
        const uint32_t mask = 0x80000000u >> (task->task_id % 32), word = task->task_id / 32 * 4;
        for (uint32_t field : {0u, 0x30u}) guest_store_u32(taskset + field + word, guest_load_u32(taskset + field + word) | mask);
        const uint32_t info = taskset + 0x80 + task->task_id * 48;
        memcpy(g_base + info, g_base + ctx.r9.u32, 16);
        guest_store_u64(info + 16, elf_addr);
        const uint32_t blocks = ctx.r6.u32 && ctx.r7.u32 >= 0x400 ? std::min(0x7au, (ctx.r7.u32 - 0x400) >> 11) : 0;
        guest_store_u64(info + 24, ctx.r6.u32 | blocks);
        if (ctx.r8.u32) memcpy(g_base + info + 32, g_base + ctx.r8.u32, 16);
    }
    memcpy(task->ls.data() + 0x2700, g_base + taskset, 128);
    memcpy(task->ls.data() + 0x2780, g_base + taskset + 0x80 + task->task_id * 48, 48);
    memcpy(task->ls.data() + 0x2840, "SPURSTASK MODULE", 16);
    ls_store_word(*task, 0x1c0, guest_load_u32(taskset + 0x60));
    ls_store_word(*task, 0x1c4, guest_load_u32(taskset + 0x64));
    ls_store_word(*task, 0x1cc, 31);
    task->ls[0x1e8] = 'T';
    task->ls[0x1e9] = 'K';
    ls_store_word(*task, 0x27b8, 0);
    ls_store_word(*task, 0x27bc, taskset);
    ls_store_word(*task, 0x27c0, 0x100);
    ls_store_word(*task, 0x27c4, 0xa70);
    ls_store_word(*task, 0x27d0, 31);
    ls_store_word(*task, 0x27d4, task->task_id);
    ls_store_word(*task, 0x2fb8, 0x2700);
    ls_store_word(*task, 0x2fbc, 0x3000);
    task->r[1][3] = 0x3fff0;
    // Task arguments are a big-endian quadword. r4 contains taskset args
    // in the preferred doubleword and the parent SPURS pointer in the other.
    for (int i = 0; i < 4; ++i) task->r[3][3 - i] = guest_load_u32(ctx.r9.u32 + i * 4);
    task->r[4][3] = guest_load_u32(taskset + 0x68);
    task->r[4][2] = guest_load_u32(taskset + 0x6c);
    task->r[4][1] = guest_load_u32(taskset + 0x60);
    task->r[4][0] = guest_load_u32(taskset + 0x64);
    log_line("spurs: starting taskset=%08X task=%u entry=%05X", taskset, task->task_id, entry);
    std::thread([task]
    {
        setup_spu_floats();
        task->pc = task->entry;
        uint32_t stop = 0x3fff;
        uint64_t steps = 0;
        struct Trace {uint32_t pc, ins, r3, r4;};
        std::array<Trace, 32> trace{};
        const bool tracing = getenv("XMBRT_SPU_TASK_TRACE") != nullptr;
        for (;;)
        {
            if (tracing) trace[steps % trace.size()] = {task->pc, ls_word(*task, task->pc), task->r[3][3], task->r[4][3]};
            const bool more = execute_one(*task, stop);
            ++steps;
            if (!more) break;
            if ((steps & 0xfffff) == 0) std::this_thread::yield();
        }
        log_line("spurs: task stopped taskset=%08X task=%u code=%04X pc=%05X steps=%llu", task->taskset, task->task_id, stop, task->pc, (unsigned long long)steps);
        task->task_state = 2;
        if (tracing)
            for (uint64_t i = steps > trace.size() ? steps - trace.size() : 0; i < steps; ++i)
            {
                const auto& item = trace[i % trace.size()];
                log_line("spurs trace: pc=%05X ins=%08X r3=%08X r4=%08X", item.pc, item.ins, item.r3, item.r4);
            }
    }).detach();
    return CELL_OK;
}

int64_t spurs_send_signal(PPCContext& ctx)
{
    KLock lock(g_kernel);
    const auto it = g_spurs_tasks.find(ctx.r3.u32);
    if (it == g_spurs_tasks.end() || ctx.r4.u32 >= it->second.size()) return int32_t(0x80410905);
    it->second[ctx.r4.u32]->task_signal = true;
    return CELL_OK;
}

void spurs_connect_task_event_queue(uint32_t taskset, uint32_t port, uint32_t queue)
{
    // The caller holds the kernel lock, like LV2's queue attachment paths.
    g_task_event_queues[taskset][port] = queue;
}

void spu_dump_stats()
{
    KLock lock(g_kernel, std::try_to_lock);
    if (!lock.owns_lock()) { log_line("spu: kernel busy while collecting state"); return; }
    for (const auto& [set, tasks] : g_spurs_tasks)
        for (const auto& task : tasks)
            log_line("spurs state: taskset=%08X task=%u pc=%05X instructions=%llu state=%s", set, task->task_id,
                task->observed_pc.load(), (unsigned long long)task->instructions.load(),
                task->task_state == 1 ? "waiting signal" : task->task_state == 2 ? "stopped" : "running");
    for (const auto& [id, thread] : g_spu_threads)
        log_line("spu state: id=%08X pc=%05X instructions=%llu exited=%u",id,thread->observed_pc.load(),
            (unsigned long long)thread->instructions.load(),unsigned(thread->exited));
}

void register_spu_syscalls()
{
    if (getenv("XMBRT_SPU_CHECKS")) check_instruction_semantics();
    register_syscall(157,sys_spu_image_import); register_syscall(158,sys_spu_image_close);
    register_syscall(165,sys_spu_get_exit_status); register_syscall(170,sys_spu_group_create); register_syscall(171,sys_spu_group_destroy);
    register_syscall(166,sys_spu_set_argument);
    register_syscall(172,sys_spu_thread_initialize); register_syscall(173,sys_spu_group_start); register_syscall(178,sys_spu_group_join);
    register_syscall(181,[](PPCContext& c)->int64_t{return sys_spu_thread_ls(c,true);});
    register_syscall(182,[](PPCContext& c)->int64_t{return sys_spu_thread_ls(c,false);});
    register_syscall(184,sys_spu_write_snr); register_syscall(185,sys_spu_group_connect_event); register_syscall(186,sys_spu_group_disconnect_event);
    register_syscall(187,sys_spu_thread_ok); register_syscall(188,sys_spu_thread_ok); register_syscall(190,sys_spu_write_mb);
    register_syscall(191,sys_spu_connect_event); register_syscall(192,sys_spu_disconnect_event); register_syscall(193,sys_spu_bind_queue);
    register_syscall(194,sys_spu_unbind_queue); register_syscall(251,sys_spu_group_connect_all); register_syscall(252,sys_spu_group_ok);
}
