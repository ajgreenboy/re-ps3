#pragma once
#include <QList>
#include <QString>
#include <QStringList>

// Stale browser-profile adoption (P6 leftover, ZF6; audit/WEB-APPS.md).
// A Chromium browser that an earlier XMB run left open still holds <data>/browser-profile, so a fresh launch of a web
// app would only forward to that browser and exit. These helpers find such a browser by its own command line (it must
// name OUR profile folder through --user-data-dir), so the shell can adopt it instead of starting a second process.
// Nothing here opens, lists or reads the profile folder, and a browser process whose command line cannot be read, or
// that does not name our profile, is never touched.
namespace xmb::webAdopt {
// Folder identity used for the comparison: absolute, cleaned, forward slashes, no trailing slash, lower-case, and the
// canonical form when the folder exists (short 8.3 names and links resolve to the same string).
QString normalizeProfilePath(const QString& path);
// The --user-data-dir value of a parsed command line, or empty when it carries none.
QString profileFromArguments(const QStringList& arguments);
// True for the browser (root) process: Chromium's own children carry --type=renderer|gpu|utility|crashpad-handler...
bool isBrowserRoot(const QStringList& arguments);
struct Holder {
    quint32 pid=0;
    QString executable;     // base name, e.g. msedge.exe
};
// Running msedge.exe / chrome.exe root processes whose --user-data-dir equals `profileDir`. `unreadable` (optional)
// counts browser processes whose command line could not be read; those are treated as unknown and left alone.
QList<Holder> findHolders(const QString& profileDir,int* unreadable=nullptr);
// True while the process tree under `rootPid` owns at least one visible top-level window.
bool treeHasVisibleWindow(quint32 rootPid);
// Restores (if minimised) and raises the frontmost visible top-level window of the tree; true when one was foregrounded.
bool bringTreeForward(quint32 rootPid);
// True when the current foreground window belongs to the tree.
bool foregroundInTree(quint32 rootPid);
// A waitable handle on the process (opaque; null when it cannot be opened), so the end is seen exactly and a recycled pid
// can never be mistaken for the browser. watchExited() is non-blocking.
void* openWatch(quint32 pid);
bool watchExited(void* handle);
void closeWatch(void* handle);
// Lets `pid` take the foreground (the forwarding browser raises the existing window).
void allowForeground(quint32 pid);
// Last resort for Quit Game: TerminateProcess on the root and its descendants (and nothing else).
int terminateTree(quint32 rootPid);
}
