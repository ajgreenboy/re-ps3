// LV2 synchronisation objects: mutex, cond, lwmutex, lwcond, semaphore, rwlock, event flag.
// Semantics follow RPCS3's sys_mutex/cond/lwmutex/lwcond/semaphore/rwlock/event_flag.cpp. Ownership
// is handed directly to the next waiter on release; waiters are FIFO (protocol is not modelled).
#include "kernel.h"

#include <algorithm>
#include <map>

namespace
{
    constexpr uint32_t SYS_SYNC_RECURSIVE = 0x10;

    // lv2_obj::create: process-shared objects are found by IPC key; every success yields a new id.
    constexpr uint32_t SYS_SYNC_PROCESS_SHARED = 0x100, SYS_SYNC_NOT_PROCESS_SHARED = 0x200;
    constexpr int32_t SYS_SYNC_NEWLY_CREATED = 1, SYS_SYNC_NOT_CREATE = 2, SYS_SYNC_NOT_CARE = 3;
    std::map<std::pair<uint8_t, uint64_t>, std::shared_ptr<KObject>> g_ipc_objects;
    std::mutex g_ipc_mutex;

    template <typename Make>
    int64_t ipc_create(uint8_t type, uint32_t pshared, uint64_t key, int32_t flags, Make&& make, uint32_t id_ptr)
    {
        std::shared_ptr<KObject> obj;
        if (pshared == SYS_SYNC_PROCESS_SHARED)
        {
            if (!key) return CELL_EINVAL;
            if (flags != SYS_SYNC_NEWLY_CREATED && flags != SYS_SYNC_NOT_CREATE && flags != SYS_SYNC_NOT_CARE) return CELL_EINVAL;
            std::lock_guard lock(g_ipc_mutex);
            const auto it = g_ipc_objects.find({ type, key });
            if (it != g_ipc_objects.end())
            {
                if (flags == SYS_SYNC_NEWLY_CREATED) return CELL_EEXIST;
                obj = it->second;
            }
            else
            {
                if (flags == SYS_SYNC_NOT_CREATE) return CELL_ESRCH;
                obj = make();
                g_ipc_objects[{ type, key }] = obj;
            }
        }
        else if (pshared == SYS_SYNC_NOT_PROCESS_SHARED)
        {
            obj = make();
        }
        else
        {
            return CELL_EINVAL;
        }
        guest_store_u32(id_ptr, kobj_add(type, obj));
        return CELL_OK;
    }

    // Sleeps on q with a timeout; on timeout removes the caller and returns ETIMEDOUT,
    // otherwise returns the result the waker passed.
    int64_t wait_in(KLock& lock, std::deque<PpuThread*>& q, uint64_t timeout_us)
    {
        q.push_back(self());
        if (!sleep_thread(lock, timeout_us))
        {
            unqueue(q, self());
            return CELL_ETIMEDOUT;
        }
        return self()->wake_result;
    }

    // ---- mutex ----
    struct KMutex : KObject
    {
        bool recursive = false;
        PpuThread* owner = nullptr;
        uint32_t count = 0;
        std::deque<PpuThread*> sq;
        uint32_t cond_count = 0;

        // Releases fully and hands ownership to the next waiter.
        void release()
        {
            owner = nullptr;
            count = 0;
            if (!sq.empty())
            {
                owner = sq.front();
                sq.pop_front();
                count = 1;
                wake_thread(owner, CELL_OK);
            }
        }
    };

    int64_t sys_mutex_create(PPCContext& ctx)
    {
        const uint32_t id_ptr = ctx.r3.u32, attr = ctx.r4.u32;
        if (!id_ptr || !attr) return CELL_EFAULT;
        // sys_mutex_attribute_t: protocol, recursive, pshared, adaptive, u64 ipc_key, s32 flags
        const uint32_t protocol = guest_load_u32(attr), recursive = guest_load_u32(attr + 4), pshared = guest_load_u32(attr + 8);
        if (protocol < 1 || protocol > 3) return CELL_EINVAL;
        if (recursive != SYS_SYNC_RECURSIVE && recursive != 0x20) return CELL_EINVAL;
        const uint64_t key = pshared == SYS_SYNC_PROCESS_SHARED ? guest_load_u64(attr + 16) : 0;
        return ipc_create(0x85, pshared, key, int32_t(guest_load_u32(attr + 24)), [&] {
            auto m = std::make_shared<KMutex>();
            m->recursive = recursive == SYS_SYNC_RECURSIVE;
            return m;
        }, id_ptr);
    }

