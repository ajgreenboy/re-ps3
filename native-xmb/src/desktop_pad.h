#pragma once
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QtGlobal>
#include <algorithm>
#include <functional>
#include <vector>
class QTimer;

// Controller-driven desktop (PHASES-HTPC D5; design, mapping table and measurements in audit/DESKTOP-MODE.md).
// Two halves: Mapper is a pure function from pad state plus time to input actions (no Qt GUI, no Win32, so it is unit
// tested through --desktop-pad-test); DesktopPadDriver reads the pads, applies the gates and delivers the actions
// through SendInput. Nothing here runs unless Desktop Mode is active.
namespace xmb::desktoppad {

// Buttons in XInput's wButtons layout (pad.h): the same bits a PlayStation pad is normalised to.
namespace btn {
constexpr quint16 Up=0x1,Down=0x2,Left=0x4,Right=0x8,Start=0x10,Back=0x20,L3=0x40,R3=0x80,LB=0x100,RB=0x200,Guide=0x400,
    Cross=0x1000,Circle=0x2000,Square=0x4000,Triangle=0x8000;
}

struct Config {
    double pointerSpeed=1.0;          // multiplier from the desktop/pointerSpeed preference (speedMultiplier)
    double pointerDeadzone=0.20;      // radial, on the left stick
    double pointerMaxPxPerSecond=1800;
    double pointerExponent=2.2;       // the acceleration curve: speed grows with deflection^2.2 after the deadzone
    double pointerFinePxPerSecond=40; // linear term, so a barely deflected stick still moves a pixel or two
    double scrollDeadzone=0.25;       // per axis, on the right stick
    double scrollMaxNotchesPerSecond=12;
    double scrollExponent=1.8;
    int scrollMinimumUnits=30;        // wheel units (120 = one notch) are batched until at least this many are due
    int repeatDelayMs=450;            // D-pad: first repeat after this long, then every repeatIntervalMs
    int repeatIntervalMs=45;
    int switcherCommitMs=1000;        // LB/RB: Alt is released this long after the last LB/RB press
};
Config configFromJson(const QJsonObject& object);
// desktop/pointerSpeed is 1..5 (Slow, Relaxed, Normal, Fast, Very Fast).
double speedMultiplier(int level);

struct Sample { quint16 buttons=0; float lx=0,ly=0,rx=0,ry=0; };   // y is +down, as pad.h

enum class Kind { Move, Button, Wheel, Key, ToggleKeyboard };
struct Action {
    Kind kind=Kind::Move;
    qint64 t=0;
    int dx=0,dy=0;                 // Move: relative pixels
    int button=0;                  // Button: 0 left, 1 right
    bool down=false;               // Button and Key
    int vk=0;                      // Key: Win32 virtual-key code
    bool extended=false;           // Key: the extended-key flag (arrows, right-hand keys)
    bool repeat=false;             // Key: an auto-repeat of a held key
    int wheelV=0,wheelH=0;         // Wheel: signed wheel units (120 = one notch); +V up, +H right
    QString why;                   // which mapping produced it (report and log only)
};
QJsonObject toJson(const Action& action);

class Mapper {
public:
    explicit Mapper(const Config& config=Config{}):config_(config) {}
    void setConfig(const Config& config) { config_=config; }
    const Config& config() const { return config_; }
    // One tick. `now` is milliseconds on any monotonic clock. Returns the actions to deliver, in order.
    std::vector<Action> update(const Sample& sample,qint64 now);
    // Everything this mapper pressed is released (mouse buttons, keys, Alt). Empty when nothing is held.
    std::vector<Action> releaseAll(qint64 now);
    // The gates closed (or Desktop Mode was just entered): release everything this mapper holds and ignore every button that is
    // down in `sample` until it is released.
    std::vector<Action> suspend(const Sample& sample,qint64 now);
    bool anythingHeld() const;
    bool switcherOpen() const { return altDown_; }
    // The curves, exposed for the tests and the documentation.
    static double pointerSpeedPxPerSecond(const Config& config,double deflection);   // deflection 0..1 (radial, raw)
    static double scrollUnitsPerSecond(const Config& config,double deflection);      // deflection 0..1 (one axis, raw)
private:
    void key(std::vector<Action>& out,qint64 now,int vk,bool down,bool extended,const char* why,bool repeat=false);
    void mouse(std::vector<Action>& out,qint64 now,int button,bool down,const char* why);
    void closeSwitcher(std::vector<Action>& out,qint64 now,const char* why);
    bool keysDownContains(int vk) const { return std::find(keysDown_.begin(),keysDown_.end(),vk|0x10000)!=keysDown_.end(); }
    Config config_;
    quint16 previous_=0,swallowed_=0;
    qint64 last_=-1,switchLast_=0;
    double accX_=0,accY_=0,accV_=0,accH_=0;
    bool suppressed_=false,altDown_=false;
    bool mouseDown_[2]{false,false};
    std::vector<int> keysDown_;                 // vk | 0x10000 when extended
    qint64 nextRepeat_[4]{0,0,0,0};
};

// --desktop-pad-test: the scenario in `input` is run through a Mapper and the actions written to `output`.
// input: { "config":{...}, "tickMs":10, "endMs":1000, "samples":[{"t":0,"buttons":4096,"lx":0,"ly":0,"rx":0,"ry":0}, ...] }
// The sample with the greatest t <= the tick time is the pad state at that tick (a step function). Returns the exit code.
int runPadTest(const QString& input,const QString& output);

// ---- Delivery ------------------------------------------------------------------------------------------
// Everything the driver needs to know about the rest of the app, so this file includes neither shell.h nor view.h.
struct Hooks {
    std::function<bool()> enabled;        // desktop/controllerMouse
    std::function<int()> pointerSpeed;    // desktop/pointerSpeed
    std::function<bool()> blocked;        // a tracked title or web app is running (Shell::titleRunning)
    std::function<bool()> xmbVisible;     // the XMB window is on screen
    std::function<void()> elevatedForeground;   // optional (ZF1): the window in front runs as administrator; called once per such window
};

class DesktopPadDriver final : public QObject {
    Q_OBJECT
public:
    DesktopPadDriver(Hooks hooks,QObject* parent=nullptr);
    ~DesktopPadDriver() override;
    // Desktop Mode entered / left. Leaving releases anything this driver is holding down before the XMB reappears.
    void setActive(bool active);
    bool active() const { return active_; }
    // Why nothing is being injected right now ("" when the driver is live): the tray tooltip and the log use it.
    QString idleReason() const { return reason_; }
    quint64 oskPid() const;   // the osk.exe this driver started and still owns (0 = none)
    // The Start button: the touch keyboard through ITipInvocation when it can be shown, else osk.exe. Only an osk.exe this
    // driver started is ever closed.
    void toggleKeyboard();
    // Test hooks: deliver a prepared action list with SendInput (the same path the timer uses).
    void deliver(const std::vector<Action>& actions);
signals:
    void idleReasonChanged(const QString& reason);
private:
    void tick();
    QString gate();
    bool yieldToFullscreenApp();
    void checkConflicts();
    QTimer* timer_=nullptr;
    Hooks hooks_;
    Mapper mapper_;
    bool active_=false;
    QString reason_;
    qint64 lastGateAt_=-1000,lastYieldAt_=-1000,lastConflictAt_=-100000;
    bool yieldNow_=false,conflictLogged_=false,primed_=false;
    QString gateCache_,yieldDetail_;
    quint64 oskPid_=0;
    void* oskProcess_=nullptr;
    qint64 activeSince_=0,injected_=0;
    // ZF1: the foreground window's process is looked up at most every 250 ms; while it runs elevated over this (non-elevated)
    // XMB nothing but releases is sent, because UIPI drops the rest and the old log line only said "may be elevated".
    bool foregroundBlockedByElevation();
    qint64 elevationCheckedAt_=-1000;
    quint32 elevatedPid_=0;
    bool elevationBlocked_=false;
};

// --desktop-pad-sink <log>: a small window that logs the mouse and key events it receives, one JSON object per line, so
// the SendInput path can be checked without touching any other application. Runs until its window is closed or `seconds` pass.
int runPadSink(const QString& logFile,int seconds);
// --desktop-pad-inject <json-in> <json-out>: runs a scenario through the Mapper in real time and delivers it with SendInput,
// but only while the foreground window and the window under the pointer both belong to the sink process named in the input.
int runPadInject(const QString& input,const QString& output);
// --desktop-pad-keyboard <json-out>: presses Start twice through the driver and reports what was on screen each time.
int runKeyboardCheck(const QString& output);
}
