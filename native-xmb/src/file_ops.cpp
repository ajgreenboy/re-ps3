#include "file_ops.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QThread>

#include <string>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#ifndef FOFX_RECYCLEONDELETE
#define FOFX_RECYCLEONDELETE 0x00080000
#endif
#endif

namespace xmb {

namespace {

constexpr qint64 kChunk = 4 * 1024 * 1024;
constexpr qint64 kSpaceMargin = 1024 * 1024;
constexpr qint64 kFatLimit = Q_INT64_C(4294967295);   // FAT32 max file size = 4 GiB - 1

// Plain separator swap. Not QDir::fromNativeSeparators: on Windows it strips a \\?\ prefix, which
// would hide a namespace path from syntaxProblem().
QString slashes(const QString& path) {
    QString s = path.trimmed();
    s.replace(u'\\', u'/');
    return s;
}

QString clean(const QString& path) {
    QString p = QDir::cleanPath(slashes(path));
    if (p.size() == 2 && p[1] == u':') p += u'/';   // "C:" -> "C:/"
    return p;
}

// Path syntax that must be refused before any file system call.
QString syntaxProblem(const QString& raw) {
    const QString s = slashes(raw);
    if (s.isEmpty()) return QStringLiteral("empty path");
    if (s.startsWith(u"//?/") || s.startsWith(u"//./")) return QStringLiteral("device or namespace path");
    for (QChar c : s)
        if (c == u'*' || c == u'?' || c == u'"' || c == u'<' || c == u'>' || c == u'|' || c.unicode() < 32)
            return QStringLiteral("wildcard or invalid character");
    const bool drive = s.size() >= 3 && s[0].isLetter() && s[1] == u':' && s[2] == u'/';
    const bool unc = s.startsWith(u"//") && s.size() > 2 && s[2] != u'/';
    if (!drive && !unc) return QStringLiteral("not an absolute path");
    if (s.indexOf(u':', drive ? 2 : 0) >= 0) return QStringLiteral("alternate data stream");
    const QStringList segs = s.mid(drive ? 3 : 2).split(u'/', Qt::SkipEmptyParts);
    for (const QString& seg : segs) {
        if (seg == u"..") return QStringLiteral("'..' segment");
        // Windows drops trailing dots and spaces ("Windows." is "Windows"); refuse instead.
        if (seg != u"." && (seg.endsWith(u'.') || seg.endsWith(u' '))) return QStringLiteral("trailing dot or space");
    }
    if (clean(s).split(u'/').contains(QStringLiteral(".."))) return QStringLiteral("'..' segment");
    return {};
}

bool sameOrInside(const QString& path, const QString& root) {
    if (path.isEmpty() || root.isEmpty()) return false;
    QString r = root;
    while (r.endsWith(u'/')) r.chop(1);
    if (path.compare(r, Qt::CaseInsensitive) == 0 || path.compare(r + u'/', Qt::CaseInsensitive) == 0) return true;
    return path.startsWith(r + u'/', Qt::CaseInsensitive);
}

bool same(const QString& a, const QString& b) { return sameOrInside(a, b) && sameOrInside(b, a); }

bool isRootPath(const QString& p) {
    if (p.size() <= 3 && p.size() >= 2 && p[1] == u':') return true;   // C:/
    if (p.startsWith(u"//")) return p.mid(2).split(u'/', Qt::SkipEmptyParts).size() <= 2;   // //server/share
    return QDir(p).isRoot();
}

// Symlink-resolved path with 8.3 short names expanded ("C:/PROGRA~1" -> "C:/Program Files").
QString canonical(const QString& p) {
    QString c = QFileInfo(p).canonicalFilePath();
    if (c.isEmpty()) return {};
#ifdef Q_OS_WIN
    const std::wstring in = QDir::toNativeSeparators(c).toStdWString();
    const DWORD need = GetLongPathNameW(in.c_str(), nullptr, 0);
    if (need > 0) {
        std::wstring buf(need, L'\0');
        const DWORD got = GetLongPathNameW(in.c_str(), buf.data(), need);
        if (got > 0 && got < need) c = QString::fromWCharArray(buf.c_str(), int(got));
    }
#endif
    return clean(c);
}

struct Protected { QString path; bool inside; };   // inside: the folder's content is protected too

QVector<Protected> protectedFolders() {
    QVector<Protected> out;
    auto add = [&](const QString& p, bool inside) {
        if (p.trimmed().isEmpty()) return;
        const QString c = clean(p);
        out.push_back({c, inside});
        const QString k = canonical(c);
        if (!k.isEmpty() && !same(k, c)) out.push_back({k, inside});
    };
    add(qEnvironmentVariable("SystemRoot"), true);
    add(qEnvironmentVariable("windir"), true);
    add(qEnvironmentVariable("ProgramFiles"), true);
    add(qEnvironmentVariable("ProgramFiles(x86)"), true);
    add(qEnvironmentVariable("ProgramW6432"), true);
    add(qEnvironmentVariable("ProgramData"), true);
    add(qEnvironmentVariable("USERPROFILE"), false);
    add(QDir::homePath(), false);
    return out;
}

// For a path that would be removed: refuse system content and anything that is or holds a
// protected folder.
QString protectedProblem(const QString& p, bool removing) {
    for (const Protected& f : protectedFolders()) {
        if (f.inside && sameOrInside(p, f.path)) return QStringLiteral("system folder %1").arg(QDir::toNativeSeparators(f.path));
        if (removing && sameOrInside(f.path, p)) return QStringLiteral("contains the protected folder %1").arg(QDir::toNativeSeparators(f.path));
    }
    return {};
}

QString refused(const QString& path, const QString& why) {
    return QStringLiteral("Refused: %1 (%2)").arg(QDir::toNativeSeparators(path), why);
}

QString human(qint64 bytes) {
    if (bytes >= Q_INT64_C(1) << 30) return QStringLiteral("%1 GB").arg(double(bytes) / double(Q_INT64_C(1) << 30), 0, 'f', 2);
    if (bytes >= Q_INT64_C(1) << 20) return QStringLiteral("%1 MB").arg(double(bytes) / double(1 << 20), 0, 'f', 1);
    return QStringLiteral("%1 KB").arg((bytes + 1023) / 1024);
}

// "name.ext" -> "name (n).ext"; dot files and folders keep the whole name as the base.
QString numbered(const QString& name, int n, bool folder) {
    const QFileInfo fi(name);
    const QString base = folder ? name : fi.completeBaseName();
    const QString suffix = folder ? QString() : fi.suffix();
    if (base.isEmpty() || suffix.isEmpty()) return QStringLiteral("%1 (%2)").arg(name).arg(n);
    return QStringLiteral("%1 (%2).%3").arg(base).arg(n).arg(suffix);
}

#ifdef Q_OS_WIN
// Copies creation / last access / last write times (files and folders).
bool copyTimes(const QString& from, const QString& to) {
    const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE src = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(from).utf16()), FILE_READ_ATTRIBUTES,
                             share, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (src == INVALID_HANDLE_VALUE) return false;
    FILETIME c{}, a{}, w{};
    const bool got = GetFileTime(src, &c, &a, &w);
    CloseHandle(src);
    if (!got) return false;
    HANDLE dst = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(to).utf16()), FILE_WRITE_ATTRIBUTES,
                             share, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (dst == INVALID_HANDLE_VALUE) return false;
    const bool ok = SetFileTime(dst, &c, &a, &w);
    CloseHandle(dst);
    return ok;
}
#else
bool copyTimes(const QString& from, const QString& to) {
    QFile f(to);
    if (!f.open(QIODevice::Append)) return false;
    const QFileInfo fi(from);
    return f.setFileTime(fi.lastModified(), QFileDevice::FileModificationTime);
}
#endif

