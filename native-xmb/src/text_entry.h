#pragma once
#include <QObject>
#include <QStringList>
#include <QString>
#include <QtGlobal>
#include <functional>
#include <vector>

// ZF2 (audit/TEXT-ENTRY.md): type into a running title or web app through the XMB's own on-screen keyboard.
//
// The in-game XMB's "Enter Text" row opens the XMB's OSK in plain text-field mode. On confirm the foreground goes back to the
// window the XMB recorded when the in-game XMB opened, and TextDelivery sends the text with SendInput and KEYEVENTF_UNICODE (one
// key-down and one key-up per UTF-16 unit, a short pause between units), so any keyboard layout works. Cancel sends nothing.
//
// The typed text is never logged, at any level: the log lines here carry counts, process ids and verdict names only.
namespace xmb::textentry {

constexpr int kUnitDelayMs=8;      // pause between UTF-16 units
constexpr int kMaxUnits=256;       // the OSK field limit for this mode

struct KeyUnit { quint16 unit; bool down; };

// The pure part: text -> the key events to send, in order (down then up for each UTF-16 unit). Control characters (below U+0020,
// U+007F..U+009F: newline, tab, escape and the like) are dropped, since a Unicode packet for them is not a key press; surrogate
// pairs are kept as two consecutive units. `dropped` (optional) receives how many units were dropped.
std::vector<KeyUnit> toKeyEvents(const QString& text,int* dropped=nullptr);

enum class Verdict { Deliver, NoWindow, NotTracked, Elevated, NoForeground, FocusLost, SendFailed };
const char* verdictName(Verdict verdict);

// `tracked(pid)` says whether a process id belongs to the tracked title's process tree.
using Tracked=std::function<bool(quint32 pid)>;
// Is `hwnd` (the recorded window) still a window, owned by a tracked process, and reachable (not elevated over this XMB)?
// Nothing is sent or moved by this call.
Verdict checkTarget(quintptr hwnd,const Tracked& tracked);

class TextDelivery final : public QObject {
    Q_OBJECT
public:
    explicit TextDelivery(QObject* parent=nullptr);
    ~TextDelivery() override;
    // checkTarget, then foreground back to `hwnd`, then the text. finished() is emitted exactly once per start().
    // Returns false when a delivery is already running (nothing is started).
    bool start(quintptr hwnd,const QString& text,Tracked tracked);
    bool busy() const { return busy_; }
signals:
    void finished(int verdict,int unitsSent);   // a Verdict, and how many UTF-16 units reached SendInput
private:
    void restore();
    void verifyForeground(bool retry);
    void step();
    void finish(Verdict verdict);
    quintptr hwnd_=0;
    std::vector<KeyUnit> events_;
    size_t next_=0;
    Tracked tracked_;
    bool busy_=false;
    int sent_=0;
    quint32 pid_=0;
};

// ---- Verification (local only; each writes JSON and returns an exit code) ---------------------------------------------
// --zf-text-test <text-file> <json-out>: the event list for the UTF-8 text in the file.
int runTextTest(const QString& textFile,const QString& output);
// --zf-text-sink <log> [seconds]: a window of our own that records the characters it receives from SendInput.
int runTextSink(const QString& logFile,int seconds);
// --zf-text-deliver <spec-json> <json-out>: TextDelivery against the sink window named in the spec (hwnd, pid, textFile,
// and optionally trackedRoot to simulate a window that is not the tracked title's). Refuses any window that is not the sink.
int runTextDeliver(const QString& specFile,const QString& output);
// --zf-elevation-check <json-out> [pid...|all]: elevation state of this process, the foreground window, each pid, or a count
// over every process ("all"; counts and pids only, no names).
int runElevationCheck(const QString& output,const QStringList& arguments);
}
