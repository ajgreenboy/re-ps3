#include "pad.h"
#include <QDebug>
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
    if(newestAt==std::chrono::steady_clock::time_point{} || std::chrono::steady_clock::now()-newestAt>std::chrono::milliseconds(300))return false;
    out=newest;return true;
}
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
