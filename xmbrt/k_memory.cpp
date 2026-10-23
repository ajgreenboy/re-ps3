// LV2 memory: sys_memory_* (user pages), memory containers, sys_mmapper_* (address ranges + shared memory).
// Areas follow what RPCS3 hands vsh: 64 KB pages from 0x10000000, 1 MB pages from 0x20000000,
// mmapper ranges from 0x30000000 in 256 MB steps.
#include "kernel.h"

#include <algorithm>
#include <map>

namespace
{
    constexpr uint32_t SYS_MEMORY_PAGE_SIZE_1M = 0x400, SYS_MEMORY_PAGE_SIZE_64K = 0x200;

    struct Area
    {
        uint32_t base, size, page;
        std::map<uint32_t, uint32_t> used; // addr -> size

        uint32_t alloc(uint32_t want, uint32_t align)
        {
            want = (want + page - 1) & ~(page - 1);
            align = std::max(align, page);
            uint32_t at = base;
            for (const auto& [a, s] : used)
            {
                if (uint64_t(at) + want <= a) break;
                at = uint32_t((uint64_t(a) + s + align - 1) & ~uint64_t(align - 1));
            }
            if (uint64_t(at) + want > uint64_t(base) + size) return 0;
            if (!guest_commit(at, want)) return 0;
            memset(g_base + at, 0, want);
            used[at] = want;
            return at;
        }

        bool free(uint32_t addr)
        {
            return used.erase(addr) != 0; // pages stay committed; contents are zeroed on reuse
        }

        bool contains(uint32_t addr) const { return addr - base < size; }
    };

    std::mutex g_mem_mutex;
    Area g_user64k{ 0x10000000, 0x10000000, 0x10000, {} };
    Area g_user1m{ 0x20000000, 0x10000000, 0x100000, {} };

    struct KContainer : KObject
    {
        uint32_t size = 0, used = 0;
    };

    struct KShm : KObject
    {
        uint32_t size = 0, flags = 0;
        uint32_t mapped_at = 0;
    };

    // mmapper address ranges, 256 MB granularity from 0x30000000 up to video memory
    std::map<uint32_t, uint32_t> g_mmap_ranges; // addr -> size
    std::map<uint32_t, uint32_t> g_mmap_maps;   // mapped addr -> shm id

    // sys_memory_allocate(size, flags, alloc_addr*)
    int64_t sys_memory_allocate(PPCContext& ctx)
    {
        const uint32_t size = ctx.r3.u32, flags = ctx.r4.u32, out = ctx.r5.u32;
        if (!out) return CELL_EFAULT;
        Area* area = flags == SYS_MEMORY_PAGE_SIZE_64K ? &g_user64k : flags == SYS_MEMORY_PAGE_SIZE_1M || flags == 0 ? &g_user1m : nullptr;
        if (!area) return CELL_EINVAL;
        if (!size || size % area->page) return CELL_EALIGN;
        std::lock_guard lock(g_mem_mutex);
        const uint32_t addr = area->alloc(size, area->page);
        if (!addr) return CELL_ENOMEM;
        guest_store_u32(out, addr);
        return CELL_OK;
    }

    // sys_memory_allocate_from_container(size, cid, flags, alloc_addr*)
    int64_t sys_memory_allocate_from_container(PPCContext& ctx)
    {
        auto c = kobj<KContainer>(ctx.r4.u32);
        if (!c) return CELL_ESRCH;
        PPCContext shifted = ctx;
        shifted.r4 = ctx.r5;
        shifted.r5 = ctx.r6;
        return sys_memory_allocate(shifted);
    }

    int64_t sys_memory_free(PPCContext& ctx)
    {
        std::lock_guard lock(g_mem_mutex);
        const uint32_t addr = ctx.r3.u32;
        if (g_user64k.contains(addr) ? g_user64k.free(addr) : g_user1m.contains(addr) && g_user1m.free(addr)) return CELL_OK;
        return CELL_EINVAL;
    }

