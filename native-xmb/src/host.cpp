#include "host.h"
#include <QGuiApplication>
#include <QHostInfo>
#include <QHostAddress>
#include <QScreen>
#include <QSettings>
#include <QStorageInfo>
#include <QVector>
#include <QStringList>
#include <QSysInfo>
#include <QDebug>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <powrprof.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <initguid.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#pragma comment(lib,"powrprof.lib")
#endif

namespace xmb::host {
namespace {
#ifdef Q_OS_WIN
// Default render endpoint's volume control; COM is initialised per call and released.
template<class F> bool withEndpoint(F&& use) {
    const HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    bool ok=false;
    IMMDeviceEnumerator* enumerator=nullptr;IMMDevice* device=nullptr;IAudioEndpointVolume* volume=nullptr;
    if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&enumerator))) &&
       SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&device)) &&
       SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&volume))))
        ok=use(volume);
    if(volume)volume->Release();if(device)device->Release();if(enumerator)enumerator->Release();
    if(SUCCEEDED(init))CoUninitialize();
    return ok;
}
#endif
QString screens() {
    QStringList lines;
    for(const auto* screen:QGuiApplication::screens()) {
        const QSize pixels=(QSizeF(screen->size())*screen->devicePixelRatio()).toSize();
        lines<<QString("%1: %2 x %3, %4 Hz%5").arg(screen->name()).arg(pixels.width()).arg(pixels.height())
                   .arg(std::lround(screen->refreshRate())).arg(screen==QGuiApplication::primaryScreen()?" (primary)":"");
    }
    return lines.join('\n');
}
// Physical adapters that are up, in enumeration order; virtual switches (Hyper-V, WSL, VPN taps) are left out
// like the PS3 has none.
struct Adapter { bool wireless=false; QString name,ip,mask,router,mac; QStringList dns; ulong mtu=0; };
QVector<Adapter> adapters() {
    QVector<Adapter> result;
#ifdef Q_OS_WIN
    ULONG size=16*1024;QByteArray buffer;ULONG status=ERROR_BUFFER_OVERFLOW;
    for(int attempt=0;attempt<3 && status==ERROR_BUFFER_OVERFLOW;++attempt) {
        buffer.resize(int(size));
        status=GetAdaptersAddresses(AF_INET,GAA_FLAG_INCLUDE_GATEWAYS|GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST,nullptr,
                                    reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),&size);
    }
    const auto ipv4=[](const SOCKET_ADDRESS& address){
        if(!address.lpSockaddr || address.lpSockaddr->sa_family!=AF_INET)return QString();
        return QHostAddress(reinterpret_cast<const sockaddr*>(address.lpSockaddr)).toString();
    };
    for(auto* adapter=status==NO_ERROR?reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()):nullptr;adapter;adapter=adapter->Next) {
        const QString description=QString::fromWCharArray(adapter->Description);
        if(adapter->OperStatus!=IfOperStatusUp || (adapter->IfType!=IF_TYPE_ETHERNET_CSMACD && adapter->IfType!=IF_TYPE_IEEE80211) ||
           description.contains("Hyper-V") || description.contains("Virtual") || description.contains("TAP-") || !adapter->FirstUnicastAddress)continue;
        Adapter entry;const auto* unicast=adapter->FirstUnicastAddress;
        entry.wireless=adapter->IfType==IF_TYPE_IEEE80211;
        entry.name=QString::fromWCharArray(adapter->FriendlyName)+" ("+description+")";
        entry.ip=ipv4(unicast->Address);
        entry.mask=QHostAddress(unicast->OnLinkPrefixLength?~quint32(0)<<(32-unicast->OnLinkPrefixLength):0).toString();
        if(adapter->FirstGatewayAddress)entry.router=ipv4(adapter->FirstGatewayAddress->Address);
        QStringList mac;for(ULONG i=0;i<adapter->PhysicalAddressLength;++i)mac<<QString("%1").arg(adapter->PhysicalAddress[i],2,16,QChar('0')).toUpper();
        entry.mac=mac.join(':');
        for(auto* server=adapter->FirstDnsServerAddress;server;server=server->Next)if(const auto ip=ipv4(server->Address);!ip.isEmpty())entry.dns<<ip;
        entry.mtu=adapter->Mtu;
        result.push_back(entry);
    }
