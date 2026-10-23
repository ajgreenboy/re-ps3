// LV2 timers (sys_timer.cpp semantics): one-shot and periodic timers that post
// {source, data1, data2, expire time} to a connected event queue. One thread services all timers.
#include "kernel.h"

#include "k_event.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace
{
    constexpr uint32_t SYS_TIMER_STATE_STOP = 0, SYS_TIMER_STATE_RUN = 1;
    constexpr uint32_t kProcessId = 1;

    struct KTimer : KObject
    {
        uint32_t id = 0;
        uint32_t state = SYS_TIMER_STATE_STOP;
        std::shared_ptr<KEventQueue> queue;
        uint64_t source = 0, data1 = 0, data2 = 0;
        uint64_t expire = 0, period = 0;

        // Fires if due; returns microseconds until the next expiry, or UINT64_MAX. Caller holds g_kernel.
        uint64_t check(uint64_t now)
        {
            if (state != SYS_TIMER_STATE_RUN || now < expire) return state == SYS_TIMER_STATE_RUN ? expire - now : UINT64_MAX;
            if (queue && !queue->destroyed) event_queue_send(*queue, source, data1, data2, expire);
            if (period)
            {
                expire += period;
                return expire > now ? expire - now : 0;
            }
            state = SYS_TIMER_STATE_STOP;
            return UINT64_MAX;
        }
    };

    std::vector<std::shared_ptr<KTimer>> g_timers; // guarded by g_kernel
    std::condition_variable g_timer_cv;
    bool g_thread_started = false;

    void timer_thread()
    {
        KLock lock(g_kernel);
        for (;;)
        {
            uint64_t wait = UINT64_MAX;
            const uint64_t now = system_time_us();
            for (auto& t : g_timers) wait = std::min(wait, t->check(now));
            if (wait == UINT64_MAX) g_timer_cv.wait(lock);
            else g_timer_cv.wait_for(lock, std::chrono::microseconds(wait));
        }
    }

    // sys_timer_create(timer_id*)
    int64_t sys_timer_create(PPCContext& ctx)
    {
        auto t = std::make_shared<KTimer>();
        KLock lock(g_kernel);
        t->id = kobj_add(0x11, t);
        g_timers.push_back(t);
        if (!g_thread_started)
        {
            g_thread_started = true;
            std::thread(timer_thread).detach();
        }
        guest_store_u32(ctx.r3.u32, t->id);
        return CELL_OK;
    }

    int64_t sys_timer_destroy(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto t = kobj<KTimer>(ctx.r3.u32);
        if (!t) return CELL_ESRCH;
        if (t->queue) return CELL_EISCONN;
        std::erase(g_timers, t);
        kobj_remove(ctx.r3.u32);
        return CELL_OK;
    }

    // sys_timer_get_information(timer_id, info*): {u64 next_expire, u64 period, u32 state, u32 pad}
    int64_t sys_timer_get_information(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto t = kobj<KTimer>(ctx.r3.u32);
        if (!t) return CELL_ESRCH;
        t->check(system_time_us());
        guest_store_u64(ctx.r4.u32, t->expire);
        guest_store_u64(ctx.r4.u32 + 8, t->period);
        guest_store_u32(ctx.r4.u32 + 16, t->state);
        guest_store_u32(ctx.r4.u32 + 20, 0);
        return CELL_OK;
    }

    // _sys_timer_start(timer_id, base_time, period)
    int64_t sys_timer_start(PPCContext& ctx)
    {
        const uint64_t base = ctx.r4.u64, period = ctx.r5.u64;
        const uint64_t start = system_time_us();
        if (period && period < 100) return CELL_EINVAL;
        KLock lock(g_kernel);
        auto t = kobj<KTimer>(ctx.r3.u32);
        if (!t) return CELL_ESRCH;
        if (!t->queue) return CELL_ENOTCONN;
        t->check(start);
        if (t->state != SYS_TIMER_STATE_STOP) return CELL_EBUSY;
        if (!period && start >= base) return CELL_ETIMEDOUT;
        uint64_t expire;
        if (!period) expire = base;
        else if (!base) expire = start + period;
        else if (start < base + period) expire = base + period;
        else
        {
            const uint64_t aligned = start - start % period + base % period;
            expire = aligned + (aligned < start ? period : 0);
        }
        t->expire = expire;
        t->period = period;
        t->state = SYS_TIMER_STATE_RUN;
        g_timer_cv.notify_all();
        return CELL_OK;
    }

    int64_t sys_timer_stop(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto t = kobj<KTimer>(ctx.r3.u32);
        if (!t) return CELL_ESRCH;
        t->check(system_time_us());
        t->state = SYS_TIMER_STATE_STOP;
        return CELL_OK;
    }

    // sys_timer_connect_event_queue(timer_id, queue_id, name, data1, data2)
    int64_t sys_timer_connect_event_queue(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto t = kobj<KTimer>(ctx.r3.u32);
        auto q = find_event_queue(ctx.r4.u32);
        if (!t || !q) return CELL_ESRCH;
        if (t->queue) return CELL_EISCONN;
        t->queue = q;
        t->source = ctx.r5.u64 ? ctx.r5.u64 : (uint64_t(kProcessId) << 32) | ctx.r3.u32;
        t->data1 = ctx.r6.u64;
        t->data2 = ctx.r7.u64;
        return CELL_OK;
    }

    int64_t sys_timer_disconnect_event_queue(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto t = kobj<KTimer>(ctx.r3.u32);
        if (!t) return CELL_ESRCH;
        if (!t->queue) return CELL_ENOTCONN;
        t->state = SYS_TIMER_STATE_STOP;
        t->queue.reset();
        return CELL_OK;
    }
}

void register_timer_syscalls()
{
    register_syscall(70, sys_timer_create);
    register_syscall(71, sys_timer_destroy);
    register_syscall(72, sys_timer_get_information);
    register_syscall(73, sys_timer_start);
    register_syscall(74, sys_timer_stop);
    register_syscall(75, sys_timer_connect_event_queue);
    register_syscall(76, sys_timer_disconnect_event_queue);
}
