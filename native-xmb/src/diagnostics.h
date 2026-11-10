#pragma once
#include <QDateTime>
#include <QString>
#include <QStringList>

// PHASES-HTPC D6 (audit/SAFE-MODE.md): safe start, crash capture, and the diagnostics the System Information page
// shows. Nothing here uploads anything; every file stays under the data folder.
namespace xmb::diagnostics {

// ---- Safe mode (--safe) ----
// A run the watchdog starts after repeated crashes: no network clients, no artwork or icon fetches, the default
// theme, no cold-boot media. It changes no stored preference; leaving the flag restores everything.
bool safeMode();
void setSafeMode(bool on);
inline constexpr const char* kSafeModeNotice="Started in safe mode after repeated errors";

// ---- Crash capture ----
// Installs the unhandled-exception filter (minidump + text under <data>/crashes, newest 5 kept) once. `extra` is
// called after the files are written (the older symbolised stack in XMB_LOG / crash.log).
using CrashExtra=long (*)(void* exceptionPointers);
void installCrashCapture(CrashExtra extra);
// Where the crash files go; call once the data folder is known (an earlier crash falls back to crash.log only).
void setCrashDirectory(const QString& dataDirectory);
// The same files for a fatal Qt message (qFatal), which aborts without raising an exception.
void captureFatal(const char* message);
// Hidden test trigger: XMB_TEST_CRASH=normal-after-ms:3000,safe-after-ms:14000[,after-ms:N][,kind:qfatal]. Inactive
// when unset. Schedules a crash on the application's thread.
void armTestCrash();

struct CrashRecord {
    bool found=false;
    QDateTime time;
    QString code;      // "0xc0000005" or "qFatal"
    QString module;    // faulting module and offset
    QString file;      // the .txt path
};
CrashRecord lastCrash(const QString& dataDirectory);
QString crashDirectory(const QString& dataDirectory);

// ---- Logs ----
// The folder XMB_LOG (or the watchdog's xmb.log) lives in; <data>/logs when no log file is configured.
QString logDirectory(const QString& dataDirectory);
// Rotates <path> when it is at least maxBytes: path -> path.1 -> path.2 ... keeping `keep` old files.
bool rotateLog(const QString& path,qint64 maxBytes=5*1024*1024,int keep=3);
// Installs the Qt message handler: appends to XMB_LOG (rotated by size) when it is set, otherwise the default
// handler; a QtFatalMsg also writes a crash capture.
void installLogHandler();

// ---- The System Information lines ----
QStringList summaryLines();
// The Diagnostics page body: a title, a blank line, then summaryLines() and the crash folder.
QString pageText();   // uses the folder given to setCrashDirectory

}  // namespace xmb::diagnostics