#endif
    return result;
}
}

QString systemInformation(const QString& renderer,const Label& label,const QString& systemName) {
    // The PS3 page lists System Software, IP Address, MAC Address and Free Space (firmware labels); the host's
    // processor, memory, graphics and displays follow.
    QString cpu;qulonglong memory=0;
#ifdef Q_OS_WIN
    cpu=QSettings("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",QSettings::NativeFormat).value("ProcessorNameString").toString().trimmed();
    MEMORYSTATUSEX status{};status.dwLength=sizeof(status);
    if(GlobalMemoryStatusEx(&status))memory=status.ullTotalPhys;
#endif
    const auto network=adapters();const QString none="-";
    const QStorageInfo disk=QStorageInfo::root();
    const auto gb=[](qint64 bytes){return QString::number(std::llround(double(bytes)/1e9))+" GB";};
    QStringList lines{label("msg_console_info","System Information"),"",
        // The name the user set in Settings > System Settings wins over the Windows host name.\n        label("msg_ps3_name","System Name")+": "+(systemName.trimmed().isEmpty()?QHostInfo::localHostName():systemName.trimmed()),
        label("msg_system_software","System Software")+": "+QSysInfo::prettyProductName()+" ("+QSysInfo::currentCpuArchitecture()+")",
        label("msg_ipaddress","IP Address")+": "+(network.isEmpty()?none:network[0].ip),
        label("msg_macaddress","MAC Address")+": "+(network.isEmpty()?none:network[0].mac),
        label("msg_space","Free Space")+": "+(disk.isValid()?gb(disk.bytesAvailable())+" / "+gb(disk.bytesTotal()):none)};
    if(!cpu.isEmpty())lines<<"Processor: "+cpu;
    if(memory)lines<<QString("Memory: %1 GB").arg(double(memory)/(1024.*1024*1024),0,'f',1);
    if(!renderer.isEmpty())lines<<"Graphics: "+renderer;
    lines<<screens();
    return lines.join('\n');
}

QString networkStatus(const Label& label) {
    QStringList blocks;const QString none="-";
    for(const auto& adapter:adapters())blocks<<QStringList{
        label("msg_connection_method","Connection Method")+": "+(adapter.wireless?label("msg_wless","Wireless"):label("msg_wired","Wired Connection")),
        adapter.name,
        label("msg_ipaddress","IP Address")+": "+adapter.ip,
        label("msg_subnetmask","Subnet Mask")+": "+adapter.mask,
        label("msg_default_router","Default Router")+": "+(adapter.router.isEmpty()?none:adapter.router),
        label("msg_primedns","Primary DNS")+": "+adapter.dns.value(0,none),
        label("msg_seconddns","Secondary DNS")+": "+adapter.dns.value(1,none),
        label("msg_macaddress","MAC Address")+": "+adapter.mac,
        label("msg_mtu","MTU")+": "+QString::number(adapter.mtu)}.join('\n');
    return label("msg_netlist_current","Settings and Connection Status List")+"\n\n"+
        (blocks.isEmpty()?QString("No network connection is active."):blocks.join("\n\n"));
}

QString audioOutput() {
    QString name;
#ifdef Q_OS_WIN
    const HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    IMMDeviceEnumerator* enumerator=nullptr;IMMDevice* device=nullptr;IPropertyStore* store=nullptr;
    if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&enumerator))) &&
       SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&device)) && SUCCEEDED(device->OpenPropertyStore(STGM_READ,&store))) {
        PROPVARIANT value;PropVariantInit(&value);
        if(SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName,&value)) && value.vt==VT_LPWSTR)name=QString::fromWCharArray(value.pwszVal);
        PropVariantClear(&value);
    }
    if(store)store->Release();if(device)device->Release();if(enumerator)enumerator->Release();
    if(SUCCEEDED(init))CoUninitialize();
#endif
    const int volume=masterVolume();
    return "Audio Output\n\n"+(name.isEmpty()?QString("No playback device is available."):name)+
        (volume<0?QString():QString("\nVolume: %1%%2").arg(volume).arg(masterMuted()?" (muted)":""));
}

QString displayOutput(){return "Video Output\n\n"+screens();}

