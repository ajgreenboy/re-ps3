#include "text_entry.h"
#include "elevation.h"
#include "game_capture.h"
#include "ps_watcher.h"
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace xmb::textentry {

std::vector<KeyUnit> toKeyEvents(const QString& text,int* dropped) {
    std::vector<KeyUnit> events;
    int skipped=0;
    events.reserve(size_t(text.size())*2);
    for(const QChar c:text) {
        const ushort u=c.unicode();
        if(u<0x20 || (u>=0x7f && u<=0x9f)){++skipped;continue;}
        events.push_back({u,true});
        events.push_back({u,false});
    }
    if(dropped)*dropped=skipped;
    return events;
}

const char* verdictName(Verdict verdict) {
    switch(verdict) {
    case Verdict::Deliver:return "deliver";
    case Verdict::NoWindow:return "no-window";
    case Verdict::NotTracked:return "not-tracked";
    case Verdict::Elevated:return "elevated";
    case Verdict::NoForeground:return "no-foreground";
    case Verdict::FocusLost:return "focus-lost";
    case Verdict::SendFailed:return "send-failed";
    }
    return "unknown";
}

#ifdef Q_OS_WIN
namespace {
quint32 windowPid(HWND hwnd) {
    DWORD pid=0;
    if(hwnd)GetWindowThreadProcessId(hwnd,&pid);
    return quint32(pid);
}
// The same recipe as View's takeForeground / TitleSession::restoreWindow: attach to the foreground thread's input queue for the one
// SetForegroundWindow call (the documented way through the foreground lock). Nothing is injected into any process.
void takeForeground(HWND window) {
    if(!window)return;
    if(IsIconic(window))ShowWindow(window,SW_RESTORE);
    const HWND foreground=GetForegroundWindow();
    if(foreground==window)return;
    const DWORD foregroundThread=foreground?GetWindowThreadProcessId(foreground,nullptr):0;
    const DWORD thisThread=GetCurrentThreadId();
    const bool attached=foregroundThread && foregroundThread!=thisThread && AttachThreadInput(thisThread,foregroundThread,TRUE);
    SetForegroundWindow(window);BringWindowToTop(window);SetActiveWindow(window);
    if(attached)AttachThreadInput(thisThread,foregroundThread,FALSE);
}
}

Verdict checkTarget(quintptr hwnd,const Tracked& tracked) {
    HWND window=reinterpret_cast<HWND>(hwnd);
    if(!window || !IsWindow(window))return Verdict::NoWindow;
    const quint32 pid=windowPid(window);
    if(!pid || !tracked || !tracked(pid))return Verdict::NotTracked;
    if(elevation::blocksDelivery(elevation::processState(pid)))return Verdict::Elevated;
    return Verdict::Deliver;
}

TextDelivery::TextDelivery(QObject* parent):QObject(parent) {}
TextDelivery::~TextDelivery() = default;

bool TextDelivery::start(quintptr hwnd,const QString& text,Tracked tracked) {
    if(busy_)return false;
    busy_=true;sent_=0;next_=0;hwnd_=hwnd;tracked_=std::move(tracked);
    int dropped=0;
    events_=toKeyEvents(text,&dropped);
    const Verdict verdict=checkTarget(hwnd_,tracked_);
    pid_=windowPid(reinterpret_cast<HWND>(hwnd_));
    // Counts only: the text itself is never written anywhere.
    qInfo().noquote()<<QString("textentry: start units=%1 dropped=%2 pid=%3 check=%4")
        .arg(events_.size()/2).arg(dropped).arg(pid_).arg(QLatin1String(verdictName(verdict)));
    if(verdict!=Verdict::Deliver){finish(verdict);return true;}
    if(events_.empty()){finish(Verdict::Deliver);return true;}
    restore();
    QTimer::singleShot(300,this,[this]{verifyForeground(false);});
    return true;
}

void TextDelivery::restore() { takeForeground(reinterpret_cast<HWND>(hwnd_)); }

void TextDelivery::verifyForeground(bool retry) {
    HWND window=reinterpret_cast<HWND>(hwnd_);
    if(!IsWindow(window)){finish(Verdict::NoWindow);return;}
    if(GetForegroundWindow()==window) {
        // The window or its process could have changed while the foreground moved; look again before the first key.
        const Verdict verdict=checkTarget(hwnd_,tracked_);
        if(verdict!=Verdict::Deliver){finish(verdict);return;}
        step();
        return;
    }
    if(retry){finish(Verdict::NoForeground);return;}
    restore();
    QTimer::singleShot(400,this,[this]{verifyForeground(true);});
}

void TextDelivery::step() {
    HWND window=reinterpret_cast<HWND>(hwnd_);
    if(!IsWindow(window) || GetForegroundWindow()!=window){finish(Verdict::FocusLost);return;}
    if(next_>=events_.size()){finish(Verdict::Deliver);return;}
    // One key-down and one key-up for the UTF-16 unit, in a single call so nothing can land between them.
    INPUT inputs[2]{};
    for(int i=0;i<2;++i) {
        const KeyUnit& unit=events_[next_+size_t(i)];
        inputs[i].type=INPUT_KEYBOARD;
        inputs[i].ki.wVk=0;
        inputs[i].ki.wScan=unit.unit;
        inputs[i].ki.dwFlags=KEYEVENTF_UNICODE|(unit.down?0:KEYEVENTF_KEYUP);
    }
    if(SendInput(2,inputs,sizeof(INPUT))!=2){finish(Verdict::SendFailed);return;}
    next_+=2;++sent_;
    QTimer::singleShot(kUnitDelayMs,this,[this]{step();});
}

void TextDelivery::finish(Verdict verdict) {
    qInfo().noquote()<<QString("textentry: finished verdict=%1 unitsSent=%2 pid=%3").arg(QLatin1String(verdictName(verdict))).arg(sent_).arg(pid_);
    events_.clear();events_.shrink_to_fit();
    tracked_=nullptr;
    busy_=false;
    emit finished(int(verdict),sent_);
}
#else
Verdict checkTarget(quintptr,const Tracked&) { return Verdict::NoWindow; }
TextDelivery::TextDelivery(QObject* parent):QObject(parent) {}
TextDelivery::~TextDelivery() = default;
bool TextDelivery::start(quintptr,const QString&,Tracked){emit finished(int(Verdict::NoWindow),0);return true;}
void TextDelivery::restore() {}
void TextDelivery::verifyForeground(bool) {}
void TextDelivery::step() {}
void TextDelivery::finish(Verdict) {}
#endif

// ---- Verification ------------------------------------------------------------------------------------------------------
namespace {
bool writeJson(const QString& path,const QJsonObject& object) {
    QFile file(path);
    const auto bytes=QJsonDocument(object).toJson();
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()){qCritical().noquote()<<"zf: cannot write"<<path<<file.errorString();return false;}
    return true;
}
}

