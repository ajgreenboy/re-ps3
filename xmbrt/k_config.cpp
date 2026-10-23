// sys_config: services, listeners and service events delivered through an event queue
// (sys_config.cpp semantics). On first open, the pad manager services are registered, which is how
// vsh learns a controller is connected.
#include "kernel.h"

#include "k_event.h"

#include <cstring>

namespace
{
    constexpr uint64_t SYS_CONFIG_SERVICE_PADMANAGER = 0x11, SYS_CONFIG_SERVICE_PADMANAGER2 = 0x12;
    constexpr uint64_t SYS_CONFIG_SERVICE_LISTENER_ONCE = 0;
    constexpr uint64_t SYS_CONFIG_EVENT_SOURCE_SERVICE = 1;

    struct Service
    {
        uint32_t id;
        uint64_t service_id, user_id, verbosity;
        uint32_t padding;
        std::vector<uint8_t> data;
        bool registered = true;
        uint64_t timestamp;
        // sizeof(sys_config_service_event_t) - 1 + data
        uint64_t event_size() const { return 47 + data.size(); }
    };

    struct Handle
    {
        uint32_t id;
        std::shared_ptr<KEventQueue> queue;
    };

    struct Listener
    {
        uint32_t id;
        std::shared_ptr<Handle> handle;
        uint64_t service_id, min_verbosity, type;
        std::vector<uint8_t> data;
        uint32_t events_sent = 0;
    };

    struct ServiceEvent
    {
        uint32_t id;
        std::shared_ptr<Handle> handle;
        std::shared_ptr<Service> service;
        uint32_t listener_id;
    };

    // Guarded by g_kernel, since delivery posts to event queues.
    std::vector<std::shared_ptr<Handle>> g_handles;
    std::vector<std::shared_ptr<Service>> g_services;
    std::vector<std::shared_ptr<Listener>> g_listeners;
    std::unordered_map<uint32_t, ServiceEvent> g_events;
    uint32_t g_next_handle, g_next_service, g_next_listener, g_next_event;
    uint64_t g_timestamp;
    bool g_initialized;

    bool applies(const Listener& l, const Service& s)
    {
        if (l.type == SYS_CONFIG_SERVICE_LISTENER_ONCE && l.events_sent) return false;
        if (l.service_id != s.service_id || l.min_verbosity > s.verbosity) return false;
        // Real hardware only sends pad events to listeners whose data starts with 0x01
        if (l.service_id == SYS_CONFIG_SERVICE_PADMANAGER && (l.data.empty() || l.data[0] != 0x01)) return false;
        return true;
    }

    void deliver(Listener& l, const std::shared_ptr<Service>& s)
    {
        if (!applies(l, *s)) return;
        const uint32_t event_id = g_next_event++;
        g_events[event_id] = { event_id, l.handle, s, l.id };
        l.events_sent++;
        const uint64_t d2 = (uint64_t(s->registered) << 32) | event_id;
        if (!event_queue_send(*l.handle->queue, SYS_CONFIG_EVENT_SOURCE_SERVICE, l.handle->id, d2, s->event_size()))
        {
            g_events.erase(event_id);
            l.events_sent--;
        }
    }

    std::shared_ptr<Service> add_service(uint64_t service_id, uint64_t user_id, uint64_t verbosity, uint32_t padding, const uint8_t* data, size_t size)
    {
        auto s = std::make_shared<Service>();
        s->id = 0x43000000 | (g_next_service++ << 8);
        s->service_id = service_id;
        s->user_id = user_id;
        s->verbosity = verbosity;
        s->padding = padding;
        s->data.assign(data, data + size);
        s->timestamp = g_timestamp++;
        g_services.push_back(s);
        for (auto& l : g_listeners) deliver(*l, s);
        return s;
    }

    void initialize()
    {
        if (g_initialized) return;
        g_initialized = true;
        // HID descriptor-like blob RPCS3 registers for controller port 0 (VID 054C, PID 0268)
        static const uint8_t hid_info[0x1A] = { 0x01, 0x01, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x4C, 0x02,
            0x68, 0x00, 0x10, 0x91, 0x88, 0x04, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00 };
        add_service(SYS_CONFIG_SERVICE_PADMANAGER, 0, 1, 0, hid_info, sizeof(hid_info));
        add_service(SYS_CONFIG_SERVICE_PADMANAGER2, 0, 1, 0, hid_info, sizeof(hid_info));
    }

    std::shared_ptr<Handle> find_handle(uint32_t id)
    {
        for (auto& h : g_handles)
            if (h->id == id) return h;
        return nullptr;
    }

