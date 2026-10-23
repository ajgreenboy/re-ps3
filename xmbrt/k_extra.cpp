// Syscalls vsh uses that have no real subsystem behind them yet, with RPCS3's observable behaviour:
// SPU thread groups (no SPU execution yet: groups "run" and exit at once), hid manager (input arrives in
// phase 5), system manager / secure services extras, storage reads, IO buffers, USB and Bluetooth stubs.
#include "kernel.h"

#include <cstring>

namespace
{
    int64_t sys_ok(PPCContext&) { return CELL_OK; }

    // ---- SPU ----
    struct KSpuGroup : KObject
    {
        uint32_t num = 0;
    };
    struct KSpuThread : KObject
    {
        uint32_t group = 0;
    };

    // _sys_spu_image_import(img*, src, size, arg4): sys_spu_image {u32 type, u32 entry_point, u32 segs, s32 nsegs}
    int64_t sys_spu_image_import(PPCContext& ctx)
    {
        const uint32_t img = ctx.r3.u32, src = ctx.r4.u32;
        guest_store_u32(img, 0);            // SYS_SPU_IMAGE_TYPE_USER
        guest_store_u32(img + 4, guest_load_u32(src + 0x18)); // ELF e_entry (32-bit SPU ELF)
        guest_store_u32(img + 8, src);
        guest_store_u32(img + 12, 0);
        return CELL_OK;
    }

    // sys_spu_thread_group_create(id*, num, prio, attr*)
    int64_t sys_spu_thread_group_create(PPCContext& ctx)
    {
        auto g = std::make_shared<KSpuGroup>();
        g->num = ctx.r4.u32;
        guest_store_u32(ctx.r3.u32, kobj_add(0x04, g));
        return CELL_OK;
    }

    // sys_spu_thread_initialize(thread*, group_id, spu_num, img*, attr*, arg*)
    int64_t sys_spu_thread_initialize(PPCContext& ctx)
    {
        if (!kobj<KSpuGroup>(ctx.r4.u32)) return CELL_ESRCH;
        auto t = std::make_shared<KSpuThread>();
        t->group = ctx.r4.u32;
        guest_store_u32(ctx.r3.u32, kobj_add(0x02, t));
        return CELL_OK;
    }

    int64_t sys_spu_thread_group_start(PPCContext& ctx)
    {
        static bool logged;
        if (!logged)
        {
            logged = true;
            log_line("spu: thread group %08X started, but SPU code is not executed yet", ctx.r3.u32);
        }
        return kobj<KSpuGroup>(ctx.r3.u32) ? CELL_OK : CELL_ESRCH;
    }

    // sys_spu_thread_group_join(id, cause*, status*): all threads exited
    int64_t sys_spu_thread_group_join(PPCContext& ctx)
    {
        if (!kobj<KSpuGroup>(ctx.r3.u32)) return CELL_ESRCH;
        if (ctx.r4.u32) guest_store_u32(ctx.r4.u32, 1); // SYS_SPU_THREAD_GROUP_JOIN_ALL_THREADS_EXIT
        if (ctx.r5.u32) guest_store_u32(ctx.r5.u32, 0);
        return CELL_OK;
    }

    // sys_spu_thread_group_connect_event_all_threads(id, eq, req, spup*)
    int64_t sys_spu_thread_group_connect_event_all_threads(PPCContext& ctx)
    {
        if (ctx.r6.u32) guest_store_u8(ctx.r6.u32, 0);
        return CELL_OK;
    }

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

    // sys_ss_random_number_generator(pkg_id, buf*, size)
    int64_t sys_ss_random_number_generator(PPCContext& ctx)
    {
        for (uint64_t i = 0; i < ctx.r5.u64; i++) guest_store_u8(ctx.r4.u32 + uint32_t(i), uint8_t(rand()));
        return CELL_OK;
    }

    // ---- storage / io buffers / usb / bluetooth ----
    // sys_storage_read(fd, mode, start_sector, num_sectors, bounce_buf*, sectors_read*, flags): zeros
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

    // sys_usbd_initialize(handle*)
    int64_t sys_usbd_initialize(PPCContext& ctx)
    {
        guest_store_u32(ctx.r3.u32, 0x115B);
        return CELL_OK;
    }

    // sys_usbd_get_device_list(handle, device_list*, max): no USB devices
    int64_t sys_usbd_get_device_list(PPCContext&) { return 0; }

    // sys_usbd_receive_event(handle, arg1*, arg2*, arg3*): blocks until a USB event; there are none
    int64_t sys_usbd_receive_event(PPCContext&)
    {
        KLock lock(g_kernel);
        for (;;) sleep_thread(lock, 0);
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
    register_syscall(157, sys_spu_image_import);
    register_syscall(170, sys_spu_thread_group_create);
    register_syscall(171, sys_ok); // group_destroy
    register_syscall(172, sys_spu_thread_initialize);
    register_syscall(173, sys_spu_thread_group_start);
    register_syscall(178, sys_spu_thread_group_join);
    register_syscall(191, sys_ok); // spu_thread_connect_event
    register_syscall(193, sys_ok); // spu_thread_bind_queue
    register_syscall(251, sys_spu_thread_group_connect_event_all_threads);
    register_syscall(378, sys_sm_get_ext_event2);
    register_syscall(500, sys_hid_manager_open);
    register_syscall(502, sys_hid_manager_read);
    register_syscall(503, sys_hid_manager_ioctl);
    register_syscall(510, sys_hid_manager_check_focus);
    register_syscall(530, sys_usbd_initialize);
    register_syscall(532, sys_usbd_get_device_list);
    register_syscall(535, sys_ok); // usbd_register_ldd
    register_syscall(540, sys_usbd_receive_event);
    register_syscall(579, sys_bluetooth_579);
    register_syscall(584, sys_ok);
    register_syscall(591, sys_ok);
    register_syscall(602, sys_storage_read);
    register_syscall(603, sys_ok); // storage_write
    register_syscall(605, sys_ok); // storage_async_configure
    register_syscall(619, sys_ok); // storage_async_send_device_command
    register_syscall(624, sys_io_buffer_create);
    register_syscall(625, sys_io_buffer_destroy);
    register_syscall(626, sys_io_buffer_allocate);
    register_syscall(627, sys_ok); // io_buffer_free
    register_syscall(638, sys_ok);
    register_syscall(847, sys_ok); // fs_truncate2
    register_syscall(862, sys_ok); // ss_virtual_trm_manager
    register_syscall(865, sys_ss_random_number_generator);
    register_syscall(866, sys_ss_secure_rtc);
    register_syscall(874, sys_ss_get_cache_of_flash_ext_flag);
}
