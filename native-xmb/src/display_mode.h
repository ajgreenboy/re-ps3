#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>
#include "display_spec.h"

// D9: per-app display modes (audit/DISPLAY-MODES.md). A title's apps.json entry may ask for a resolution, a refresh rate
// and an HDR state. The mode is validated against what the monitor lists, applied temporarily (CDS_FULLSCREEN, never
// CDS_UPDATEREGISTRY) and put back exactly when the title session ends. The state to restore is written to
// <data>/display-restore.json BEFORE anything changes, so a crash, a kill or a watchdog restart of the XMB is repaired at
// the next start. Everything that decides is a pure function over plain data (below); only Backend touches Windows.
namespace xmb::display {

struct Mode { int width=0,height=0,refresh=0;
    bool valid() const { return width>0 && height>0 && refresh>0; }
    bool operator==(const Mode& o) const { return width==o.width && height==o.height && refresh==o.refresh; }
};
// The whole current mode of one display, as ChangeDisplaySettingsEx needs it to put it back exactly.
struct FullMode { Mode mode; int bitsPerPixel=0,positionX=0,positionY=0,orientation=0;
    bool operator==(const FullMode& o) const { return mode==o.mode && bitsPerPixel==o.bitsPerPixel && positionX==o.positionX &&
        positionY==o.positionY && orientation==o.orientation; }
};
enum class Hdr { Unsupported, Off, On };
const char* hdrName(Hdr h);
struct Snapshot { QString device; FullMode current; Hdr hdr=Hdr::Unsupported; };

enum class HdrWish { Keep, On, Off };
struct Request { int width=0,height=0,refresh=0; HdrWish hdr=HdrWish::Keep;
    bool any() const { return width>0 || height>0 || refresh>0 || hdr!=HdrWish::Keep; } };
Request requestFrom(const DisplaySpec& spec);

struct Plan {
    bool changeMode=false; FullMode target;
    bool changeHdr=false; bool hdrOn=false;
    QStringList skipped;   // one reason per part that was refused; the part is skipped, never forced
    bool anything() const { return changeMode || changeHdr; }
};
// Pure. `available` is what EnumDisplaySettings lists for the display (at the current colour depth). A mode the monitor
// does not list is skipped with a reason; HDR is skipped when the display does not support advanced colour.
Plan makePlan(const Request& request,const Snapshot& now,const QVector<Mode>& available);

// What a restore has to undo, and its on-disk form.
struct RestoreState {
    QString device,title; qint64 at=0;
    FullMode original; Hdr originalHdr=Hdr::Unsupported;
    bool changedMode=false,changedHdr=false;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& object,RestoreState* out,QString* error);
};
struct RestorePlan { bool restoreMode=false,restoreHdr=false; };
// Pure: only what actually differs from the saved original is touched, HDR first and the mode second.
RestorePlan makeRestorePlan(const RestoreState& state,const Snapshot& now);

// "1920x1080@60" -> Mode; false for anything else.
bool parseModeText(const QString& text,Mode* out);
QString modeText(const Mode& m);

// ---- the part that talks to Windows ----------------------------------------------------------------------------------
class Backend {
public:
    virtual ~Backend()=default;
    virtual bool read(Snapshot* out,QString* error)=0;
    virtual QVector<Mode> modes()=0;
    virtual bool setMode(const FullMode& mode,QString* error)=0;
    virtual bool setHdr(bool on,QString* error)=0;
    virtual bool dryRun() const { return false; }
    // Restore works on the display the change was made on, which may not be the one the XMB window is on now.
    virtual void pinDevice(const QString&) {}
};
// The Win32 backend for the display named by `device` ("\.\DISPLAY1"; empty = the primary). XMB_DISPLAY_DRY_RUN=1
// (or dryRun=true) reads the real display but logs "would ..." instead of writing, and keeps a simulated state so the
// restore logic can be exercised: with XMB_DISPLAY_FAKE_HDR=1 the dry run also pretends the display supports HDR.
std::unique_ptr<Backend> makeWin32Backend(std::function<QString()> deviceProvider,bool dryRun);
bool dryRunRequested();   // XMB_DISPLAY_DRY_RUN=1

class Controller {
public:
    Controller(std::unique_ptr<Backend> backend,const QString& stateFile);
    // Validates, saves the restore state, then applies. Returns what was applied (empty note = nothing needed).
    // A part the monitor does not support is logged and skipped; the title still launches.
    bool apply(const DisplaySpec& spec,const QString& title,QString* note=nullptr);
    bool pending() const;
    // Puts everything back from the saved state; true when nothing is left to restore. A failed restore keeps the file.
    bool restore(const QString& why);
    // At XMB start: a leftover state file means the previous run ended with a changed display.
    bool restoreIfPending();
    // One line for Settings: the current mode and HDR state.
    QString describe();
    Backend* backend() const { return backend_.get(); }
private:
    bool saveState(const RestoreState& state);
    std::unique_ptr<Backend> backend_;
    QString stateFile_;
};

// Settings > Display Settings, read-only: "1920 x 1080 at 60 Hz, HDR off". Reads the real display.
QString currentSummary();
// Process-wide controller the shell uses; set once by main. nullptr until then.
void setActiveController(Controller* controller);
Controller* activeController();

// --display-mode-test <w>x<h>@<hz> <seconds>: applies one listed mode, waits, and restores even on failure. Exit code 0
// only when the display is back exactly where it started. Honours XMB_DISPLAY_DRY_RUN.
int runModeTest(const QString& spec,int seconds,const QString& dataDir);
}