bool isLink(const QFileInfo& fi) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    if (fi.isJunction()) return true;
#endif
    return fi.isSymLink();
}

} // namespace

FileOperations::FileOperations(QObject* parent) : QObject(parent) {}

FileOperations::~FileOperations() {
    cancelAll();
    for (QThread* t : std::as_const(threads_)) t->wait();
    qDeleteAll(threads_);
    threads_.clear();
}

void FileOperations::setAllowedRoots(QStringList roots) {
    roots_.clear();
    for (const QString& r : roots) {
        if (!syntaxProblem(r).isEmpty()) {
            qInfo().noquote() << "files: ignoring library root" << r << "-" << syntaxProblem(r);
            continue;
        }
        const QString c = clean(r);
        if (!protectedProblem(c, false).isEmpty()) {
            qInfo().noquote() << "files: ignoring library root" << r << "-" << protectedProblem(c, false);
            continue;
        }
        if (!roots_.contains(c, Qt::CaseInsensitive)) roots_ << c;
        const QString k = canonical(c);
        if (!k.isEmpty() && !roots_.contains(k, Qt::CaseInsensitive)) roots_ << k;
    }
    qInfo().noquote() << "files: library roots" << (roots_.isEmpty() ? QStringLiteral("(none)") : roots_.join(u"; "));
}

