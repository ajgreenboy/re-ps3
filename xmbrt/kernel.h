// LV2 kernel model for xmbrt. Semantics follow RPCS3 (rpcs3 0b0fa08, Emu/Cell/lv2/*.cpp).
// One big kernel lock guards every kernel object; a guest thread that blocks sleeps on its own
// condition variable and is woken with a result code, like RPCS3's lv2_obj::sleep/awake.
#pragma once

#include "xmbrt.h"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

enum : int32_t
{
    CELL_OK = 0,
    CELL_EAGAIN = int32_t(0x80010001),
    CELL_EINVAL = int32_t(0x80010002),
    CELL_ENOMEM = int32_t(0x80010004),
    CELL_ESRCH = int32_t(0x80010005),
    CELL_ENOENT = int32_t(0x80010006),
    CELL_ENOEXEC = int32_t(0x80010007),
    CELL_EDEADLK = int32_t(0x80010008),
    CELL_EPERM = int32_t(0x80010009),
    CELL_EBUSY = int32_t(0x8001000A),
    CELL_ETIMEDOUT = int32_t(0x8001000B),
    CELL_EABORT = int32_t(0x8001000C),
    CELL_EFAULT = int32_t(0x8001000D),
    CELL_ESTAT = int32_t(0x8001000F),
    CELL_EALIGN = int32_t(0x80010010),
    CELL_EKRESOURCE = int32_t(0x80010011),
    CELL_EISDIR = int32_t(0x80010012),
    CELL_ECANCELED = int32_t(0x80010013),
    CELL_EEXIST = int32_t(0x80010014),
    CELL_EISCONN = int32_t(0x80010015),
    CELL_ENOTCONN = int32_t(0x80010016),
    CELL_EROFS = int32_t(0x80010026),
    CELL_EBADF = int32_t(0x8001002A),
    CELL_EIO = int32_t(0x8001002B),
    CELL_EMFILE = int32_t(0x8001002C),
    CELL_ENOTDIR = int32_t(0x8001002E),
    CELL_ENOTEMPTY = int32_t(0x80010036),
    CELL_ENOTSUP = int32_t(0x80010037),
    CELL_ENOTMOUNTED = int32_t(0x8001003A),
};

// ---- guest memory helpers -------------------------------------------------------------------
inline uint8_t guest_load_u8(uint32_t a) { return g_base[a]; }
inline uint16_t guest_load_u16(uint32_t a) { return __builtin_bswap16(*(uint16_t*)(g_base + a)); }
inline void guest_store_u8(uint32_t a, uint8_t v) { g_base[a] = v; }
inline void guest_store_u16(uint32_t a, uint16_t v) { *(uint16_t*)(g_base + a) = __builtin_bswap16(v); }
inline void* guest_ptr(uint32_t a) { return g_base + a; }
std::string guest_string(uint32_t addr, size_t max = 0x400);
// Commits guest pages (idempotent); false if Windows refuses.
bool guest_commit(uint32_t addr, uint64_t size);

// ---- threads --------------------------------------------------------------------------------
struct PpuThread
{
    uint32_t id = 0;
    std::string name;
    PPCContext* ctx = nullptr;
    uint32_t entry_opd = 0;
    uint64_t arg0 = 0, arg1 = 0;
    uint32_t tls = 0;
    int32_t prio = 0;
    uint32_t stack_addr = 0, stack_size = 0;
    bool joinable = false;
    bool started = false;
    bool exited = false;
    uint64_t exit_status = 0;
    PpuThread* joiner = nullptr;

    // Sleep state, guarded by the kernel lock.
    std::condition_variable cv;
    bool woken = false;
    int64_t wake_result = 0;
    uint64_t wake_data[4]{}; // event payload handed to a sleeping receiver
};

extern std::mutex g_kernel;
using KLock = std::unique_lock<std::mutex>;

PpuThread* self();
PpuThread* find_thread(uint32_t id);
// Sleeps the calling thread until woken; timeout_us 0 = forever. Returns false on timeout.
bool sleep_thread(KLock& lock, uint64_t timeout_us);
void wake_thread(PpuThread* t, int64_t result);
// Removes t from q if present.
bool unqueue(std::deque<PpuThread*>& q, PpuThread* t);

// Thrown to unwind a guest thread's host stack on sys_ppu_thread_exit.
struct ThreadExit
{
    uint64_t status;
};

void kernel_init_main_thread(PPCContext* ctx, uint32_t stack_addr, uint32_t stack_size);
uint32_t alloc_stack(uint32_t size);
// Runs a guest function (OPD) on the calling host thread's context.
void call_guest_opd(PPCContext& ctx, uint32_t opd);

// ---- object registry --------------------------------------------------------------------------
struct KObject
{
    virtual ~KObject() = default;
};

// IDs follow RPCS3's shape: type byte in the top 8 bits, then 0x100 steps.
uint32_t kobj_add(uint8_t type, std::shared_ptr<KObject> obj);
std::shared_ptr<KObject> kobj_get(uint32_t id);
bool kobj_remove(uint32_t id);

template <typename T>
std::shared_ptr<T> kobj(uint32_t id)
{
    return std::dynamic_pointer_cast<T>(kobj_get(id));
}

// ---- syscall table ------------------------------------------------------------------------------
using SyscallFn = int64_t (*)(PPCContext& ctx);
void register_syscall(uint32_t number, SyscallFn fn);
// Each k_*.cpp registers its handlers from one of these.
void register_thread_syscalls();
void register_sync_syscalls();
void register_event_syscalls();
void register_memory_syscalls();
void register_fs_syscalls();
void register_prx_syscalls();
void register_misc_syscalls();

// Memory areas (k_memory.cpp), for other subsystems that need guest allocations.
uint32_t user_alloc(uint32_t size, uint32_t align);
