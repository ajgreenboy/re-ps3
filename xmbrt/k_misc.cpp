// Small LV2 syscalls: process, time, tty, system manager (sm), secure services (ss), storage, hid manager,
// and stubs whose RPCS3 implementation only returns CELL_OK. Values follow RPCS3's sys_*.cpp.
#include "kernel.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <chrono>
#include <cstring>
#include <thread>

namespace
{
    uint32_t g_sdk_version = 0;
    const auto g_start = std::chrono::steady_clock::now();

    // ---- process ----
    int64_t sys_process_getpid(PPCContext&) { return 1; }

    // sys_process_get_sdk_version(pid, version*)
    int64_t sys_process_get_sdk_version(PPCContext& ctx)
    {
        guest_store_u32(ctx.r4.u32, g_sdk_version);
        return CELL_OK;
    }

    int64_t sys_process_get_paramsfo(PPCContext&) { return CELL_ENOENT; } // vsh has no PARAM.SFO

    int64_t sys_process_get_number_of_object(PPCContext& ctx)
    {
        guest_store_u32(ctx.r4.u32, 0);
        return CELL_OK;
    }

    int64_t sys_process_exit(PPCContext& ctx)
    {
        log_line("process exit, status %llX", (unsigned long long)ctx.r3.u64);
        fatal_exit(int(ctx.r3.u32));
    }

    // ---- time ----
    int64_t sys_time_get_timebase_frequency(PPCContext&) { return 80000000; }

