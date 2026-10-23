// LV2 PRX management (sys_prx.cpp semantics) over a prelinked image: every module xmbld placed is
// already in memory with its imports bound, so "loading" one hands back its id, and start/stop
// return the module's start/stop OPDs for liblv2 (inside vsh) to call. Modules outside the image
// load as stand-ins with no start function; calls into them reach the HLE thunks.
#include "kernel.h"

#include <cstring>
#include <vector>

namespace
{
    constexpr int32_t CELL_PRX_ERROR_ERROR = int32_t(0x80011001);
    constexpr int32_t CELL_PRX_ERROR_UNKNOWN_MODULE = int32_t(0x8001112E);
    constexpr int32_t CELL_PRX_ERROR_NOT_STARTED = int32_t(0x80011134);
    constexpr int32_t CELL_PRX_ERROR_ALREADY_STOPPED = int32_t(0x80011135);
    constexpr int32_t CELL_PRX_ERROR_CAN_NOT_STOP = int32_t(0x80011136);

    enum class State { Initialized, Starting, Started, Stopping, Stopped };

    struct ImageModule
    {
        std::string file; // "xmb_plugin.prx"
        std::string stem; // "xmb_plugin"
        std::string lib_name;
        uint32_t base = 0, end = 0, toc = 0, start_opd = 0, stop_opd = 0;
        bool dirty = false; // ran once; restore pristine data before the next load
    };

    struct KPrx : KObject
    {
        std::string path;
        ImageModule* image = nullptr; // null for modules outside the image
        State state = State::Initialized;
    };

    std::vector<ImageModule> g_modules;
    std::vector<uint8_t> g_pristine; // image span as loaded, for module reloads
    std::mutex g_prx_mutex;

    std::string stem_of(const std::string& path)
    {
        std::string s = path.substr(path.find_last_of('/') + 1);
        return s.substr(0, s.find_last_of('.'));
    }

    ImageModule* find_module(const std::string& path)
    {
        const std::string stem = stem_of(path);
        for (auto& m : g_modules)
            if (m.stem == stem) return &m;
        return nullptr;
    }

    int64_t load_module(const std::string& path, uint32_t& id_out)
    {
        std::lock_guard lock(g_prx_mutex);
        auto prx = std::make_shared<KPrx>();
        prx->path = path;
        prx->image = find_module(path);
        if (prx->image && prx->image->dirty)
        {
            // Reloading after an unload: put back the module's pristine data
            memcpy(g_base + prx->image->base, g_pristine.data() + (prx->image->base - PPC_IMAGE_BASE), prx->image->end - prx->image->base);
            prx->image->dirty = false;
        }
        id_out = kobj_add(0x23, prx);
        log_line("prx: load %s -> %08X%s", path.c_str(), id_out, prx->image ? "" : " (not in image; stand-in)");
        return id_out;
    }

    // _sys_prx_load_module(path*, flags, pOpt*)
    int64_t sys_prx_load_module(PPCContext& ctx)
    {
        uint32_t id;
        return load_module(guest_string(ctx.r3.u32), id);
    }

    // _sys_prx_load_module_on_memcontainer(path*, mem_ct, flags, pOpt*)
    int64_t sys_prx_load_module_on_memcontainer(PPCContext& ctx)
    {
        uint32_t id;
        return load_module(guest_string(ctx.r3.u32), id);
    }

    // _sys_prx_load_module_list(count, path_list**, flags, pOpt*, id_list*)
    int64_t sys_prx_load_module_list(PPCContext& ctx)
    {
        const int32_t count = int32_t(ctx.r3.u32);
        const uint32_t paths = ctx.r4.u32, ids = ctx.r7.u32;
        for (int32_t i = 0; i < count; i++)
        {
            uint32_t id;
            load_module(guest_string(guest_load_u32(paths + i * 4)), id);
            guest_store_u32(ids + i * 4, id);
        }
        return CELL_OK;
    }