    int64_t sys_mutex_destroy(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto m = kobj<KMutex>(ctx.r3.u32);
        if (!m) return CELL_ESRCH;
        if (m->owner || !m->sq.empty()) return CELL_EBUSY;
        if (m->cond_count) return CELL_EPERM;
        kobj_remove(ctx.r3.u32);
        return CELL_OK;
    }

    int64_t mutex_lock(PPCContext& ctx, bool wait)
    {
        KLock lock(g_kernel);
        auto m = kobj<KMutex>(ctx.r3.u32);
        if (!m) return CELL_ESRCH;
        if (m->owner == self())
        {
            if (!m->recursive) return CELL_EDEADLK;
            if (m->count == 0xFFFFFFFF) return CELL_EKRESOURCE;
            m->count++;
            return CELL_OK;
        }
        if (!m->owner)
        {
            m->owner = self();
            m->count = 1;
            return CELL_OK;
        }
        if (!wait) return CELL_EBUSY;
        return wait_in(lock, m->sq, ctx.r4.u64);
    }

    int64_t sys_mutex_lock(PPCContext& ctx) { return mutex_lock(ctx, true); }
    int64_t sys_mutex_trylock(PPCContext& ctx) { return mutex_lock(ctx, false); }

    int64_t sys_mutex_unlock(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto m = kobj<KMutex>(ctx.r3.u32);
        if (!m) return CELL_ESRCH;
        if (m->owner != self()) return CELL_EPERM;
        if (--m->count == 0) m->release();
        return CELL_OK;
    }

    // ---- cond ----
    struct KCond : KObject
    {
        std::shared_ptr<KMutex> mutex;
        std::deque<PpuThread*> sq;
    };

    // Moves a woken cond waiter onto the mutex: it owns it now or waits in its queue.
    void cond_wake_to_mutex(KMutex& m, PpuThread* t)
    {
        if (!m.owner)
        {
            m.owner = t;
            m.count = 1;
            wake_thread(t, CELL_OK);
        }
        else
        {
            m.sq.push_back(t);
        }
    }

    int64_t sys_cond_create(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto m = kobj<KMutex>(ctx.r4.u32);
        if (!m) return CELL_ESRCH;
        // sys_cond_attribute_t: pshared, s32 flags, u64 ipc_key
        const uint32_t attr = ctx.r5.u32, pshared = guest_load_u32(attr);
        const uint64_t key = pshared == SYS_SYNC_PROCESS_SHARED ? guest_load_u64(attr + 8) : 0;
        return ipc_create(0x86, pshared, key, int32_t(guest_load_u32(attr + 4)), [&] {
            auto c = std::make_shared<KCond>();
            c->mutex = m;
            m->cond_count++;
            return c;
        }, ctx.r3.u32);
    }

    int64_t sys_cond_destroy(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto c = kobj<KCond>(ctx.r3.u32);
        if (!c) return CELL_ESRCH;
        if (!c->sq.empty()) return CELL_EBUSY;
        c->mutex->cond_count--;
        kobj_remove(ctx.r3.u32);
        return CELL_OK;
    }

