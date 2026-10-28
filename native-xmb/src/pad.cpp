#include "pad.h"
#include <QDebug>
#include <QStringList>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}
#endif

namespace xmb::pad {
namespace {
constexpr quint16 up=0x1,down=0x2,left=0x4,right=0x8,startButton=0x10,selectButton=0x20,l3=0x40,r3=0x80,l1=0x100,r1=0x200,ps=0x400,
    cross=0x1000,circle=0x2000,square=0x4000,triangle=0x8000;
// Battery field offsets, relative to the payload after the report id (the same `d` pointer the button parser
// uses). DualSense: byte 52 of the payload, low nibble = 0-10 capacity, high nibble = 0 discharging,
// 1 charging, 2 full, 0xa/0xb/0xf error/out-of-range (SDL_hidapi_ps5.c and Linux hid-playstation). DualShock 4:
// byte 29, low nibble = 0-11 level, bit 4 = cable connected (SDL_hidapi_ps4.c; Linux hid-sony).
constexpr size_t kDualSenseBatteryByte=52, kDs4BatteryByte=29;

// ---- Scripted-run input gate (pad.h, audit/PS-BUTTON.md) ----
// The pad keeps being read in a scripted run; it just cannot reach the shell. The reader threads still store
// the newest report (so --pad-report and the battery line keep working), and latest() is the single place
// where a live state is either delivered or withheld.
std::atomic_bool scriptOnlyMode{false};
std::atomic<quint64> ignoredPadEvents{0};

// XMB_INPUT_TRACE=1 logs each input decision. Read once; on unless the value is empty or 0.
bool inputTrace() {
    static const bool on=[]{
        const QString value=qEnvironmentVariable("XMB_INPUT_TRACE");
        return !value.isEmpty() && value!="0";
    }();
    return on;
}

// What View::pollController() makes of a state: the D-pad directions the report's hat carries, the left stick
// past the same 0.5 deadzone pollController maps onto the D-pad, the buttons it acts on, and the triggers it
// ramps the volume with (pad y is +down, as the HID report sends it). Empty means the state could not have
// moved or pressed anything, so a scripted run counts and logs only states that could have steered it.
QString padAction(const State& s) {
    struct Named { quint16 bit; const char* name; };
    static const Named named[]={
        {up,"dpad-up"},{down,"dpad-down"},{left,"dpad-left"},{right,"dpad-right"},
        {cross,"cross"},{circle,"circle"},{square,"square"},{triangle,"triangle"},
        {l1,"l1"},{r1,"r1"},{startButton,"start"},{selectButton,"select"},{l3,"l3"},{r3,"r3"},{ps,"ps"}};
    QStringList parts;
    for(const auto& entry:named)if(s.buttons&entry.bit)parts<<QString::fromLatin1(entry.name);
    if(s.ly<-.5f)parts<<QStringLiteral("stick-up");
    else if(s.ly>.5f)parts<<QStringLiteral("stick-down");
    if(s.lx<-.5f)parts<<QStringLiteral("stick-left");
    else if(s.lx>.5f)parts<<QStringLiteral("stick-right");
    if(s.l2>.5f)parts<<QStringLiteral("l2");
    if(s.r2>.5f)parts<<QStringLiteral("r2");
    return parts.join(QChar('+'));
}

// Bounded by design: a decision is logged when the action changes and otherwise at most once a second, so a
// stick held for a minute is a handful of lines with a running count, not thousands of lines.
struct TraceMark { std::chrono::steady_clock::time_point at{}; QString action; };
TraceMark traceMark;
bool traceDue(const QString& action) {
    const auto now=std::chrono::steady_clock::now();
    if(action==traceMark.action && now-traceMark.at<std::chrono::milliseconds(1000))return false;
    traceMark.action=action;traceMark.at=now;return true;
}

// Local diagnostic (unset in a normal run): XMB_PAD_FAKE pretends a controller is being held, so the live path
// and the scripted gate can both be proved without moving hardware. Tokens are comma separated:
// up|down|left|right, cross|circle|square|triangle|l1|r1|start|select|l3|r3|ps, and lx=|ly=|rx=|ry=|l2=|r2=
// with floating-point values (pad sign: +ly is down). It is sampled exactly like a real controller, so a
// scripted run ignores it too.
struct Fake { bool active=false; State state; };
Fake parseFake(const QString& spec) {
    Fake fake;
    if(spec.trimmed().isEmpty())return fake;
    fake.active=true;
    const auto number=[&](const QString& token,int at){
        bool ok=false;const float value=token.mid(at).toFloat(&ok);
        if(!ok)qWarning().noquote()<<"input: XMB_PAD_FAKE:"<<token<<"is not a number";
        return ok?value:0.f;
    };
    for(const QString& raw:spec.split(',')) {
        const QString token=raw.trimmed().toLower();
        if(token.isEmpty())continue;
        if(token=="up")fake.state.buttons|=up;
        else if(token=="down")fake.state.buttons|=down;
        else if(token=="left")fake.state.buttons|=left;
        else if(token=="right")fake.state.buttons|=right;
        else if(token=="cross")fake.state.buttons|=cross;
        else if(token=="circle")fake.state.buttons|=circle;
        else if(token=="square")fake.state.buttons|=square;
        else if(token=="triangle")fake.state.buttons|=triangle;
        else if(token=="l1")fake.state.buttons|=l1;
        else if(token=="r1")fake.state.buttons|=r1;
        else if(token=="start")fake.state.buttons|=startButton;
        else if(token=="select")fake.state.buttons|=selectButton;
        else if(token=="l3")fake.state.buttons|=l3;
        else if(token=="r3")fake.state.buttons|=r3;
        else if(token=="ps")fake.state.buttons|=ps;
        else if(token.startsWith("lx="))fake.state.lx=number(token,3);
        else if(token.startsWith("ly="))fake.state.ly=number(token,3);
        else if(token.startsWith("rx="))fake.state.rx=number(token,3);
        else if(token.startsWith("ry="))fake.state.ry=number(token,3);
        else if(token.startsWith("l2="))fake.state.l2=number(token,3);
        else if(token.startsWith("r2="))fake.state.r2=number(token,3);
        else qWarning().noquote()<<"input: XMB_PAD_FAKE: unknown token"<<raw;
    }
    qInfo().noquote()<<QString("input: diagnostic XMB_PAD_FAKE=%1 -> buttons=0x%2 lx=%3 ly=%4 rx=%5 ry=%6 l2=%7 r2=%8")
        .arg(spec).arg(fake.state.buttons,0,16).arg(fake.state.lx).arg(fake.state.ly).arg(fake.state.rx)
        .arg(fake.state.ry).arg(fake.state.l2).arg(fake.state.r2);
    return fake;
}
const Fake& fakeState() {
    static const Fake fake=parseFake(qEnvironmentVariable("XMB_PAD_FAKE"));
    return fake;
}
// The PS bit as last observed in a scripted run, so a press and a release are each logged once.
bool lastPsSeen=false;

std::mutex mutex;
State newest;
Info newestInfo;
std::chrono::steady_clock::time_point newestAt{};

float stick(quint8 v){return std::clamp((float(v)-128.f)/127.f,-1.f,1.f);}
quint16 hat(quint8 v) {
    static const quint16 directions[8]={up,quint16(up|right),right,quint16(right|down),down,quint16(down|left),left,quint16(left|up)};
    return (v&0x0f)<8?directions[v&0x0f]:0;
}
struct Bit { int byte; quint8 mask; quint16 button; };
void apply(const quint8* d,std::initializer_list<Bit> bits,State& s){for(const auto& b:bits)if(d[b.byte]&b.mask)s.buttons|=b.button;}

// One input report (report id in byte 0, as Windows returns it). `info` carries what the same report says about
// the controller and its battery; a byte the layout does not reach is never read, and a field the layout does not
// define (DualShock 3 has no battery in its input report) stays -1/empty rather than being invented.
bool parse(Kind kind,const std::vector<quint8>& r,State& s,Info& info) {
    s=State();info=Info();info.kind=kind;
    switch(kind) {
    case Kind::Ds3: {
        if(r.size()<20 || r[0]!=0x01)return false;
        info.transport=Transport::Usb;
        apply(r.data(),{{2,0x01,selectButton},{2,0x02,l3},{2,0x04,r3},{2,0x08,startButton},{2,0x10,up},{2,0x20,right},{2,0x40,down},{2,0x80,left},
            {3,0x04,l1},{3,0x08,r1},{3,0x10,triangle},{3,0x20,circle},{3,0x40,cross},{3,0x80,square},{4,0x01,ps}},s);
        s.lx=stick(r[6]);s.ly=stick(r[7]);s.rx=stick(r[8]);s.ry=stick(r[9]);s.l2=r[18]/255.f;s.r2=r[19]/255.f;
        return true;   // DS3 reports capacity in its 0xF2 status feature report, which this build does not read
    }
    case Kind::Ds4: { // USB report 0x01, Bluetooth 0x11 (two more bytes in front)
        const size_t base=r.empty()?0:r[0]==0x01?1:r[0]==0x11?3:0;
        if(!base || r.size()<base+9)return false;
        info.transport=base==1?Transport::Usb:Transport::Bluetooth;
        const quint8* d=r.data()+base;
        s.lx=stick(d[0]);s.ly=stick(d[1]);s.rx=stick(d[2]);s.ry=stick(d[3]);s.buttons|=hat(d[4]);
        apply(d,{{4,0x10,square},{4,0x20,cross},{4,0x40,circle},{4,0x80,triangle},{5,0x01,l1},{5,0x02,r1},
            {5,0x10,selectButton},{5,0x20,startButton},{5,0x40,l3},{5,0x80,r3},{6,0x01,ps}},s);
        s.l2=d[7]/255.f;s.r2=d[8]/255.f;
        if(r.size()>=base+kDs4BatteryByte+1) {
            const quint8 battery=d[kDs4BatteryByte];
            const int level=battery&0x0f;
            if(battery&0x10) { // cabled: 0-10 charging, 11 charge complete
                if(level>=11){info.batteryPercent=100;info.charging=QStringLiteral("full");}
                else {info.batteryPercent=std::min(level*10+5,100);info.charging=QStringLiteral("charging");}
            } else {
                info.batteryPercent=std::min(level*10+5,100);
                info.charging=QStringLiteral("discharging");
            }
        }
        return true;
    }
    case Kind::DualSense: { // USB report 0x01, Bluetooth 0x31 (one more byte in front)
        const size_t base=r.empty()?0:r[0]==0x01?1:r[0]==0x31?2:0;
        if(!base || r.size()<base+10)return false;
        info.transport=base==1?Transport::Usb:Transport::Bluetooth;
        const quint8* d=r.data()+base;
        s.lx=stick(d[0]);s.ly=stick(d[1]);s.rx=stick(d[2]);s.ry=stick(d[3]);s.l2=d[4]/255.f;s.r2=d[5]/255.f;s.buttons|=hat(d[7]);
        apply(d,{{7,0x10,square},{7,0x20,cross},{7,0x40,circle},{7,0x80,triangle},{8,0x01,l1},{8,0x02,r1},
            {8,0x10,selectButton},{8,0x20,startButton},{8,0x40,l3},{8,0x80,r3},{9,0x01,ps}},s);
        if(r.size()>=base+kDualSenseBatteryByte+1) {
            const quint8 status=d[kDualSenseBatteryByte];
            const int level=status&0x0f,charge=(status>>4)&0x0f;
            switch(charge) {
            case 0x0:info.batteryPercent=std::min(level*10+5,100);info.charging=QStringLiteral("discharging");break;
            case 0x1:info.batteryPercent=std::min(level*10+5,100);info.charging=QStringLiteral("charging");break;
            case 0x2:info.batteryPercent=100;info.charging=QStringLiteral("full");break;
            case 0xb:info.batteryPercent=0;info.charging=QStringLiteral("not-charging");break; // temperature error
            default:info.batteryPercent=-1;info.charging=QStringLiteral("unknown");break;       // 0xa or 0xf
            }
        }
        return true;
    }
    case Kind::None:break;
    }
    return false;
}

#ifdef Q_OS_WIN
bool kindOf(USHORT vendor,USHORT product,Kind& kind) {
    if(vendor!=0x054c)return false;
    switch(product) {
    case 0x0268:kind=Kind::Ds3;return true;
    case 0x05c4:case 0x09cc:case 0x0ba0:kind=Kind::Ds4;return true;
    case 0x0ce6:case 0x0df2:kind=Kind::DualSense;return true;
    default:return false;
    }
}
std::vector<std::pair<std::wstring,Kind>> enumerate() {
    std::vector<std::pair<std::wstring,Kind>> out;
    GUID guid;HidD_GetHidGuid(&guid);
    const HDEVINFO set=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(set==INVALID_HANDLE_VALUE)return out;
    SP_DEVICE_INTERFACE_DATA data{};data.cbSize=sizeof(data);
    for(DWORD i=0;SetupDiEnumDeviceInterfaces(set,nullptr,&guid,i,&data);++i) {
        DWORD need=0;SetupDiGetDeviceInterfaceDetailW(set,&data,nullptr,0,&need,nullptr);
        if(need<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))continue;
        std::vector<quint8> buffer(need);
        auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());detail->cbSize=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if(!SetupDiGetDeviceInterfaceDetailW(set,&data,detail,need,nullptr,nullptr))continue;
        // The attribute query needs no access rights.
        const HANDLE handle=CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if(handle==INVALID_HANDLE_VALUE)continue;
        HIDD_ATTRIBUTES attributes{};attributes.Size=sizeof(attributes);
        Kind kind;
        if(HidD_GetAttributes(handle,&attributes) && kindOf(attributes.VendorID,attributes.ProductID,kind))out.emplace_back(detail->DevicePath,kind);
        CloseHandle(handle);
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}
void read(std::wstring path,Kind kind,std::mutex* openMutex,std::set<std::wstring>* open) {
    const HANDLE handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    if(handle!=INVALID_HANDLE_VALUE) {
        size_t length=64;
        PHIDP_PREPARSED_DATA preparsed=nullptr;
        if(HidD_GetPreparsedData(handle,&preparsed)) {
            HIDP_CAPS caps{};
            if(HidP_GetCaps(preparsed,&caps)==HIDP_STATUS_SUCCESS && caps.InputReportByteLength>0)length=caps.InputReportByteLength;
            HidD_FreePreparsedData(preparsed);
        }
        if(kind==Kind::Ds3) { // reading feature report 0xF2 starts the input reports (0xF4 on some setups)
            quint8 feature[64]{0xf2};HidD_GetFeature(handle,feature,18);
            quint8 enable[5]{0xf4,0x42,0x0c,0x00,0x00};HidD_SetFeature(handle,enable,sizeof(enable));
        } else { // over Bluetooth, reading the calibration report switches to the full report
            quint8 calibration[80]{0x05};HidD_GetFeature(handle,calibration,41);
            quint8 version[80]{0x02};HidD_GetFeature(handle,version,37);
        }
        std::vector<quint8> report(std::max<size_t>(length,10));
        for(;;) {
            DWORD got=0;
            if(!ReadFile(handle,report.data(),DWORD(report.size()),&got,nullptr) || got==0)break;
            State s;
            Info info;
            if(!parse(kind,std::vector<quint8>(report.begin(),report.begin()+got),s,info))continue;
            // One line when the attached pad's own report changes what the status page shows, so a physical
            // controller proves its kind, transport and battery in the log without polling. The newest report
            // wins, exactly as latest() already behaves with more than one controller attached.
            QString line;
            {
                const std::lock_guard lock(mutex);
                if(info.kind!=newestInfo.kind || info.transport!=newestInfo.transport ||
                   info.batteryPercent!=newestInfo.batteryPercent || info.charging!=newestInfo.charging) {
                    const QString battery=info.batteryPercent<0?QStringLiteral("not reported")
                        :QString::number(info.batteryPercent)+"%"+(info.charging.isEmpty()?QString():QString(" (")+info.charging+")");
                    line=QString("pad: %1 over %2, battery %3").arg(kindName(info.kind),transportName(info.transport),battery);
                }
                newest=s;newestInfo=info;newestAt=std::chrono::steady_clock::now();
            }
            if(!line.isEmpty())qInfo().noquote()<<line;
        }
        CloseHandle(handle);
    }
    const std::lock_guard lock(*openMutex);open->erase(path);
}
#endif
}