QString FileOperations::checkDeletable(const QString& path) const {
    if (const QString s = syntaxProblem(path); !s.isEmpty()) return refused(path, s);
    const QString p = clean(path);
    const QFileInfo fi(p);
    if (!fi.exists() && !fi.isSymLink()) return refused(p, QStringLiteral("does not exist"));
    if (isRootPath(p)) return refused(p, QStringLiteral("drive or share root"));
    const QString k = canonical(p);
    for (const QString& form : {p, k}) {
        if (form.isEmpty()) continue;
        if (isRootPath(form)) return refused(p, QStringLiteral("drive or share root"));
        if (const QString s = protectedProblem(form, true); !s.isEmpty()) return refused(p, s);
    }
    if (roots_.isEmpty()) return refused(p, QStringLiteral("no library roots configured"));
    auto inRoots = [&](const QString& form) {
        for (const QString& r : roots_)
            if (sameOrInside(form, r) && !same(form, r)) return true;
        return false;
    };
    for (const QString& r : roots_)
        if (same(p, r) || (!k.isEmpty() && same(k, r))) return refused(p, QStringLiteral("is a library root"));
    if (!inRoots(p)) return refused(p, QStringLiteral("outside the library folders"));
    if (!k.isEmpty() && !inRoots(k)) return refused(p, QStringLiteral("resolves outside the library folders"));
    return {};
}

QString FileOperations::checkCopy(const QStringList& paths, const QString& destinationFolder) const {
    if (paths.isEmpty()) return QStringLiteral("Refused: nothing to copy");
    if (const QString s = syntaxProblem(destinationFolder); !s.isEmpty()) return refused(destinationFolder, s);
    const QString dest = clean(destinationFolder);
    const QFileInfo di(dest);
    if (!di.exists() || !di.isDir()) return refused(dest, QStringLiteral("destination is not an existing folder"));
    const QString destK = canonical(dest);
    for (const QString& form : {dest, destK})
        if (!form.isEmpty())
            if (const QString s = protectedProblem(form, false); !s.isEmpty()) return refused(dest, QStringLiteral("destination is a ") + s);
    for (const QString& raw : paths) {
        if (const QString s = syntaxProblem(raw); !s.isEmpty()) return refused(raw, s);
        const QString p = clean(raw);
        const QFileInfo fi(p);
        if (!fi.exists()) return refused(p, QStringLiteral("does not exist"));
        if (isRootPath(p)) return refused(p, QStringLiteral("drive or share root"));
        if (isLink(fi)) return refused(p, QStringLiteral("symbolic link or junction"));
        if (fi.isDir()) {
            const QString k = canonical(p);
            for (const QString& d : {dest, destK})
                for (const QString& s : {p, k})
                    if (!d.isEmpty() && !s.isEmpty() && sameOrInside(d, s))
                        return refused(dest, QStringLiteral("destination is inside the source folder %1").arg(QDir::toNativeSeparators(p)));
        }
    }
    return {};
}