    int64_t sys_cond_wait(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto c = kobj<KCond>(ctx.r3.u32);
        if (!c) return CELL_ESRCH;
        KMutex& m = *c->mutex;
        if (m.owner != self()) return CELL_EPERM;
        const uint32_t saved = m.count;
        m.release();
        c->sq.push_back(self());
        int64_t result = CELL_OK;
        if (!sleep_thread(lock, ctx.r4.u64))
        {
            // Timed out: if still waiting on the cond, take the mutex back before returning.
            if (unqueue(c->sq, self()))
            {
                result = CELL_ETIMEDOUT;
                if (!m.owner)
                {
                    m.owner = self();
                }
                else
                {
                    m.sq.push_back(self());
                    sleep_thread(lock, 0);
                }
            }
            else if (unqueue(m.sq, self()) || m.owner != self())
            {
                // Signalled but still waiting for the mutex
                if (m.owner != self())
                {
                    m.sq.push_back(self());
                    sleep_thread(lock, 0);
                }
            }
        }
        m.count = saved;
        return result;
    }

    int64_t cond_signal(uint32_t id, uint32_t to_thread, bool all)
    {
        KLock lock(g_kernel);
        auto c = kobj<KCond>(id);
        if (!c) return CELL_ESRCH;
        if (to_thread)
        {
            PpuThread* t = find_thread(to_thread);
            if (!t) return CELL_ESRCH;
            if (!unqueue(c->sq, t)) return CELL_EPERM;
            cond_wake_to_mutex(*c->mutex, t);
            return CELL_OK;
        }
        while (!c->sq.empty())
        {
            PpuThread* t = c->sq.front();
            c->sq.pop_front();
            cond_wake_to_mutex(*c->mutex, t);
            if (!all) break;
        }
        return CELL_OK;
    }

    int64_t sys_cond_signal(PPCContext& ctx) { return cond_signal(ctx.r3.u32, 0, false); }
    int64_t sys_cond_signal_all(PPCContext& ctx) { return cond_signal(ctx.r3.u32, 0, true); }
    int64_t sys_cond_signal_to(PPCContext& ctx) { return cond_signal(ctx.r3.u32, ctx.r4.u32, false); }

    // ---- lwmutex (kernel half; the fast path is guest atomics on sys_lwmutex_t) ----
    struct KLwmutex : KObject
    {
        int32_t signaled = 0; // bit 0: unlocked by unlock, INT32_MIN: by unlock2
        std::deque<PpuThread*> sq;
        uint32_t control = 0;
    };

    // Unlock without a waiter leaves a signal for the next locker; with one, hands it over.
    void lwmutex_unlock(KLwmutex& m, bool unlock2)
    {
        if (m.sq.empty())
        {
            m.signaled |= unlock2 ? INT32_MIN : 1;
            return;
        }
        PpuThread* t = m.sq.front();
        m.sq.pop_front();
        wake_thread(t, unlock2 ? CELL_EBUSY : CELL_OK);
    }

    // _sys_lwmutex_create(lwmutex_id*, protocol, control*, has_name, name)
    int64_t sys_lwmutex_create(PPCContext& ctx)
    {
        auto m = std::make_shared<KLwmutex>();
        m->control = ctx.r5.u32;
        guest_store_u32(ctx.r3.u32, kobj_add(0x95, m));
        return CELL_OK;
    }

    int64_t sys_lwmutex_destroy(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto m = kobj<KLwmutex>(ctx.r3.u32);
        if (!m) return CELL_ESRCH;
        if (!m->sq.empty()) return CELL_EBUSY;
        kobj_remove(ctx.r3.u32);
        return CELL_OK;
    }

    int64_t sys_lwmutex_lock(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto m = kobj<KLwmutex>(ctx.r3.u32);
        if (!m) return CELL_ESRCH;
        if (const int32_t s = m->signaled)
        {
            m->signaled = 0;
            return (s & 1) ? CELL_OK : CELL_EBUSY;
        }
        return wait_in(lock, m->sq, ctx.r4.u64);
    }

    int64_t sys_lwmutex_trylock(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto m = kobj<KLwmutex>(ctx.r3.u32);
        if (!m) return CELL_ESRCH;
        if (m->signaled & 1)
        {
            m->signaled = 0;
            return CELL_OK;
        }
        return CELL_EBUSY;
    }

