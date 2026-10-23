// LV2 event queues and ports (sys_event.cpp semantics).
#include "kernel.h"

#include "k_event.h"

namespace
{
    constexpr uint32_t SYS_PPU_QUEUE = 1, SYS_SPU_QUEUE = 2;
    constexpr uint32_t SYS_EVENT_PORT_LOCAL = 1, SYS_EVENT_PORT_IPC = 3;
    constexpr uint32_t kProcessId = 1; // RPCS3 process_getpid()

    std::unordered_map<uint64_t, uint32_t> g_ipc_queues; // ipc key -> queue id, guarded by g_kernel

    struct KEventPort : KObject
    {
        uint32_t type = SYS_EVENT_PORT_LOCAL;
        uint64_t name = 0;
        std::shared_ptr<KEventQueue> queue;
    };

    // sys_event_queue_create(equeue_id*, attr*, ipc_key, size)
    int64_t sys_event_queue_create(PPCContext& ctx)
    {
        const uint32_t id_ptr = ctx.r3.u32, attr = ctx.r4.u32;
        const uint64_t key = ctx.r5.u64;
        const int32_t size = int32_t(ctx.r6.u32);
        if (size <= 0 || size > 127) return CELL_EINVAL;
        const uint32_t type = guest_load_u32(attr + 4);
        if (type != SYS_PPU_QUEUE && type != SYS_SPU_QUEUE) return CELL_EINVAL;
        KLock lock(g_kernel);
        if (key && g_ipc_queues.count(key)) return CELL_EEXIST;
        auto q = std::make_shared<KEventQueue>();
        q->type = type;
        q->size = uint32_t(size);
        q->key = key;
        q->name = guest_load_u64(attr + 8);
        const uint32_t id = kobj_add(0x8D, q);
        if (key) g_ipc_queues[key] = id;
        guest_store_u32(id_ptr, id);
        return CELL_OK;
    }

    // sys_event_queue_destroy(equeue_id, mode): mode 1 (FORCE) cancels waiters
    int64_t sys_event_queue_destroy(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto q = kobj<KEventQueue>(ctx.r3.u32);
        if (!q) return CELL_ESRCH;
        if (!q->sq.empty() && ctx.r4.u32 != 1) return CELL_EBUSY;
        for (PpuThread* t : q->sq) wake_thread(t, CELL_ECANCELED);
        q->sq.clear();
        q->destroyed = true;
        if (q->key) g_ipc_queues.erase(q->key);
        kobj_remove(ctx.r3.u32);
        return CELL_OK;
    }

    // sys_event_queue_receive(equeue_id, event* (unused), timeout): event returned in r4..r7
    int64_t sys_event_queue_receive(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto q = kobj<KEventQueue>(ctx.r3.u32);
        if (!q) return CELL_ESRCH;
        if (q->type != SYS_PPU_QUEUE) return CELL_EINVAL;
        if (!q->events.empty())
        {
            const auto e = q->events.front();
            q->events.pop_front();
            ctx.r4.u64 = e[0];
            ctx.r5.u64 = e[1];
            ctx.r6.u64 = e[2];
            ctx.r7.u64 = e[3];
            return CELL_OK;
        }
        q->sq.push_back(self());
        if (!sleep_thread(lock, ctx.r5.u64))
        {
            unqueue(q->sq, self());
            return CELL_ETIMEDOUT;
        }
        if (self()->wake_result == CELL_OK)
        {
            ctx.r4.u64 = self()->wake_data[0];
            ctx.r5.u64 = self()->wake_data[1];
            ctx.r6.u64 = self()->wake_data[2];
            ctx.r7.u64 = self()->wake_data[3];
        }
        return self()->wake_result;
    }

    // sys_event_queue_tryreceive(equeue_id, event_array*, size, number*)
    int64_t sys_event_queue_tryreceive(PPCContext& ctx)
    {
        const uint32_t array = ctx.r4.u32, number = ctx.r6.u32;
        const int32_t size = int32_t(ctx.r5.u32);
        KLock lock(g_kernel);
        auto q = kobj<KEventQueue>(ctx.r3.u32);
        if (!q) return CELL_ESRCH;
        if (q->type != SYS_PPU_QUEUE) return CELL_EINVAL;
        uint32_t n = 0;
        while (int32_t(n) < size && !q->events.empty())
        {
            const auto e = q->events.front();
            q->events.pop_front();
            for (int i = 0; i < 4; i++) guest_store_u64(array + n * 32 + i * 8, e[i]);
            n++;
        }
        guest_store_u32(number, n);
        return CELL_OK;
    }

    int64_t sys_event_queue_drain(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto q = kobj<KEventQueue>(ctx.r3.u32);
        if (!q) return CELL_ESRCH;
        q->events.clear();
        return CELL_OK;
    }