    // sys_config_open(equeue_id, out_config_hdl*)
    int64_t sys_config_open(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto q = find_event_queue(ctx.r3.u32);
        if (!q) return CELL_ESRCH;
        initialize();
        auto h = std::make_shared<Handle>();
        h->id = 0x41000000 | (g_next_handle++ << 8);
        h->queue = q;
        g_handles.push_back(h);
        guest_store_u32(ctx.r4.u32, h->id);
        return CELL_OK;
    }

    int64_t sys_config_close(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        for (auto it = g_handles.begin(); it != g_handles.end(); ++it)
        {
            if ((*it)->id != ctx.r3.u32) continue;
            g_handles.erase(it);
            return CELL_OK;
        }
        return CELL_ESRCH;
    }

    // sys_config_add_service_listener(config_hdl, service_id, min_verbosity, in*, size, type, out_listener_hdl*)
    int64_t sys_config_add_service_listener(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto h = find_handle(ctx.r3.u32);
        if (!h) return CELL_ESRCH;
        auto l = std::make_shared<Listener>();
        l->id = 0x42000000 | (g_next_listener++ << 8);
        l->handle = h;
        l->service_id = ctx.r4.u64;
        l->min_verbosity = ctx.r5.u64;
        l->type = ctx.r8.u64;
        if (ctx.r6.u32 && ctx.r7.u64) l->data.assign(g_base + ctx.r6.u32, g_base + ctx.r6.u32 + ctx.r7.u64);
        g_listeners.push_back(l);
        // Notify with all past services, oldest first
        for (auto& s : g_services)
            if (s->registered) deliver(*l, s);
        guest_store_u32(ctx.r9.u32, l->id);
        return CELL_OK;
    }

    int64_t sys_config_remove_service_listener(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        for (auto it = g_listeners.begin(); it != g_listeners.end(); ++it)
        {
            if ((*it)->id != ctx.r4.u32) continue;
            g_listeners.erase(it);
            return CELL_OK;
        }
        return CELL_ESRCH;
    }

    // sys_config_register_service(config_hdl, service_id, user_id, verbosity, data_buf*, size, out_service_hdl*)
    int64_t sys_config_register_service(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        if (!find_handle(ctx.r3.u32)) return CELL_ESRCH;
        auto s = add_service(ctx.r4.u64, ctx.r5.u64, ctx.r6.u64, 0, ctx.r7.u32 ? g_base + ctx.r7.u32 : nullptr, ctx.r7.u32 ? size_t(ctx.r8.u64) : 0);
        guest_store_u32(ctx.r9.u32, s->id);
        return CELL_OK;
    }

    // sys_config_unregister_service(config_hdl, service_hdl)
    int64_t sys_config_unregister_service(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        for (auto it = g_services.begin(); it != g_services.end(); ++it)
        {
            if ((*it)->id != ctx.r4.u32) continue;
            auto s = *it;
            s->registered = false;
            for (auto& l : g_listeners) deliver(*l, s);
            g_services.erase(it);
            return CELL_OK;
        }
        return CELL_ESRCH;
    }

    // sys_config_get_service_event(config_hdl, event_id, dst*, size)
    int64_t sys_config_get_service_event(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        auto h = find_handle(ctx.r3.u32);
        if (!h) return CELL_ESRCH;
        const auto it = g_events.find(ctx.r4.u32);
        if (it == g_events.end() || it->second.handle != h) return CELL_ESRCH;
        const ServiceEvent& e = it->second;
        if (e.service->event_size() > ctx.r6.u64) return CELL_EAGAIN;
        // sys_config_service_event_t: u32 listener, u32 registered, u64 service_id, u64 user_id,
        // [u64 verbosity, u32 data_size, u32 padding, u8 data[]] when registered
        const uint32_t dst = ctx.r5.u32;
        guest_store_u32(dst, e.listener_id);
        guest_store_u32(dst + 4, e.service->registered ? 1 : 0);
        guest_store_u64(dst + 8, e.service->service_id);
        guest_store_u64(dst + 16, e.service->user_id);
        if (e.service->registered)
        {
            guest_store_u64(dst + 24, e.service->verbosity);
            guest_store_u32(dst + 32, uint32_t(e.service->data.size()));
            guest_store_u32(dst + 36, e.service->padding);
            if (!e.service->data.empty()) memcpy(g_base + dst + 40, e.service->data.data(), e.service->data.size());
        }
        return CELL_OK;
    }
}

void register_config_syscalls()
{
    register_syscall(516, sys_config_open);
    register_syscall(517, sys_config_close);
    register_syscall(518, sys_config_get_service_event);
    register_syscall(519, sys_config_add_service_listener);
    register_syscall(520, sys_config_remove_service_listener);
    register_syscall(521, sys_config_register_service);
    register_syscall(522, sys_config_unregister_service);
}
