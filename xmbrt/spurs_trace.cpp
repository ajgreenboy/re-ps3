// Optional observation of the genuine firmware call. Entry names are resolved
// from exports.csv by the build script so module relocation does not break it.
#include "kernel.h"
#include <cstdlib>
#include <cstring>

#if defined(XMBRT_SPURS_SIGNAL_ENTRY) && defined(XMBRT_SPURS_SIGNAL_IMPL)
PPC_FUNC_IMPL(XMBRT_SPURS_SIGNAL_IMPL);

PPC_FUNC(XMBRT_SPURS_SIGNAL_ENTRY)
{
    const bool enabled=getenv("XMBRT_TRACE_SPURS_TASK")!=nullptr;
    const uint32_t taskset=ctx.r3.u32,task=ctx.r4.u32,caller=uint32_t(ctx.lr);
    uint32_t before[24]{};
    if(enabled && taskset && uint64_t(taskset)+sizeof(before)<=PPC_MEMORY_SIZE)
        memcpy(before,base+taskset,sizeof(before));
    XMBRT_SPURS_SIGNAL_IMPL(ctx,base);
    if(enabled && ctx.r3.u32==0x80410905 && taskset)
    {
        log_line("spurs signal SRCH: taskset=%08X task=%u caller=%08X wid=%u",taskset,task,caller,guest_load_u32(taskset+0x74));
        static const char* names[]={"running","ready","pending","enabled","signalled","waiting"};
        for(unsigned i=0;i<6;i++)
            log_line("spurs masks %s: before=%08X %08X %08X %08X after=%08X %08X %08X %08X",names[i],
                __builtin_bswap32(before[i*4]),__builtin_bswap32(before[i*4+1]),__builtin_bswap32(before[i*4+2]),__builtin_bswap32(before[i*4+3]),
                guest_load_u32(taskset+i*16),guest_load_u32(taskset+i*16+4),guest_load_u32(taskset+i*16+8),guest_load_u32(taskset+i*16+12));
    }
}
#endif
