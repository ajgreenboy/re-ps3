#pragma once
// Game window identity and one-frame capture (INGAME-XMB-DESIGN step 4).
// Standalone: Qt Core/Gui plus Win32/WinRT, no dependency on shell or view.
// Windows.Graphics.Capture through DWM only - nothing is injected into, hooked
// in, or suspended in the target process.
#include <QImage>
#include <QRect>
#include <QHash>
#include <QString>
#include <QtGlobal>

namespace xmb {

enum class GameWindowMode {
    Invalid,     // not a live window
    Minimized,
    Windowed,
    Borderless,  // covers its monitor without exclusive ownership (DWM composed)
    Exclusive    // detected as exclusive fullscreen: not capturable through DWM
};

enum class GameCaptureStatus {
    Ok,      // a frame with visible content
    Black,   // a frame arrived but every sampled pixel is black
    Failed   // no frame (unsupported, window gone/minimised, timeout, device error)
};

struct GameWindowInfo {
    quintptr hwnd = 0;
    quint32 pid = 0;
    QString title;
    QString className;
    GameWindowMode mode = GameWindowMode::Invalid;
    QRect windowRect;      // GetWindowRect, virtual-desktop pixels
    QRect monitorRect;     // monitor holding most of the window
    QString outputName;    // DXGI output (e.g. "\.\DISPLAY1"), empty if unresolved
    bool popupStyle = false;     // WS_POPUP set
    bool captionStyle = false;   // WS_CAPTION set
    bool coversMonitor = false;  // window rect contains the monitor rect
    bool cloaked = false;        // DwmGetWindowAttribute(DWMWA_CLOAKED) != 0
    bool foreground = false;     // is GetForegroundWindow()
    int notificationState = -1;  // SHQueryUserNotificationState (QUERY_USER_NOTIFICATION_STATE), -1 failed
    QString reason;              // one-line why the mode was chosen
};

struct GameCaptureResult {
    GameCaptureStatus status = GameCaptureStatus::Failed;
    QImage image;        // RGB32, client+frame as DWM composes it; null unless status != Failed
    int elapsedMs = 0;
    bool borderOff = false;  // IsBorderRequired=false was accepted
    bool cursorOff = false;  // IsCursorCaptureEnabled=false was accepted
    QString detail;      // HRESULT / reason on Failed, sample summary otherwise
};

// Classify a top-level window: windowed / borderless / exclusive.
// Exclusive is a heuristic (SHQueryUserNotificationState == D3D_FULL_SCREEN,
// monitor-sized popup, foreground); it is not proof, and Fullscreen
// Optimizations make some exclusive-looking titles plain borderless.
GameWindowInfo classifyGameWindow(quintptr hwnd);

// The visible, uncloaked top-level window of `pid` with the largest area (0 if none).
quintptr findMainWindow(quint32 pid);

// Capture one frame of `hwnd` with Windows.Graphics.Capture, bounded by
// timeoutMs (default 2 s). Runs the WinRT work on its own MTA thread so the
// caller's apartment (e.g. Qt's STA) is irrelevant. Never throws.
GameCaptureResult captureGameWindow(quintptr hwnd, int timeoutMs = 2000);

// True when `pid` is `rootPid` or a descendant of it (Toolhelp snapshot; a launcher that
// hands over to a game keeps the game in the tree).
bool pidInTree(quint32 pid, quint32 rootPid);

// Ask a launched title to close: post WM_CLOSE to every visible top-level window of `rootPid`
// and its descendants. This is the cooperative half of Quit Game (INGAME-XMB-DESIGN step 3):
// nothing is injected and no process is terminated here. Returns the number of windows asked.
int postCloseToProcessTree(quint32 rootPid);

// "ingame: window pid=.. mode=.. capture=ok|black|failed ..." log line.
QString gameCaptureLogLine(const GameWindowInfo& info, const GameCaptureResult& result);
const char* gameWindowModeName(GameWindowMode m);
const char* gameCaptureStatusName(GameCaptureStatus s);

}  // namespace xmb
