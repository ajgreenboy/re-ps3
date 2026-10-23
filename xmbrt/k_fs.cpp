// LV2 filesystem (sys_fs.cpp semantics) over an overlay: reads come from RPCS3's installed firmware
// tree (XMBRT_ROOT, default C:\dev\rpcs3\bin), writes go to XMBRT_VFS (default <exe dir>\vfs) so xmbrt
// never modifies the RPCS3 install. /dev_flash and /dev_bdvd are read-only.
#include "kernel.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace
{
    constexpr uint32_t CELL_FS_O_ACCMODE = 3, CELL_FS_O_WRONLY = 1, CELL_FS_O_RDWR = 2;
    constexpr uint32_t CELL_FS_O_CREAT = 0x40, CELL_FS_O_EXCL = 0x80, CELL_FS_O_TRUNC = 0x200, CELL_FS_O_APPEND = 0x400;
    constexpr uint8_t CELL_FS_TYPE_DIRECTORY = 1, CELL_FS_TYPE_REGULAR = 2;

    struct Mount
    {
        const char* guest;
        bool writable;
        const char* filesystem;
        const char* device;
        uint64_t sector_size;
        uint64_t block_size;
    };
    const Mount kMounts[] = {
        { "/dev_flash", false, "CELL_FS_FAT", "CELL_FS_IOS:BUILTIN_FLSH1", 512, 8192 },
        { "/dev_flash2", true, "CELL_FS_FAT", "CELL_FS_IOS:BUILTIN_FLSH2", 512, 8192 },
        { "/dev_flash3", true, "CELL_FS_FAT", "CELL_FS_IOS:BUILTIN_FLSH3", 512, 8192 },
        { "/dev_hdd0", true, "CELL_FS_UFS", "CELL_FS_UTILITY:HDD0", 512, 4096 },
        { "/dev_hdd1", true, "CELL_FS_FAT", "CELL_FS_UTILITY:HDD1", 512, 32768 },
        { "/dev_usb000", true, "CELL_FS_FAT", "CELL_FS_IOS:USB_MASS_STORAGE000", 512, 4096 },
        { "/dev_bdvd", false, "CELL_FS_ISO9660", "CELL_FS_IOS:PATA0_BDVD_DRIVE", 2048, 2048 },
    };

    fs::path g_lower, g_upper;
    std::mutex g_fs_mutex;
    std::set<std::string> g_mounted;

    // An upper-layer tombstone hides the corresponding lower entry, including
    // descendants of a removed directory. Markers live outside the guest tree.
    // Keep the marker when a file is recreated: the new upper file must not
    // resurrect an older lower copy if it is subsequently removed again.
    bool lower_hidden(const fs::path& upper)
    {
        const fs::path relative = upper.lexically_relative(g_upper);
        fs::path marker = g_upper / ".xmbrt-whiteouts";
        std::error_code ec;
        for (const auto& part : relative)
        {
            marker /= part;
            if (fs::is_regular_file(marker / ".deleted", ec)) return true;
        }
        return false;
    }

    bool hide_lower(const fs::path& upper)
    {
        const fs::path marker = g_upper / ".xmbrt-whiteouts" / upper.lexically_relative(g_upper);
        std::error_code ec;
        fs::create_directories(marker, ec);
        if (ec) return false;
        const fs::path file = marker / ".deleted";
        HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        CloseHandle(h);
        return true;
    }

    struct Resolved
    {
        fs::path lower, upper;
        bool writable = false;
        bool ok = false;
        const Mount* mount = nullptr;
        fs::path read_path() const
        {
            std::error_code ec;
            return fs::exists(upper, ec) || lower_hidden(upper) ? upper : lower;
        }
    };

    fs::path utf8_path(const std::string& s) { return fs::u8path(s); }

    std::string to_utf8(const fs::path& p)
    {
        const std::u8string s = p.u8string();
        return std::string(s.begin(), s.end());
    }

    // Normalises a guest path ("//dev_hdd0/./a/../b/" -> "/dev_hdd0/b") and maps it to both layers.
    Resolved resolve(const std::string& guest)
    {
        std::vector<std::string> parts;
        size_t i = 0;
        while (i < guest.size())
        {
            while (i < guest.size() && guest[i] == '/') i++;
            size_t j = guest.find('/', i);
            if (j == std::string::npos) j = guest.size();
            std::string part = guest.substr(i, j - i);
            if (part == "..") { if (!parts.empty()) parts.pop_back(); }
            else if (!part.empty() && part != ".") parts.push_back(part);
            i = j;
        }
        Resolved r;
        if (parts.empty()) return r;
        const std::string mount = "/" + parts[0];
        for (const Mount& m : kMounts)
        {
            if (mount != m.guest) continue;
            fs::path rest;
            for (size_t k = 1; k < parts.size(); k++) rest /= utf8_path(parts[k]);
            r.lower = g_lower / utf8_path(parts[0]) / rest;
            r.upper = g_upper / utf8_path(parts[0]) / rest;
            r.writable = m.writable;
            r.mount = &m;
            r.ok = true;
        }
        return r;
    }

    // Copies a lower-layer file up before it is modified.
    bool copy_up(const Resolved& r)
    {
        std::error_code ec;
        if (fs::exists(r.upper, ec)) return true;
        fs::create_directories(r.upper.parent_path(), ec);
        if (ec) return false;
        if (lower_hidden(r.upper)) return true;
        if (fs::is_directory(r.lower, ec)) return fs::create_directories(r.upper, ec) || fs::exists(r.upper, ec);
        if (fs::exists(r.lower, ec)) return fs::copy_file(r.lower, r.upper, ec);
        return true;
    }

    struct FsFile
    {
        HANDLE h = INVALID_HANDLE_VALUE;
        bool append = false;
        uint32_t flags = 0;
        bool dir = false;
        std::vector<std::pair<std::string, bool>> entries; // name, is_dir
        size_t next = 0;
        std::string guest_path;
    };

    std::map<uint32_t, std::unique_ptr<FsFile>> g_files;

    uint32_t add_file(std::unique_ptr<FsFile> f)
    {
        uint32_t fd = 3;
        while (g_files.count(fd)) fd++;
        g_files[fd] = std::move(f);
        return fd;
    }

    FsFile* get_file(uint32_t fd)
    {
        const auto it = g_files.find(fd);
        return it == g_files.end() ? nullptr : it->second.get();
    }

    int64_t to_unix(const fs::file_time_type& t)
    {
        using namespace std::chrono;
        return duration_cast<seconds>(clock_cast<system_clock>(t).time_since_epoch()).count();
    }

    // CellFsStat: s32 mode, s32 uid, s32 gid, s64 atime, s64 mtime, s64 ctime, u64 size, u64 blksize (packed, 0x34 bytes)
    void write_stat(uint32_t sb, bool dir, uint64_t size, int64_t mtime)
    {
        const uint32_t mode = dir ? 0040000 | 0777 : 0100000 | 0666;
        guest_store_u32(sb + 0, mode);
        guest_store_u32(sb + 4, 0);
        guest_store_u32(sb + 8, 0);
        // the s64 fields are only 4-byte aligned
        for (int k = 0; k < 3; k++)
        {
            guest_store_u32(sb + 12 + k * 8, uint32_t(uint64_t(mtime) >> 32));
            guest_store_u32(sb + 16 + k * 8, uint32_t(mtime));
        }
        guest_store_u32(sb + 36, uint32_t(size >> 32));
        guest_store_u32(sb + 40, uint32_t(size));
        guest_store_u32(sb + 44, 0);
        guest_store_u32(sb + 48, 4096);
    }

    int64_t stat_path(const fs::path& p, uint32_t sb)
    {
        std::error_code ec;
        const auto st = fs::status(p, ec);
        if (ec || !fs::exists(st)) return CELL_ENOENT;
        const bool dir = fs::is_directory(st);
        const uint64_t size = dir ? 0 : fs::file_size(p, ec);
        const auto t = fs::last_write_time(p, ec);
        write_stat(sb, dir, size, ec ? 0 : to_unix(t));
        return CELL_OK;
    }

    // sys_fs_open(path*, flags, fd*, mode, arg*, size)
    int64_t sys_fs_open(PPCContext& ctx)
    {
        const std::string path = guest_string(ctx.r3.u32);
        const uint32_t flags = ctx.r4.u32, fd_ptr = ctx.r5.u32;
        std::lock_guard lock(g_fs_mutex);
        const Resolved r = resolve(path);
        if (!r.ok) return CELL_ENOENT;
        const uint32_t acc = flags & CELL_FS_O_ACCMODE;
        if (acc == CELL_FS_O_ACCMODE) return CELL_EINVAL;
        const bool write = acc == CELL_FS_O_WRONLY || acc == CELL_FS_O_RDWR || (flags & (CELL_FS_O_CREAT | CELL_FS_O_TRUNC | CELL_FS_O_APPEND));
        std::error_code ec;
        const fs::path visible = r.read_path();
        const bool exists = fs::exists(visible, ec);
        if (fs::is_directory(visible, ec)) return CELL_EISDIR;
        // Check exclusivity before copy-up, which itself creates an upper file.
        if ((flags & CELL_FS_O_CREAT) && (flags & CELL_FS_O_EXCL) && exists) return CELL_EEXIST;
        if (!(flags & CELL_FS_O_CREAT) && !exists) return CELL_ENOENT;
        fs::path host;
        if (write)
        {
            if (!r.writable) return CELL_EROFS;
            const fs::path parent_upper = r.upper.parent_path(), parent_lower = r.lower.parent_path();
            if (!fs::is_directory(parent_upper, ec) &&
                (lower_hidden(parent_upper) || !fs::is_directory(parent_lower, ec))) return CELL_ENOENT;
            if (!(flags & CELL_FS_O_TRUNC) && !copy_up(r)) return CELL_EIO;
            fs::create_directories(r.upper.parent_path(), ec);
            if (ec) return CELL_EIO;
            host = r.upper;
        }
        else
        {
            host = r.read_path();
        }
        if (fs::is_directory(host, ec)) return CELL_EISDIR;
        if (!write && !fs::exists(host, ec)) return CELL_ENOENT;
        DWORD access = acc == CELL_FS_O_WRONLY ? GENERIC_WRITE : acc == CELL_FS_O_RDWR ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ;
        if (write) access |= GENERIC_WRITE;
        const DWORD disp = (flags & CELL_FS_O_CREAT) ? ((flags & CELL_FS_O_EXCL) ? CREATE_NEW : (flags & CELL_FS_O_TRUNC) ? CREATE_ALWAYS : OPEN_ALWAYS)
            : (flags & CELL_FS_O_TRUNC) ? CREATE_ALWAYS : OPEN_EXISTING;
        HANDLE h = CreateFileW(host.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, disp, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? CELL_ENOENT : CELL_EIO;
        auto f = std::make_unique<FsFile>();
        f->h = h;
        f->append = (flags & CELL_FS_O_APPEND) != 0;
        f->flags = flags;
        f->guest_path = path;
        guest_store_u32(fd_ptr, add_file(std::move(f)));
        return CELL_OK;
    }

    // sys_fs_read(fd, buf*, nbytes, nread*)
    int64_t sys_fs_read(PPCContext& ctx)
    {
        std::lock_guard lock(g_fs_mutex);
        FsFile* f = get_file(ctx.r3.u32);
        if (!f || f->dir) return CELL_EBADF;
        DWORD got = 0;
        uint64_t total = 0, want = ctx.r5.u64;
        const uint32_t buf = ctx.r4.u32;
        while (total < want)
        {
            const DWORD chunk = DWORD(std::min<uint64_t>(want - total, 0x10000000));
            if (!guest_commit(buf + uint32_t(total), chunk)) return CELL_EFAULT;
            if (!ReadFile(f->h, g_base + buf + total, chunk, &got, nullptr)) return CELL_EIO;
            total += got;
            if (got < chunk) break;
        }
        if (ctx.r6.u32) guest_store_u64(ctx.r6.u32, total);
        return CELL_OK;
    }

    // sys_fs_write(fd, buf*, nbytes, nwrite*)
    int64_t sys_fs_write(PPCContext& ctx)
    {
        std::lock_guard lock(g_fs_mutex);
        FsFile* f = get_file(ctx.r3.u32);
        if (!f || f->dir) return CELL_EBADF;
        if (f->append)
        {
            LARGE_INTEGER zero{};
            SetFilePointerEx(f->h, zero, nullptr, FILE_END);
        }
        DWORD put = 0;
        if (!WriteFile(f->h, g_base + ctx.r4.u32, DWORD(ctx.r5.u64), &put, nullptr)) return CELL_EIO;
        if (ctx.r6.u32) guest_store_u64(ctx.r6.u32, put);
        return CELL_OK;
    }

    int64_t sys_fs_close(PPCContext& ctx)
    {
        std::lock_guard lock(g_fs_mutex);
        FsFile* f = get_file(ctx.r3.u32);
        if (!f || f->dir) return CELL_EBADF;
        CloseHandle(f->h);
        g_files.erase(ctx.r3.u32);
        return CELL_OK;
    }

    // sys_fs_lseek(fd, offset, whence, pos*)
    int64_t sys_fs_lseek(PPCContext& ctx)
    {
        std::lock_guard lock(g_fs_mutex);
        FsFile* f = get_file(ctx.r3.u32);
        if (!f || f->dir) return CELL_EBADF;
        const uint32_t whence = ctx.r5.u32;
        if (whence > 2) return CELL_EINVAL;
        LARGE_INTEGER off, pos;
        off.QuadPart = ctx.r4.s64;
        if (!SetFilePointerEx(f->h, off, &pos, whence == 0 ? FILE_BEGIN : whence == 1 ? FILE_CURRENT : FILE_END)) return CELL_EINVAL;
        guest_store_u64(ctx.r6.u32, uint64_t(pos.QuadPart));
        return CELL_OK;
    }

    int64_t sys_fs_fstat(PPCContext& ctx)
    {
        std::lock_guard lock(g_fs_mutex);
        FsFile* f = get_file(ctx.r3.u32);
        if (!f || f->dir) return CELL_EBADF;
        LARGE_INTEGER size{};
        GetFileSizeEx(f->h, &size);
        FILETIME mt{};
        GetFileTime(f->h, nullptr, nullptr, &mt);
        const int64_t unix_time = int64_t((uint64_t(mt.dwHighDateTime) << 32 | mt.dwLowDateTime) / 10000000ull) - 11644473600ll;
        write_stat(ctx.r4.u32, false, uint64_t(size.QuadPart), unix_time);
        return CELL_OK;
    }

    int64_t sys_fs_stat(PPCContext& ctx)
    {
        const std::string path = guest_string(ctx.r3.u32);
        std::lock_guard lock(g_fs_mutex);
        const Resolved r = resolve(path);
        if (!r.ok)
        {
            // The root and bare mount points exist as directories
            if (path == "/" || path.empty()) return write_stat(ctx.r4.u32, true, 0, 0), CELL_OK;
            return CELL_ENOENT;
        }
        return stat_path(r.read_path(), ctx.r4.u32);
    }

    // sys_fs_opendir(path*, fd*): lists both layers, upper entries winning
    int64_t sys_fs_opendir(PPCContext& ctx)
    {
        const std::string path = guest_string(ctx.r3.u32);
        std::lock_guard lock(g_fs_mutex);
        const Resolved r = resolve(path);
        if (!r.ok) return CELL_ENOENT;
        std::error_code ec;
        const bool in_lower = !lower_hidden(r.upper) && fs::is_directory(r.lower, ec), in_upper = fs::is_directory(r.upper, ec);
        if (!in_lower && !in_upper) return fs::exists(r.read_path(), ec) ? CELL_ENOTDIR : CELL_ENOENT;
        auto f = std::make_unique<FsFile>();
        f->dir = true;
        f->guest_path = path;
        f->entries = { { ".", true }, { "..", true } };
        std::set<std::string> seen;
        for (const fs::path& layer : { r.upper, r.lower })
        {
            if (layer == r.lower && !in_lower) continue;
            if (!fs::is_directory(layer, ec)) continue;
            for (const auto& e : fs::directory_iterator(layer, ec))
            {
                const std::string name = to_utf8(e.path().filename());
                if (layer == r.lower && lower_hidden(r.upper / e.path().filename())) continue;
                if (seen.insert(name).second) f->entries.push_back({ name, e.is_directory(ec) });
            }
        }
        guest_store_u32(ctx.r4.u32, add_file(std::move(f)));
        return CELL_OK;
    }

    // sys_fs_readdir(fd, CellFsDirent*, nread*): nread = sizeof(CellFsDirent) per entry, 0 at the end
    int64_t sys_fs_readdir(PPCContext& ctx)
    {
        std::lock_guard lock(g_fs_mutex);
        FsFile* f = get_file(ctx.r3.u32);
        if (!f || !f->dir) return CELL_EBADF;
        const uint32_t dirent = ctx.r4.u32;
        if (f->next >= f->entries.size())
        {
            guest_store_u64(ctx.r5.u32, 0);
            return CELL_OK;
        }
        const auto& [name, is_dir] = f->entries[f->next++];
        const size_t len = std::min<size_t>(name.size(), 255);
        guest_store_u8(dirent, is_dir ? CELL_FS_TYPE_DIRECTORY : CELL_FS_TYPE_REGULAR);
        guest_store_u8(dirent + 1, uint8_t(len));
        memcpy(g_base + dirent + 2, name.data(), len);
        g_base[dirent + 2 + len] = 0;
        guest_store_u64(ctx.r5.u32, 258);
        return CELL_OK;
    }

    int64_t sys_fs_closedir(PPCContext& ctx)
    {
        std::lock_guard lock(g_fs_mutex);
        FsFile* f = get_file(ctx.r3.u32);
        if (!f || !f->dir) return CELL_EBADF;
        g_files.erase(ctx.r3.u32);
        return CELL_OK;
    }

    int64_t sys_fs_mkdir(PPCContext& ctx)
    {
        const std::string path = guest_string(ctx.r3.u32);
        std::lock_guard lock(g_fs_mutex);
        const Resolved r = resolve(path);
        if (!r.ok) return CELL_ENOENT;
        std::error_code ec;
        if (fs::exists(r.read_path(), ec)) return CELL_EEXIST;
        if (!r.writable) return CELL_EROFS;
        const Resolved parent = resolve(path + "/..");
        if (!parent.ok || !fs::is_directory(parent.read_path(), ec)) return CELL_ENOENT;
        fs::create_directories(r.upper, ec);
        return ec ? CELL_EIO : CELL_OK;
    }

    // Never remove lower-layer data. A persistent marker gives guest unlink/rmdir
    // normal semantics even when the file originally came from the RPCS3 tree.
    int64_t remove_path(const std::string& path, bool dir)
    {
        std::lock_guard lock(g_fs_mutex);
        const Resolved r = resolve(path);
        if (!r.ok) return CELL_ENOENT;
        std::error_code ec;
        const bool in_upper = fs::exists(r.upper, ec), in_lower = !lower_hidden(r.upper) && fs::exists(r.lower, ec);
        if (!in_upper && !in_lower) return CELL_ENOENT;
        if (!r.writable) return CELL_EROFS;
        const fs::path p = in_upper ? r.upper : r.lower;
        if (dir != fs::is_directory(p, ec)) return dir ? CELL_ENOTDIR : CELL_EISDIR;
        if (dir && !fs::is_empty(p, ec)) return CELL_ENOTEMPTY;
        if (in_lower && !hide_lower(r.upper)) return CELL_EIO;
        if (in_upper && !fs::remove(r.upper, ec)) return CELL_EIO;
        return CELL_OK;
    }

    int64_t sys_fs_unlink(PPCContext& ctx) { return remove_path(guest_string(ctx.r3.u32), false); }
    int64_t sys_fs_rmdir(PPCContext& ctx) { return remove_path(guest_string(ctx.r3.u32), true); }

    int64_t sys_fs_rename(PPCContext& ctx)
    {
        const std::string from = guest_string(ctx.r3.u32), to = guest_string(ctx.r4.u32);
        std::lock_guard lock(g_fs_mutex);
        const Resolved a = resolve(from), b = resolve(to);
        if (!a.ok || !b.ok) return CELL_ENOENT;
        if (!a.writable || !b.writable) return CELL_EROFS;
        std::error_code ec;
        if (!fs::exists(a.read_path(), ec)) return CELL_ENOENT;
        if (!copy_up(a)) return CELL_EIO;
        fs::create_directories(b.upper.parent_path(), ec);
        fs::rename(a.upper, b.upper, ec);
        if (!ec && fs::exists(a.lower, ec) && !hide_lower(a.upper)) return CELL_EIO;
        return ec ? CELL_EIO : CELL_OK;
    }

    // sys_fs_truncate(path*, size)
    int64_t sys_fs_truncate(PPCContext& ctx)
    {
        std::lock_guard lock(g_fs_mutex);
        const Resolved r = resolve(guest_string(ctx.r3.u32));
        if (!r.ok) return CELL_ENOENT;
        if (!r.writable) return CELL_EROFS;
        std::error_code ec;
        if (!fs::exists(r.read_path(), ec)) return CELL_ENOENT;
        if (!copy_up(r)) return CELL_EIO;
        fs::resize_file(r.upper, ctx.r4.u64, ec);
        return ec ? CELL_EIO : CELL_OK;
    }

    int64_t sys_fs_ftruncate(PPCContext& ctx)
    {
        std::lock_guard lock(g_fs_mutex);
        FsFile* f = get_file(ctx.r3.u32);
        if (!f || f->dir) return CELL_EBADF;
        FILE_END_OF_FILE_INFO eof{};
        eof.EndOfFile.QuadPart = ctx.r4.s64;
        return SetFileInformationByHandle(f->h, FileEndOfFileInfo, &eof, sizeof(eof)) ? CELL_OK : CELL_EIO;
    }

    int64_t sys_fs_ok(PPCContext&) { return CELL_OK; }

    int64_t sys_fs_fsync(PPCContext& ctx)
    {
        std::lock_guard lock(g_fs_mutex);
        const FsFile* f = get_file(ctx.r3.u32);
        if (!f || f->dir || !(f->flags & CELL_FS_O_ACCMODE)) return CELL_EBADF;
        return FlushFileBuffers(f->h) ? CELL_OK : CELL_EIO;
    }

    // utime with a null times pointer fails like on RPCS3; otherwise accepted without effect
    int64_t sys_fs_utime(PPCContext& ctx) { return ctx.r4.u32 ? CELL_OK : CELL_EFAULT; }

    // sys_fs_fcntl(fd, op, arg*, size)
    int64_t sys_fs_fcntl(PPCContext& ctx)
    {
        const uint32_t op = ctx.r4.u32, arg = ctx.r5.u32;
        if (!arg) return CELL_EFAULT;
        switch (op)
        {
        case 0x80000004: // RPCS3's scalar query: output is zero
            if (ctx.r6.u32 > 4) return CELL_EINVAL;
            guest_store_u32(arg, 0);
            return CELL_OK;
        case 0xC0000002: // cellFsGetFreeSize: lv2_file_c0000002 (0x28 bytes)
        {
            if (ctx.r6.u32 < 0x28) return CELL_EINVAL;
            std::lock_guard lock(g_fs_mutex);
            const Resolved r = resolve(guest_string(guest_load_u32(arg + 0x0C)));
            if (!r.ok) return CELL_ENOTMOUNTED;
            std::error_code ec;
            const auto space = fs::space(g_upper, ec);
            if (ec) return CELL_EIO;
            guest_store_u32(arg + 0x18, uint32_t(CELL_OK));
            guest_store_u32(arg + 0x1C, uint32_t(r.mount->block_size));
            guest_store_u64(arg + 0x20, space.available / r.mount->block_size);
            return CELL_OK;
        }
        case 0xE0000017: // cellFsAllocateFileAreaWithoutZeroFill
        {
            // The file-size field is eight-byte aligned, so it begins at +0x18.
            if (ctx.r6.u32 < 0x28 || guest_load_u32(arg) != 0x28 ||
                guest_load_u32(arg + 4) != 0x10 || guest_load_u32(arg + 8) != 0x20) return CELL_EINVAL;
            PPCContext truncate_ctx{};
            truncate_ctx.r3.u32 = guest_load_u32(arg + 0x10);
            truncate_ctx.r4.u64 = guest_load_u64(arg + 0x18);
            guest_store_u32(arg + 0x20, uint32_t(sys_fs_truncate(truncate_ctx)));
            return CELL_OK;
        }
        case 0xC0000006: // mount-point query; RPCS3 answers ENOTSUP in out_code
            if (guest_load_u32(arg) != 0x20 || guest_load_u32(arg + 4) != 0x10 || guest_load_u32(arg + 8) != 0x18) return CELL_OK;
            guest_store_u32(arg + 0x18, uint32_t(CELL_ENOTSUP));
            guest_store_u32(arg + 0x1C, 0);
            return CELL_OK;
        default:
            log_line("fs: unimplemented fcntl op %08X (fd %u)", op, ctx.r3.u32);
            return CELL_ENOTSUP;
        }
    }

    // sys_fs_mount(dev_name*, file_system*, path*, ...): mount points that exist give EEXIST, as on RPCS3
    int64_t sys_fs_mount(PPCContext& ctx)
    {
        const std::string path = guest_string(ctx.r5.u32);
        std::lock_guard lock(g_fs_mutex);
        if (!g_mounted.insert(path).second) return CELL_EEXIST;
        return CELL_OK;
    }

    int64_t sys_fs_unmount(PPCContext& ctx)
    {
        std::lock_guard lock(g_fs_mutex);
        g_mounted.erase(guest_string(ctx.r3.u32));
        return CELL_OK;
    }

    // sys_fs_disk_free(path*, total*, avail*)
    int64_t sys_fs_disk_free(PPCContext& ctx)
    {
        std::lock_guard lock(g_fs_mutex);
        const Resolved r = resolve(guest_string(ctx.r3.u32));
        if (!r.ok) return CELL_ENOENT;
        std::error_code ec;
        const auto space = fs::space(g_upper, ec);
        guest_store_u64(ctx.r4.u32, ec ? 0 : space.capacity);
        guest_store_u64(ctx.r5.u32, ec ? 0 : space.available);
        return CELL_OK;
    }

    // sys_fs_get_mount_info_size(len*)
    int64_t sys_fs_get_mount_info_size(PPCContext& ctx)
    {
        guest_store_u64(ctx.r3.u32, std::size(kMounts));
        return CELL_OK;
    }

    // sys_fs_get_mount_info(info*, len, out_len*): CellFsMountInfo {char mount_path[0x20], char filesystem[0x20],
    // char dev_name[0x40], be_t<u32> unk[5]} (0x94 bytes)
    int64_t sys_fs_get_mount_info(PPCContext& ctx)
    {
        const uint32_t info = ctx.r3.u32;
        const uint64_t len = ctx.r4.u64;
        uint64_t n = 0;
        for (const Mount& m : kMounts)
        {
            if (n >= len) break;
            const uint32_t e = info + uint32_t(n) * 0x94;
            memset(g_base + e, 0, 0x94);
            strncpy((char*)g_base + e, m.guest, 0x1F);
            strncpy((char*)g_base + e + 0x20, m.filesystem, 0x1F);
            strncpy((char*)g_base + e + 0x40, m.device, 0x3F);
            if (!m.writable) guest_store_u32(e + 0x90, 0x10000000);
            n++;
        }
        guest_store_u64(ctx.r5.u32, n);
        return CELL_OK;
    }

    // sys_fs_get_block_size(path*, sector_size*, block_size*, arg4*)
    int64_t sys_fs_get_block_size(PPCContext& ctx)
    {
        std::lock_guard lock(g_fs_mutex);
        const Resolved r = resolve(guest_string(ctx.r3.u32));
        if (!r.ok) return CELL_ENOTMOUNTED;
        if (ctx.r4.u32) guest_store_u64(ctx.r4.u32, r.mount->sector_size);
        if (ctx.r5.u32) guest_store_u64(ctx.r5.u32, r.mount->block_size);
        if (ctx.r6.u32) guest_store_u64(ctx.r6.u32, r.mount->sector_size);
        return CELL_OK;
    }

    int64_t sys_fs_fget_block_size(PPCContext& ctx)
    {
        std::lock_guard lock(g_fs_mutex);
        const FsFile* f = get_file(ctx.r3.u32);
        if (!f || f->dir) return CELL_EBADF;
        const Resolved r = resolve(f->guest_path);
        if (!r.ok) return CELL_ENOTMOUNTED;
        if (ctx.r4.u32) guest_store_u64(ctx.r4.u32, r.mount->sector_size);
        if (ctx.r5.u32) guest_store_u64(ctx.r5.u32, r.mount->block_size);
        if (ctx.r6.u32) guest_store_u64(ctx.r6.u32, r.mount->sector_size);
        if (ctx.r7.u32) guest_store_u32(ctx.r7.u32, f->flags);
        return CELL_OK;
    }
}

void register_fs_syscalls()
{
    const char* root = getenv("XMBRT_ROOT");
    g_lower = utf8_path(root && *root ? root : "C:\\dev\\rpcs3\\bin");
    const char* vfs = getenv("XMBRT_VFS");
    if (vfs && *vfs)
    {
        g_upper = utf8_path(vfs);
    }
    else
    {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        g_upper = fs::path(exe).parent_path() / "vfs";
    }
    std::error_code ec;
    fs::create_directories(g_upper, ec);
    for (const Mount& m : kMounts) g_mounted.insert(m.guest);
    log_line("fs: read layer %s, write layer %s", to_utf8(g_lower).c_str(), to_utf8(g_upper).c_str());

    // VSH creates /dev_hdd0/tmp/turnoff at boot and deletes it on a clean power-off. The runtime is
    // normally closed or killed rather than powered off by the guest, so the marker survives and every
    // following boot takes the "not turned off properly" path: UI teardown, recovery offer, reboot,
    // database rebuild. Clearing it here makes each launch a normal boot. XMBRT_KEEP_TURNOFF=1 keeps
    // it (to exercise the firmware's recovery path on purpose).
    if (!getenv("XMBRT_KEEP_TURNOFF"))
        if (remove_path("/dev_hdd0/tmp/turnoff", false) == CELL_OK)
            log_line("fs: cleared stale shutdown marker /dev_hdd0/tmp/turnoff (previous run did not power off cleanly)");

    register_syscall(801, sys_fs_open);
    register_syscall(802, sys_fs_read);
    register_syscall(803, sys_fs_write);
    register_syscall(804, sys_fs_close);
    register_syscall(805, sys_fs_opendir);
    register_syscall(806, sys_fs_readdir);
    register_syscall(807, sys_fs_closedir);
    register_syscall(808, sys_fs_stat);
    register_syscall(809, sys_fs_fstat);
    register_syscall(811, sys_fs_mkdir);
    register_syscall(812, sys_fs_rename);
    register_syscall(813, sys_fs_rmdir);
    register_syscall(814, sys_fs_unlink);
    register_syscall(815, sys_fs_utime);
    register_syscall(817, sys_fs_fcntl);
    register_syscall(818, sys_fs_lseek);
    register_syscall(819, sys_fs_fsync); // fdatasync
    register_syscall(820, sys_fs_fsync);
    register_syscall(821, sys_fs_fget_block_size);
    register_syscall(822, sys_fs_get_block_size);
    register_syscall(831, sys_fs_truncate);
    register_syscall(832, sys_fs_ftruncate);
    register_syscall(834, sys_fs_ok); // chmod
    register_syscall(837, sys_fs_mount);
    register_syscall(838, sys_fs_unmount);
    register_syscall(839, sys_fs_ok); // sync
    register_syscall(840, sys_fs_disk_free);
    register_syscall(841, sys_fs_get_mount_info_size);
    register_syscall(842, sys_fs_get_mount_info);
}
