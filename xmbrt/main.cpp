// xmbrt entry: maps guest memory, loads the xmbld image, fills XenonRecomp's indirect-call table,
// and starts vsh the way RPCS3's ppu_load_exec does (rpcs3 0b0fa08, Emu/Cell/PPUModule.cpp).
//
// usage: xmbrt <boot493 dir> <vsh.elf>
//   <boot493 dir> holds image.bin, image.elf and hle.csv from xmbld.
//   <vsh.elf> is the decrypted vsh, read for its TLS and process-param program headers.
// env:   XMBRT_LOG (default xmbrt.log), XMBRT_MAX_CALLS (default 10000), XMBRT_IDLE_SECONDS (default 15)
#include "xmbrt.h"
#include "kernel.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

uint8_t* g_base;

static FILE* g_log;
static std::mutex g_log_mutex;
static std::atomic<uint64_t> g_call_seq;
static uint64_t g_max_calls = 10000;

void lv2_init();
bool prx_init(const std::string& dir);
void misc_set_sdk_version(uint32_t version);

void log_line(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::lock_guard lock(g_log_mutex);
    if (g_log)
    {
        fputs(buf, g_log);
        fputc('\n', g_log);
        fflush(g_log);
    }
    fputs(buf, stderr);
    fputc('\n', stderr);
}

uint64_t next_call_seq()
{
    const uint64_t seq = ++g_call_seq;
    if (seq > g_max_calls)
    {
        log_line("stopping: reached XMBRT_MAX_CALLS=%llu", (unsigned long long)g_max_calls);
        fatal_exit(0);
    }
    return seq;
}

void fatal_exit(int code)
{
    {
        std::lock_guard lock(g_log_mutex);
        if (g_log) fflush(g_log);
    }
    // Other guest threads may still be running; skip static destructors.
    ExitProcess(code);
}

static uint64_t env_u64(const char* name, uint64_t fallback)
{
    const char* v = getenv(name);
    return v && *v ? strtoull(v, nullptr, 0) : fallback;
}

static std::vector<uint8_t> read_file(const std::string& path)
{
    std::vector<uint8_t> data;
    if (FILE* f = fopen(path.c_str(), "rb"))
    {
        fseek(f, 0, SEEK_END);
        data.resize(size_t(ftell(f)));
        fseek(f, 0, SEEK_SET);
        if (fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
        fclose(f);
    }
    return data;
}

static uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
static uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
static uint64_t be64(const uint8_t* p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }

static bool commit(uint32_t addr, uint64_t size)
{
    const uint32_t start = addr & ~0xFFFu;
    const uint64_t end = (uint64_t(addr) + size + 0xFFF) & ~uint64_t(0xFFF);
    if (!VirtualAlloc(g_base + start, size_t(end - start), MEM_COMMIT, PAGE_READWRITE))
    {
        log_line("commit %08X+%llX failed (%lu)", addr, (unsigned long long)size, GetLastError());
        return false;
    }
    return true;
}

// image.bin: "XMBIMG01", be32 count, then {be32 addr, be32 size, be32 flags, bytes[size]} (xmbld.c)
static bool load_image(const std::string& path)
{
    const std::vector<uint8_t> data = read_file(path);
    if (data.size() < 12 || memcmp(data.data(), "XMBIMG01", 8) != 0)
    {
        log_line("%s: not an xmbld image", path.c_str());
        return false;
    }
    const uint32_t count = be32(&data[8]);
    size_t pos = 12;
    uint64_t bytes = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        if (pos + 12 > data.size()) return log_line("%s: truncated segment header", path.c_str()), false;
        const uint32_t addr = be32(&data[pos]), size = be32(&data[pos + 4]);
        pos += 12;
        if (pos + size > data.size() || addr < PPC_IMAGE_BASE || uint64_t(addr) + size > kImageEnd)
            return log_line("%s: segment %u (%08X+%X) outside the image span", path.c_str(), i, addr, size), false;
        memcpy(g_base + addr, &data[pos], size);
        pos += size;
        bytes += size;
    }
    log_line("image: %u segments, %llu bytes from %s", count, (unsigned long long)bytes, path.c_str());
    return true;
}

// Default for every table slot that isn't a translated function or an HLE stub.
static void bad_indirect_call(PPCContext& __restrict__ ctx, uint8_t*)
{
    // Name the nearest translated function below the target, to tell a mid-function target from data
    uint32_t below = 0;
    for (uint32_t a = ctx.ctr.u32 & ~3u; a >= PPC_CODE_BASE && a + 0x10000 > ctx.ctr.u32; a -= 4)
    {
        if (func_slot(a) != &bad_indirect_call)
        {
            below = a;
            break;
        }
    }
    log_line("indirect call to an address with no function: ctr=%08X lr=%08X (nearest function below: %08X) T%08X",
        ctx.ctr.u32, uint32_t(ctx.lr), below, current_thread_id());
    fatal_exit(2);
}

