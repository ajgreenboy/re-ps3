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
    struct VmBlock { uint32_t size,physical;std::shared_ptr<KContainer> container; };
    std::map<uint32_t,VmBlock> g_vm_blocks;

    int64_t sys_vm_memory_map(PPCContext& c)
    {
        const uint64_t size=c.r3.u64,physical=c.r4.u64,flags=c.r6.u64;
        if(!c.r8.u32)return CELL_EFAULT;
        if(!size||size%0x2000000||size>0x10000000||!physical||physical%0x10000||
            physical>0xffffffffull||c.r7.u64!=1||(c.r11.u32==300&&physical<0x100000)||
            (flags&0xf4)||((flags&0xf00)!=0&&(flags&0xf00)!=0x200&&(flags&0xf00)!=0x400))return CELL_EINVAL;
        auto container=c.r5.u32==0xffffffff?nullptr:kobj<KContainer>(c.r5.u32);
        if(c.r5.u32!=0xffffffff&&!container)return CELL_ESRCH;
        std::lock_guard lock(g_mem_mutex);
        uint64_t virtual_total=0,physical_total=0;
        for(const auto& [addr,block]:g_vm_blocks){virtual_total+=block.size;physical_total+=block.physical;}
        if(virtual_total+size>0x1e000000)return CELL_EBUSY;
        if(container){if(uint64_t(container->used)+physical>container->size)return CELL_ENOMEM;}
        else
        {
            for(const auto& [addr,used]:g_user64k.used)physical_total+=used;
            for(const auto& [addr,used]:g_user1m.used)physical_total+=used;
            if(physical_total+physical>0x0d500000)return CELL_ENOMEM;
        }
        for(uint64_t address=0x30000000;address<0xc0000000;address+=0x10000000)
        {
            bool available=true;
            for(const auto& [start,length]:g_mmap_ranges)
                if(address<uint64_t(start)+length&&start<address+0x10000000)available=false;
            if(!available)continue;
            if(!guest_commit(uint32_t(address),size))return CELL_ENOMEM;
            memset(g_base+address,0,size);
            g_mmap_ranges[uint32_t(address)]=0x10000000;
            g_vm_blocks.emplace(uint32_t(address),VmBlock{uint32_t(size),uint32_t(physical),container});
            if(container)container->used+=uint32_t(physical);
            guest_store_u32(c.r8.u32,uint32_t(address));
            return CELL_OK;
        }
        return CELL_ENOMEM;
    }
    int64_t sys_vm_unmap(PPCContext& c)
    {
        std::lock_guard lock(g_mem_mutex);
        auto it=g_vm_blocks.find(c.r3.u32);if(it==g_vm_blocks.end())return CELL_EINVAL;
        if(it->second.container)it->second.container->used-=it->second.physical;
        g_mmap_ranges.erase(it->first);g_vm_blocks.erase(it);return CELL_OK;
    }
    int64_t sys_vm_resize(PPCContext& c,bool append)
    {
        const uint64_t size=c.r4.u64;if(!size||size%0x100000||size>0xffffffffull)return CELL_EINVAL;
        std::lock_guard lock(g_mem_mutex);
        auto it=g_vm_blocks.find(c.r3.u32);if(it==g_vm_blocks.end())return CELL_EINVAL;
        auto& block=it->second;
        if(!append)
        {
            if(block.physical<=size||block.physical-size<0x100000)return CELL_EBUSY;
            block.physical-=uint32_t(size);if(block.container)block.container->used-=uint32_t(size);
        }
        else
        {
            uint64_t used=0;
            if(block.container){used=block.container->used;if(used+size>block.container->size)return CELL_ENOMEM;}
            else
            {
                for(const auto& [addr,other]:g_vm_blocks)if(!other.container)used+=other.physical;
                for(const auto& [addr,length]:g_user64k.used)used+=length;
                for(const auto& [addr,length]:g_user1m.used)used+=length;
                if(used+size>0x0d500000)return CELL_ENOMEM;
            }
            block.physical+=uint32_t(size);if(block.container)block.container->used+=uint32_t(size);
        }
        return CELL_OK;
    }
    int64_t sys_vm_range(PPCContext& c,bool test=false)
    {
        std::lock_guard lock(g_mem_mutex);
        const uint32_t address=c.r3.u32,size=c.r4.u32;
        auto it=g_vm_blocks.upper_bound(address);
        if(it==g_vm_blocks.begin())return CELL_EINVAL;
        --it;
        if((!test&&!size)||uint64_t(address)+size>uint64_t(it->first)+it->second.size)return CELL_EINVAL;
        if(test){if(!c.r5.u32)return CELL_EFAULT;guest_store_u64(c.r5.u32,2);}
        // All mapped guest pages reside in host RAM; flush/store/sync require no
        // swap-file transfers. Retain the reference's range/error semantics.
        return CELL_OK;
    }
    int64_t sys_vm_statistics(PPCContext& c)
    {
        std::lock_guard lock(g_mem_mutex);
        auto it=g_vm_blocks.find(c.r3.u32);if(it==g_vm_blocks.end())return CELL_EINVAL;
        if(!c.r4.u32)return CELL_EFAULT;
        memset(g_base+c.r4.u32,0,48);
        guest_store_u32(c.r4.u32+32,it->second.physical);
        guest_store_u64(c.r4.u32+40,ppc_mftb());return CELL_OK;
    }

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
        for (const auto& [a, block] : g_vm_blocks) if(!block.container)used+=block.physical;
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
        std::lock_guard lock(g_mem_mutex);
        auto container=kobj<KContainer>(ctx.r3.u32);
        if(!container)return CELL_ESRCH;
        if(container->used)return CELL_EBUSY;
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
        if(g_vm_blocks.contains(ctx.r3.u32))return CELL_EINVAL;
        const auto range=g_mmap_ranges.find(ctx.r3.u32);
        if(range!=g_mmap_ranges.end())
            for(const auto& [address,id]:g_mmap_maps)
                if(address>=range->first&&uint64_t(address)<uint64_t(range->first)+range->second)return CELL_EBUSY;
        return g_mmap_ranges.erase(ctx.r3.u32) ? CELL_OK : CELL_EINVAL;
    }

    // IPC-keyed shared memory (lv2_obj::create): each successful call gets a new id for the shared object
    constexpr uint64_t SYS_MMAPPER_NO_SHM_KEY = 0xFFFF000000000000ull;
    std::unordered_map<uint64_t, std::shared_ptr<KShm>> g_shm_keys;

    enum class CreateMode { NewlyCreated, NotCare, NotCreate };

    int64_t create_shm(uint64_t key, uint64_t size, uint64_t flags, CreateMode mode, uint32_t mem_id_ptr)
    {
        if (!size) return CELL_EALIGN;
        switch (flags & 0xF00) // SYS_MEMORY_GRANULARITY_MASK
        {
        case 0: case 0x400: if (size % 0x100000) return CELL_EALIGN; break;
        case 0x200: if (size % 0x10000) return CELL_EALIGN; break;
        default: return CELL_EINVAL;
        }
        std::lock_guard lock(g_mem_mutex);
        std::shared_ptr<KShm> m;
        if (key != SYS_MMAPPER_NO_SHM_KEY)
        {
            const auto it = g_shm_keys.find(key);
            if (it != g_shm_keys.end())
            {
                if (mode == CreateMode::NewlyCreated) return CELL_EEXIST;
                m = it->second;
            }
            else if (mode == CreateMode::NotCreate)
            {
                return CELL_ESRCH;
            }
        }
        if (!m)
        {
            m = std::make_shared<KShm>();
            m->size = uint32_t(size);
            m->flags = uint32_t(flags);
            if (key != SYS_MMAPPER_NO_SHM_KEY) g_shm_keys[key] = m;
        }
        guest_store_u32(mem_id_ptr, kobj_add(0x08, m));
        return CELL_OK;
    }

    CreateMode shm_mode(uint64_t flags)
    {
        return (flags & 0xC000) == 0xC000 ? CreateMode::NewlyCreated : (flags & 0x8000) ? CreateMode::NotCare : CreateMode::NotCreate;
    }

    // sys_mmapper_allocate_shared_memory(ipc_key, size, flags, mem_id*)
    int64_t sys_mmapper_allocate_shared_memory(PPCContext& ctx)
    {
        return create_shm(ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, shm_mode(ctx.r5.u64), ctx.r6.u32);
    }

    // sys_mmapper_allocate_shared_memory_ext(ipc_key, size, flags, entries*, entry_count, mem_id*): always exclusive
    int64_t sys_mmapper_allocate_shared_memory_ext(PPCContext& ctx)
    {
        const int32_t count = int32_t(ctx.r7.u32);
        if (count <= 0 || count > 0x10) return CELL_EINVAL;
        return create_shm(ctx.r3.u64, ctx.r4.u64, ctx.r5.u32, CreateMode::NewlyCreated, ctx.r8.u32);
    }

    // sys_mmapper_allocate_shared_memory_from_container(ipc_key, size, cid, flags, mem_id*)
    int64_t sys_mmapper_allocate_shared_memory_from_container(PPCContext& ctx)
    {
        if (!kobj<KContainer>(ctx.r5.u32)) return CELL_ESRCH;
        return create_shm(ctx.r3.u64, ctx.r4.u64, ctx.r6.u64, shm_mode(ctx.r6.u64), ctx.r7.u32);
    }

    // Extended container allocation uses flags before cid and a 24-byte permission-entry array.
    int64_t sys_mmapper_allocate_shared_memory_from_container_ext(PPCContext& ctx)
    {
        const int32_t count=ctx.r8.s32;
        if(ctx.r5.u64 & ~0xF00ull) return CELL_EINVAL;
        if(count<=0 || count>16) return CELL_EINVAL;
        if(!ctx.r7.u32 || !ctx.r9.u32) return CELL_EFAULT;
        for(int32_t i=0;i<count;++i)
        {
            const uint64_t type=guest_load_u64(ctx.r7.u32+uint32_t(i)*24+16);
            if(type!=0 && type!=1 && type!=3 && type!=5) return CELL_EPERM;
            if(type==5 && ctx.r5.u64!=0x200) return CELL_EPERM;
        }
        if(!kobj<KContainer>(ctx.r6.u32)) return CELL_ESRCH;
        return create_shm(ctx.r3.u64,ctx.r4.u64,ctx.r5.u64,CreateMode::NewlyCreated,ctx.r9.u32);
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
    register_syscall(300,sys_vm_memory_map);register_syscall(313,sys_vm_memory_map);
    register_syscall(301,sys_vm_unmap);
    register_syscall(302,[](PPCContext& c){return sys_vm_resize(c,true);});
    register_syscall(303,[](PPCContext& c){return sys_vm_resize(c,false);});
    for(uint32_t number=304;number<=310;++number)register_syscall(number,[](PPCContext& c){return sys_vm_range(c);});
    register_syscall(311,[](PPCContext& c){return sys_vm_range(c,true);});
    register_syscall(312,sys_vm_statistics);
    register_syscall(324, sys_memory_container_create);
    register_syscall(325, sys_memory_container_destroy);
    register_syscall(328, sys_mmapper_allocate_shared_memory_from_container_ext);
    register_syscall(329, sys_mmapper_free_shared_memory);
    register_syscall(330, sys_mmapper_allocate_address);
    register_syscall(331, sys_mmapper_free_address);
    register_syscall(332, sys_mmapper_allocate_shared_memory);
    register_syscall(334, sys_mmapper_map_shared_memory);
    register_syscall(335, sys_mmapper_unmap_shared_memory);
    register_syscall(337, sys_mmapper_search_and_map);
    register_syscall(339, sys_mmapper_allocate_shared_memory_ext);
    register_syscall(341, sys_memory_container_create);
    register_syscall(342, sys_memory_container_destroy);
    register_syscall(343, sys_memory_container_get_size);
    register_syscall(345, [](PPCContext& c)->int64_t {
        if(c.r4.u32) return CELL_EINVAL;
        return sys_memory_container_destroy(c);
    });
    register_syscall(348, sys_memory_allocate);
    register_syscall(349, sys_memory_free);
    register_syscall(350, sys_memory_allocate_from_container);
    register_syscall(351, sys_memory_get_page_attribute);
    register_syscall(352, sys_memory_get_user_memory_size);
    register_syscall(362, sys_mmapper_allocate_shared_memory_from_container);
}
