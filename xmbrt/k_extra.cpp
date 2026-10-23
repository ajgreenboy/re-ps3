// Additional VSH services: HID input, system-manager/secure-service helpers,
// storage and IO buffers, USB control events and Bluetooth placeholders.
#include "kernel.h"

#include "k_event.h"

#include <cstring>

namespace
{
    int64_t sys_ok(PPCContext&) { return CELL_OK; }

    // ---- hid manager (no pad data until phase 5) ----
    uint32_t g_hid_handle = 0x100;

    // sys_hid_manager_open(device_type, port_no, handle*)
    int64_t sys_hid_manager_open(PPCContext& ctx)
    {
        if (ctx.r3.u64 > 3) return CELL_EINVAL;
        if (!ctx.r5.u32) return CELL_EFAULT;
        guest_store_u32(ctx.r5.u32, g_hid_handle++);
        return CELL_OK;
    }

    // sys_hid_manager_ioctl(handle, pkg_id, buf*, size): device info a DualShock 3 reports
    int64_t sys_hid_manager_ioctl(PPCContext& ctx)
    {
        const uint32_t pkg = ctx.r4.u32, buf = ctx.r5.u32;
        if (pkg == 2)
        {
            static const uint8_t realhw[17] = { 0x01, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x03, 0x50, 0x00, 0x00, 0x1C, 0x1F };
            guest_store_u16(buf, 0x054C);
            guest_store_u16(buf + 2, 0x0268);
            memcpy(g_base + buf + 4, realhw, sizeof(realhw));
        }
        else if (pkg == 5)
        {
            guest_store_u16(buf, 0x054C);
            guest_store_u16(buf + 2, 0x0268);
        }
        return CELL_OK;
    }

    int64_t sys_hid_manager_check_focus(PPCContext&) { return 1; }

    // sys_hid_manager_read(handle, pkg_id, buf*, size): pkg 2 returns bytes copied, pkg 0x81 the u16 count
    int64_t sys_hid_manager_read(PPCContext& ctx)
    {
        const uint32_t pkg = ctx.r4.u32, buf = ctx.r5.u32;
        if (!buf) return CELL_EFAULT;
        if (pkg == 2) return int64_t(pad_read(buf, ctx.r6.u64)) * 2;
        if (pkg == 0x81) return pad_read(buf, ctx.r6.u64);
        return CELL_OK;
    }

    // ---- system manager / secure services ----
    // sys_sm_get_ext_event2(a1*, a2*, a3*, a4): no event
    int64_t sys_sm_get_ext_event2(PPCContext& ctx)
    {
        if (ctx.r6.u64 > 1) return CELL_EINVAL;
        if (!ctx.r3.u32 || !ctx.r4.u32 || !ctx.r5.u32) return CELL_EFAULT;
        guest_store_u64(ctx.r3.u32, 0);
        guest_store_u64(ctx.r4.u32, 0);
        guest_store_u64(ctx.r5.u32, 0);
        return CELL_EAGAIN;
    }

    int64_t sys_ss_get_cache_of_flash_ext_flag(PPCContext& ctx)
    {
        if (!ctx.r3.u32) return CELL_EFAULT;
        guest_store_u64(ctx.r3.u32, 0xFE); // NAND console
        return CELL_OK;
    }

    // sys_ss_secure_rtc(cmd, a2, a3, a4)
    int64_t sys_ss_secure_rtc(PPCContext& ctx)
    {
        switch (ctx.r3.u32)
        {
        case 0x3001: return ctx.r5.u64 == 0x20 ? CELL_OK : int32_t(0x80010500);
        case 0x3002: // get time: a3 = timebase value, a4 = status
            if (ctx.r4.u64 > 1) return int32_t(0x80010500);
            guest_store_u64(ctx.r5.u32, ppc_mftb());
            guest_store_u64(ctx.r6.u32, 0);
            return CELL_OK;
        case 0x3003: return CELL_OK;
        default: return int32_t(0x80010500);
        }
    }

    // ---- storage / io buffers / usb / bluetooth ----
    // sys_storage_read(fd, mode, start_sector, num_sectors, bounce_buf*, sectors_read*, flags): zeros
    // Async storage completion queue (RPCS3 sys_storage.cpp: one queue per process, set by configure)
    uint32_t g_storage_async_queue = 0;

    // sys_storage_async_configure(fd, io_buf, equeue_id, unk*)
    int64_t sys_storage_async_configure(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        if (!find_event_queue(ctx.r5.u32)) return CELL_ESRCH;
        g_storage_async_queue = ctx.r5.u32;
        return CELL_OK;
    }

    // sys_storage_async_send_device_command(dev, cmd, in, inlen, out, outlen, unk): completes at once,
    // posting (0, unk, unk, unk) to the configured queue as RPCS3 does
    int64_t sys_storage_async_send_device_command(PPCContext& ctx)
    {
        const uint64_t unk = ctx.r9.u64;
        KLock lock(g_kernel);
        if (g_storage_async_queue)
            if (const auto q = find_event_queue(g_storage_async_queue)) event_queue_send(*q, 0, unk, unk, unk);
        return CELL_OK;
    }

    int64_t sys_storage_read(PPCContext& ctx)
    {
        const uint32_t n = ctx.r6.u32, buf = ctx.r7.u32;
        if (buf) memset(g_base + buf, 0, size_t(n) * 0x200);
        if (ctx.r8.u32) guest_store_u32(ctx.r8.u32, n);
        return CELL_OK;
    }

    struct KIoBuffer : KObject
    {
        uint32_t block_count = 0, block_size = 0;
    };