    // sys_memory_get_page_attribute(addr, attr*): {u64 attribute, u64 access_right, u32 page_size, u32 pad}
    int64_t sys_memory_get_page_attribute(PPCContext& ctx)
    {
        const uint32_t addr = ctx.r3.u32, attr = ctx.r4.u32;
        guest_store_u64(attr, 0x40000ull); // SYS_MEMORY_PROT_READ_WRITE
        guest_store_u64(attr + 8, 0xFull);  // SYS_MEMORY_ACCESS_RIGHT_ANY
        guest_store_u32(attr + 16, addr >= 0xD0000000 ? 0x1000 : g_user1m.contains(addr) ? 0x100000 : 0x10000);
        guest_store_u32(attr + 20, 0);
        return CELL_OK;
    }

    // sys_memory_get_user_memory_size(mem_info*): {u32 total, u32 available}
    int64_t sys_memory_get_user_memory_size(PPCContext& ctx)
    {
        std::lock_guard lock(g_mem_mutex);
        uint64_t used = 0;
        for (const auto& [a, s] : g_user64k.used) used += s;
        for (const auto& [a, s] : g_user1m.used) used += s;
        const uint32_t total = 0x0D500000; // RPCS3's default user memory size
        guest_store_u32(ctx.r3.u32, total);
        guest_store_u32(ctx.r3.u32 + 4, uint32_t(total > used ? total - used : 0));
        return CELL_OK;
    }

    // sys_memory_container_create(cid*, size)
    int64_t sys_memory_container_create(PPCContext& ctx)
    {
        auto c = std::make_shared<KContainer>();
        c->size = ctx.r4.u32;
        guest_store_u32(ctx.r3.u32, kobj_add(0x3F, c));
        return CELL_OK;
    }

    int64_t sys_memory_container_destroy(PPCContext& ctx)
    {
        return kobj_remove(ctx.r3.u32) ? CELL_OK : CELL_ESRCH;
    }

    // sys_memory_container_get_size(mem_info*, cid): {total, available}
    int64_t sys_memory_container_get_size(PPCContext& ctx)
    {
        auto c = kobj<KContainer>(ctx.r4.u32);
        if (!c) return CELL_ESRCH;
        guest_store_u32(ctx.r3.u32, c->size);
        guest_store_u32(ctx.r3.u32 + 4, c->size - c->used);
        return CELL_OK;
    }

    // sys_mmapper_allocate_address(size, flags, alignment, alloc_addr*)
    int64_t sys_mmapper_allocate_address(PPCContext& ctx)
    {
        const uint64_t size = ctx.r3.u64, alignment = ctx.r5.u64;
        const uint32_t out = ctx.r6.u32;
        if (size % 0x10000000 || !size) return CELL_EALIGN;
        const uint64_t align = std::max<uint64_t>(alignment ? alignment : 0x10000000, 0x10000000);
        std::lock_guard lock(g_mem_mutex);
        for (uint64_t at = 0x30000000; at + size <= 0xC0000000; at += align)
        {
            bool free = true;
            for (const auto& [a, s] : g_mmap_ranges)
                if (at < uint64_t(a) + s && uint64_t(a) < at + size) free = false;
            if (free)
            {
                g_mmap_ranges[uint32_t(at)] = uint32_t(size);
                guest_store_u32(out, uint32_t(at));
                return CELL_OK;
            }
        }
        return CELL_ENOMEM;
    }

    int64_t sys_mmapper_free_address(PPCContext& ctx)
    {
        std::lock_guard lock(g_mem_mutex);
        return g_mmap_ranges.erase(ctx.r3.u32) ? CELL_OK : CELL_EINVAL;
    }

    // sys_mmapper_allocate_shared_memory(ipc_key, size, flags, mem_id*)
    int64_t sys_mmapper_allocate_shared_memory(PPCContext& ctx)
    {
        auto m = std::make_shared<KShm>();
        m->size = ctx.r4.u32;
        m->flags = ctx.r5.u32;
        guest_store_u32(ctx.r6.u32, kobj_add(0x08, m));
        return CELL_OK;
    }

    // sys_mmapper_allocate_shared_memory_from_container(ipc_key, size, cid, flags, mem_id*)
    int64_t sys_mmapper_allocate_shared_memory_from_container(PPCContext& ctx)
    {
        auto m = std::make_shared<KShm>();
        m->size = ctx.r4.u32;
        m->flags = ctx.r6.u32;
        guest_store_u32(ctx.r7.u32, kobj_add(0x08, m));
        return CELL_OK;
    }