int masterVolume() {
#ifdef Q_OS_WIN
    float level=-1;
    if(withEndpoint([&](IAudioEndpointVolume* v){return SUCCEEDED(v->GetMasterVolumeLevelScalar(&level));}))return int(std::lround(level*100));
#endif
    return -1;
}
bool setMasterVolume(int percent) {
#ifdef Q_OS_WIN
    const float level=float(std::clamp(percent,0,100))/100.f;
    return withEndpoint([&](IAudioEndpointVolume* v){return SUCCEEDED(v->SetMasterVolumeLevelScalar(level,nullptr));});
#else
    Q_UNUSED(percent);return false;
#endif
}
bool masterMuted() {
#ifdef Q_OS_WIN
    BOOL muted=FALSE;withEndpoint([&](IAudioEndpointVolume* v){return SUCCEEDED(v->GetMute(&muted));});return muted;
#else
    return false;
#endif
}
bool setMasterMuted(bool muted) {
#ifdef Q_OS_WIN
    return withEndpoint([&](IAudioEndpointVolume* v){return SUCCEEDED(v->SetMute(muted,nullptr));});
#else
    Q_UNUSED(muted);return false;
#endif
}
// ES_CONTINUOUS makes the request persist until it is cleared; ES_DISPLAY_REQUIRED additionally keeps the
// screen on, which is what a two-hour film needs. SetThreadExecutionState answers with the flags that were in
// effect before the call, so a release reporting ES_SYSTEM_REQUIRED|ES_DISPLAY_REQUIRED proves the hold was live.
// A zero return is documented as failure but is also what a thread with no outstanding request can report, so
// it is logged rather than treated as fatal: the release line is the reliable evidence either way.
bool holdDisplayAwake(bool hold) {
#ifdef Q_OS_WIN
    static bool held=false;
    if(hold==held)return held;
    const EXECUTION_STATE previous=SetThreadExecutionState(hold?(ES_CONTINUOUS|ES_SYSTEM_REQUIRED|ES_DISPLAY_REQUIRED):ES_CONTINUOUS);
    held=hold;
    if(previous)qInfo().noquote()<<QString("session: display-awake %1 (previous 0x%2)")
        .arg(hold?"held":"released").arg(quint32(previous),8,16,QChar('0'));
    else qWarning().noquote()<<"session: display-awake"<<(hold?"held":"released")<<"(previous state unreported)";
    return held;
#else
    Q_UNUSED(hold);return false;
#endif
}
// ---- Auto-Off -------------------------------------------------------------------------------------
// See host.h and audit/SESSION-POWER.md. The request is a window message, not a power-plan write: nothing is
// persisted, no privilege is used, and the first key press or controller button the OS sees wakes the panel.
namespace {
#ifdef Q_OS_WIN
// GUID_CONSOLE_DISPLAY_STATE {6FE69556-704A-47A0-8F24-C28D936FDA47}. Defined here rather than using the SDK
// symbol because this file's initguid.h comes after <windows.h>, so the SDK DEFINE_GUID expands to a bare
// extern declaration; a local constant avoids depending on uuid.lib.
constexpr GUID kConsoleDisplayState{0x6fe69556,0x704a,0x47a0,{0x8f,0x24,0xc2,0x8d,0x93,0x6f,0xda,0x47}};
std::atomic_bool displayOffActive{false};
std::atomic<int> displayStateValue{-1};      // -1 = nothing observed yet
std::atomic<qint64> lastDisplayOffRequestMs{0};
std::atomic<int> displayWatch{0};            // 0 not tried, 1 registered, -1 unavailable
// The system delivers a power-setting change on its own thread. 0 off, 1 on, 2 dimmed.
ULONG CALLBACK consoleDisplayStateChanged(PVOID,ULONG type,PVOID setting) {
    if(type!=PBT_POWERSETTINGCHANGE || !setting)return ERROR_SUCCESS;
    const auto* change=static_cast<const POWERBROADCAST_SETTING*>(setting);
    if(change->DataLength!=sizeof(DWORD))return ERROR_SUCCESS;
    DWORD state=0;std::memcpy(&state,change->Data,sizeof(state));
    displayStateValue.store(int(state));
    qInfo().noquote()<<QString("power: console display state is now %1 (%2)")
        .arg(state).arg(state==0?"off":state==1?"on":state==2?"dimmed":"other");
    // The panel came back: the Auto-Off request no longer stands, so the next idle period may ask again.
    if((state==1 || state==2) && displayOffActive.exchange(false))
        qInfo()<<"power: Auto-Off re-armed after the display came back";
    return ERROR_SUCCESS;
}
#endif
}