int runTextTest(const QString& textFile,const QString& output) {
    QFile in(textFile);
    if(!in.open(QIODevice::ReadOnly)){qCritical().noquote()<<"zf-text-test: cannot read"<<textFile;return 2;}
    QString text=QString::fromUtf8(in.readAll());
    if(text.endsWith(QLatin1Char('\n')) && !text.endsWith(QStringLiteral("\n\n")))text.chop(text.endsWith(QStringLiteral("\r\n"))?2:1);   // the file's own final newline
    int dropped=0;
    const auto events=toKeyEvents(text,&dropped);
    QJsonArray list;
    for(const auto& e:events)list.append(QJsonObject{{"unit",int(e.unit)},{"down",e.down}});
    const QJsonObject out{{"inputUnits",int(text.size())},{"dropped",dropped},{"unitsToSend",int(events.size()/2)},{"eventCount",int(events.size())},{"events",list}};
    if(!writeJson(output,out))return 2;
    qInfo().noquote()<<QString("zf-text-test: %1 units in, %2 dropped, %3 events out").arg(text.size()).arg(dropped).arg(events.size());
    return 0;
}

#ifdef Q_OS_WIN
namespace {
// A window of our own that records what SendInput delivers to it, as the raw window messages any Win32 application would receive:
// WM_KEYDOWN/WM_KEYUP with VK_PACKET, and the UTF-16 unit in each WM_CHAR. (Qt's own key events drop surrogate pairs, so they are not
// used.) Test text only; the app itself never writes typed text anywhere.
class TextSink final : public QWidget {
public:
    explicit TextSink(const QString& logFile):log_(logFile) {
        setWindowTitle(QStringLiteral("xmb text-entry sink"));
        setWindowFlag(Qt::WindowStaysOnTopHint,true);
        setFocusPolicy(Qt::StrongFocus);
        resize(520,260);
        clock_.start();
        if(!log_.open(QIODevice::WriteOnly|QIODevice::Truncate))qWarning()<<"zf text sink: cannot open the log:"<<log_.errorString();
    }
    void write(QJsonObject o) {
        o.insert("ms",double(clock_.elapsed()));
        log_.write(QJsonDocument(o).toJson(QJsonDocument::Compact)+"\n");
        log_.flush();
    }
    void finish(const char* kind="done") {
        QJsonArray units;
        for(const QChar c:received_)units.append(int(c.unicode()));
        write({{"e",kind},{"received",received_},{"units",units},{"keyDowns",downs_},{"keyUps",ups_}});
    }
protected:
    bool nativeEvent(const QByteArray& type,void* message,qintptr* result) override {
        Q_UNUSED(type);Q_UNUSED(result);
        const MSG* msg=static_cast<const MSG*>(message);
        if(msg->message==WM_KEYDOWN && msg->wParam==VK_PACKET)++downs_;
        else if(msg->message==WM_KEYUP && msg->wParam==VK_PACKET)++ups_;
        else if(msg->message==WM_CHAR)received_+=QChar(ushort(msg->wParam));
        return false;
    }
    bool focusNextPrevChild(bool) override { return false; }
private:
    QFile log_;
    QElapsedTimer clock_;
    QString received_;
    int downs_=0,ups_=0;
};
}

