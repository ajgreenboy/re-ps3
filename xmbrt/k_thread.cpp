// PPU threads: one host thread per guest thread (sys_ppu_thread.cpp semantics).
#include "kernel.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>

namespace
{
    std::unordered_map<uint32_t, PpuThread*> g_threads; // guarded by g_kernel
    uint32_t g_next_thread_id = kMainThreadId + 1;
    thread_local PpuThread* t_self;
    uint32_t g_stack_next = kStackBase;
    std::mutex g_stack_mutex;

    constexpr uint32_t kHostStackReserve = 16 * 1024 * 1024; // translated guest calls nest on the host stack

    void finish_thread(PpuThread* t, uint64_t status)
    {
        KLock lock(g_kernel);
        t->exited = true;
        t->exit_status = status;
        if (t->joiner)
        {
            t->joiner->wake_data[0] = status;
            wake_thread(t->joiner, CELL_OK);
        }
        else if (!t->joinable)
        {
            g_threads.erase(t->id);
        }
        log_line("thread %08X '%s' exited with %llX", t->id, t->name.c_str(), (unsigned long long)status);
    }

    unsigned __stdcall thread_main(void* param)
    {
        PpuThread* t = static_cast<PpuThread*>(param);
        t_self = t;
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &t->host_thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
        PPCContext& ctx = *t->ctx;
        ctx.r1.u64 = t->stack_addr + t->stack_size - 0x70;
        ctx.r3.u64 = t->arg0;
        ctx.r4.u64 = t->arg1;
        ctx.r13.u64 = t->tls;
        ctx.lr = 0;
        uint64_t status;
        try
        {
            call_guest_opd(ctx, t->entry_opd);
            status = ctx.r3.u64;
        }
        catch (const ThreadExit& e)
        {
            status = e.status;
        }
        finish_thread(t, status);
        return 0;
    }

    PPCContext* new_context()
    {
        PPCContext* ctx = new PPCContext{};
        ctx->syscall = lv2_syscall;
        ctx->trap = lv2_trap;
        ctx->fpscr.loadFromHost();
        return ctx;
    }

    int64_t sys_ppu_thread_exit(PPCContext& ctx)
    {
        throw ThreadExit{ ctx.r3.u64 };
    }

    int64_t sys_ppu_thread_yield(PPCContext&)
    {
        SwitchToThread();
        return CELL_OK;
    }

    // sys_ppu_thread_join(thread_id, vptr)
    int64_t sys_ppu_thread_join(PPCContext& ctx)
    {
        const uint32_t id = ctx.r3.u32, vptr = ctx.r4.u32;
        KLock lock(g_kernel);
        const auto it = g_threads.find(id);
        if (it == g_threads.end()) return CELL_ESRCH;
        PpuThread* t = it->second;
        if (t == self()) return CELL_EDEADLK;
        if (!t->joinable || t->joiner) return CELL_EINVAL;
        uint64_t status = t->exit_status;
        if (!t->exited)
        {
            t->joiner = self();
            sleep_thread(lock, 0);
            status = self()->wake_data[0];
        }
        g_threads.erase(id);
        if (vptr) guest_store_u64(vptr, status);
        return CELL_OK;
    }

    int64_t sys_ppu_thread_detach(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        const auto it = g_threads.find(ctx.r3.u32);
        if (it == g_threads.end()) return CELL_ESRCH;
        PpuThread* t = it->second;
        if (!t->joinable) return CELL_EINVAL;
        if (t->joiner) return CELL_EBUSY;
        t->joinable = false;
        if (t->exited) g_threads.erase(it);
        return CELL_OK;
    }

    int64_t sys_ppu_thread_get_join_state(PPCContext& ctx)
    {
        guest_store_u32(ctx.r3.u32, self()->joinable ? 1 : 0);
        return CELL_OK;
    }

    int64_t sys_ppu_thread_set_priority(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        PpuThread* t = find_thread(ctx.r3.u32);
        if (!t) return CELL_ESRCH;
        t->prio = int32_t(ctx.r4.u32);
        return CELL_OK;
    }

    int64_t sys_ppu_thread_get_priority(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        PpuThread* t = find_thread(ctx.r3.u32);
        if (!t) return CELL_ESRCH;
        guest_store_u32(ctx.r4.u32, uint32_t(t->prio));
        return CELL_OK;
    }

    // sys_ppu_thread_get_stack_information(sys_ppu_thread_stack_t* {u32 addr, u32 size})
    int64_t sys_ppu_thread_get_stack_information(PPCContext& ctx)
    {
        guest_store_u32(ctx.r3.u32, self()->stack_addr);
        guest_store_u32(ctx.r3.u32 + 4, self()->stack_size);
        return CELL_OK;
    }

    // _sys_ppu_thread_create(thread_id*, param*, arg, unk, prio, stacksize, flags, threadname*)
    int64_t sys_ppu_thread_create(PPCContext& ctx)
    {
        const uint32_t id_ptr = ctx.r3.u32, param = ctx.r4.u32;
        if (!param || !guest_load_u32(param)) return CELL_EFAULT;
        const int32_t prio = int32_t(ctx.r7.u32);
        if (prio < -512 || prio > 3071) return CELL_EINVAL; // vsh is root: RPCS3 allows down to -512
        const uint32_t stack_size = uint32_t((ctx.r8.u64 ? ctx.r8.u64 : 4096) + 0xFFF) & ~0xFFFu;
        auto* t = new PpuThread;
        t->entry_opd = guest_load_u32(param);
        t->tls = guest_load_u32(param + 4);
        t->arg0 = ctx.r5.u64;
        t->arg1 = ctx.r6.u64;
        t->prio = prio;
        t->joinable = (ctx.r9.u64 & 1) != 0;
        t->name = guest_string(ctx.r10.u32, 27);
        t->stack_size = stack_size;
        t->stack_addr = alloc_stack(stack_size);
        if (!t->stack_addr)
        {
            delete t;
            return CELL_ENOMEM;
        }
        t->ctx = new_context();
        {
            KLock lock(g_kernel);
            t->id = g_next_thread_id++;
            g_threads[t->id] = t;
        }
        guest_store_u64(id_ptr, t->id);
        return CELL_OK;
    }