void observeDisplayState() {
#ifdef Q_OS_WIN
    static std::once_flag once;
    std::call_once(once,[]{
        static DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS parameters{consoleDisplayStateChanged,nullptr};
        static HPOWERNOTIFY handle=nullptr;
        const DWORD result=PowerSettingRegisterNotification(&kConsoleDisplayState,DEVICE_NOTIFY_CALLBACK,&parameters,&handle);
        if(result==ERROR_SUCCESS) {
            displayWatch.store(1);
            qInfo()<<"power: observing the console display state (GUID_CONSOLE_DISPLAY_STATE)";
        } else {
            displayWatch.store(-1);
            qWarning().noquote()<<QString("power: display state observation unavailable (PowerSettingRegisterNotification error %1)").arg(result);
        }
    });
#endif
}

int displayState() {
#ifdef Q_OS_WIN
    return displayStateValue.load();
#else
    return -1;
#endif
}

bool displayOffRequested() {
#ifdef Q_OS_WIN
    return displayOffActive.load();
#else
    return false;
#endif
}

void noteUserActivity() {
#ifdef Q_OS_WIN
    if(displayOffActive.exchange(false))qInfo()<<"power: user activity after Auto-Off; the next idle period may request it again";
#endif
}

bool requestDisplayOff() {
#ifdef Q_OS_WIN
    observeDisplayState();
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    // One broadcast per idle period. The window also lets an ignored request be retried on a later idle
    // period even when Windows never reports the panel back on (an RDP or virtual display, for instance).
    if(displayOffActive.load() && now-lastDisplayOffRequestMs.load()<60000)return true;
    const bool first=!displayOffActive.exchange(true);
    lastDisplayOffRequestMs.store(now);
    // An outstanding display-awake hold is a contradiction: drop it before asking. ES_CONTINUOUS alone keeps
    // the system's own power plan in charge (nothing is persisted, no setting is written).
    const EXECUTION_STATE previous=SetThreadExecutionState(ES_CONTINUOUS);
    qInfo().noquote()<<QString("power: display-off requested (%1; execution state 0x%2 cleared)")
        .arg(first?"Auto-Off":"Auto-Off repeat").arg(quint32(previous),8,16,QChar('0'));
    DWORD_PTR reply=0;
    const LRESULT delivered=SendMessageTimeoutW(HWND_BROADCAST,WM_SYSCOMMAND,SC_MONITORPOWER,2,
                                                SMTO_ABORTIFHUNG|SMTO_BLOCK,1000,&reply);
    if(delivered)
        qInfo().noquote()<<QString("power: SC_MONITORPOWER broadcast delivered (reply 0x%1)").arg(quintptr(reply),1,16,QChar('0'));
    else
        qWarning().noquote()<<QString("power: SC_MONITORPOWER broadcast did not complete (last error %1); the request may be ignored")
            .arg(GetLastError());
    // The capture cannot prove the panel's power state, so the only evidence claimed is what the OS reports:
    // a GUID_CONSOLE_DISPLAY_STATE notification after the request. A 6 s window is longer than a consumer
    // display's own power-down response and short enough to belong to this test.
    std::thread([first]{
        std::this_thread::sleep_for(std::chrono::seconds(6));
        const int state=displayState();
        if(state<0)
            qWarning()<<"power: read-back 6 s after the display-off request: display state never reported (no monitor, no notification, or an ignored request)";
        else if(state==0)
            qInfo()<<"power: read-back 6 s after the display-off request: display state off";
        else
            qWarning().noquote()<<QString("power: read-back 6 s after the display-off request: display state %1 (on/other: the request did not power the panel off)")
                .arg(state);
        if(first)qInfo()<<"power: displayOffRequested stays true until activity or a display-on event";
    }).detach();
    return true;
#else
    return false;
#endif
}
}
