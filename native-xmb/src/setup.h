#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QtGlobal>

// D8, first-run setup (audit/FIRST-RUN.md). The pure half: where the settings live, what counts as a usable
// dev_flash, which folders to offer, and how a pad's buttons are named. No Qt GUI, no Shell: tests/setup_tests.cpp
// runs this file on its own. The sequence itself (the pages) is setup_shell.cpp.
namespace xmb::setup {
// ---- setup.json (<data>/setup.json) ----
// {"version":1,"completed":true,"skipped":false,"when":"<ISO time>"}; the sequence runs while "completed" is not true.
QString statePath(const QString& dataDirectory);
bool completed(const QString& dataDirectory);
bool markCompleted(const QString& dataDirectory,bool skipped,QString* error=nullptr);
// ---- xmb-config.json next to the exe (the packaged build's launcher config) ----
// Read by run-kiosk.ps1 / watchdog.ps1 and, for any option the command line does not give, by main.cpp:
// {"devFlash":"...","dataDir":"...","vshModules":"...","display":1}. Setup edits keys in place and keeps the rest.
QString configPath();
QJsonObject loadConfig(const QString& path);
bool saveConfig(const QString& path,const QJsonObject& config,QString* error=nullptr);
// ---- the firmware folder ----
// dev_flash is the user's own console firmware; the XMB never downloads or ships it. A folder is usable when these
// files exist (the ones the shell cannot start without); `missing` lists those that do not, relative to the root.
struct FirmwareCheck { bool exists=false,ok=false; QStringList missing; };
FirmwareCheck checkFirmware(const QString& root);
QStringList requiredFirmwareFiles();
// dev_flash folders worth offering without browsing: next to the exe, in a sibling RPCS3 install, and at the root of
// each drive. Only folders that pass checkFirmware() are returned. Read-only; nothing under them is touched.
QStringList findFirmwareCandidates();
// ---- library folders ----
// Windows known folders (Pictures, Music, Videos); empty when the OS names none. kind is "photo", "music" or "video".
QString defaultLibraryFolder(const QString& kind);
// ---- folder browser (controller-driven; the pages are shell rows) ----
// The drive roots ("C:/", ...) when path is empty, else the sub-folders of path sorted by name.
// parentOf("C:/") is "" (the drive list).
QStringList browseRoots();
QStringList browseFolders(const QString& path);
QString parentOf(const QString& path);
// ---- pad test ----
// XInput wButtons layout (pad.h): the names of every bit set, in a fixed order, e.g. "Cross, L1".
QString buttonNames(quint16 buttons);
QString axisText(float lx,float ly,float rx,float ry,float l2,float r2);
}
