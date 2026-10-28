#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace xmb {

struct SteamGame {
    QString appId;
    QString title;
    QString installDirectory;
    QString artwork;
};

struct SteamInstallation {
    QString root;
    QString steamId;
    QVector<SteamGame> games;
    QStringList warnings;
};

// Reads Steam's local configuration and installed-game manifests. This function
// performs no network access and makes no changes to Steam files.
SteamInstallation scanSteam(const QString& explicitRoot = {});

// Live state of one title as the running Steam client reports it in the registry
// (HKCU\Software\Valve\Steam). Read-only; always false off Windows.
struct SteamAppState {
    bool running = false;
    bool updating = false;
};
SteamAppState steamAppState(const QString& appId);

// Ask the running Steam client to stop a title through its console command line. The client executes
// "+<command>" arguments handed to steam.exe, so this is the supported-by-observation way to quit a
// Steam game without killing processes (audit/STEAM-CLI.md). Returns false when the client cannot be
// started or the id is not a number; the caller still has to wait for Apps\<id>\Running to clear.
// The forwarding process exits immediately with no stdout and no exit status, so a true return only
// means the command line was handed over - use steamVerifyStop() for the client's own record of it.
bool steamStopApp(const QString& appId,const QString& steamRoot = {}, QString* error = nullptr);

// The exact argument shape of the stop handoff: {"+app_stop", "<numeric app id>"}. Kept in one place
// so the command line, the log match and the audit cannot drift apart. Empty for an invalid id.
QStringList steamStopArguments(const QString& appId);

// ---- The Steam console log: the only return channel for a "+command" handoff ------------------
// A second "steam.exe +<command> ..." forwards to the running client and exits: no stdout, no exit
// code. The client writes the command line it received, and the command's own output, into
// <steamRoot>/logs/console_log.txt, so a bounded read of the region after a snapshot offset is the
// only evidence that a command was delivered and accepted (audit/STEAM-CLI.md section 3).
struct SteamConsoleLogSlice {
    bool available = false; // the log exists and the region could be read
    bool truncated = false; // the file is shorter than the requested offset (rotation or truncation)
    qint64 offset = 0;      // where this read started
    qint64 size = 0;        // the file's size at the moment of the read
    QByteArray bytes;       // at most the requested maximum of the region after `offset`
    QString path;           // the log file that was read, empty when none was found
    QString error;          // why the read failed, empty on success
};

// Read at most `maximumBytes` (default 64 KiB, capped at 1 MiB) of the log starting at `offset`.
// `offset` -1 reads the tail of the file. Read-only and bounded: nothing under the Steam
// installation is ever written, and only <root>/logs/console_log.txt is opened.
SteamConsoleLogSlice steamConsoleLogTail(const QString& steamRoot, qint64 offset,
                                        qsizetype maximumBytes = 64 * 1024);

// The log's current size in bytes, or -1 when no log was found. Take this before a handoff and pass
// it to steamVerifyStop() so earlier commands in the same file cannot be mistaken for this one.
qint64 steamConsoleLogSize(const QString& steamRoot);

// What the client's own log says about one stop handoff issued after `offset`.
struct SteamStopEvidence {
    bool logAvailable = false;
    bool delivered = false; // the client logged our exact "+app_stop <id>" command line
    bool rejected = false;  // the client answered "command not found" for app_stop
    QString commandLine;    // the line that was matched, for the log
    QString context;        // the lines the client wrote immediately after (bounded; may include
                            // adjacent client spew, so `delivered` is the signal, not this text)
    QString error;          // why no verdict could be read (missing log, unreadable region)
};
SteamStopEvidence steamVerifyStop(const QString& appId, qint64 offset,
                                  const QString& steamRoot = {});

}