static void build_func_table()
{
    const uint64_t slots = kFuncTableBytes / 8;
    PPCFunc** table = (PPCFunc**)(g_base + kImageEnd);
    std::fill(table, table + slots, &bad_indirect_call);
    size_t n = 0;
    for (const PPCFuncMapping* m = PPCFuncMappings; m->host; m++, n++)
        func_slot(uint32_t(m->guest)) = m->host;
    log_line("function table: %zu functions, %llu slots at %08X", n, (unsigned long long)slots, kImageEnd);
}

static LONG CALLBACK on_exception(EXCEPTION_POINTERS* info)
{
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO)
        return EXCEPTION_CONTINUE_SEARCH;
    const uint64_t rip = info->ContextRecord->Rip;
    const HMODULE exe = GetModuleHandleW(nullptr);
    if (code == EXCEPTION_ACCESS_VIOLATION)
    {
        const uint64_t host = info->ExceptionRecord->ExceptionInformation[1];
        const char* op = info->ExceptionRecord->ExceptionInformation[0] == 1 ? "write" : "read";
        if (host >= uint64_t(g_base) && host < uint64_t(g_base) + PPC_MEMORY_SIZE)
            log_line("guest %s fault at %08X (host rip exe+%llX)", op, uint32_t(host - uint64_t(g_base)), (unsigned long long)(rip - uint64_t(exe)));
        else
            log_line("host %s fault at %llX (host rip exe+%llX)", op, (unsigned long long)host, (unsigned long long)(rip - uint64_t(exe)));
    }
    else
    {
        log_line("exception %08lX at host rip exe+%llX", code, (unsigned long long)(rip - uint64_t(exe)));
    }
    fatal_exit(1);
}

struct VshParams
{
    uint32_t entry_opd = 0;
    uint32_t tls_vaddr = 0, tls_fsize = 0, tls_vsize = 0;
    int32_t primary_prio = 1001;
    uint32_t primary_stacksize = 0x100000;
    uint32_t malloc_pagesize = 0x100000;
    uint32_t sdk_version = 0;
};

// Reads e_entry, PT_TLS and the process-param segment from vsh.elf (ELF64 big-endian).
static bool read_vsh_params(const std::string& path, VshParams& p)
{
    const std::vector<uint8_t> elf = read_file(path);
    if (elf.size() < 64 || memcmp(elf.data(), "\x7F" "ELF\x02\x02", 6) != 0)
        return log_line("%s: not a big-endian ELF64", path.c_str()), false;
    p.entry_opd = uint32_t(be64(&elf[0x18]));
    const uint64_t phoff = be64(&elf[0x20]);
    const uint16_t phentsize = be16(&elf[0x36]), phnum = be16(&elf[0x38]);
    for (uint16_t i = 0; i < phnum; i++)
    {
        const uint8_t* ph = &elf[size_t(phoff + uint64_t(i) * phentsize)];
        const uint32_t type = be32(ph);
        const uint32_t vaddr = uint32_t(be64(ph + 0x10));
        if (type == 7) // PT_TLS
        {
            p.tls_vaddr = vaddr;
            p.tls_fsize = uint32_t(be64(ph + 0x20));
            p.tls_vsize = uint32_t(be64(ph + 0x28));
        }
        else if (type == 0x60000001 && be64(ph + 0x20) >= 0x1C) // sys_process_param_t, already in guest memory
        {
            if (guest_load_u32(vaddr + 4) != 0x13BCC5F6)
            {
                log_line("process param at %08X has a bad magic", vaddr);
                continue;
            }
            p.sdk_version = guest_load_u32(vaddr + 0x0C);
            p.primary_prio = int32_t(guest_load_u32(vaddr + 0x10));
            p.primary_stacksize = guest_load_u32(vaddr + 0x14);
            p.malloc_pagesize = guest_load_u32(vaddr + 0x18);
        }
    }
    return true;
}

// Same sizing as ppu_load_exec: SYS_PROCESS_PRIMARY_STACK_SIZE_* codes, else clamp to 64 KB..1 MB.
static uint32_t primary_stack_size(uint32_t sz)
{
    switch (sz)
    {
    case 0x10: return 32 * 1024;
    case 0x20: return 64 * 1024;
    case 0x30: return 96 * 1024;
    case 0x40: return 128 * 1024;
    case 0x50: return 256 * 1024;
    case 0x60: return 512 * 1024;
    case 0x70: return 1024 * 1024;
    default: return (std::clamp<uint32_t>(sz, 0x10000, 0x100000) + 0xFFF) & ~0xFFFu;
    }
}