    int64_t lwmutex_unlock_syscall(PPCContext& ctx, bool unlock2)
    {
        KLock lock(g_kernel);
        auto m = kobj<KLwmutex>(ctx.r3.u32);
        if (!m) return CELL_ESRCH;
        lwmutex_unlock(*m, unlock2);
        return CELL_OK;
    }

    int64_t sys_lwmutex_unlock(PPCContext& ctx) { return lwmutex_unlock_syscall(ctx, false); }
    int64_t sys_lwmutex_unlock2(PPCContext& ctx) { return lwmutex_unlock_syscall(ctx, true); }

    // ---- lwcond ----
    struct KLwcond : KObject
    {
        uint32_t lwmutex_id = 0;
        std::deque<PpuThread*> sq;
    };

    // _sys_lwcond_create(lwcond_id*, lwmutex_id, control*, name)
    int64_t sys_lwcond_create(PPCContext& ctx)
    {
        auto c = std::make_shared<KLwcond>();
        c->lwmutex_id = ctx.r4.u32;
        guest_store_u32(ctx.r3.u32, kobj_add(0x97, c));
        return CELL_OK;
    }

    int64_t sys_lwcond_destroy(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto c = kobj<KLwcond>(ctx.r3.u32);
        if (!c) return CELL_ESRCH;
        if (!c->sq.empty()) return CELL_EBUSY;
        kobj_remove(ctx.r3.u32);
        return CELL_OK;
    }

    // _sys_lwcond_queue_wait(lwcond_id, lwmutex_id, timeout): releases the lwmutex and sleeps.
    // A mode-1 signal moves the waiter onto the lwmutex queue, where it waits without a timeout.
    int64_t sys_lwcond_queue_wait(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto c = kobj<KLwcond>(ctx.r3.u32);
        auto m = kobj<KLwmutex>(ctx.r4.u32);
        if (!c || !m) return CELL_ESRCH;
        c->sq.push_back(self());
        lwmutex_unlock(*m, false);
        if (!sleep_thread(lock, ctx.r5.u64))
        {
            if (unqueue(c->sq, self())) return CELL_ETIMEDOUT;
            sleep_thread(lock, 0); // already moved to the lwmutex queue
        }
        return self()->wake_result;
    }

    // _sys_lwcond_signal(lwcond_id, lwmutex_id, ppu_thread_id, mode)
    //   mode 1: signaller owns the lwmutex, the waiter queues on it
    //   mode 2: signaller doesn't own it, the waiter returns EBUSY
    //   mode 3: signaller forcibly owned it, the first lwmutex waiter (in queue order) gets it
    int64_t sys_lwcond_signal(PPCContext& ctx)
    {
        const uint32_t to = ctx.r5.u32, mode = ctx.r6.u32;
        if (mode < 1 || mode > 3) return CELL_EINVAL;
        KLock lock(g_kernel);
        auto c = kobj<KLwcond>(ctx.r3.u32);
        if (!c) return CELL_ESRCH;
        std::shared_ptr<KLwmutex> m;
        if (mode != 2 && !(m = kobj<KLwmutex>(ctx.r4.u32))) return CELL_ESRCH;
        PpuThread* t = nullptr;
        if (to != 0xFFFFFFFF)
        {
            PpuThread* target = find_thread(to);
            if (!target) return CELL_ESRCH;
            if (unqueue(c->sq, target)) t = target;
        }
        else if (!c->sq.empty())
        {
            t = c->sq.front();
            c->sq.pop_front();
        }
        if (!t)
        {
            if (to == 0xFFFFFFFF) return mode == 3 ? CELL_ENOENT : mode == 2 ? CELL_OK : CELL_EPERM;
            return CELL_EPERM;
        }
        if (mode == 2)
        {
            wake_thread(t, CELL_EBUSY);
        }
        else if (mode == 1)
        {
            m->sq.push_back(t);
        }
        else if (!m->sq.empty())
        {
            m->sq.push_back(t);
            PpuThread* first = m->sq.front();
            m->sq.pop_front();
            wake_thread(first, CELL_OK);
        }
        else
        {
            wake_thread(t, CELL_OK);
        }
        return CELL_OK;
    }