int runTextSink(const QString& logFile,int seconds) {
    TextSink window(logFile);
    window.move(QGuiApplication::primaryScreen()->availableGeometry().topLeft()+QPoint(140,140));
    window.show();window.raise();window.activateWindow();
    bringWindowForward(&window,&window);
    const auto ready=[&]{
        window.write({{"e","ready"},{"pid",double(GetCurrentProcessId())},{"hwnd",double(window.winId())},
            {"foreground",GetForegroundWindow()==reinterpret_cast<HWND>(window.winId())}});
    };
    QTimer wait;wait.setInterval(100);
    int tries=0;
    QObject::connect(&wait,&QTimer::timeout,&window,[&]{
        const bool front=GetForegroundWindow()==reinterpret_cast<HWND>(window.winId());
        if(front || ++tries>=30){wait.stop();ready();}
        else bringWindowForward(&window,&window);
    });
    wait.start();
    // A snapshot twice a second, so a run whose process is stopped from outside still leaves what was received.
    QTimer snapshot;snapshot.setInterval(500);
    QObject::connect(&snapshot,&QTimer::timeout,&window,[&]{window.finish("snap");});
    snapshot.start();
    QTimer::singleShot(std::max(1,seconds)*1000,&window,[]{QApplication::quit();});
    const int code=QApplication::exec();
    window.finish();
    return code;
}

