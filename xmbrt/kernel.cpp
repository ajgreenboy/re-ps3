// Kernel core: sleep/wake, object registry, guest memory helpers.
#include "kernel.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <chrono>

std::mutex g_kernel;

namespace
{
    const auto g_timebase_start = std::chrono::steady_clock::now();
}

// Guest timebase (mftb): 80 MHz since process start, the rate sys_time_get_timebase_frequency reports.
extern "C" uint64_t ppc_mftb()
{
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - g_timebase_start).count();
    return uint64_t(ns) * 2 / 25;
}

uint64_t system_time_us()
{
    return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - g_timebase_start).count());
}

std::string guest_string(uint32_t addr, size_t max)
{
    std::string s;
    if (!addr) return s;
    for (size_t i = 0; i < max; i++)
    {
        const char c = char(g_base[addr + i]);
        if (!c) break;
        s += c;
    }
    return s;
}

bool guest_commit(uint32_t addr, uint64_t size)
{
    if (!size) return true;
    const uint64_t start = addr & ~0xFFFull;
    const uint64_t end = (uint64_t(addr) + size + 0xFFF) & ~0xFFFull;
    return VirtualAlloc(g_base + start, size_t(end - start), MEM_COMMIT, PAGE_READWRITE) != nullptr;
}

bool sleep_thread(KLock& lock, uint64_t timeout_us)
{
    PpuThread* t = self();
    t->woken = false;
    if (!timeout_us)
    {
        t->cv.wait(lock, [t] { return t->woken; });
        return true;
    }
    return t->cv.wait_for(lock, std::chrono::microseconds(timeout_us), [t] { return t->woken; });
}

void wake_thread(PpuThread* t, int64_t result)
{
    t->wake_result = result;
    t->woken = true;
    t->cv.notify_one();
}

bool unqueue(std::deque<PpuThread*>& q, PpuThread* t)
{
    for (auto it = q.begin(); it != q.end(); ++it)
    {
        if (*it == t)
        {
            q.erase(it);
            return true;
        }
    }
    return false;
}

namespace
{
    std::mutex g_objects_mutex;
    std::unordered_map<uint32_t, std::shared_ptr<KObject>> g_objects;
    uint32_t g_next_index[256];
}

uint32_t kobj_add(uint8_t type, std::shared_ptr<KObject> obj)
{
    std::lock_guard lock(g_objects_mutex);
    const uint32_t id = uint32_t(type) << 24 | (++g_next_index[type] << 8);
    g_objects[id] = std::move(obj);
    return id;
}

std::shared_ptr<KObject> kobj_get(uint32_t id)
{
    std::lock_guard lock(g_objects_mutex);
    const auto it = g_objects.find(id);
    return it == g_objects.end() ? nullptr : it->second;
}

bool kobj_remove(uint32_t id)
{
    std::lock_guard lock(g_objects_mutex);
    return g_objects.erase(id) != 0;
}