void start() {
#ifdef Q_OS_WIN
    static std::atomic_bool started=false;
    if(started.exchange(true))return;
    std::thread([]{
        static std::mutex openMutex;static std::set<std::wstring> open;
        for(;;) {
            for(auto& [path,kind]:enumerate()) {
                {const std::lock_guard lock(openMutex);if(!open.insert(path).second)continue;}
                std::thread(read,path,kind,&openMutex,&open).detach();
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }).detach();
#endif
}
bool latest(State& out) {
    const std::lock_guard lock(mutex);
    // XMB_PAD_FAKE stands in for a controller, so the live path and the scripted gate can be proved with no
    // hardware movement. It is a diagnostic: unset in every normal run.
    const Fake& fake=fakeState();
    State sample;
    if(fake.active)sample=fake.state;
    else {
        if(newestAt==std::chrono::steady_clock::time_point{} || std::chrono::steady_clock::now()-newestAt>std::chrono::milliseconds(300))return false;
        sample=newest;
    }
    const QString action=padAction(sample);
    if(scriptOnlyMode.load()) {
        // PS is observed and logged - a press and its release, once each - but it is withheld with everything
        // else: PsWatcher consumes the bit to open the system menu, which is navigation. A scripted run drives
        // the watcher only through View::replayPsTap()/replayPsHold(), i.e. through its own ps/pshold tokens.
        const bool psNow=(sample.buttons&ps)!=0;
        if(psNow!=lastPsSeen) {
            qInfo().noquote()<<(psNow?QStringLiteral("input: PS press observed on the pad; ignored (scripted run)")
                                    :QStringLiteral("input: PS release observed on the pad; ignored (scripted run)"));
            lastPsSeen=psNow;
        }
        if(!action.isEmpty()) {
            const quint64 count=ignoredPadEvents.fetch_add(1)+1;
            if(inputTrace() && traceDue(action))
                qInfo().noquote()<<QString("input: trace source=pad action=%1 accepted=0 ignored=%2 (scripted run)")
                    .arg(action).arg(count);
        }
        return false;
    }
    if(inputTrace() && !action.isEmpty() && traceDue(action))
        qInfo().noquote()<<QString("input: trace source=pad action=%1 accepted=1").arg(action);
    out=sample;return true;
}
void setScriptOnly(bool on) {
    if(scriptOnlyMode.exchange(on)==on)return;
    if(on)qInfo().noquote()<<QStringLiteral("input: scripted run; live pad events ignored (XMB_INPUT_SCRIPT_ONLY)");
    else qInfo().noquote()<<QStringLiteral("input: live pad events accepted (no --script, XMB_INPUT_SCRIPT_ONLY unset)");
}
bool scriptOnly() { return scriptOnlyMode.load(); }
quint64 ignoredEvents() { return ignoredPadEvents.load(); }
bool info(Info& out) {
    const std::lock_guard lock(mutex);
    if(newestAt==std::chrono::steady_clock::time_point{} || std::chrono::steady_clock::now()-newestAt>std::chrono::milliseconds(300))return false;
    out=newestInfo;return true;
}
bool parseReport(Kind kind,const QVector<quint8>& report,Info& out) {
    State state;
    const std::vector<quint8> bytes(report.cbegin(),report.cend());
    return parse(kind,bytes,state,out);
}
QString kindName(Kind kind) {
    switch(kind) {
    case Kind::Ds3:return QStringLiteral("DualShock 3");
    case Kind::Ds4:return QStringLiteral("DualShock 4");
    case Kind::DualSense:return QStringLiteral("DualSense");
    case Kind::None:break;
    }
    return QStringLiteral("None");
}
QString transportName(Transport kind) {
    switch(kind) {
    case Transport::Usb:return QStringLiteral("USB");
    case Transport::Bluetooth:return QStringLiteral("Bluetooth");
    case Transport::Unknown:break;
    }
    return QStringLiteral("Unknown");
}
}