quint64 FileOperations::refuse(const QString& kind, const QString& reason) {
    const quint64 request = ++nextRequest_;
    qInfo().noquote() << QStringLiteral("files: %1 #%2").arg(kind).arg(request) << reason;
    finishLater(request, false, reason, {});
    return request;
}

quint64 FileOperations::start(const QString& kind, std::function<void(quint64, CancelFlag)> work) {
    const quint64 request = ++nextRequest_;
    CancelFlag cancel = std::make_shared<std::atomic_bool>(false);
    QThread* thread = QThread::create([work = std::move(work), request, cancel] { work(request, cancel); });
    thread->setObjectName(QStringLiteral("files-%1-%2").arg(kind).arg(request));
    jobs_.insert(request, cancel);
    threads_ << thread;
    connect(thread, &QThread::finished, this, [this, thread] {
        threads_.removeOne(thread);
        thread->deleteLater();
    });
    thread->start(QThread::LowPriority);
    return request;
}

void FileOperations::finishLater(quint64 request, bool ok, const QString& message, const QStringList& paths) {
    QMetaObject::invokeMethod(this, [this, request, ok, message, paths] {
        jobs_.remove(request);
        emit finished(request, ok, message, paths);
    }, Qt::QueuedConnection);
}

void FileOperations::progressLater(quint64 request, qint64 done, qint64 total, const QString& file) {
    QMetaObject::invokeMethod(this, [this, request, done, total, file] {
        if (jobs_.contains(request)) emit progress(request, done, total, file);
    }, Qt::QueuedConnection);
}

void FileOperations::cancel(quint64 request) {
    if (const CancelFlag flag = jobs_.value(request)) {
        flag->store(true);
        qInfo().noquote() << "files: cancel requested for #" + QString::number(request);
    }
}

void FileOperations::cancelAll() {
    for (const CancelFlag& flag : std::as_const(jobs_)) flag->store(true);
}

// ---------------------------------------------------------------- delete

quint64 FileOperations::deleteToRecycleBin(QStringList paths) {
    if (paths.isEmpty()) return refuse(QStringLiteral("delete"), QStringLiteral("Refused: nothing to delete"));
    QStringList cleaned;
    for (const QString& p : paths) {
        if (const QString why = checkDeletable(p); !why.isEmpty()) return refuse(QStringLiteral("delete"), why);
        const QString c = clean(p);
        if (!cleaned.contains(c, Qt::CaseInsensitive)) cleaned << c;
    }
    // An item inside another listed folder goes with that folder.
    QStringList top;
    for (const QString& c : cleaned) {
        bool nested = false;
        for (const QString& o : cleaned)
            if (!same(c, o) && sameOrInside(c, o)) nested = true;
        if (!nested) top << c;
    }
#ifndef Q_OS_WIN
    return refuse(QStringLiteral("delete"), QStringLiteral("Refused: the Recycle Bin is only supported on Windows"));
#else
    const quint64 request = start(QStringLiteral("delete"), [this, top](quint64 req, CancelFlag cancel) {
        runDelete(req, top, cancel);
    });
    qInfo().noquote() << QStringLiteral("files: delete #%1 %2 item(s) to the Recycle Bin, first %3")
                             .arg(request).arg(top.size()).arg(QDir::toNativeSeparators(top.first()));
    return request;
#endif
}

#ifdef Q_OS_WIN
namespace {

// Aborts any item the shell would delete permanently instead of recycling, records what reached the
// bin, forwards progress and honours cancel.
class RecycleSink final : public IFileOperationProgressSink {
public:
    RecycleSink(std::function<bool()> cancelled, std::function<void(UINT, UINT)> onProgress)
        : cancelled_(std::move(cancelled)), onProgress_(std::move(onProgress)) {}