    int64_t sys_time_get_system_time(PPCContext&)
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - g_start).count();
    }

    // sys_time_get_current_time(sec*, nsec*)
    int64_t sys_time_get_current_time(PPCContext& ctx)
    {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
        guest_store_u64(ctx.r3.u32, uint64_t(ns / 1000000000));
        guest_store_u64(ctx.r4.u32, uint64_t(ns % 1000000000));
        return CELL_OK;
    }

    // sys_time_get_timezone(timezone*, summertime*)
    int64_t sys_time_get_timezone(PPCContext& ctx)
    {
        guest_store_u32(ctx.r3.u32, 0);
        guest_store_u32(ctx.r4.u32, 0);
        return CELL_OK;
    }

    // sys_time_get_rtc(rtc*): seconds since 2000-01-01
    int64_t sys_time_get_rtc(PPCContext& ctx)
    {
        const auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        guest_store_u64(ctx.r3.u32, uint64_t(secs - 946684800));
        return CELL_OK;
    }

    int64_t sys_timer_usleep(PPCContext& ctx)
    {
        const uint64_t us = ctx.r3.u64;
        if (us) std::this_thread::sleep_for(std::chrono::microseconds(us));
        else SwitchToThread();
        return CELL_OK;
    }

    int64_t sys_timer_sleep(PPCContext& ctx)
    {
        std::this_thread::sleep_for(std::chrono::seconds(ctx.r3.u32));
        return CELL_OK;
    }

    // ---- tty ----
    // sys_tty_write(ch, buf*, len, pwritelen*)
    int64_t sys_tty_write(PPCContext& ctx)
    {
        const uint32_t len = ctx.r5.u32;
        std::string text(reinterpret_cast<const char*>(g_base + ctx.r4.u32), len);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        if (!text.empty()) log_line("tty%u: %s", ctx.r3.u32, text.c_str());
        if (ctx.r6.u32) guest_store_u32(ctx.r6.u32, len);
        return CELL_OK;
    }

    // sys_tty_read(ch, buf*, len, preadlen*)
    int64_t sys_tty_read(PPCContext& ctx)
    {
        if (ctx.r6.u32) guest_store_u32(ctx.r6.u32, 0);
        return CELL_OK;
    }

    // sys_console_write(buf*, len)
    int64_t sys_console_write(PPCContext& ctx)
    {
        std::string text(reinterpret_cast<const char*>(g_base + ctx.r3.u32), ctx.r4.u32);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        if (!text.empty()) log_line("console: %s", text.c_str());
        return CELL_OK;
    }

    // ---- system manager ----
    // sys_sm_get_params(u8* a, u8* b, u32* c, u64* d)
    int64_t sys_sm_get_params(PPCContext& ctx)
    {
        if (!ctx.r3.u32 || !ctx.r4.u32 || !ctx.r5.u32 || !ctx.r6.u32) return CELL_EFAULT;
        guest_store_u8(ctx.r3.u32, 0);
        guest_store_u8(ctx.r4.u32, 0);
        guest_store_u32(ctx.r5.u32, 0x200);
        guest_store_u64(ctx.r6.u32, 7);
        return CELL_OK;
    }

    // ---- secure services ----
    int64_t sys_ss_get_cache_of_product_mode(PPCContext& ctx)
    {
        if (!ctx.r3.u32) return CELL_EINVAL;
        guest_store_u8(ctx.r3.u32, 0xFF);
        return CELL_OK;
    }

    int64_t sys_ss_get_boot_device(PPCContext& ctx)
    {
        if (!ctx.r3.u32) return CELL_EINVAL;
        guest_store_u64(ctx.r3.u32, 0x190);
        return CELL_OK;
    }

    // sys_ss_appliance_info_manager(code, buffer*)
    int64_t sys_ss_appliance_info_manager(PPCContext& ctx)
    {
        const uint32_t buf = ctx.r4.u32;
        if (!buf) return CELL_EFAULT;
        switch (ctx.r3.u32)
        {
        case 0x19002: // device type: retail
        {
            static const uint8_t product_code[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x89 };
            memcpy(g_base + buf, product_code, 16);
            break;
        }
        case 0x19003: // device id (IDPS)
        {
            static const uint8_t idps[16] = { 0x00, 0x00, 0x00, 0x01, 0x00, 0x89, 0x00, 0x0B, 0x14, 0x00, 0xEF, 0xDD, 0xCA, 0x25, 0x52, 0x66 };
            memcpy(g_base + buf, idps, 16);
            break;
        }
        case 0x19004: // ps code
        {
            static const uint8_t pscode[8] = { 0x00, 0x01, 0x00, 0x85, 0x00, 0x07, 0x00, 0x04 };
            memcpy(g_base + buf, pscode, 8);
            break;
        }
        case 0x19005: // open psid
            memset(g_base + buf, 0, 16);
            break;
        default:
            break;
        }
        return CELL_OK;
    }

    std::mutex g_eeprom_mutex;
    std::unordered_map<uint32_t, uint8_t> g_eeprom = {
        { 0x48C18, 0x00 }, { 0x48C19, 0x00 }, { 0x48C1A, 0x00 }, { 0x48C1B, 0x01 }, // system language (English)
        { 0x48C1C, 0x00 }, { 0x48C1D, 0x00 }, { 0x48C1E, 0x00 }, { 0x48C1F, 0x00 }, // vsh target
    };

    // sys_ss_update_manager(pkg_id, a1, a2, a3, a4, a5, a6)
    int64_t sys_ss_update_manager(PPCContext& ctx)
    {
        const uint64_t a1 = ctx.r4.u64, a2 = ctx.r5.u64, a3 = ctx.r6.u64;
        switch (ctx.r3.u32)
        {
        case 0x6003: // installed package info: system software version 4.93
            if (!a2) return CELL_EFAULT;
            guest_store_u64(uint32_t(a2), 0x49300ull << 32);
            break;
        case 0x600B: // read eeprom (offset, u8* value)
        {
            std::lock_guard lock(g_eeprom_mutex);
            const auto it = g_eeprom.find(uint32_t(a1));
            guest_store_u8(uint32_t(a2), it == g_eeprom.end() ? 0xFF : it->second);
            break;
        }
        case 0x600C: // write eeprom (offset, value); 0xFF unsets
        {
            std::lock_guard lock(g_eeprom_mutex);
            if (uint8_t(a2) != 0xFF) g_eeprom[uint32_t(a1)] = uint8_t(a2);
            else g_eeprom.erase(uint32_t(a1));
            break;
        }
        case 0x600E: // allocate buffer (size, addr*)
        {
            if (!a2) return CELL_EFAULT;
            const uint32_t addr = user_alloc(uint32_t(a1), 0x10000);
            if (!addr) return CELL_ENOMEM;
            guest_store_u32(uint32_t(a2), addr);
            break;
        }
        case 0x6011: // applicable version (unused, addr*)
            if (!a2) return CELL_EFAULT;
            guest_store_u64(uint32_t(a2), 0x30040ull << 32);
            break;
        case 0x6012: // allocate buffer from container (cid, size, addr*)
        {
            if (!a3) return CELL_EFAULT;
            const uint32_t addr = user_alloc(uint32_t(a2), 0x10000);
            if (!addr) return CELL_ENOMEM;
            guest_store_u32(uint32_t(a3), addr);
            break;
        }
        case 0x6001: case 0x6002: case 0x6004: case 0x6005: case 0x6006: case 0x6007: case 0x6008:
        case 0x6009: case 0x600A: case 0x600D: case 0x600F: case 0x6010: case 0x6013:
            break;
        default:
            return CELL_EINVAL;
        }
        return CELL_OK;
    }

    // ---- storage ----
    struct KStorage : KObject
    {
        uint64_t device;
    };

    // sys_storage_open(device, mode, fd*, flags)
    int64_t sys_storage_open(PPCContext& ctx)
    {
        if (!ctx.r3.u64) return CELL_ENOENT;
        if (!ctx.r5.u32) return CELL_EFAULT;
        auto s = std::make_shared<KStorage>();
        s->device = ctx.r3.u64;
        guest_store_u32(ctx.r5.u32, kobj_add(0x45, s));
        return CELL_OK;
    }

    int64_t sys_storage_close(PPCContext& ctx) { return kobj_remove(ctx.r3.u32) ? CELL_OK : CELL_ESRCH; }

    // sys_storage_get_device_info(device, StorageDeviceInfo*): name[0x20], u32 zero, u32 zero2,
    // u64 sector_count, u32 sector_size, u32 one, u8 flags[8]
    int64_t sys_storage_get_device_info(PPCContext& ctx)
    {
        const uint32_t buf = ctx.r4.u32;
        if (!buf) return CELL_EFAULT;
        memset(g_base + buf, 0, 0x40);
        const uint64_t storage = ctx.r3.u64 & 0xFFFFF00FFFFFFFFull;
        const uint32_t dev = uint32_t(ctx.r3.u64 >> 32) & 0xFF;
        uint64_t sectors = 0;
        uint32_t sector_size = 0x200;
        uint8_t flag1 = 0;
        switch (storage)
        {
        case 0x101000000000007: // ATA HDD
        {
            if (dev > 2) return int32_t(-5);
            static const uint64_t counts[] = { 0x2542EAB0, 0x24FAEA98, 0x3FFFF8 };
            sectors = counts[dev];
            flag1 = 1;
            break;
        }
        case 0x101000000000006: // BD drive
            if (dev > 0) return int32_t(-5);
            sectors = 0x4D955;
            sector_size = 0x800;
            break;
        case 0x10300000000000A: // USB mass storage
            if (dev > 0) return int32_t(-5);
            break;
        case 0x100000000000001: // NAND flash
        {
            if (dev > 6) return int32_t(-5);
            static const uint64_t counts[] = { 0x80000, 0x75F8, 0x63E00, 0x8000, 0x400, 0x2000, 0x200 };
            sectors = counts[dev];
            flag1 = 1;
            break;
        }
        case 0x100000000000004: // NOR flash
        {
            if (dev > 3) return int32_t(-5);
            static const uint64_t counts[] = { 0x8000, 0x77F8, 0x100, 0x400 };
            sectors = counts[dev];
            break;
        }
        case 0x100000000000003: // NAND (unknown)
            if (dev > 1) return int32_t(-5);
            sectors = dev == 0 ? 0x7FFFFFFF : 0;
            sector_size = 0x800;
            break;
        default:
            log_line("storage: unknown device %llX", (unsigned long long)ctx.r3.u64);
            return CELL_OK;
        }
        memcpy(g_base + buf, "unnamed", 7);
        guest_store_u64(buf + 0x28, sectors);
        guest_store_u32(buf + 0x30, sector_size);
        guest_store_u32(buf + 0x34, 1);
        guest_store_u8(buf + 0x38 + 1, flag1);
        guest_store_u8(buf + 0x38 + 2, 1);
        guest_store_u8(buf + 0x38 + 7, 1);
        return CELL_OK;
    }

    int64_t sys_storage_get_device_config(PPCContext& ctx)
    {
        if (!ctx.r3.u32 || !ctx.r4.u32) return CELL_EFAULT;
        guest_store_u32(ctx.r3.u32, 6);
        guest_store_u32(ctx.r4.u32, 17);
        return CELL_OK;
    }

    // sys_storage_report_devices(storages, start, devices, device_ids*)
    int64_t sys_storage_report_devices(PPCContext& ctx)
    {
        static const uint64_t all[17] = {
            0x10300000000000A,
            0x100000000000001, 0x100000100000001, 0x100000200000001, 0x100000300000001, 0x100000400000001, 0x100000500000001, 0x100000600000001,
            0x101000000000007, 0x101000100000007, 0x101000200000007,
            0x101000000000006,
            0x100000000000004, 0x100000100000004, 0x100000200000004, 0x100000300000004,
            0x100000000000003,
        };
        const uint32_t start = ctx.r4.u32, devices = ctx.r5.u32, out = ctx.r6.u32;
        if (!out) return CELL_EFAULT;
        if (!devices || start >= 17 || devices > 17 - start) return CELL_EINVAL;
        for (uint32_t i = 0; i < devices; i++) guest_store_u64(out + i * 8, all[start + i]);
        return CELL_OK;
    }

    int64_t sys_ok(PPCContext&) { return CELL_OK; }

    // ---- vuart (AV manager); requests are accepted, no replies yet ----
    int64_t sys_uart_send(PPCContext& ctx) { return int64_t(ctx.r4.u64); }
    int64_t sys_uart_receive(PPCContext&) { return 0; }
}