    // _sys_lwcond_signal_all(lwcond_id, lwmutex_id, mode); mode 1 returns the number moved
    int64_t sys_lwcond_signal_all(PPCContext& ctx)
    {
        const uint32_t mode = ctx.r5.u32;
        if (mode < 1 || mode > 2) return CELL_EINVAL;
        KLock lock(g_kernel);
        auto c = kobj<KLwcond>(ctx.r3.u32);
        if (!c) return CELL_ESRCH;
        std::shared_ptr<KLwmutex> m;
        if (mode != 2 && !(m = kobj<KLwmutex>(ctx.r4.u32))) return CELL_ESRCH;
        int64_t n = 0;
        while (!c->sq.empty())
        {
            PpuThread* t = c->sq.front();
            c->sq.pop_front();
            if (mode == 1) m->sq.push_back(t);
            else wake_thread(t, CELL_EBUSY);
            n++;
        }
        return mode == 1 ? n : CELL_OK;
    }

    // ---- semaphore (val < 0 counts waiters, as in RPCS3) ----
    struct KSema : KObject
    {
        int32_t val = 0, max = 0;
        std::deque<PpuThread*> sq;
    };

    // sys_semaphore_create(sem_id*, attr*, initial, max)
    int64_t sys_semaphore_create(PPCContext& ctx)
    {
        const int32_t initial = int32_t(ctx.r5.u32), max = int32_t(ctx.r6.u32);
        if (max <= 0 || initial < 0 || initial > max) return CELL_EINVAL;
        // sys_semaphore_attribute_t: protocol, pshared, u64 ipc_key, s32 flags
        const uint32_t attr = ctx.r4.u32, pshared = guest_load_u32(attr + 4);
        const uint64_t key = pshared == SYS_SYNC_PROCESS_SHARED ? guest_load_u64(attr + 8) : 0;
        return ipc_create(0x96, pshared, key, int32_t(guest_load_u32(attr + 16)), [&] {
            auto s = std::make_shared<KSema>();
            s->val = initial;
            s->max = max;
            return s;
        }, ctx.r3.u32);
    }

    int64_t sys_semaphore_destroy(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto s = kobj<KSema>(ctx.r3.u32);
        if (!s) return CELL_ESRCH;
        if (!s->sq.empty()) return CELL_EBUSY;
        kobj_remove(ctx.r3.u32);
        return CELL_OK;
    }

    int64_t sys_semaphore_wait(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto s = kobj<KSema>(ctx.r3.u32);
        if (!s) return CELL_ESRCH;
        if (s->val-- > 0) return CELL_OK;
        const int64_t r = wait_in(lock, s->sq, ctx.r4.u64);
        if (r == CELL_ETIMEDOUT) s->val++;
        return r;
    }

    int64_t sys_semaphore_trywait(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto s = kobj<KSema>(ctx.r3.u32);
        if (!s) return CELL_ESRCH;
        if (s->val <= 0) return CELL_EBUSY;
        s->val--;
        return CELL_OK;
    }

    int64_t sys_semaphore_post(PPCContext& ctx)
    {
        const int32_t count = int32_t(ctx.r4.u32);
        KLock lock(g_kernel);
        auto s = kobj<KSema>(ctx.r3.u32);
        if (!s) return CELL_ESRCH;
        if (count <= 0) return CELL_EINVAL;
        if (int64_t(count) > int64_t(s->max) - s->val) return CELL_EBUSY;
        s->val += count;
        for (int32_t i = 0; i < count && !s->sq.empty(); i++)
        {
            PpuThread* t = s->sq.front();
            s->sq.pop_front();
            wake_thread(t, CELL_OK);
        }
        return CELL_OK;
    }

    int64_t sys_semaphore_get_value(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto s = kobj<KSema>(ctx.r3.u32);
        if (!s) return CELL_ESRCH;
        guest_store_u32(ctx.r4.u32, uint32_t(std::max(s->val, 0)));
        return CELL_OK;
    }