    QStringList recycled, problems;

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == __uuidof(IFileOperationProgressSink)) {
            *ppv = static_cast<IFileOperationProgressSink*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override { return --refs_; }   // stack object, owner outlives the operation

    IFACEMETHODIMP PreDeleteItem(DWORD flags, IShellItem* item) override {
        if (cancelled_()) return E_ABORT;
        if (!(flags & TSF_DELETE_RECYCLE_IF_POSSIBLE)) {
            problems << QStringLiteral("%1 cannot go to the Recycle Bin (too large, or the drive has none); left in place")
                            .arg(name(item));
            return E_ABORT;
        }
        return S_OK;
    }
    IFACEMETHODIMP PostDeleteItem(DWORD, IShellItem* item, HRESULT hr, IShellItem* newlyCreated) override {
        const QString n = name(item);
        if (FAILED(hr)) {
            if (hr != E_ABORT && hr != HRESULT_FROM_WIN32(ERROR_CANCELLED) && hr != COPYENGINE_E_USER_CANCELLED)
                problems << QStringLiteral("%1: error 0x%2").arg(n).arg(quint32(hr), 8, 16, QLatin1Char('0'));
        } else if (newlyCreated) {
            recycled << n;
        } else {
            // Should be impossible given PreDeleteItem; report loudly if the shell ever does it.
            problems << QStringLiteral("%1 was removed without a Recycle Bin entry").arg(n);
        }
        return S_OK;
    }
    IFACEMETHODIMP UpdateProgress(UINT total, UINT soFar) override {
        onProgress_(total, soFar);
        return cancelled_() ? E_ABORT : S_OK;
    }

    IFACEMETHODIMP StartOperations() override { return S_OK; }
    IFACEMETHODIMP FinishOperations(HRESULT) override { return S_OK; }
    IFACEMETHODIMP PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return E_ABORT; }
    IFACEMETHODIMP PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return E_ABORT; }
    IFACEMETHODIMP PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return E_ABORT; }
    IFACEMETHODIMP PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return E_ABORT; }
    IFACEMETHODIMP PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP ResetTimer() override { return S_OK; }
    IFACEMETHODIMP PauseTimer() override { return S_OK; }
    IFACEMETHODIMP ResumeTimer() override { return S_OK; }

    static QString name(IShellItem* item) {
        if (!item) return QStringLiteral("(item)");
        PWSTR s = nullptr;
        QString out;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &s)) && s) {
            out = QString::fromWCharArray(s);
            CoTaskMemFree(s);
        } else if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &s)) && s) {
            out = QString::fromWCharArray(s);
            CoTaskMemFree(s);
        }
        return out;
    }

private:
    std::function<bool()> cancelled_;
    std::function<void(UINT, UINT)> onProgress_;
    ULONG refs_ = 1;
};

QString volumeRoot(const QString& path) {
    wchar_t buf[MAX_PATH + 1] = {};
    if (GetVolumePathNameW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()), buf, MAX_PATH))
        return QString::fromWCharArray(buf);
    return {};
}

} // namespace
#endif