int runTextDeliver(const QString& specFile,const QString& output) {
    QFile in(specFile);
    if(!in.open(QIODevice::ReadOnly)){qCritical().noquote()<<"zf-text-deliver: cannot read"<<specFile;return 2;}
    const auto spec=QJsonDocument::fromJson(in.readAll()).object();
    const quintptr hwnd=quintptr(spec.value("hwnd").toDouble());
    const quint32 sinkPid=quint32(spec.value("pid").toDouble());
    // The only window this mode ever acts on is the test sink.
    if(!hwnd || !sinkPid || windowPid(reinterpret_cast<HWND>(hwnd))!=sinkPid){qCritical()<<"zf-text-deliver: hwnd is not a window of the named sink process; refusing";return 3;}
    QFile textFile(spec.value("textFile").toString());
    if(!textFile.open(QIODevice::ReadOnly)){qCritical()<<"zf-text-deliver: cannot read the text file";return 2;}
    QString text=QString::fromUtf8(textFile.readAll());
    if(text.endsWith(QLatin1Char('\n')) && !text.endsWith(QStringLiteral("\n\n")))text.chop(text.endsWith(QStringLiteral("\r\n"))?2:1);
    const quint32 root=quint32(spec.value("trackedRoot").toDouble(double(sinkPid)));
    const bool closeFirst=spec.value("destroyBeforeStart").toBool();
    const Tracked tracked=[root](quint32 pid){return pid==root || pidInTree(pid,root);};
    TextDelivery delivery;
    int verdict=-1,sent=0;
    QEventLoop loop;
    QObject::connect(&delivery,&TextDelivery::finished,&loop,[&](int v,int s){verdict=v;sent=s;loop.quit();});
    QTimer::singleShot(30000,&loop,&QEventLoop::quit);
    const bool foregroundBefore=GetForegroundWindow()==reinterpret_cast<HWND>(hwnd);
    quintptr target=hwnd;
    if(closeFirst)target=quintptr(0x00000002);   // a handle that is not a window
    delivery.start(target,text,tracked);
    if(verdict<0)loop.exec();
    const QJsonObject out{{"verdict",QLatin1String(verdictName(Verdict(verdict)))},{"unitsSent",sent},{"foregroundBeforeStart",foregroundBefore},
        {"foregroundAfter",GetForegroundWindow()==reinterpret_cast<HWND>(hwnd)}};
    if(!writeJson(output,out))return 2;
    return verdict==int(Verdict::Deliver)?0:4;
}

int runElevationCheck(const QString& output,const QStringList& arguments) {
    QJsonObject out;
    out.insert("self",QLatin1String(elevation::name(elevation::selfState())));
    const HWND foreground=GetForegroundWindow();
    out.insert("foreground",QLatin1String(elevation::name(elevation::windowState(quintptr(foreground)))));
    QJsonObject pids;
    for(const QString& a:arguments) {
        if(a==QLatin1String("all")) {
            int counts[3]{0,0,0};
            QJsonArray elevated;
            HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
            if(snap!=INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W e{};e.dwSize=sizeof(e);
                for(BOOL ok=Process32FirstW(snap,&e);ok;ok=Process32NextW(snap,&e)) {
                    if(!e.th32ProcessID)continue;
                    const auto state=elevation::processState(e.th32ProcessID);
                    ++counts[int(state)];
                    if(state==elevation::State::Elevated && elevated.size()<5)elevated.append(int(e.th32ProcessID));
                }
                CloseHandle(snap);
            }
            out.insert("all",QJsonObject{{"notElevated",counts[0]},{"elevated",counts[1]},{"unknown",counts[2]},{"someElevatedPids",elevated}});
            continue;
        }
        bool ok=false;const uint pid=a.toUInt(&ok);
        if(ok)pids.insert(a,QLatin1String(elevation::name(elevation::processState(pid))));
    }
    out.insert("pids",pids);
    if(!writeJson(output,out))return 2;
    qInfo().noquote()<<QString("zf-elevation-check: self=%1 foreground=%2").arg(QLatin1String(elevation::name(elevation::selfState())),QLatin1String(elevation::name(elevation::windowState(quintptr(foreground)))));
    return 0;
}
#else
int runTextSink(const QString&,int) { return 2; }
int runTextDeliver(const QString&,const QString&) { return 2; }
int runElevationCheck(const QString&,const QStringList&) { return 2; }
#endif
}