    // ---- rwlock ----
    struct KRwlock : KObject
    {
        PpuThread* writer = nullptr;
        uint32_t readers = 0;
        std::deque<PpuThread*> rq, wq;
    };

    // sys_rwlock_create(rw_lock_id*, attr*): attr = protocol, pshared, u64 ipc_key, s32 flags
    int64_t sys_rwlock_create(PPCContext& ctx)
    {
        const uint32_t attr = ctx.r4.u32, pshared = guest_load_u32(attr + 4);
        const uint64_t key = pshared == SYS_SYNC_PROCESS_SHARED ? guest_load_u64(attr + 8) : 0;
        return ipc_create(0x88, pshared, key, int32_t(guest_load_u32(attr + 16)), [] { return std::make_shared<KRwlock>(); }, ctx.r3.u32);
    }

    int64_t sys_rwlock_destroy(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto l = kobj<KRwlock>(ctx.r3.u32);
        if (!l) return CELL_ESRCH;
        if (l->writer || l->readers || !l->rq.empty() || !l->wq.empty()) return CELL_EBUSY;
        kobj_remove(ctx.r3.u32);
        return CELL_OK;
    }

    int64_t rwlock_rlock(PPCContext& ctx, bool wait)
    {
        KLock lock(g_kernel);
        auto l = kobj<KRwlock>(ctx.r3.u32);
        if (!l) return CELL_ESRCH;
        if (l->writer == self()) return CELL_EDEADLK;
        if (!l->writer && l->wq.empty())
        {
            l->readers++;
            return CELL_OK;
        }
        if (!wait) return CELL_EBUSY;
        return wait_in(lock, l->rq, ctx.r4.u64);
    }

    int64_t sys_rwlock_rlock(PPCContext& ctx) { return rwlock_rlock(ctx, true); }
    int64_t sys_rwlock_tryrlock(PPCContext& ctx) { return rwlock_rlock(ctx, false); }

    void rwlock_hand_off(KRwlock& l)
    {
        if (!l.wq.empty())
        {
            l.writer = l.wq.front();
            l.wq.pop_front();
            wake_thread(l.writer, CELL_OK);
            return;
        }
        while (!l.rq.empty())
        {
            l.readers++;
            wake_thread(l.rq.front(), CELL_OK);
            l.rq.pop_front();
        }
    }

    int64_t sys_rwlock_runlock(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto l = kobj<KRwlock>(ctx.r3.u32);
        if (!l) return CELL_ESRCH;
        if (!l->readers) return CELL_EPERM;
        if (--l->readers == 0) rwlock_hand_off(*l);
        return CELL_OK;
    }

    int64_t rwlock_wlock(PPCContext& ctx, bool wait)
    {
        KLock lock(g_kernel);
        auto l = kobj<KRwlock>(ctx.r3.u32);
        if (!l) return CELL_ESRCH;
        if (l->writer == self()) return CELL_EDEADLK;
        if (!l->writer && !l->readers)
        {
            l->writer = self();
            return CELL_OK;
        }
        if (!wait) return CELL_EBUSY;
        return wait_in(lock, l->wq, ctx.r4.u64);
    }

    int64_t sys_rwlock_wlock(PPCContext& ctx) { return rwlock_wlock(ctx, true); }
    int64_t sys_rwlock_trywlock(PPCContext& ctx) { return rwlock_wlock(ctx, false); }

    int64_t sys_rwlock_wunlock(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto l = kobj<KRwlock>(ctx.r3.u32);
        if (!l) return CELL_ESRCH;
        if (l->writer != self()) return CELL_EPERM;
        l->writer = nullptr;
        rwlock_hand_off(*l);
        return CELL_OK;
    }

    // ---- event flag ----
    constexpr uint32_t SYS_EVENT_FLAG_WAIT_AND = 0x01, SYS_EVENT_FLAG_WAIT_OR = 0x02;
    constexpr uint32_t SYS_EVENT_FLAG_WAIT_CLEAR = 0x10, SYS_EVENT_FLAG_WAIT_CLEAR_ALL = 0x20;