void FileOperations::runDelete(quint64 request, QStringList paths, CancelFlag cancel) {
#ifdef Q_OS_WIN
    auto fail = [&](const QString& msg) {
        qInfo().noquote() << QStringLiteral("files: delete #%1 failed:").arg(request) << msg;
        finishLater(request, false, msg, {});
    };
    // A volume without a Recycle Bin (network share, some removable media) is refused up front.
    for (const QString& p : paths) {
        const QString root = volumeRoot(p);
        SHQUERYRBINFO info{};
        info.cbSize = sizeof(info);
        if (root.isEmpty() || FAILED(SHQueryRecycleBinW(reinterpret_cast<LPCWSTR>(root.utf16()), &info))) {
            fail(refused(p, QStringLiteral("this drive has no Recycle Bin")));
            return;
        }
    }
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(init)) { fail(QStringLiteral("Delete failed: COM init 0x%1").arg(quint32(init), 8, 16, QLatin1Char('0'))); return; }
    {
        IFileOperation* op = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op));
        if (FAILED(hr)) {
            fail(QStringLiteral("Delete failed: IFileOperation unavailable (0x%1)").arg(quint32(hr), 8, 16, QLatin1Char('0')));
            CoUninitialize();
            return;
        }
        hr = op->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT | FOF_NOCONFIRMMKDIR
                                   | FOFX_RECYCLEONDELETE | FOFX_NOMINIMIZEBOX);
        RecycleSink sink([cancel] { return cancel->load(); },
                         [this, request](UINT total, UINT soFar) { progressLater(request, soFar, total, {}); });
        DWORD cookie = 0;
        if (SUCCEEDED(hr)) hr = op->Advise(&sink, &cookie);
        for (const QString& p : paths) {
            if (FAILED(hr)) break;
            IShellItem* item = nullptr;
            hr = SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(p).utf16()), nullptr,
                                             IID_PPV_ARGS(&item));
            if (SUCCEEDED(hr)) {
                hr = op->DeleteItem(item, nullptr);
                item->Release();
            }
        }
        if (SUCCEEDED(hr) && !cancel->load()) hr = op->PerformOperations();
        BOOL aborted = FALSE;
        op->GetAnyOperationsAborted(&aborted);
        if (cookie) op->Unadvise(cookie);
        op->Release();

        // Report the original paths that reached the bin (the sink saw shell display paths).
        QStringList done;
        for (const QString& p : paths)
            for (const QString& r : sink.recycled)
                if (same(clean(r), p)) { done << QDir::toNativeSeparators(p); break; }
        if (done.size() == paths.size() && sink.problems.isEmpty()) {
            const QString msg = QStringLiteral("Delete completed: %1 item(s) moved to the Recycle Bin.").arg(done.size());
            qInfo().noquote() << QStringLiteral("files: delete #%1 ->").arg(request) << msg;
            finishLater(request, true, msg, done);
        } else {
            QString msg = cancel->load() ? QStringLiteral("Cancelled: ") : QStringLiteral("Delete incomplete: ");
            msg += QStringLiteral("%1 of %2 item(s) moved to the Recycle Bin.").arg(done.size()).arg(paths.size());
            if (!sink.problems.isEmpty()) msg += u' ' + sink.problems.join(u"; ");
            else if (FAILED(hr) && hr != E_ABORT && hr != COPYENGINE_E_USER_CANCELLED)
                msg += QStringLiteral(" Error 0x%1.").arg(quint32(hr), 8, 16, QLatin1Char('0'));
            qInfo().noquote() << QStringLiteral("files: delete #%1 ->").arg(request) << msg << (aborted ? "(aborted)" : "");
            finishLater(request, false, msg, done);
        }
    }
    CoUninitialize();
#else
    Q_UNUSED(paths); Q_UNUSED(cancel);
    finishLater(request, false, QStringLiteral("Refused: the Recycle Bin is only supported on Windows"), {});
#endif
}

// ---------------------------------------------------------------- copy

quint64 FileOperations::copy(QStringList paths, QString destinationFolder) {
    if (const QString why = checkCopy(paths, destinationFolder); !why.isEmpty()) return refuse(QStringLiteral("copy"), why);
    QStringList cleaned;
    for (const QString& p : paths)
        if (!cleaned.contains(clean(p), Qt::CaseInsensitive)) cleaned << clean(p);
    const QString dest = clean(destinationFolder);
    const qint64 freeOverride = freeSpaceOverride_;
    const int chunkDelay = chunkDelayMs_;
    const quint64 request = start(QStringLiteral("copy"), [this, cleaned, dest, freeOverride, chunkDelay](quint64 req, CancelFlag cancel) {
        runCopy(req, cleaned, dest, freeOverride, chunkDelay, cancel);
    });
    qInfo().noquote() << QStringLiteral("files: copy #%1 %2 item(s) -> %3").arg(request).arg(cleaned.size())
                             .arg(QDir::toNativeSeparators(dest));
    return request;
}