    // _sys_prx_load_module_list_on_memcontainer(count, path_list**, mem_ct, flags, pOpt*, id_list*)
    int64_t sys_prx_load_module_list_on_memcontainer(PPCContext& ctx)
    {
        const int32_t count = int32_t(ctx.r3.u32);
        const uint32_t paths = ctx.r4.u32, ids = ctx.r8.u32;
        for (int32_t i = 0; i < count; i++)
        {
            uint32_t id;
            load_module(guest_string(guest_load_u32(paths + i * 4)), id);
            guest_store_u32(ids + i * 4, id);
        }
        return CELL_OK;
    }

    // sys_prx_start_stop_module_option_t: u64 size, u64 cmd, u64 entry, u64 res, u64 entry2
    void set_entries(uint32_t opt, uint64_t entry)
    {
        guest_store_u64(opt + 0x10, entry);
        if (guest_load_u64(opt) != 0x20) guest_store_u64(opt + 0x20, ~0ull);
    }

    // _sys_prx_start_module(id, flags, pOpt*): cmd 1 returns the start entry, cmd 2 reports its result
    int64_t sys_prx_start_module(PPCContext& ctx)
    {
        const uint32_t opt = ctx.r5.u32;
        if (!ctx.r3.u32 || !opt) return CELL_EINVAL;
        std::lock_guard lock(g_prx_mutex);
        auto prx = kobj<KPrx>(ctx.r3.u32);
        if (!prx) return CELL_ESRCH;
        switch (guest_load_u64(opt + 8) & 0xF)
        {
        case 1:
            if (prx->state != State::Initialized) return CELL_PRX_ERROR_ERROR;
            prx->state = State::Starting;
            set_entries(opt, prx->image && prx->image->start_opd ? prx->image->start_opd : ~0ull);
            if (prx->image) prx->image->dirty = true;
            return CELL_OK;
        case 2:
        {
            const uint64_t res = guest_load_u64(opt + 0x18);
            if (res == 0) // SYS_PRX_RESIDENT
            {
                prx->state = State::Started;
                return CELL_OK;
            }
            if (res & 0xFFFFFFFFull)
            {
                log_line("prx: %s start returned %llX (no resident); unloading", prx->path.c_str(), (unsigned long long)res);
                prx->state = State::Stopped;
                kobj_remove(ctx.r3.u32);
                return int64_t(int32_t(res));
            }
            return CELL_OK;
        }
        default:
            return CELL_PRX_ERROR_ERROR;
        }
    }

    // _sys_prx_stop_module(id, flags, pOpt*): cmd 1 returns the stop entry, cmd 2 reports its result
    int64_t sys_prx_stop_module(PPCContext& ctx)
    {
        const uint32_t opt = ctx.r5.u32;
        std::lock_guard lock(g_prx_mutex);
        auto prx = kobj<KPrx>(ctx.r3.u32);
        if (!prx) return CELL_ESRCH;
        if (!opt) return CELL_EINVAL;
        switch (guest_load_u64(opt + 8) & 0xF)
        {
        case 1:
            if (prx->state == State::Initialized) return CELL_PRX_ERROR_NOT_STARTED;
            if (prx->state == State::Stopped) return CELL_PRX_ERROR_ALREADY_STOPPED;
            if (prx->state != State::Started) return CELL_PRX_ERROR_ERROR;
            prx->state = State::Stopping;
            set_entries(opt, prx->image && prx->image->stop_opd ? prx->image->stop_opd : ~0ull);
            return CELL_OK;
        case 2:
            switch (guest_load_u64(opt + 0x18))
            {
            case 0:
                prx->state = State::Stopped;
                return CELL_OK;
            case 1:
                return CELL_PRX_ERROR_CAN_NOT_STOP;
            default:
                return CELL_OK;
            }
        default:
            return CELL_PRX_ERROR_ERROR;
        }
    }

    // _sys_prx_unload_module(id, flags, pOpt*)
    int64_t sys_prx_unload_module(PPCContext& ctx)
    {
        std::lock_guard lock(g_prx_mutex);
        auto prx = kobj<KPrx>(ctx.r3.u32);
        if (!prx) return CELL_ESRCH;
        log_line("prx: unload %s", prx->path.c_str());
        kobj_remove(ctx.r3.u32);
        return CELL_OK;
    }

