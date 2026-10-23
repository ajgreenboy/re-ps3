// Secure-service ABI follows RPCS3 lv2/sys_ss.{cpp,h} and sys_crypto_engine.h.
// Random bytes come from Windows CNG. Identity belongs to this emulated console.
#include "kernel.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>

namespace
{
    constexpr int32_t RNG_INVALID_PKG = int32_t(0x80010500);
    constexpr int32_t RNG_ENOMEM = int32_t(0x80010501);
    constexpr int32_t RNG_EAGAIN = int32_t(0x80010503);
    constexpr int32_t RNG_EFAULT = int32_t(0x80010509);

    bool writable_guest_span(uint32_t address, uint64_t size)
    {
        if (!size) return true;
        if (!address || size > PPC_MEMORY_SIZE - uint64_t(address)) return false;
        const uintptr_t end = uintptr_t(g_base) + address + size;
        for (uintptr_t cursor = uintptr_t(g_base) + address; cursor < end;)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info)) ||
                info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
                !(info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
                return false;
            const uintptr_t next = uintptr_t(info.BaseAddress) + info.RegionSize;
            if (next <= cursor) return false;
            cursor = next;
        }
        return true;
    }

    int64_t random_bytes(uint32_t address, uint64_t size)
    {
        if (!writable_guest_span(address, size)) return CELL_EFAULT;
        for (uint64_t offset = 0; offset < size;)
        {
            const ULONG count = ULONG(std::min<uint64_t>(size - offset, 0x100000));
            const NTSTATUS status = BCryptGenRandom(nullptr, g_base + address + offset, count, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            if (status < 0)
            {
                log_line("ss: Windows CNG random generation failed (%08X)",uint32_t(status));
                return CELL_EIO;
            }
            offset += count;
        }
        return CELL_OK;
    }

    int64_t sys_crypto_engine_random_generate(PPCContext& ctx)
    {
        // Unlike RPCS3's current TODO success stub, actually initializes the output.
        const int64_t result = random_bytes(ctx.r3.u32, ctx.r4.u64);
        static std::atomic_bool reported{};
        if (result == CELL_OK && ctx.r4.u64 && !reported.exchange(true))
            log_line("ss: crypto RNG active (Windows CNG, first request %llu bytes)", (unsigned long long)ctx.r4.u64);
        return result;
    }

    int64_t sys_ss_random_number_generator(PPCContext& ctx)
    {
        if (ctx.r3.u64 == 1)
        {
            // The runtime currently hosts privileged VSH. Match RPCS3's root-only
            // package-1 result; this package is not an arbitrary-size random request.
            if (!writable_guest_span(ctx.r4.u32, 0x18)) return RNG_EFAULT;
            memset(g_base + ctx.r4.u32, 0, 0x18);
            return CELL_OK;
        }
        if (ctx.r3.u64 != 2) return RNG_INVALID_PKG;
        if (ctx.r5.u64 > 0x10000000) return RNG_ENOMEM;
        const int64_t result = random_bytes(ctx.r4.u32, ctx.r5.u64);
        static std::atomic_bool reported{};
        if (result == CELL_OK && ctx.r5.u64 && !reported.exchange(true))
            log_line("ss: secure RNG package 2 active (Windows CNG)");
        return result == CELL_EFAULT ? RNG_EFAULT : result == CELL_EIO ? RNG_EAGAIN : result;
    }

    std::once_flag g_psid_once;
    std::array<uint8_t, 16> g_psid{};
    int64_t g_psid_status = CELL_EIO;

    int hex_digit(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    void initialize_psid()
    {
        if (const char* configured = getenv("XMBRT_CONSOLE_PSID"))
        {
            if (strlen(configured) != 32) { g_psid_status = CELL_EINVAL; return; }
            for (unsigned i = 0; i < g_psid.size(); ++i)
            {
                const int high = hex_digit(configured[i * 2]), low = hex_digit(configured[i * 2 + 1]);
                if (high < 0 || low < 0) { g_psid_status = CELL_EINVAL; return; }
                g_psid[i] = uint8_t(high * 16 + low);
            }
            g_psid_status = CELL_OK;
            log_line("ss: using configured emulated console PSID");
            return;
        }

        wchar_t executable[32768];
        const DWORD length = GetModuleFileNameW(nullptr, executable, DWORD(std::size(executable)));
        if (!length || length >= std::size(executable)) return;
        const auto path = std::filesystem::path(executable).parent_path() / L"console-psid.bin";
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            LARGE_INTEGER size{};
            DWORD count = 0;
            const bool valid = GetFileSizeEx(file, &size) && size.QuadPart == g_psid.size() &&
                ReadFile(file, g_psid.data(), DWORD(g_psid.size()), &count, nullptr) && count == g_psid.size();
            CloseHandle(file);
            if (valid) { g_psid_status = CELL_OK; log_line("ss: loaded persistent emulated console PSID"); }
            else log_line("ss: invalid console-psid.bin; identity was not replaced");
            return;
        }
        if (const DWORD error = GetLastError(); error != ERROR_FILE_NOT_FOUND)
        { log_line("ss: cannot open persistent console identity (%lu)",error); return; }
        if (BCryptGenRandom(nullptr, g_psid.data(), DWORD(g_psid.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return;
        file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        { log_line("ss: cannot create persistent console identity (%lu)",GetLastError()); return; }
        DWORD count = 0;
        const bool saved = WriteFile(file, g_psid.data(), DWORD(g_psid.size()), &count, nullptr) &&
            count == g_psid.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (saved) { g_psid_status = CELL_OK; log_line("ss: created persistent emulated console PSID"); }
        else log_line("ss: could not persist emulated console PSID");
    }

    int64_t sys_ss_get_open_psid(PPCContext& ctx) { return ss_copy_open_psid(ctx.r3.u32); }
}

int64_t ss_copy_open_psid(uint32_t address)
{
    if (!writable_guest_span(address, g_psid.size())) return CELL_EFAULT;
    std::call_once(g_psid_once, initialize_psid);
    if (g_psid_status != CELL_OK) return g_psid_status;
    memcpy(g_base + address, g_psid.data(), g_psid.size());
    return CELL_OK;
}

void register_security_syscalls()
{
    register_syscall(865, sys_ss_random_number_generator);
    register_syscall(872, sys_ss_get_open_psid);
    register_syscall(984, sys_crypto_engine_random_generate);
}