    int64_t sys_ppu_thread_start(PPCContext& ctx)
    {
        PpuThread* t;
        {
            KLock lock(g_kernel);
            t = find_thread(ctx.r3.u32);
            if (!t) return CELL_ESRCH;
            if (t->started) return CELL_EBUSY;
            t->started = true;
        }
        const uintptr_t h = _beginthreadex(nullptr, kHostStackReserve, thread_main, t, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
        if (!h) return CELL_EAGAIN;
        CloseHandle(HANDLE(h));
        return CELL_OK;
    }

    int64_t sys_ppu_thread_rename(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        PpuThread* t = find_thread(ctx.r3.u32);
        if (!t) return CELL_ESRCH;
        t->name = guest_string(ctx.r4.u32, 27);
        return CELL_OK;
    }
}

PpuThread* self() { return t_self; }

uint32_t current_thread_id() { return t_self ? t_self->id : 0; }

PpuThread* find_thread(uint32_t id)
{
    const auto it = g_threads.find(id);
    return it == g_threads.end() ? nullptr : it->second;
}

// Stacks come from RPCS3's vm::stack area, each preceded by a 4 KB guard (main stack: 0xD0001000).
uint32_t alloc_stack(uint32_t size)
{
    std::lock_guard lock(g_stack_mutex);
    const uint32_t addr = g_stack_next + 0x1000;
    if (uint64_t(addr) + size > 0xE0000000ull || !guest_commit(addr, size)) return 0;
    g_stack_next = addr + size;
    return addr;
}

void call_guest_opd(PPCContext& ctx, uint32_t opd)
{
    const uint32_t func = guest_load_u32(opd);
    ctx.r2.u64 = guest_load_u32(opd + 4);
    func_slot(func)(ctx, g_base);
}

void kernel_init_main_thread(PPCContext* ctx, uint32_t stack_addr, uint32_t stack_size)
{
    auto* t = new PpuThread;
    t->id = kMainThreadId;
    t->name = "main_thread";
    t->ctx = ctx;
    t->stack_addr = stack_addr;
    t->stack_size = stack_size;
    t->prio = 1001;
    t->started = true;
    t->joinable = false;
    t_self = t;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &t->host_thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
    KLock lock(g_kernel);
    g_threads[t->id] = t;
}

void rsx_dump_stats();

void dump_threads()
{
    rsx_dump_stats();
    spu_dump_stats();
    // Copy registry metadata under a nonblocking lock. Iterating the live unordered
    // map while other guest threads start/exit can invalidate its iterators.
    struct Snapshot { uint32_t id; std::string name; void* handle; PPCContext* ctx; };
    std::vector<Snapshot> threads;
    {
        KLock lock(g_kernel,std::try_to_lock);
        if (!lock.owns_lock()) { log_line("ppu: kernel busy while collecting thread registry"); return; }
        for (const auto& [id,t] : g_threads)
            if (!t->exited && t->host_thread && t->ctx)
                threads.push_back({id,t->name,t->host_thread,t->ctx});
    }
    // Do not hold the kernel lock while suspending guest threads.
    const uint64_t exe = uint64_t(GetModuleHandleW(nullptr));
    for (const auto& t : threads)
    {
        CONTEXT hc{};
        hc.ContextFlags = CONTEXT_CONTROL;
        uint64_t rip = 0;
        PPCContext c{};
        if (SuspendThread(HANDLE(t.handle)) != DWORD(-1))
        {
            if (GetThreadContext(HANDLE(t.handle), &hc)) rip = hc.Rip;
            c = *t.ctx;
            ResumeThread(HANDLE(t.handle));
        }
        log_line("  thread %08X '%s' host_tid=%lu host rip exe+%llX lr=%08X r1=%08X r3=%08X r4=%08X ctr=%08X", t.id, t.name.c_str(),
            (unsigned long)GetThreadId(HANDLE(t.handle)),
            (unsigned long long)(rip ? rip - exe : 0), uint32_t(c.lr), c.r1.u32, c.r3.u32, c.r4.u32, c.ctr.u32);
    }
}

void register_thread_syscalls()
{
    register_syscall(41, sys_ppu_thread_exit);
    register_syscall(43, sys_ppu_thread_yield);
    register_syscall(44, sys_ppu_thread_join);
    register_syscall(45, sys_ppu_thread_detach);
    register_syscall(46, sys_ppu_thread_get_join_state);
    register_syscall(47, sys_ppu_thread_set_priority);
    register_syscall(48, sys_ppu_thread_get_priority);
    register_syscall(49, sys_ppu_thread_get_stack_information);
    register_syscall(52, sys_ppu_thread_create);
    register_syscall(53, sys_ppu_thread_start);
    register_syscall(56, sys_ppu_thread_rename);
}