    // sys_event_port_create(eport_id*, port_type, name)
    int64_t sys_event_port_create(PPCContext& ctx)
    {
        const uint32_t type = ctx.r4.u32;
        if (type != SYS_EVENT_PORT_LOCAL && type != SYS_EVENT_PORT_IPC) return CELL_EINVAL;
        auto p = std::make_shared<KEventPort>();
        p->type = type;
        p->name = ctx.r5.u64;
        guest_store_u32(ctx.r3.u32, kobj_add(0x0E, p));
        return CELL_OK;
    }

    int64_t sys_event_port_destroy(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto p = kobj<KEventPort>(ctx.r3.u32);
        if (!p) return CELL_ESRCH;
        if (p->queue) return CELL_EISCONN;
        kobj_remove(ctx.r3.u32);
        return CELL_OK;
    }

    int64_t sys_event_port_connect_local(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto p = kobj<KEventPort>(ctx.r3.u32);
        auto q = kobj<KEventQueue>(ctx.r4.u32);
        if (!p || !q) return CELL_ESRCH;
        if (p->type != SYS_EVENT_PORT_LOCAL) return CELL_EINVAL;
        if (p->queue) return CELL_EISCONN;
        p->queue = q;
        return CELL_OK;
    }

    // sys_event_port_connect_ipc(eport_id, ipc_key)
    int64_t sys_event_port_connect_ipc(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto p = kobj<KEventPort>(ctx.r3.u32);
        if (!p) return CELL_ESRCH;
        const auto it = g_ipc_queues.find(ctx.r4.u64);
        if (it == g_ipc_queues.end()) return CELL_ESRCH;
        if (p->type != SYS_EVENT_PORT_IPC) return CELL_EINVAL;
        if (p->queue) return CELL_EISCONN;
        p->queue = kobj<KEventQueue>(it->second);
        return CELL_OK;
    }

    int64_t sys_event_port_disconnect(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto p = kobj<KEventPort>(ctx.r3.u32);
        if (!p) return CELL_ESRCH;
        if (!p->queue) return CELL_ENOTCONN;
        p->queue.reset();
        return CELL_OK;
    }

    // sys_event_port_send(eport_id, data1, data2, data3)
    int64_t sys_event_port_send(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto p = kobj<KEventPort>(ctx.r3.u32);
        if (!p) return CELL_ESRCH;
        if (!p->queue || p->queue->destroyed) return CELL_ENOTCONN;
        const uint64_t source = p->name ? p->name : (uint64_t(kProcessId) << 32) | ctx.r3.u32;
        return event_queue_send(*p->queue, source, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64) ? CELL_OK : CELL_EBUSY;
    }
}

bool event_queue_send(KEventQueue& q, uint64_t source, uint64_t d1, uint64_t d2, uint64_t d3)
{
    if (!q.sq.empty())
    {
        PpuThread* t = q.sq.front();
        q.sq.pop_front();
        t->wake_data[0] = source;
        t->wake_data[1] = d1;
        t->wake_data[2] = d2;
        t->wake_data[3] = d3;
        wake_thread(t, CELL_OK);
        return true;
    }
    if (q.events.size() >= q.size) return false;
    q.events.push_back({ source, d1, d2, d3 });
    return true;
}

std::shared_ptr<KEventQueue> find_event_queue(uint32_t id) { return kobj<KEventQueue>(id); }

uint32_t create_event_queue_internal(uint32_t size, std::shared_ptr<KEventQueue>* out)
{
    auto q = std::make_shared<KEventQueue>();
    q->type = SYS_PPU_QUEUE;
    q->size = size;
    if (out) *out = q;
    return kobj_add(0x8D, q);
}

uint32_t create_local_port_internal(const std::shared_ptr<KEventQueue>& q)
{
    auto p = std::make_shared<KEventPort>();
    p->queue = q;
    return kobj_add(0x0E, p);
}

int32_t event_port_send_internal(uint32_t port_id, uint64_t d1, uint64_t d2, uint64_t d3)
{
    auto p = kobj<KEventPort>(port_id);
    if (!p) return CELL_ESRCH;
    if (!p->queue || p->queue->destroyed) return CELL_ENOTCONN;
    const uint64_t source = p->name ? p->name : (uint64_t(kProcessId) << 32) | port_id;
    return event_queue_send(*p->queue, source, d1, d2, d3) ? CELL_OK : CELL_EBUSY;
}

void register_event_syscalls()
{
    register_syscall(128, sys_event_queue_create);
    register_syscall(129, sys_event_queue_destroy);
    register_syscall(130, sys_event_queue_receive);
    register_syscall(131, sys_event_queue_tryreceive);
    register_syscall(133, sys_event_queue_drain);
    register_syscall(134, sys_event_port_create);
    register_syscall(135, sys_event_port_destroy);
    register_syscall(136, sys_event_port_connect_local);
    register_syscall(137, sys_event_port_disconnect);
    register_syscall(138, sys_event_port_send);
    register_syscall(140, sys_event_port_connect_ipc);
}
