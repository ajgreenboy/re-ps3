#include "host.h"
#include <QGuiApplication>
#include <QHostInfo>
#include <QHostAddress>
#include <QScreen>
#include <QSettings>
#include <QStringList>
#include <QSysInfo>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <initguid.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
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
}

QString systemInformation(const QString& renderer) {
    QString cpu;qulonglong memory=0;
#ifdef Q_OS_WIN
    cpu=QSettings("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",QSettings::NativeFormat).value("ProcessorNameString").toString().trimmed();
    MEMORYSTATUSEX status{};status.dwLength=sizeof(status);
    if(GlobalMemoryStatusEx(&status))memory=status.ullTotalPhys;
#endif
    QStringList lines{"System Information","",
        "System Name: "+QHostInfo::localHostName(),
        "System Software: "+QSysInfo::prettyProductName()+" ("+QSysInfo::currentCpuArchitecture()+")"};
    if(!cpu.isEmpty())lines<<"Processor: "+cpu;
    if(memory)lines<<QString("Memory: %1 GB").arg(double(memory)/(1024.*1024*1024),0,'f',1);
    if(!renderer.isEmpty())lines<<"Graphics: "+renderer;
    lines<<screens();
    return lines.join('\n');
}

QString networkStatus(const Label& label) {
    // The PS3 list's fields (sysconf_plugin msg_connection_method, msg_ipaddress, ...) for each physical adapter
    // that is up; virtual switches (Hyper-V, WSL, VPN taps) are left out like the PS3 has none.
    QStringList blocks;
#ifdef Q_OS_WIN
    ULONG size=16*1024;QByteArray buffer;ULONG result=ERROR_BUFFER_OVERFLOW;
    for(int attempt=0;attempt<3 && result==ERROR_BUFFER_OVERFLOW;++attempt) {
        buffer.resize(int(size));
        result=GetAdaptersAddresses(AF_INET,GAA_FLAG_INCLUDE_GATEWAYS|GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST,nullptr,
                                    reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),&size);
    }
    const auto ipv4=[](const SOCKET_ADDRESS& address){
        if(!address.lpSockaddr || address.lpSockaddr->sa_family!=AF_INET)return QString();
        return QHostAddress(reinterpret_cast<const sockaddr*>(address.lpSockaddr)).toString();
    };
    for(auto* adapter=result==NO_ERROR?reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()):nullptr;adapter;adapter=adapter->Next) {
        const QString description=QString::fromWCharArray(adapter->Description);
        if(adapter->OperStatus!=IfOperStatusUp || (adapter->IfType!=IF_TYPE_ETHERNET_CSMACD && adapter->IfType!=IF_TYPE_IEEE80211) ||
           description.contains("Hyper-V") || description.contains("Virtual") || description.contains("TAP-") || !adapter->FirstUnicastAddress)continue;
        const auto* unicast=adapter->FirstUnicastAddress;
        const quint32 mask=unicast->OnLinkPrefixLength?~quint32(0)<<(32-unicast->OnLinkPrefixLength):0;
        QStringList mac;for(ULONG i=0;i<adapter->PhysicalAddressLength;++i)mac<<QString("%1").arg(adapter->PhysicalAddress[i],2,16,QChar('0')).toUpper();
        QStringList dns;for(auto* server=adapter->FirstDnsServerAddress;server;server=server->Next)if(const auto ip=ipv4(server->Address);!ip.isEmpty())dns<<ip;
        const QString none="-";
        blocks<<QStringList{
            label("msg_connection_method","Connection Method")+": "+(adapter->IfType==IF_TYPE_IEEE80211?label("msg_wless","Wireless"):label("msg_wired","Wired Connection")),
            QString::fromWCharArray(adapter->FriendlyName)+" ("+description+")",
            label("msg_ipaddress","IP Address")+": "+ipv4(unicast->Address),
            label("msg_subnetmask","Subnet Mask")+": "+QHostAddress(mask).toString(),
            label("msg_default_router","Default Router")+": "+(adapter->FirstGatewayAddress?ipv4(adapter->FirstGatewayAddress->Address):none),
            label("msg_primedns","Primary DNS")+": "+dns.value(0,none),
            label("msg_seconddns","Secondary DNS")+": "+dns.value(1,none),
            label("msg_macaddress","MAC Address")+": "+mac.join(':'),
            label("msg_mtu","MTU")+": "+QString::number(adapter->Mtu)}.join('\n');
    }
#else
    Q_UNUSED(label);
#endif
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
}
