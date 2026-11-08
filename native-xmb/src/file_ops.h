#pragma once
// Copy and Delete for local media files (the firmware's msg_copy / msg_del option rows).
//
// Delete never unlinks. deleteToRecycleBin() hands the items to the shell's IFileOperation on a
// worker thread (COM apartment initialised there) with FOFX_RECYCLEONDELETE | FOF_ALLOWUNDO and
// every UI flag off (FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT | FOFX_NOMINIMIZEBOX). A
// progress sink aborts any item the shell would not send to the Recycle Bin (no bin on that volume,
// item larger than the bin), so an item is either recycled or left where it was. The volume is also
// checked up front with SHQueryRecycleBin.
//
// Delete refusals (the whole request is refused if any path is, nothing is touched):
//   - no allowed roots configured, or the path is not strictly inside one of them (a root itself is
//     never deleted); checked on the cleaned path and on the canonical (symlink-resolved) path
//   - relative paths, '..' segments, wildcards, device/namespace paths (\\?\, \\.\), NTFS streams
//   - drive roots and UNC share roots
//   - the Windows folder, Program Files (both), ProgramData and anything inside them
//   - the user profile root, and any folder that contains a protected folder (C:\Users, ...)
//   - paths that do not exist
//
// Copy is a streaming copy on a worker thread: 4 MB QFile chunks, timestamps preserved (files and
// folders), never overwrites ("name (2).ext", "Folder (2)"; the file is created with NewOnly so a
// race cannot overwrite either). Folders are copied recursively; symbolic links and junctions inside
// a folder are skipped. Progress is throttled to ~10 Hz plus one event per finished file.
// cancel(request) stops between chunks and removes the partially written file; files already
// finished stay and are listed in resultingPaths.
//
// Copy refusals: same path syntax rules for sources and destination; sources must exist; the
// destination must be an existing folder, not inside (or equal to) a source folder, and not a
// protected system folder; free space on the destination volume (QStorageInfo) must cover the total
// size plus 1 MB; a file of 4 GiB or more cannot go to a FAT/FAT32 volume.
//
// Every request answers exactly once with finished(), always queued (also for refusals), never
// synchronously from inside copy()/deleteToRecycleBin(). Messages are plain English; the stable
// prefixes below let the shell pick a firmware string:
//   "Not enough free space"  -> msg_copy_incomplete
//   "Cancelled"              -> (copy cancelled; msg_copy_cancel_ask was the question)
//   "Refused: "              -> a safety rule rejected the request (nothing touched)
//   "Copy completed" / "Delete completed" on success (msg_copied / msg_deleted)
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <atomic>
#include <functional>
#include <memory>

class QThread;

namespace xmb {

class FileOperations final : public QObject {
    Q_OBJECT
public:
    explicit FileOperations(QObject* parent = nullptr);
    // Cancels every running request and waits for the worker threads; no signal fires afterwards.
    ~FileOperations() override;

    // Library roots that Delete may touch (strictly inside). Relative, empty, drive-less or
    // protected-system roots are dropped. Existing roots are also stored canonicalised.
    void setAllowedRoots(QStringList roots);
    QStringList allowedRoots() const { return roots_; }

    // Returns the request id carried by finished(). resultingPaths = the original paths that are
    // now in the Recycle Bin.
    quint64 deleteToRecycleBin(QStringList paths);
    // Copies files and/or folders into destinationFolder. progress() while it runs;
    // resultingPaths = the top-level copies created (files or folders).
    quint64 copy(QStringList paths, QString destinationFolder);
    // Cancels one request (copy: partial file removed; delete: items not yet recycled stay).
    void cancel(quint64 request);
    void cancelAll();
    bool busy() const { return !jobs_.isEmpty(); }

    // Pure checks, exposed so the shell can grey a row out before asking. Empty string = allowed,
    // otherwise the "Refused: ..." reason.
    QString checkDeletable(const QString& path) const;
    QString checkCopy(const QStringList& paths, const QString& destinationFolder) const;

    // Testing only: pretend the destination volume has this many free bytes (-1 = real value).
    void setFreeSpaceOverrideForTesting(qint64 bytes) { freeSpaceOverride_ = bytes; }
    // Testing only: sleep this long after every 4 MB chunk (to cancel a copy that is in flight).
    void setChunkDelayForTesting(int ms) { chunkDelayMs_ = qMax(0, ms); }

signals:
    void progress(quint64 request, qint64 done, qint64 total, QString currentFile);
    void finished(quint64 request, bool ok, QString message, QStringList resultingPaths);

private:
    using CancelFlag = std::shared_ptr<std::atomic_bool>;
    quint64 start(const QString& kind, std::function<void(quint64, CancelFlag)> work);
    quint64 refuse(const QString& kind, const QString& reason);
    // Thread-safe: post to the owner thread.
    void finishLater(quint64 request, bool ok, const QString& message, const QStringList& paths);
    void progressLater(quint64 request, qint64 done, qint64 total, const QString& file);
    void runDelete(quint64 request, QStringList paths, CancelFlag cancel);
    void runCopy(quint64 request, QStringList paths, QString destination, qint64 freeOverride, int chunkDelay, CancelFlag cancel);

    QStringList roots_;
    QHash<quint64, CancelFlag> jobs_;   // running requests
    QList<QThread*> threads_;           // worker threads not yet finished
    quint64 nextRequest_ = 0;
    qint64 freeSpaceOverride_ = -1;
    int chunkDelayMs_ = 0;
};

} // namespace xmb