    struct KEventFlag : KObject
    {
        uint64_t pattern = 0;
        struct Waiter
        {
            PpuThread* t;
            uint64_t bits;
            uint32_t mode;
        };
        std::deque<Waiter> sq;

        static bool satisfied(uint64_t pattern, uint64_t bits, uint32_t mode)
        {
            return (mode & 0xF) == SYS_EVENT_FLAG_WAIT_AND ? (pattern & bits) == bits : (pattern & bits) != 0;
        }

        void consume(uint64_t bits, uint32_t mode)
        {
            if (mode & SYS_EVENT_FLAG_WAIT_CLEAR) pattern &= ~bits;
            if (mode & SYS_EVENT_FLAG_WAIT_CLEAR_ALL) pattern = 0;
        }
    };

    bool valid_flag_mode(uint32_t mode)
    {
        const uint32_t wait = mode & 0xF, clear = mode & ~0xFu;
        return (wait == SYS_EVENT_FLAG_WAIT_AND || wait == SYS_EVENT_FLAG_WAIT_OR) &&
            (clear == 0 || clear == SYS_EVENT_FLAG_WAIT_CLEAR || clear == SYS_EVENT_FLAG_WAIT_CLEAR_ALL);
    }

    // sys_event_flag_create(id*, attr*, init)
    int64_t sys_event_flag_create(PPCContext& ctx)
    {
        // sys_event_flag_attribute_t: protocol, pshared, u64 ipc_key, s32 flags, s32 type
        const uint32_t attr = ctx.r4.u32, pshared = guest_load_u32(attr + 4);
        const uint64_t key = pshared == SYS_SYNC_PROCESS_SHARED ? guest_load_u64(attr + 8) : 0;
        return ipc_create(0x98, pshared, key, int32_t(guest_load_u32(attr + 16)), [&] {
            auto f = std::make_shared<KEventFlag>();
            f->pattern = ctx.r5.u64;
            return f;
        }, ctx.r3.u32);
    }

    int64_t sys_event_flag_destroy(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto f = kobj<KEventFlag>(ctx.r3.u32);
        if (!f) return CELL_ESRCH;
        if (!f->sq.empty()) return CELL_EBUSY;
        kobj_remove(ctx.r3.u32);
        return CELL_OK;
    }

    // sys_event_flag_wait(id, bitptn, mode, result*, timeout)
    int64_t event_flag_wait(PPCContext& ctx, bool wait)
    {
        const uint64_t bits = ctx.r4.u64;
        const uint32_t mode = ctx.r5.u32, result = ctx.r6.u32;
        if (!valid_flag_mode(mode)) return CELL_EINVAL;
        KLock lock(g_kernel);
        auto f = kobj<KEventFlag>(ctx.r3.u32);
        if (!f) return CELL_ESRCH;
        if (KEventFlag::satisfied(f->pattern, bits, mode))
        {
            if (result) guest_store_u64(result, f->pattern);
            f->consume(bits, mode);
            return CELL_OK;
        }
        if (!wait)
        {
            if (result) guest_store_u64(result, 0);
            return CELL_EBUSY;
        }
        f->sq.push_back({ self(), bits, mode });
        if (!sleep_thread(lock, ctx.r7.u64))
        {
            for (auto it = f->sq.begin(); it != f->sq.end(); ++it)
                if (it->t == self()) { f->sq.erase(it); break; }
            if (result) guest_store_u64(result, f->pattern);
            return CELL_ETIMEDOUT;
        }
        if (result) guest_store_u64(result, self()->wake_data[0]);
        return self()->wake_result;
    }

    int64_t sys_event_flag_wait(PPCContext& ctx) { return event_flag_wait(ctx, true); }
    int64_t sys_event_flag_trywait(PPCContext& ctx) { return event_flag_wait(ctx, false); }