    int64_t sys_mmapper_free_shared_memory(PPCContext& ctx)
    {
        auto m = kobj<KShm>(ctx.r3.u32);
        if (!m) return CELL_ESRCH;
        if (m->mapped_at) return CELL_EBUSY;
        kobj_remove(ctx.r3.u32);
        return CELL_OK;
    }

    // sys_mmapper_map_shared_memory(addr, mem_id, flags)
    int64_t sys_mmapper_map_shared_memory(PPCContext& ctx)
    {
        const uint32_t addr = ctx.r3.u32;
        auto m = kobj<KShm>(ctx.r4.u32);
        if (!m) return CELL_ESRCH;
        std::lock_guard lock(g_mem_mutex);
        if (m->mapped_at) return CELL_EBUSY;
        if (!guest_commit(addr, m->size)) return CELL_ENOMEM;
        m->mapped_at = addr;
        g_mmap_maps[addr] = ctx.r4.u32;
        return CELL_OK;
    }

    // sys_mmapper_search_and_map(start_addr, mem_id, flags, alloc_addr*)
    int64_t sys_mmapper_search_and_map(PPCContext& ctx)
    {
        const uint32_t start = ctx.r3.u32, out = ctx.r6.u32;
        auto m = kobj<KShm>(ctx.r4.u32);
        if (!m) return CELL_ESRCH;
        std::lock_guard lock(g_mem_mutex);
        const auto range = g_mmap_ranges.find(start);
        if (range == g_mmap_ranges.end()) return CELL_EINVAL;
        const uint32_t page = m->size >= 0x100000 ? 0x100000 : 0x10000;
        uint64_t at = start;
        for (const auto& [a, id] : g_mmap_maps)
        {
            if (a < start || a >= start + uint64_t(range->second)) continue;
            auto other = kobj<KShm>(id);
            if (at + m->size <= a) break;
            at = (uint64_t(a) + (other ? other->size : 0) + page - 1) & ~uint64_t(page - 1);
        }
        if (at + m->size > uint64_t(start) + range->second) return CELL_ENOMEM;
        if (!guest_commit(uint32_t(at), m->size)) return CELL_ENOMEM;
        m->mapped_at = uint32_t(at);
        g_mmap_maps[uint32_t(at)] = ctx.r4.u32;
        guest_store_u32(out, uint32_t(at));
        return CELL_OK;
    }

    // sys_mmapper_unmap_shared_memory(addr, mem_id*)
    int64_t sys_mmapper_unmap_shared_memory(PPCContext& ctx)
    {
        std::lock_guard lock(g_mem_mutex);
        const auto it = g_mmap_maps.find(ctx.r3.u32);
        if (it == g_mmap_maps.end()) return CELL_EINVAL;
        if (auto m = kobj<KShm>(it->second)) m->mapped_at = 0;
        if (ctx.r4.u32) guest_store_u32(ctx.r4.u32, it->second);
        g_mmap_maps.erase(it);
        return CELL_OK;
    }
}

uint32_t user_alloc(uint32_t size, uint32_t align)
{
    std::lock_guard lock(g_mem_mutex);
    return g_user64k.alloc(size, align);
}

void register_memory_syscalls()
{
    register_syscall(324, sys_memory_container_create);
    register_syscall(325, sys_memory_container_destroy);
    register_syscall(329, sys_mmapper_free_shared_memory);
    register_syscall(330, sys_mmapper_allocate_address);
    register_syscall(331, sys_mmapper_free_address);
    register_syscall(332, sys_mmapper_allocate_shared_memory);
    register_syscall(334, sys_mmapper_map_shared_memory);
    register_syscall(335, sys_mmapper_unmap_shared_memory);
    register_syscall(337, sys_mmapper_search_and_map);
    register_syscall(341, sys_memory_container_create);
    register_syscall(342, sys_memory_container_destroy);
    register_syscall(343, sys_memory_container_get_size);
    register_syscall(348, sys_memory_allocate);
    register_syscall(349, sys_memory_free);
    register_syscall(350, sys_memory_allocate_from_container);
    register_syscall(351, sys_memory_get_page_attribute);
    register_syscall(352, sys_memory_get_user_memory_size);
    register_syscall(362, sys_mmapper_allocate_shared_memory_from_container);
}