    // sys_io_buffer_create(block_count, block_size, blocks, unk1, handle*)
    int64_t sys_io_buffer_create(PPCContext& ctx)
    {
        auto b = std::make_shared<KIoBuffer>();
        b->block_count = ctx.r3.u32;
        b->block_size = ctx.r4.u32;
        guest_store_u32(ctx.r7.u32, kobj_add(0x44, b));
        return CELL_OK;
    }

    // sys_io_buffer_allocate(handle, block*)
    int64_t sys_io_buffer_allocate(PPCContext& ctx)
    {
        auto b = kobj<KIoBuffer>(ctx.r3.u32);
        if (!b) return CELL_ESRCH;
        const uint32_t addr = user_alloc(b->block_count * b->block_size, 0x10000);
        if (!addr) return CELL_ENOMEM;
        guest_store_u32(ctx.r4.u32, addr);
        return CELL_OK;
    }

    int64_t sys_io_buffer_destroy(PPCContext& ctx) { return kobj_remove(ctx.r3.u32) ? CELL_OK : CELL_ESRCH; }

    struct UsbEvent { uint64_t args[3]; };
    bool g_usb_initialized = false;
    std::deque<UsbEvent> g_usb_events;
    std::deque<PpuThread*> g_usb_receivers;

    // sys_usbd_initialize(handle*)
    int64_t sys_usbd_initialize(PPCContext& ctx)
    {
        if (!ctx.r3.u32) return CELL_EFAULT;
        KLock lock(g_kernel);
        g_usb_initialized = true;
        guest_store_u32(ctx.r3.u32, 0x115B);
        return CELL_OK;
    }

    // sys_usbd_get_device_list(handle, device_list*, max): no USB devices
    int64_t sys_usbd_get_device_list(PPCContext&) { return 0; }

    // USB control events also carry the firmware's callback-thread shutdown message.
    int64_t sys_usbd_receive_event(PPCContext& ctx)
    {
        if (!ctx.r4.u32 || !ctx.r5.u32 || !ctx.r6.u32) return CELL_EFAULT;
        KLock lock(g_kernel);
        if (!g_usb_initialized) return CELL_EINVAL;
        UsbEvent event{};
        if (!g_usb_events.empty())
        {
            event = g_usb_events.front();
            g_usb_events.pop_front();
        }
        else
        {
            g_usb_receivers.push_back(self());
            sleep_thread(lock, 0);
            for (unsigned i=0;i<3;++i) event.args[i]=self()->wake_data[i];
        }
        guest_store_u64(ctx.r4.u32,event.args[0]);
        guest_store_u64(ctx.r5.u32,event.args[1]);
        guest_store_u64(ctx.r6.u32,event.args[2]);
        return CELL_OK;
    }

    int64_t sys_usbd_event_port_send(PPCContext& ctx)
    {
        KLock lock(g_kernel);
        if (!g_usb_initialized) return CELL_EINVAL;
        UsbEvent event{{ctx.r4.u64,ctx.r5.u64,ctx.r6.u64}};
        if (g_usb_receivers.empty()) g_usb_events.push_back(event);
        else
        {
            auto receiver=g_usb_receivers.front();
            g_usb_receivers.pop_front();
            for(unsigned i=0;i<3;++i) receiver->wake_data[i]=event.args[i];
            wake_thread(receiver,CELL_OK);
        }
        return CELL_OK;
    }

    int64_t sys_usbd_finalize(PPCContext&)
    {
        KLock lock(g_kernel);
        if (!g_usb_initialized) return CELL_EINVAL;
        if (!g_usb_receivers.empty()) return CELL_EBUSY;
        g_usb_initialized=false;
        g_usb_events.clear();
        return CELL_OK;
    }

    // sys_bluetooth_aud_serial_get_event_579(arg, out1*, out2*, out3*, out4*): no event
    int64_t sys_bluetooth_579(PPCContext& ctx)
    {
        for (uint32_t p : { ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32 })
            if (p) guest_store_u32(p, 0);
        return CELL_OK;
    }
}

void register_extra_syscalls()
{
    register_syscall(378, sys_sm_get_ext_event2);
    register_syscall(500, sys_hid_manager_open);
    register_syscall(502, sys_hid_manager_read);
    register_syscall(503, sys_hid_manager_ioctl);
    register_syscall(510, sys_hid_manager_check_focus);
    register_syscall(530, sys_usbd_initialize);
    register_syscall(531, sys_usbd_finalize);
    register_syscall(532, sys_usbd_get_device_list);
    register_syscall(535, sys_ok); // usbd_register_ldd
    register_syscall(540, sys_usbd_receive_event);
    register_syscall(549, sys_usbd_event_port_send);
    register_syscall(579, sys_bluetooth_579);
    register_syscall(584, sys_ok);
    register_syscall(591, sys_ok);
    register_syscall(602, sys_storage_read);
    register_syscall(603, sys_ok); // storage_write
    register_syscall(605, sys_storage_async_configure);
    register_syscall(619, sys_storage_async_send_device_command);
    register_syscall(624, sys_io_buffer_create);
    register_syscall(625, sys_io_buffer_destroy);
    register_syscall(626, sys_io_buffer_allocate);
    register_syscall(627, sys_ok); // io_buffer_free
    register_syscall(638, sys_ok);
    register_syscall(847, sys_ok); // fs_truncate2
    register_syscall(862, sys_ok); // ss_virtual_trm_manager
    register_syscall(866, sys_ss_secure_rtc);
    register_syscall(874, sys_ss_get_cache_of_flash_ext_flag);
}