void FileOperations::runCopy(quint64 request, QStringList paths, QString destination, qint64 freeOverride, int chunkDelay, CancelFlag cancel) {
    struct Entry { QString src; QString rel; bool dir; qint64 size; };   // rel: path under the top-level copy
    struct Top { QString src; bool dir; QVector<Entry> entries; };
    QVector<Top> tops;
    qint64 total = 0, biggest = 0;
    int skippedLinks = 0;

    std::function<void(const QString&, const QString&, QVector<Entry>&)> scan =
        [&](const QString& dir, const QString& rel, QVector<Entry>& into) {
            const QFileInfoList list = QDir(dir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                                               QDir::Name | QDir::DirsFirst);
            for (const QFileInfo& fi : list) {
                if (cancel->load()) return;
                if (isLink(fi)) { ++skippedLinks; continue; }
                const QString r = rel.isEmpty() ? fi.fileName() : rel + u'/' + fi.fileName();
                if (fi.isDir()) {
                    into.push_back({fi.absoluteFilePath(), r, true, 0});
                    scan(fi.absoluteFilePath(), r, into);
                } else {
                    into.push_back({fi.absoluteFilePath(), r, false, fi.size()});
                    total += fi.size();
                    biggest = qMax(biggest, fi.size());
                }
            }
        };
    for (const QString& p : paths) {
        const QFileInfo fi(p);
        Top t{p, fi.isDir(), {}};
        if (t.dir) scan(p, {}, t.entries);
        else { total += fi.size(); biggest = qMax(biggest, fi.size()); }
        tops.push_back(std::move(t));
    }
    auto done = [&](bool ok, const QString& msg, const QStringList& created) {
        qInfo().noquote() << QStringLiteral("files: copy #%1 ->").arg(request) << msg;
        finishLater(request, ok, msg, created);
    };
    if (cancel->load()) { done(false, QStringLiteral("Cancelled: nothing was copied."), {}); return; }

    const QStorageInfo storage(destination);
    const qint64 available = freeOverride >= 0 ? freeOverride : storage.bytesAvailable();
    if (available < total + kSpaceMargin) {
        done(false, QStringLiteral("Not enough free space: the copy needs %1 and %2 has %3 free.")
                        .arg(human(total), QDir::toNativeSeparators(storage.rootPath()), human(qMax<qint64>(available, 0))), {});
        return;
    }
    const QString fs = QString::fromLatin1(storage.fileSystemType()).toUpper();
    if ((fs == u"FAT32" || fs == u"FAT" || fs == u"FAT16" || fs == u"VFAT" || fs == u"MSDOS") && biggest > kFatLimit) {
        done(false, QStringLiteral("Refused: a file is larger than 4 GB and %1 is formatted %2.")
                        .arg(QDir::toNativeSeparators(storage.rootPath()), fs), {});
        return;
    }

    QByteArray buffer(int(kChunk), Qt::Uninitialized);
    qint64 copied = 0;
    QElapsedTimer tick;
    tick.start();
    QStringList created;
    int files = 0;
    progressLater(request, 0, total, {});

    enum class R { Ok, Cancelled, Error };
    QString error;
    // Streams one file to a new, never pre-existing path in dir; outPath receives the name used.
    auto copyFile = [&](const QString& src, const QString& dir, const QString& name, QString* outPath) -> R {
        QFile in(src);
        if (!in.open(QIODevice::ReadOnly)) { error = QStringLiteral("cannot read %1: %2").arg(QDir::toNativeSeparators(src), in.errorString()); return R::Error; }
        QFile out;
        QString target;
        for (int n = 1; n < 10000; ++n) {
            target = QDir(dir).filePath(n == 1 ? name : numbered(name, n, false));
            if (QFileInfo::exists(target)) continue;
            out.setFileName(target);
            if (out.open(QIODevice::WriteOnly | QIODevice::NewOnly)) break;
            if (!QFileInfo::exists(target)) { error = QStringLiteral("cannot create %1: %2").arg(QDir::toNativeSeparators(target), out.errorString()); return R::Error; }
        }
        if (!out.isOpen()) { error = QStringLiteral("no free name for %1").arg(name); return R::Error; }
        *outPath = target;
        const QString shown = QFileInfo(src).fileName();
        auto drop = [&] { out.close(); QFile::remove(target); };
        for (;;) {
            if (cancel->load()) { drop(); return R::Cancelled; }
            const qint64 n = in.read(buffer.data(), kChunk);
            if (n < 0) { error = QStringLiteral("read error in %1: %2").arg(QDir::toNativeSeparators(src), in.errorString()); drop(); return R::Error; }
            if (n == 0) break;
            if (out.write(buffer.constData(), n) != n) {
                error = QStringLiteral("write error on %1: %2").arg(QDir::toNativeSeparators(target), out.errorString());
                drop();
                return R::Error;
            }
            copied += n;
            if (tick.elapsed() >= 100) { tick.restart(); progressLater(request, copied, total, shown); }
            if (chunkDelay > 0) QThread::msleep(chunkDelay);
        }
        if (!out.flush()) { error = QStringLiteral("write error on %1: %2").arg(QDir::toNativeSeparators(target), out.errorString()); drop(); return R::Error; }
        out.close();
        if (out.error() != QFileDevice::NoError) { error = QStringLiteral("write error on %1: %2").arg(QDir::toNativeSeparators(target), out.errorString()); QFile::remove(target); return R::Error; }
        if (!copyTimes(src, target)) qInfo().noquote() << "files: could not copy timestamps to" << QDir::toNativeSeparators(target);
        ++files;
        progressLater(request, copied, total, shown);
        return R::Ok;
    };
    auto makeDir = [&](const QString& parent, const QString& name, QString* outPath) -> bool {
        for (int n = 1; n < 10000; ++n) {
            const QString target = QDir(parent).filePath(n == 1 ? name : numbered(name, n, true));
            if (QFileInfo::exists(target)) continue;
            if (QDir().mkdir(target)) { *outPath = target; return true; }
            if (!QFileInfo::exists(target)) return false;
        }
        return false;
    };

    R result = R::Ok;
    QStringList newDirs;   // every folder this request created, for timestamps and cancel cleanup
    for (const Top& t : tops) {
        const QString name = QFileInfo(t.src).fileName();
        if (!t.dir) {
            QString out;
            result = copyFile(t.src, destination, name, &out);
            if (result == R::Ok) created << QDir::toNativeSeparators(out);
        } else {
            QString root;
            if (!makeDir(destination, name, &root)) { error = QStringLiteral("cannot create folder %1").arg(name); result = R::Error; }
            else {
                created << QDir::toNativeSeparators(root);
                newDirs << root;
                QVector<QPair<QString, QString>> dirTimes{{t.src, root}};
                for (const Entry& e : t.entries) {
                    const QString target = QDir(root).filePath(e.rel);
                    if (e.dir) {
                        if (!QDir().mkdir(target)) { error = QStringLiteral("cannot create folder %1").arg(QDir::toNativeSeparators(target)); result = R::Error; break; }
                        newDirs << target;
                        dirTimes.push_back({e.src, target});
                    } else {
                        QString out;
                        const QFileInfo rel(target);
                        result = copyFile(e.src, rel.absolutePath(), rel.fileName(), &out);
                        if (result != R::Ok) break;
                    }
                }
                // Deepest first so writing into a folder does not bump its time afterwards.
                for (auto it = dirTimes.crbegin(); it != dirTimes.crend(); ++it) copyTimes(it->first, it->second);
            }
        }
        if (result != R::Ok) break;
    }

    if (result == R::Ok) {
        QString msg = QStringLiteral("Copy completed: %1 file(s), %2.").arg(files).arg(human(copied));
        if (skippedLinks) msg += QStringLiteral(" %1 link(s) skipped.").arg(skippedLinks);
        done(true, msg, created);
        return;
    }
    // Remove folders this request created that ended up empty (deepest first; rmdir only removes empty ones).
    for (auto it = newDirs.crbegin(); it != newDirs.crend(); ++it)
        if (QDir().rmdir(*it)) created.removeAll(QDir::toNativeSeparators(*it));
    if (result == R::Cancelled)
        done(false, QStringLiteral("Cancelled: %1 file(s) copied before cancel; the partial file was removed.").arg(files), created);
    else
        done(false, QStringLiteral("Copy failed: %1. %2 file(s) were copied.").arg(error).arg(files), created);
}

} // namespace xmb