static void idle_watchdog(uint32_t idle_seconds)
{
    uint64_t last = 0;
    for (uint32_t idle = 0;; )
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const uint64_t now = g_call_seq.load();
        idle = now == last ? idle + 1 : 0;
        last = now;
        if (idle >= idle_seconds)
        {
            log_line("stopping: no syscall or HLE call for %u s (last call %llu)", idle_seconds, (unsigned long long)now);
            fatal_exit(4);
        }
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: xmbrt <boot493 dir> <vsh.elf>\n");
        return 64;
    }
    const std::string dir = argv[1];
    const char* log_path = getenv("XMBRT_LOG");
    g_log = fopen(log_path && *log_path ? log_path : "xmbrt.log", "w");
    g_max_calls = env_u64("XMBRT_MAX_CALLS", 10000);

    g_base = (uint8_t*)VirtualAlloc(nullptr, PPC_MEMORY_SIZE, MEM_RESERVE, PAGE_NOACCESS);
    if (!g_base)
        return log_line("cannot reserve 4 GB of guest address space (%lu)", GetLastError()), 1;
    log_line("guest base %p", g_base);
    AddVectoredExceptionHandler(1, on_exception);

    if (!commit(uint32_t(PPC_IMAGE_BASE), uint64_t(kImageEnd - PPC_IMAGE_BASE) + kFuncTableBytes)) return 1;
    if (!load_image(dir + "\\image.bin")) return 1;
    build_func_table();
    if (!hle_load((dir + "\\hle.csv").c_str())) return 1;
    hle_install();
    if (!prx_init(dir)) return log_line("cannot read %s\\modules.csv", dir.c_str()), 1;
    lv2_init();

    VshParams vsh;
    if (!read_vsh_params(argv[2], vsh)) return 1;
    const uint32_t entry_func = guest_load_u32(vsh.entry_opd), entry_toc = guest_load_u32(vsh.entry_opd + 4);
    log_line("vsh: entry OPD %08X -> %08X toc %08X; TLS %08X fsize %X vsize %X; prio %d stack %X malloc_pagesize %X",
        vsh.entry_opd, entry_func, entry_toc, vsh.tls_vaddr, vsh.tls_fsize, vsh.tls_vsize,
        vsh.primary_prio, vsh.primary_stacksize, vsh.malloc_pagesize);

    misc_set_sdk_version(vsh.sdk_version);

    // Main thread stack, laid out as ppu_load_exec does: argv/envp pointer block and strings
    // at the top, r1 = top - 0x70 (ppu_stack_start_offset) - that block.
    const uint32_t stack_size = primary_stack_size(vsh.primary_stacksize);
    const uint32_t stack_addr = alloc_stack(stack_size); // 0xD0001000, as in RPCS3
    if (!stack_addr) return log_line("cannot allocate the main stack"), 1;
    const std::vector<std::string> guest_argv = { "/dev_flash/vsh/module/vsh.self" };
    const uint32_t pointers_size = 8 * ((uint32_t(guest_argv.size()) + 2 + 1) & ~1u);
    uint32_t alloc_size = pointers_size;
    for (const auto& a : guest_argv) alloc_size += (uint32_t(a.size()) + 1 + 0xF) & ~0xFu;
    const uint32_t args = stack_addr + stack_size - alloc_size;
    uint32_t ptr = args, data = args + pointers_size;
    for (const auto& a : guest_argv)
    {
        memcpy(g_base + data, a.c_str(), a.size() + 1);
        guest_store_u64(ptr, data);
        ptr += 8;
        data += (uint32_t(a.size()) + 1 + 0xF) & ~0xFu;
    }
    guest_store_u64(ptr, 0); // argv terminator
    const uint32_t envp = ptr + 8;
    guest_store_u64(envp, 0);

    PPCContext* ctx = new PPCContext{};
    ctx->syscall = lv2_syscall;
    ctx->trap = lv2_trap;
    ctx->fpscr.loadFromHost();
    ctx->r1.u64 = stack_addr + stack_size - 0x70 - alloc_size;
    ctx->r2.u64 = entry_toc;
    ctx->r3.u64 = guest_argv.size();
    ctx->r4.u64 = args;
    ctx->r5.u64 = envp;
    ctx->r6.u64 = 0; // envc
    ctx->r7.u64 = kMainThreadId;
    ctx->r8.u64 = vsh.tls_vaddr;
    ctx->r9.u64 = vsh.tls_fsize;
    ctx->r10.u64 = vsh.tls_vsize;
    ctx->r11.u64 = vsh.entry_opd;
    ctx->r12.u64 = vsh.malloc_pagesize;
    ctx->lr = 0;
    kernel_init_main_thread(ctx, stack_addr, stack_size);

    std::thread(idle_watchdog, uint32_t(env_u64("XMBRT_IDLE_SECONDS", 15))).detach();

    PPCFunc* entry = func_slot(entry_func);
    if (entry == &bad_indirect_call)
        return log_line("vsh entry %08X has no translated function", entry_func), 1;
    log_line("calling vsh entry %08X (r1=%08X r2=%08X)", entry_func, ctx->r1.u32, ctx->r2.u32);
    try
    {
        entry(*ctx, g_base);
        log_line("vsh entry returned r3=%llX after %llu calls", (unsigned long long)ctx->r3.u64, (unsigned long long)g_call_seq.load());
    }
    catch (const ThreadExit& e)
    {
        // On LV2 the process outlives its main thread; other threads keep running until process exit.
        log_line("main thread exited with %llX; process continues", (unsigned long long)e.status);
        for (;;) Sleep(INFINITE);
    }
    fatal_exit(0);
}