    int64_t sys_prx_ok(PPCContext&) { return CELL_OK; }

    // _sys_prx_get_module_id_by_name(name*, flags, pOpt*): matches the module_info name
    int64_t sys_prx_get_module_id_by_name(PPCContext& ctx)
    {
        const std::string name = guest_string(ctx.r3.u32, 28);
        for (const auto& m : g_modules)
        {
            if (m.lib_name != name) continue;
            // Only modules that have been loaded have an id
            for (uint32_t i = 1; i < 0x10000; i++)
            {
                auto prx = kobj<KPrx>(0x23000000 | (i << 8));
                if (!prx) continue;
                if (prx->image == &m) return 0x23000000 | (i << 8);
            }
        }
        return CELL_PRX_ERROR_UNKNOWN_MODULE;
    }

    // _sys_prx_get_module_id_by_address(addr)
    int64_t sys_prx_get_module_id_by_address(PPCContext& ctx)
    {
        const uint32_t addr = ctx.r3.u32;
        for (const auto& m : g_modules)
        {
            if (addr < m.base || addr >= m.end) continue;
            for (uint32_t i = 1; i < 0x10000; i++)
            {
                auto prx = kobj<KPrx>(0x23000000 | (i << 8));
                if (prx && prx->image == &m) return 0x23000000 | (i << 8);
            }
        }
        return CELL_PRX_ERROR_UNKNOWN_MODULE;
    }
}

bool prx_init(const std::string& dir)
{
    FILE* f = fopen((dir + "\\modules.csv").c_str(), "r");
    if (!f) return false;
    char line[512];
    fgets(line, sizeof(line), f); // module,lib_name,base,toc,module_start_opd,module_stop_opd
    while (fgets(line, sizeof(line), f))
    {
        char file[128] = {}, lib[128] = {};
        unsigned base, toc, start, stop;
        // lib_name is empty for vsh.elf
        if (sscanf(line, "%127[^,],%127[^,],0x%X,0x%X,0x%X,0x%X", file, lib, &base, &toc, &start, &stop) != 6 &&
            sscanf(line, "%127[^,],,0x%X,0x%X,0x%X,0x%X", file, &base, &toc, &start, &stop) != 5)
            continue;
        ImageModule m;
        m.file = file;
        m.stem = m.file.substr(0, m.file.find_last_of('.'));
        m.lib_name = lib;
        m.base = base;
        m.toc = toc;
        m.start_opd = start;
        m.stop_opd = stop;
        if (m.base) g_modules.push_back(m); // vsh.elf (base 0) is the process, not a loadable module
    }
    fclose(f);
    // Each module spans to the next one's base; the last one to the HLE descriptors.
    for (size_t i = 0; i < g_modules.size(); i++)
        g_modules[i].end = i + 1 < g_modules.size() ? g_modules[i + 1].base : g_modules[i].base + 0x10000;
    g_pristine.assign(g_base + PPC_IMAGE_BASE, g_base + kImageEnd);
    log_line("prx: %zu prelinked modules", g_modules.size());
    return true;
}

// The last module ends where xmbld put the HLE descriptors (first HLE OPD), set once hle.csv is read.
void prx_set_image_modules_end(uint32_t end)
{
    if (!g_modules.empty()) g_modules.back().end = end;
}

void register_prx_syscalls()
{
    register_syscall(461, sys_prx_get_module_id_by_address);
    register_syscall(465, sys_prx_load_module_list);
    register_syscall(466, sys_prx_load_module_list_on_memcontainer);
    register_syscall(480, sys_prx_load_module);
    register_syscall(481, sys_prx_start_module);
    register_syscall(482, sys_prx_stop_module);
    register_syscall(483, sys_prx_unload_module);
    register_syscall(484, sys_prx_ok); // register_module
    register_syscall(486, sys_prx_ok); // register_library
    register_syscall(487, sys_prx_ok); // unregister_library
    register_syscall(496, sys_prx_get_module_id_by_name);
    register_syscall(497, sys_prx_load_module_on_memcontainer);
}
