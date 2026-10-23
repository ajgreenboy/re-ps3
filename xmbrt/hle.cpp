// HLE imports: the functions xmbld bound to synthetic descriptors instead of a module in the image.
// Each stub's code address gets its own host thunk in the indirect-call table, which routes to
// one dispatcher with the stub index. Libraries with a handler get it; the rest are logged and
// fail with CELL_ENOSYS.
#include "xmbrt.h"

#include <array>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    constexpr size_t kMaxHle = 4096; // xmbld HLE_MAX

    struct HleImport
    {
        std::string library;
        uint32_t nid;
        uint32_t opd;
        uint32_t code;
    };

    std::vector<HleImport> g_imports;

    using HleFn = int64_t (*)(PPCContext& ctx);

    int64_t hle_ok(PPCContext&) { return 0; }

    // Per-library handlers. Swap rule from the project plan: cellSpurs is stubbed (succeeds, no SPU work)
    // until something visibly needs SPU work; media decoders go to ffmpeg later, so cellAdec reports no data.
    const std::unordered_map<std::string, HleFn> kLibraryHandlers = {
        { "cellSpurs", hle_ok },
        { "cellAdec", hle_ok },
    };

    void hle_dispatch(PPCContext& ctx, size_t index)
    {
        const HleImport& imp = g_imports[index];
        const uint64_t seq = next_call_seq();
        const uint32_t lr = uint32_t(ctx.lr);
        const uint64_t r3 = ctx.r3.u64, r4 = ctx.r4.u64, r5 = ctx.r5.u64, r6 = ctx.r6.u64;
        const auto handler = kLibraryHandlers.find(imp.library);
        const int64_t result = handler != kLibraryHandlers.end() ? handler->second(ctx) : CELL_ENOSYS;
        ctx.r3.s64 = result;
        log_line("%llu T%08X hle %s 0x%08X lr=%08X r3=%llX r4=%llX r5=%llX r6=%llX -> %s%llX",
            (unsigned long long)seq, current_thread_id(), imp.library.c_str(), imp.nid, lr,
            (unsigned long long)r3, (unsigned long long)r4, (unsigned long long)r5, (unsigned long long)r6,
            handler != kLibraryHandlers.end() ? "" : "ENOSYS ", (unsigned long long)uint64_t(result));
    }

    template <size_t N>
    void hle_thunk(PPCContext& __restrict__ ctx, uint8_t*)
    {
        hle_dispatch(ctx, N);
    }

    template <size_t... I>
    constexpr std::array<PPCFunc*, sizeof...(I)> make_thunks(std::index_sequence<I...>)
    {
        return { &hle_thunk<I>... };
    }

    constexpr auto kThunks = make_thunks(std::make_index_sequence<kMaxHle>{});
}

bool hle_load(const char* hle_csv)
{
    FILE* f = fopen(hle_csv, "r");
    if (!f)
    {
        log_line("cannot open %s", hle_csv);
        return false;
    }
    char line[256];
    fgets(line, sizeof(line), f); // header: index,library,nid,opd,code
    while (fgets(line, sizeof(line), f))
    {
        unsigned index, nid, opd, code;
        char library[64];
        if (sscanf(line, "%u,%63[^,],0x%X,0x%X,0x%X", &index, library, &nid, &opd, &code) != 5)
            continue;
        if (index != g_imports.size() || index >= kMaxHle)
        {
            log_line("hle.csv: unexpected index %u", index);
            fclose(f);
            return false;
        }
        g_imports.push_back({ library, nid, opd, code });
    }
    fclose(f);
    log_line("hle: %zu imports from %s", g_imports.size(), hle_csv);
    return !g_imports.empty();
}

void hle_install()
{
    for (size_t i = 0; i < g_imports.size(); i++)
    {
        const HleImport& imp = g_imports[i];
        // The descriptor xmbld wrote must point at this stub's code word.
        if (guest_load_u32(imp.opd) != imp.code)
            log_line("hle: OPD %08X points to %08X, expected %08X", imp.opd, guest_load_u32(imp.opd), imp.code);
        func_slot(imp.code) = kThunks[i];
    }
}