    int64_t sys_event_flag_set(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto f = kobj<KEventFlag>(ctx.r3.u32);
        if (!f) return CELL_ESRCH;
        f->pattern |= ctx.r4.u64;
        for (auto it = f->sq.begin(); it != f->sq.end();)
        {
            if (KEventFlag::satisfied(f->pattern, it->bits, it->mode))
            {
                it->t->wake_data[0] = f->pattern;
                f->consume(it->bits, it->mode);
                wake_thread(it->t, CELL_OK);
                it = f->sq.erase(it);
            }
            else
            {
                ++it;
            }
        }
        return CELL_OK;
    }

    int64_t sys_event_flag_clear(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto f = kobj<KEventFlag>(ctx.r3.u32);
        if (!f) return CELL_ESRCH;
        f->pattern &= ctx.r4.u64;
        return CELL_OK;
    }

    int64_t sys_event_flag_cancel(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto f = kobj<KEventFlag>(ctx.r3.u32);
        if (!f) return CELL_ESRCH;
        const uint32_t n = uint32_t(f->sq.size());
        for (auto& w : f->sq)
        {
            w.t->wake_data[0] = f->pattern;
            wake_thread(w.t, CELL_ECANCELED);
        }
        f->sq.clear();
        if (ctx.r4.u32) guest_store_u32(ctx.r4.u32, n);
        return CELL_OK;
    }

    int64_t sys_event_flag_get(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto f = kobj<KEventFlag>(ctx.r3.u32);
        if (!f)
        {
            if (ctx.r4.u32) guest_store_u64(ctx.r4.u32, 0);
            return CELL_ESRCH;
        }
        guest_store_u64(ctx.r4.u32, f->pattern);
        return CELL_OK;
    }
}

void register_sync_syscalls()
{
    register_syscall(82, sys_event_flag_create);
    register_syscall(83, sys_event_flag_destroy);
    register_syscall(85, sys_event_flag_wait);
    register_syscall(86, sys_event_flag_trywait);
    register_syscall(87, sys_event_flag_set);
    register_syscall(90, sys_semaphore_create);
    register_syscall(91, sys_semaphore_destroy);
    register_syscall(92, sys_semaphore_wait);
    register_syscall(93, sys_semaphore_trywait);
    register_syscall(94, sys_semaphore_post);
    register_syscall(95, sys_lwmutex_create);
    register_syscall(96, sys_lwmutex_destroy);
    register_syscall(97, sys_lwmutex_lock);
    register_syscall(98, sys_lwmutex_unlock);
    register_syscall(99, sys_lwmutex_trylock);
    register_syscall(100, sys_mutex_create);
    register_syscall(101, sys_mutex_destroy);
    register_syscall(102, sys_mutex_lock);
    register_syscall(103, sys_mutex_trylock);
    register_syscall(104, sys_mutex_unlock);
    register_syscall(105, sys_cond_create);
    register_syscall(106, sys_cond_destroy);
    register_syscall(107, sys_cond_wait);
    register_syscall(108, sys_cond_signal);
    register_syscall(109, sys_cond_signal_all);
    register_syscall(110, sys_cond_signal_to);
    register_syscall(111, sys_lwcond_create);
    register_syscall(112, sys_lwcond_destroy);
    register_syscall(113, sys_lwcond_queue_wait);
    register_syscall(114, sys_semaphore_get_value);
    register_syscall(115, sys_lwcond_signal);
    register_syscall(116, sys_lwcond_signal_all);
    register_syscall(117, sys_lwmutex_unlock2);
    register_syscall(118, sys_event_flag_clear);
    register_syscall(120, sys_rwlock_create);
    register_syscall(121, sys_rwlock_destroy);
    register_syscall(122, sys_rwlock_rlock);
    register_syscall(123, sys_rwlock_tryrlock);
    register_syscall(124, sys_rwlock_runlock);
    register_syscall(125, sys_rwlock_wlock);
    register_syscall(126, sys_rwlock_trywlock);
    register_syscall(127, sys_rwlock_wunlock);
    register_syscall(132, sys_event_flag_cancel);
    register_syscall(139, sys_event_flag_get);
}