void misc_set_sdk_version(uint32_t version) { g_sdk_version = version; }

void register_misc_syscalls()
{
    register_syscall(1, sys_process_getpid);
    register_syscall(3, sys_process_exit);
    register_syscall(12, sys_process_get_number_of_object);
    register_syscall(14, sys_ok); // is_spu_lock_line_reservation_address
    register_syscall(22, sys_process_exit);
    register_syscall(25, sys_process_get_sdk_version);
    register_syscall(26, sys_process_exit);
    register_syscall(30, sys_process_get_paramsfo);
    register_syscall(119, sys_time_get_rtc);
    register_syscall(141, sys_timer_usleep);
    register_syscall(142, sys_timer_sleep);
    register_syscall(143, sys_ok); // set_timezone
    register_syscall(144, sys_time_get_timezone);
    register_syscall(145, sys_time_get_current_time);
    register_syscall(146, sys_ok); // set_current_time
    register_syscall(147, sys_time_get_timebase_frequency);
    register_syscall(149, sys_time_get_system_time);
    register_syscall(169, sys_ok); // spu_initialize
    register_syscall(367, sys_ok); // uart_initialize
    register_syscall(368, sys_uart_receive);
    register_syscall(369, sys_uart_send);
    register_syscall(372, sys_ok); // game watchdog start
    register_syscall(373, sys_ok);
    register_syscall(374, sys_ok);
    register_syscall(375, sys_ok); // _sys_game_set_system_sw_version
    register_syscall(377, sys_ok); // sm_set_shop_mode
    register_syscall(380, sys_sm_get_params);
    register_syscall(382, sys_ok); // sm_initialize
    register_syscall(385, sys_ok); // sm_request_led
    register_syscall(386, sys_ok); // sm_control_led
    register_syscall(398, sys_console_write);
    register_syscall(402, sys_tty_read);
    register_syscall(403, sys_tty_write);
    for (uint32_t n = 500; n <= 514; n++) register_syscall(n, sys_ok); // hid manager
    register_syscall(600, sys_storage_open);
    register_syscall(601, sys_storage_close);
    register_syscall(609, sys_storage_get_device_info);
    register_syscall(610, sys_storage_get_device_config);
    register_syscall(611, sys_storage_report_devices);
    register_syscall(612, sys_ok); // configure_medium_event
    register_syscall(613, sys_ok); // set_medium_polling_interval
    register_syscall(635, sys_ok); // btsetting_if
    for (uint32_t n = 650; n <= 659; n++) register_syscall(n, sys_ok); // rsxaudio (phase 5)
    register_syscall(721, sys_ok); // net_abort
    register_syscall(863, sys_ss_update_manager);
    register_syscall(867, sys_ss_appliance_info_manager);
    register_syscall(873, sys_ss_get_cache_of_product_mode);
    register_syscall(875, sys_ss_get_boot_device);
}
