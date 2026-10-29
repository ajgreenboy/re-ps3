#include "host.h"
#include "pad.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QHostInfo>
#include <QHostAddress>
#include <QScreen>
#include <QSettings>
#include <QStorageInfo>
#include <QVector>
#include <QStringList>
#include <QSysInfo>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDateTime>
#include <QLocale>
#include <QTimeZone>
#include <QThread>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QTcpSocket>
#include <QDebug>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cwchar>
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
#include <audioclient.h>
#include <mmreg.h>
#include <sddl.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <winspool.h>
#include <wlanapi.h>
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
QString regString(const QString& path,const QString& key) {
    return QSettings(path,QSettings::NativeFormat).value(key).toString().trimmed();
}
QByteArray regBinary(const QString& path,const QString& key) {
    return QSettings(path,QSettings::NativeFormat).value(key).toByteArray();
}
QString wide(const wchar_t* text) { return text?QString::fromWCharArray(text):QString{}; }
#endif
QString bytesText(qint64 value) {
    if(value<0)return {};
    if(value<1024)return QString::number(value)+QStringLiteral(" bytes");
    const double gib=1024.*1024*1024;
    if(value>=qint64(gib))return QString::number(double(value)/gib,'f',value>=qint64(gib*100)?0:1)+" GB";
    if(value>=qint64(1024.*1024))return QString::number(double(value)/(1024.*1024),'f',0)+" MB";
    return QString::number(double(value)/1024.,'f',0)+" KB";
}
QString kilohertz(int hz) {
    if(hz<=0)return {};
    if(hz%1000==0)return QString::number(hz/1000)+" kHz";
    return QString::number(double(hz)/1000.,'f',1)+" kHz";
}
QString linkSpeedText(qint64 bitsPerSecond) {
    if(bitsPerSecond<=0)return {};
    if(bitsPerSecond>=1000000000LL)return QString::number(double(bitsPerSecond)/1e9,'f',bitsPerSecond%1000000000LL?1:0)+" Gb/s";
    if(bitsPerSecond>=1000000LL)return QString::number(double(bitsPerSecond)/1e6,'f',0)+" Mb/s";
    return QString::number(double(bitsPerSecond)/1e3,'f',0)+" kb/s";
}
QString subnetMask(int prefixLength) {
    if(prefixLength<=0)return {};
    const quint32 mask=prefixLength>=32?0xffffffffu:~quint32(0)<<(32-prefixLength);
    return QHostAddress(mask).toString();
}
QString clockOffsetText(int seconds) {
    const int sign=seconds<0?-1:1;
    const int absolute=std::abs(seconds);
    return QStringLiteral("GMT%1%2:%3").arg(sign<0?'-':'+').arg(absolute/3600,2,10,QChar('0'))
        .arg((absolute%3600)/60,2,10,QChar('0'));
}
}  // namespace

// ==== elevation ===================================================================================
bool elevated() {
#ifdef Q_OS_WIN
    static const bool value=[]{
        HANDLE token=nullptr;
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return false;
        TOKEN_ELEVATION state{};DWORD size=0;bool result=false;
        if(GetTokenInformation(token,TokenElevation,&state,sizeof(state),&size))result=state.TokenIsElevated!=0;
        CloseHandle(token);
        return result;
    }();
    return value;
#else
    return false;
#endif
}
QString elevationText() {
    // The token check is the whole answer this build acts on: it never asks for elevation, never touches the
    // manifest and never re-launches itself. A read that needs more than the token holds is reported as such
    // on the row rather than attempted.
    const QString tail=QStringLiteral(": administrator-only reads are reported as such, not attempted");
    if(elevated())return QStringLiteral("elevated (administrator token): administrator reads and writes run normally");
    QString account;
#ifdef Q_OS_WIN
    wchar_t name[257]{};DWORD size=257;
    if(GetUserNameW(name,&size))account=wide(name);
#endif
    return QStringLiteral("not elevated (standard user")+(account.isEmpty()?QString{}:QStringLiteral(" ")+account)+
        QLatin1Char(')')+tail;
}
void logElevation() {
    static std::once_flag once;
    std::call_once(once,[]{qInfo().noquote()<<"settings: elevation:"<<elevationText();});
}

// ==== machine readers (each guarded internally; a missing platform yields "unavailable") ===========
namespace {
QVector<Volume> readVolumes() {
    QVector<Volume> result;
    const auto systemDrive=qEnvironmentVariable("SystemDrive");
    for(const auto& info:QStorageInfo::mountedVolumes()) {
        if(!info.isValid() || !info.isReady() || info.rootPath().isEmpty())continue;
        Volume volume;
        const auto root=QDir::toNativeSeparators(info.rootPath());
        volume.mount=root;
        const auto label=info.displayName().trimmed();
        volume.filesystem=QString::fromLatin1(info.fileSystemType());
        volume.totalBytes=info.bytesTotal();
        volume.freeBytes=info.bytesAvailable();
        volume.readOnly=info.isReadOnly();
#ifdef Q_OS_WIN
        const auto letter=root.left(2);
        volume.system=(letter.compare(systemDrive,Qt::CaseInsensitive)==0);
        const UINT type=GetDriveTypeW(reinterpret_cast<LPCWSTR>(QString(letter+"\\").utf16()));
        volume.removable=(type==DRIVE_REMOVABLE);
        volume.kind=type==DRIVE_FIXED?QStringLiteral("Fixed"):type==DRIVE_REMOVABLE?QStringLiteral("Removable"):
            type==DRIVE_REMOTE?QStringLiteral("Network"):type==DRIVE_CDROM?QStringLiteral("Optical"):
            type==DRIVE_RAMDISK?QStringLiteral("RAM disk"):QStringLiteral("Other");
#else
        Q_UNUSED(systemDrive);
        volume.kind=QStringLiteral("Fixed");
#endif
        if(volume.kind==QLatin1String("Optical"))continue;
        volume.label=(label.isEmpty()?QString{}:label+QLatin1Char(' '))+QLatin1Char('(')+root.left(2)+QLatin1Char(')');
        result.push_back(volume);
    }
    return result;
}
QString opticalDrive() {
#ifdef Q_OS_WIN
    const DWORD length=GetLogicalDriveStringsW(0,nullptr);
    if(!length)return {};
    QVector<wchar_t> buffer(int(length)+2,0);
    if(!GetLogicalDriveStringsW(DWORD(buffer.size()),buffer.data()))return {};
    for(const wchar_t* letter=buffer.constData();*letter;letter+=std::wcslen(letter)+1) {
        const QString root=QString::fromWCharArray(letter);
        if(GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16()))!=DRIVE_CDROM)continue;
        wchar_t label[261]{};
        const bool media=GetVolumeInformationW(reinterpret_cast<LPCWSTR>(root.utf16()),label,DWORD(std::size(label)),
            nullptr,nullptr,nullptr,nullptr,0)!=FALSE;
        return root.left(2)+(media?QStringLiteral(" (")+wide(label)+QLatin1Char(')'):QStringLiteral(" (no disc)"));
    }
#endif
    return {};
}
QVector<AdapterInfo> readAdapters() {
    QVector<AdapterInfo> result;
#ifdef Q_OS_WIN
    ULONG size=32*1024;QByteArray buffer;ULONG status=ERROR_BUFFER_OVERFLOW;
    for(int attempt=0;attempt<3 && status==ERROR_BUFFER_OVERFLOW;++attempt) {
        buffer.resize(int(size));
        status=GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_INCLUDE_GATEWAYS|GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST,
                                    nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),&size);
    }
    if(status!=NO_ERROR)return result;
    const auto hostOf=[](const SOCKET_ADDRESS& address) {
        if(!address.lpSockaddr)return QHostAddress{};
        if(address.lpSockaddr->sa_family!=AF_INET && address.lpSockaddr->sa_family!=AF_INET6)return QHostAddress{};
        return QHostAddress(reinterpret_cast<const sockaddr*>(address.lpSockaddr));
    };
    for(auto* adapter=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());adapter;adapter=adapter->Next) {
        const QString description=wide(adapter->Description);
        if(adapter->IfType!=IF_TYPE_ETHERNET_CSMACD && adapter->IfType!=IF_TYPE_IEEE80211)continue;
        if(description.contains("Hyper-V",Qt::CaseInsensitive) || description.contains("Virtual",Qt::CaseInsensitive) ||
           description.contains("TAP-",Qt::CaseInsensitive))continue;
        AdapterInfo entry;
        entry.wireless=adapter->IfType==IF_TYPE_IEEE80211;
        entry.kind=entry.wireless?QStringLiteral("Wireless"):QStringLiteral("Ethernet");
        entry.description=description;
        entry.name=wide(adapter->FriendlyName);
        if(entry.name.isEmpty())entry.name=description;
        entry.up=adapter->OperStatus==IfOperStatusUp;
        entry.dhcp=(adapter->Flags&IP_ADAPTER_DHCP_ENABLED)!=0;
        entry.linkSpeed=qint64(adapter->TransmitLinkSpeed);
        entry.mtu=int(adapter->Mtu);
        QStringList mac;
        for(ULONG i=0;i<adapter->PhysicalAddressLength;++i)
            mac<<QString("%1").arg(adapter->PhysicalAddress[i],2,16,QChar('0')).toUpper();
        entry.mac=mac.join(QLatin1Char(':'));
        for(auto* unicast=adapter->FirstUnicastAddress;unicast;unicast=unicast->Next) {
            const auto host=hostOf(unicast->Address);
            if(host.isNull() || host.isLoopback())continue;
            if(host.protocol()==QAbstractSocket::IPv4Protocol) {
                entry.ipv4<<host.toString();
                if(entry.ipv4.size()==1 && unicast->OnLinkPrefixLength) {
                    entry.prefixLength=int(unicast->OnLinkPrefixLength);
                    entry.subnet=subnetMask(entry.prefixLength);
                    entry.ipv4.last()+=QStringLiteral(" (")+entry.subnet+QLatin1Char(')');
                }
            } else if(!host.isLinkLocal())entry.ipv6<<host.toString();
        }
        for(auto* gateway=adapter->FirstGatewayAddress;gateway;gateway=gateway->Next)
            if(const auto host=hostOf(gateway->Address);!host.isNull())entry.gateways<<host.toString();
        for(auto* server=adapter->FirstDnsServerAddress;server;server=server->Next)
            if(const auto host=hostOf(server->Address);!host.isNull())entry.dns<<host.toString();
        const QString interfaceKey=QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\")+
            QString::fromLatin1(adapter->AdapterName);
        entry.dhcpServer=regString(interfaceKey,QStringLiteral("DhcpServer"));
        const auto lease=QSettings(interfaceKey,QSettings::NativeFormat).value(QStringLiteral("LeaseObtainedTime")).toLongLong();
        if(lease>0)entry.dhcpLease=QDateTime::fromSecsSinceEpoch(lease).toString(QStringLiteral("yyyy/MM/dd HH:mm"));
        result.push_back(entry);
    }
    // SSID and signal quality come from the WLAN service, loaded at run time so a machine without it still
    // reports its wired adapters.
    if(HMODULE wlan=LoadLibraryW(L"wlanapi.dll")) {
        using OpenHandleFn=DWORD(WINAPI*)(DWORD,PVOID,DWORD*,HANDLE*);
        using CloseHandleFn=DWORD(WINAPI*)(HANDLE);
        using EnumInterfacesFn=DWORD(WINAPI*)(HANDLE,PWLAN_INTERFACE_INFO_LIST*);
        using QueryInterfaceFn=DWORD(WINAPI*)(HANDLE,const GUID*,WLAN_INTF_OPCODE,DWORD*,PVOID*,DWORD*,PWLAN_OPCODE_VALUE_TYPE);
        using FreeMemoryFn=DWORD(WINAPI*)(PVOID);
        auto openHandle=reinterpret_cast<OpenHandleFn>(reinterpret_cast<void*>(GetProcAddress(wlan,"WlanOpenHandle")));
        auto closeHandle=reinterpret_cast<CloseHandleFn>(reinterpret_cast<void*>(GetProcAddress(wlan,"WlanCloseHandle")));
        auto enumInterfaces=reinterpret_cast<EnumInterfacesFn>(reinterpret_cast<void*>(GetProcAddress(wlan,"WlanEnumInterfaces")));
        auto queryInterface=reinterpret_cast<QueryInterfaceFn>(reinterpret_cast<void*>(GetProcAddress(wlan,"WlanQueryInterface")));
        auto freeMemory=reinterpret_cast<FreeMemoryFn>(reinterpret_cast<void*>(GetProcAddress(wlan,"WlanFreeMemory")));
        DWORD negotiated=0;HANDLE client=nullptr;
        if(openHandle && closeHandle && enumInterfaces && queryInterface && freeMemory &&
           openHandle(2,nullptr,&negotiated,&client)==ERROR_SUCCESS && client) {
            PWLAN_INTERFACE_INFO_LIST interfaces=nullptr;
            if(enumInterfaces(client,&interfaces)==ERROR_SUCCESS && interfaces) {
                for(DWORD i=0;i<interfaces->dwNumberOfItems;++i) {
                    const auto& item=interfaces->InterfaceInfo[i];
                    PVOID data=nullptr;DWORD dataSize=0;WLAN_OPCODE_VALUE_TYPE type;
                    if(queryInterface(client,&item.InterfaceGuid,wlan_intf_opcode_current_connection,nullptr,
                                      &data,&dataSize,&type)!=ERROR_SUCCESS || !data)continue;
                    const auto* attributes=static_cast<const WLAN_CONNECTION_ATTRIBUTES*>(data);
                    const auto& ssid=attributes->wlanAssociationAttributes.dot11Ssid;
                    if(attributes->isState==wlan_interface_state_connected && ssid.uSSIDLength) {
                        const QString name=QString::fromUtf8(reinterpret_cast<const char*>(ssid.ucSSID),int(ssid.uSSIDLength));
                        for(auto& entry:result)if(entry.wireless && entry.ssid.isEmpty()) {
                            entry.ssid=name;
                            entry.signalQuality=int(attributes->wlanAssociationAttributes.wlanSignalQuality);
                        }
                    }
                    freeMemory(data);
                }
                freeMemory(interfaces);
            }
            closeHandle(client);
        }
        FreeLibrary(wlan);
    }
#endif
    return result;
}
QString decodeEdid(const QByteArray& edid) {
    if(edid.size()<128)return {};
    const auto* data=reinterpret_cast<const quint8*>(edid.constData());
    if(data[0]!=0x00 || data[1]!=0xff || data[7]!=0x00)return {};
    const quint16 vendor=quint16((data[8]<<8)|data[9]);
    QString maker;
    for(int shift=10;shift>=0;shift-=5) {
        const int letter=(vendor>>shift)&0x1f;
        if(letter>0 && letter<=26)maker+=QChar(QLatin1Char('A').unicode()+letter-1);
    }
    const quint16 product=quint16(data[10]|(data[11]<<8));
    const int week=data[16],year=1990+data[17];
    QString name;
    for(int block=54;block+17<edid.size() && block<109;block+=18) {
        if(data[block]==0 && data[block+1]==0 && data[block+2]==0 && data[block+3]==0xfc) {
            QByteArray text(reinterpret_cast<const char*>(data+block+5),13);
            name=QString::fromLatin1(text).trimmed();
            const int newline=name.indexOf(QLatin1Char('\n'));
            if(newline>=0)name=name.left(newline);
            break;
        }
    }
    QStringList parts;
    if(!name.isEmpty())parts<<name;
    if(!maker.isEmpty())parts<<maker+QLatin1Char(' ')+QString::number(uint(product),16).toUpper();
    if(year>=1990)parts<<QStringLiteral("week %1 %2").arg(week).arg(year);
    if(data[21] && data[22])parts<<QStringLiteral("%1x%2 cm").arg(data[21]).arg(data[22]);
    return parts.join(QStringLiteral(", "));
}
QString monitorEdid(const QString& deviceId) {
    // "MONITOR\\GSM5B7F\\{4d36e96e-...}\\0005" -> the Enum key's Device Parameters value "EDID".
    const auto parts=deviceId.split(QLatin1Char('\\'));
    if(parts.size()<3 || parts[0].compare(QLatin1String("MONITOR"),Qt::CaseInsensitive)!=0)return {};
#ifdef Q_OS_WIN
    const QString key=QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Enum\\MONITOR\\")+parts[1]+
        QLatin1Char('\\')+parts[2]+QStringLiteral("\\Device Parameters");
    return decodeEdid(regBinary(key,QStringLiteral("EDID")));
#else
    return {};
#endif
}
QVector<DisplayInfo> readDisplays() {
    QVector<DisplayInfo> result;
    const auto screens=QGuiApplication::screens();
    for(int index=0;index<screens.size();++index) {
        const auto* screen=screens[index];
        DisplayInfo info;
        info.name=screen->name();
        info.resolution=(QSizeF(screen->size())*screen->devicePixelRatio()).toSize();
        info.refresh=screen->refreshRate();
        info.depth=screen->depth();
        info.dpi=screen->logicalDotsPerInch();
        info.primary=(screen==QGuiApplication::primaryScreen());
#ifdef Q_OS_WIN
        const QString device=info.name.isEmpty()?QStringLiteral("\\\\.\\DISPLAY%1").arg(index+1):info.name;
        DEVMODEW mode{};mode.dmSize=sizeof(mode);
        if(EnumDisplaySettingsW(reinterpret_cast<LPCWSTR>(device.utf16()),ENUM_CURRENT_SETTINGS,&mode)) {
            if(info.resolution.isEmpty())info.resolution=QSize(int(mode.dmPelsWidth),int(mode.dmPelsHeight));
            if(info.refresh<=0)info.refresh=double(mode.dmDisplayFrequency);
            info.bitsPerPixel=int(mode.dmBitsPerPel);
        }
        DISPLAY_DEVICEW adapter{};adapter.cb=sizeof(adapter);
        DISPLAY_DEVICEW monitor{};monitor.cb=sizeof(monitor);
        if(EnumDisplayDevicesW(reinterpret_cast<LPCWSTR>(device.utf16()),0,&adapter,0)) {
            info.adapter=wide(adapter.DeviceString);
            if(EnumDisplayDevicesW(reinterpret_cast<LPCWSTR>(device.utf16()),0,&monitor,EDD_GET_DEVICE_INTERFACE_NAME)) {
                info.monitor=wide(monitor.DeviceString);
                info.deviceId=wide(monitor.DeviceID);
                info.edid=monitorEdid(info.deviceId);
            }
        }
#else
        Q_UNUSED(index);
#endif
        result.push_back(info);
    }
    return result;
}
#ifdef Q_OS_WIN
// One endpoint's mix format: channels, sample rate and container bits, the only sample-rate authority
// Windows exposes per device.
AudioInfo describeEndpoint(IMMDevice* device,const QString& name) {
    AudioInfo info;
    info.name=name;
    IAudioClient* client=nullptr;
    if(device && SUCCEEDED(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&client))) && client) {
        WAVEFORMATEX* format=nullptr;
        if(SUCCEEDED(client->GetMixFormat(&format)) && format) {
            info.channels=int(format->nChannels);
            info.sampleRate=int(format->nSamplesPerSec);
            info.bits=int(format->wBitsPerSample);
            CoTaskMemFree(format);
        }
        client->Release();
    }
    return info;
}
QVector<AudioInfo> readAudio(EDataFlow flow) {
    QVector<AudioInfo> result;
    const HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    IMMDeviceEnumerator* enumerator=nullptr;
    IMMDeviceCollection* collection=nullptr;
    IMMDevice* defaultDevice=nullptr;
    QString defaultId;
    if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator))) && enumerator) {
        const ERole role=flow==eRender?eConsole:eCommunications;
        if(SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow,role,&defaultDevice)) && defaultDevice) {
            LPWSTR id=nullptr;
            if(SUCCEEDED(defaultDevice->GetId(&id)) && id) {defaultId=wide(id);CoTaskMemFree(id);}
        }
        if(SUCCEEDED(enumerator->EnumAudioEndpoints(flow,DEVICE_STATE_ACTIVE,&collection)) && collection) {
            UINT count=0;
            if(SUCCEEDED(collection->GetCount(&count))) {
                for(UINT i=0;i<count;++i) {
                    IMMDevice* device=nullptr;
                    if(FAILED(collection->Item(i,&device)) || !device)continue;
                    QString name,adapterName,id;
                    LPWSTR deviceId=nullptr;
                    if(SUCCEEDED(device->GetId(&deviceId)) && deviceId) {id=wide(deviceId);CoTaskMemFree(deviceId);}
                    IPropertyStore* store=nullptr;
                    if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,&store)) && store) {
                        PROPVARIANT value;PropVariantInit(&value);
                        if(SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName,&value)) && value.vt==VT_LPWSTR)
                            name=wide(value.pwszVal);
                        PropVariantClear(&value);
                        PropVariantInit(&value);
                        if(SUCCEEDED(store->GetValue(PKEY_DeviceInterface_FriendlyName,&value)) && value.vt==VT_LPWSTR)
                            adapterName=wide(value.pwszVal);
                        PropVariantClear(&value);
                        store->Release();
                    }
                    if(name.isEmpty())name=adapterName;
                    if(name.isEmpty()) {device->Release();continue;}
                    AudioInfo info=describeEndpoint(device,name);
                    info.adapter=adapterName;
                    info.id=id;
                    info.isDefault=(id==defaultId);
                    result.push_back(info);
                    device->Release();
                }
            }
            collection->Release();
        }
        if(defaultDevice)defaultDevice->Release();
        enumerator->Release();
    }
    if(SUCCEEDED(init))CoUninitialize();
    return result;
}
QVector<PrinterInfo> readPrinters() {
    QVector<PrinterInfo> result;
    HMODULE spool=LoadLibraryW(L"winspool.drv");
    if(!spool)return result;
    using EnumPrintersFn=BOOL(WINAPI*)(DWORD,LPWSTR,DWORD,LPBYTE,DWORD,LPDWORD,LPDWORD);
    using GetDefaultPrinterFn=BOOL(WINAPI*)(LPWSTR,LPDWORD);
    auto enumPrinters=reinterpret_cast<EnumPrintersFn>(reinterpret_cast<void*>(GetProcAddress(spool,"EnumPrintersW")));
    auto getDefault=reinterpret_cast<GetDefaultPrinterFn>(reinterpret_cast<void*>(GetProcAddress(spool,"GetDefaultPrinterW")));
    if(!enumPrinters) {FreeLibrary(spool);return result;}
    DWORD needed=0,returned=0;
    enumPrinters(PRINTER_ENUM_LOCAL|PRINTER_ENUM_CONNECTIONS,nullptr,2,nullptr,0,&needed,&returned);
    if(needed) {
        QByteArray buffer(int(needed),char(0));
        if(enumPrinters(PRINTER_ENUM_LOCAL|PRINTER_ENUM_CONNECTIONS,nullptr,2,
                        reinterpret_cast<LPBYTE>(buffer.data()),needed,&needed,&returned)) {
            auto* rows=reinterpret_cast<PRINTER_INFO_2W*>(buffer.data());
            for(DWORD i=0;i<returned;++i) {
                PrinterInfo info;
                info.name=wide(rows[i].pPrinterName);
                info.driver=wide(rows[i].pDriverName);
                info.port=wide(rows[i].pPortName);
                info.offline=(rows[i].Status&PRINTER_STATUS_OFFLINE)!=0;
                result.push_back(info);
            }
        }
    }
    QString defaultPrinter;
    if(getDefault) {
        DWORD size=0;getDefault(nullptr,&size);
        if(size) {
            QVector<wchar_t> name(int(size)+2,0);
            if(getDefault(name.data(),&size))defaultPrinter=wide(name.constData());
        }
    }
    for(auto& printer:result)printer.isDefault=(printer.name==defaultPrinter);
    FreeLibrary(spool);
    return result;
}
QString defaultBrowserName() {
    const QString choiceKey=QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\http\\UserChoice");
    const QString progId=regString(choiceKey,QStringLiteral("ProgId"));
    if(progId.isEmpty())return {};
    // A known ProgId gets its product name; anything else is reported as the association Windows holds,
    // which is a real identifier and not a guess.
    static const QHash<QString,QString> known{
        {QStringLiteral("ChromeHTML"),QStringLiteral("Google Chrome")},
        {QStringLiteral("MSEdgeHTM"),QStringLiteral("Microsoft Edge")},
        {QStringLiteral("FirefoxURL-308046B0AF4A39CB"),QStringLiteral("Mozilla Firefox")},
        {QStringLiteral("BraveHTML"),QStringLiteral("Brave")},
        {QStringLiteral("OperaStable"),QStringLiteral("Opera")},
        {QStringLiteral("VivaldiHTML"),QStringLiteral("Vivaldi")}};
    const auto found=known.constFind(progId);
    return found!=known.cend()?*found:progId;
}
QString serviceState(const wchar_t* name) {
    SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
    if(!manager)return QStringLiteral("service manager unavailable");
    QString state;
    SC_HANDLE service=OpenServiceW(manager,name,SERVICE_QUERY_STATUS);
    if(!service) {
        const DWORD error=GetLastError();
        state=error==ERROR_SERVICE_DOES_NOT_EXIST?QStringLiteral("not installed"):QStringLiteral("not readable");
    } else {
        SERVICE_STATUS status{};
        if(QueryServiceStatus(service,&status)) {
            switch(status.dwCurrentState) {
            case SERVICE_RUNNING:state=QStringLiteral("running");break;
            case SERVICE_STOPPED:state=QStringLiteral("stopped");break;
            case SERVICE_START_PENDING:state=QStringLiteral("starting");break;
            case SERVICE_STOP_PENDING:state=QStringLiteral("stopping");break;
            case SERVICE_PAUSED:state=QStringLiteral("paused");break;
            default:state=QStringLiteral("state ")+QString::number(status.dwCurrentState);break;
            }
        } else state=QStringLiteral("status not readable");
        CloseServiceHandle(service);
    }
    CloseServiceHandle(manager);
    return state;
}
bool serviceRunning(const wchar_t* name) { return serviceState(name)==QLatin1String("running"); }
QVector<PadInfo> readPads() {
    QVector<PadInfo> result;
    GUID hidGuid{};
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO devices=SetupDiGetClassDevsW(&hidGuid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(devices==INVALID_HANDLE_VALUE)return result;
    SP_DEVICE_INTERFACE_DATA interfaceData{};
    interfaceData.cbSize=sizeof(interfaceData);
    for(DWORD index=0;SetupDiEnumDeviceInterfaces(devices,nullptr,&hidGuid,index,&interfaceData);++index) {
        DWORD needed=0;
        SetupDiGetDeviceInterfaceDetailW(devices,&interfaceData,nullptr,0,&needed,nullptr);
        if(!needed)continue;
        QByteArray buffer(int(needed),char(0));
        auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if(!SetupDiGetDeviceInterfaceDetailW(devices,&interfaceData,detail,needed,nullptr,nullptr))continue;
        const QString path=QString::fromWCharArray(detail->DevicePath);
        HANDLE handle=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),0,FILE_SHARE_READ|FILE_SHARE_WRITE,
                                  nullptr,OPEN_EXISTING,0,nullptr);
        if(handle==INVALID_HANDLE_VALUE)continue;
        HIDD_ATTRIBUTES attributes{};
        attributes.Size=sizeof(attributes);
        if(HidD_GetAttributes(handle,&attributes) && attributes.VendorID==0x054C) {
            PadInfo info;
            wchar_t product[256]{};
            HidD_GetProductString(handle,product,sizeof(product));
            info.vendor=QStringLiteral("Sony");
            info.product=wide(product);
            info.wireless=path.contains(QLatin1String("BTHENUM"),Qt::CaseInsensitive);
            info.transport=info.wireless?QStringLiteral("Bluetooth"):QStringLiteral("USB");
            switch(attributes.ProductID) {
            case 0x0268:info.name=QStringLiteral("DualShock 3");break;
            case 0x05C4:case 0x09CC:info.name=QStringLiteral("DualShock 4");break;
            case 0x0CE6:info.name=QStringLiteral("DualSense");break;
            case 0x0DF2:info.name=QStringLiteral("DualSense Edge");break;
            default:info.name=info.product.isEmpty()?QStringLiteral("PlayStation controller"):info.product;break;
            }
            // The battery is the report reader's: a fresh pad::info() when the app is running, and an
            // honest "not read" otherwise (the HID descriptor carries no level).
            pad::Info state;
            if(pad::info(state) && state.kind!=pad::Kind::None) {
                info.batteryPercent=state.batteryPercent;
                info.charging=state.charging;
            }
            result.push_back(info);
        }
        CloseHandle(handle);
    }
    SetupDiDestroyDeviceInfoList(devices);
    return result;
}
KeyboardInfo readKeyboard() {
    KeyboardInfo info;
    const HKL layout=GetKeyboardLayout(0);
    wchar_t name[64]{};
    if(GetKeyboardLayoutNameW(name))info.layoutId=wide(name);
    if(layout) {
        const LANGID language=LANGID(LOWORD(reinterpret_cast<quintptr>(layout)));
        wchar_t display[128]{};wchar_t iso[128]{};
        if(GetLocaleInfoW(MAKELCID(language,SORT_DEFAULT),LOCALE_SLOCALIZEDDISPLAYNAME,display,int(std::size(display))))
            info.layoutName=wide(display);
        if(GetLocaleInfoW(MAKELCID(language,SORT_DEFAULT),LOCALE_SNAME,iso,int(std::size(iso))))
            info.layoutLocale=wide(iso);
    }
    int value=0;
    if(SystemParametersInfoW(SPI_GETKEYBOARDDELAY,0,&value,0))info.repeatDelay=value;
    value=0;
    if(SystemParametersInfoW(SPI_GETKEYBOARDSPEED,0,&value,0))info.repeatSpeed=value;
    info.installed=int(GetKeyboardLayoutList(0,nullptr));
    return info;
}
MouseInfo readMouse() {
    MouseInfo info;
    int value=0;
    if(SystemParametersInfoW(SPI_GETMOUSESPEED,0,&value,0))info.speed=value;
    // SPI_GETMOUSEBUTTONSWAP does not exist in winuser.h; SM_SWAPBUTTON is the documented read and
    // SwapMouseButton() the write (Settings > Accessory > Mouse Type).
    info.swapped=GetSystemMetrics(SM_SWAPBUTTON)!=0;
    info.buttons=GetSystemMetrics(SM_CMOUSEBUTTONS);
    return info;
}
void rawInputCounts(int& keyboards,int& mice) {
    keyboards=0;mice=0;
    UINT count=0;
    if(GetRawInputDeviceList(nullptr,&count,sizeof(RAWINPUTDEVICELIST))!=0 || count==0)return;
    QVector<RAWINPUTDEVICELIST> list;list.resize(int(count));
    UINT filled=count;
    if(GetRawInputDeviceList(list.data(),&filled,sizeof(RAWINPUTDEVICELIST))==UINT(-1))return;
    for(UINT i=0;i<filled;++i) {
        if(list[int(i)].dwType==RIM_TYPEKEYBOARD)++keyboards;
        else if(list[int(i)].dwType==RIM_TYPEMOUSE)++mice;
    }
}
BatteryInfo readBattery() {
    BatteryInfo info;
    SYSTEM_POWER_STATUS status{};
    if(!GetSystemPowerStatus(&status))return info;
    if(status.BatteryFlag==128) {info.state=QStringLiteral("no battery");return info;}
    info.present=true;
    info.acOnline=status.ACLineStatus==1;
    if(status.BatteryLifePercent!=255)info.percent=int(status.BatteryLifePercent);
    if(status.BatteryLifeTime!=DWORD(-1))info.secondsLeft=qint64(status.BatteryLifeTime);
    if(status.BatteryFlag&8)info.state=QStringLiteral("charging");
    else if(info.percent>=100 && info.acOnline)info.state=QStringLiteral("full");
    else if(info.percent<0)info.state=QStringLiteral("unknown");
    else info.state=info.acOnline?QStringLiteral("not charging"):QStringLiteral("discharging");
    return info;
}
QString powerSchemeName() {
    GUID* scheme=nullptr;
    if(PowerGetActiveScheme(nullptr,&scheme)!=ERROR_SUCCESS || !scheme)return {};
    DWORD bytesNeeded=0;
    QString name;
    if(PowerReadFriendlyName(nullptr,scheme,nullptr,nullptr,nullptr,&bytesNeeded)==ERROR_SUCCESS && bytesNeeded) {
        QVector<uchar> buffer(int(bytesNeeded)+2,0);
        if(PowerReadFriendlyName(nullptr,scheme,nullptr,nullptr,buffer.data(),&bytesNeeded)==ERROR_SUCCESS)
            name=QString::fromWCharArray(reinterpret_cast<const wchar_t*>(buffer.constData()));
    }
    LocalFree(scheme);
    return name;
}
int powerTimerValue(const GUID& subgroup,const GUID& setting,bool ac) {
    GUID* scheme=nullptr;
    if(PowerGetActiveScheme(nullptr,&scheme)!=ERROR_SUCCESS || !scheme)return -1;
    DWORD seconds=0;
    const DWORD result=ac?PowerReadACValueIndex(nullptr,scheme,&subgroup,&setting,&seconds)
                        :PowerReadDCValueIndex(nullptr,scheme,&subgroup,&setting,&seconds);
    LocalFree(scheme);
    if(result!=ERROR_SUCCESS)return -1;
    return int(seconds/60);
}
#endif
ClockInfo readClock() {
    ClockInfo info;
    info.now=QDateTime::currentDateTime();
    const auto zone=QTimeZone::systemTimeZone();
    info.timeZoneId=QString::fromUtf8(QTimeZone::systemTimeZoneId());
    info.utcOffsetSeconds=zone.offsetFromUtc(info.now);
    info.daylight=zone.isDaylightTime(info.now);
    const QLocale locale=QLocale::system();
    info.localeName=locale.name();
    info.language=QLocale::languageToString(locale.language())+QStringLiteral(" (")+
        QLocale::territoryToString(locale.territory())+QLatin1Char(')');
    info.country=QLocale::territoryToString(locale.territory());
    info.datePattern=locale.dateFormat(QLocale::ShortFormat);
    info.timePattern=locale.timeFormat(QLocale::ShortFormat);
    info.timeZoneName=QString::fromUtf8(zone.id());
#ifdef Q_OS_WIN
    TIME_ZONE_INFORMATION tz{};
    if(GetTimeZoneInformation(&tz)!=TIME_ZONE_ID_INVALID) {
        info.standardName=wide(tz.StandardName);
        info.daylightName=wide(tz.DaylightName);
        if(info.daylight && !info.daylightName.isEmpty())info.timeZoneName=info.daylightName;
        else if(!info.standardName.isEmpty())info.timeZoneName=info.standardName;
    }
    info.timeServicePresent=true;
    info.timeServiceRunning=serviceRunning(L"W32Time");
    const auto configured=regString(QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\W32Time\\Parameters"),
                                    QStringLiteral("Type"));
    info.automatic=info.timeServiceRunning &&
        (configured.compare(QLatin1String("NTP"),Qt::CaseInsensitive)==0 || configured.isEmpty());
    info.autoDetail=QStringLiteral("the Windows Time service (W32Time) is ")+serviceState(L"W32Time")+
        (configured.isEmpty()?QString{}:QStringLiteral(", sync type ")+configured);
#endif
    return info;
}
// ---- the snapshot cache ---------------------------------------------------------------------------
QString gDataDirectory;
std::atomic<qint64> gSnapshotAt{0};
Snapshot gCached;
QString dataDirectoryText(const QString& path) {
    if(path.isEmpty())return {};
    qint64 total=0;int files=0;
    // The data directory holds preferences.json, profiles.json and a handful of per-user files, so a bounded
    // breadth-first walk is enough. It is spelled out rather than left to QDirIterator: the report runs with
    // no event loop, and a recursive iterator on a path whose last component a caller created a moment ago
    // must not be able to spin. Every level is capped by `files<20000` and by the depth guard.
    QStringList pending{path};
    int depth=0;
    while(!pending.isEmpty() && files<20000 && depth<4) {
        QStringList next;
        for(const auto& current:pending) {
            const QDir directory(current);
            const auto entries=directory.entryInfoList(
                QDir::Files|QDir::Hidden|QDir::System|QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name);
            for(const auto& entry:entries) {
                if(files>=20000)break;
                if(entry.isDir()){next<<entry.absoluteFilePath();continue;}
                if(entry.fileName().isEmpty())continue;
                total+=entry.size();
                ++files;
            }
        }
        pending=next;
        ++depth;
    }
    return files?bytesText(total)+QStringLiteral(" in %1 files").arg(files):QStringLiteral("empty");
}
Snapshot readSnapshot() {
    Snapshot state;
    state.takenAtMs=QDateTime::currentMSecsSinceEpoch();
    state.hostName=QSysInfo::machineHostName();
    state.osName=QSysInfo::prettyProductName();
    state.osVersion=QSysInfo::productVersion();
    state.kernel=QSysInfo::kernelVersion();
    state.architecture=QSysInfo::currentCpuArchitecture();
    state.appVersion=QCoreApplication::applicationVersion().isEmpty()?QStringLiteral("0.2.0"):QCoreApplication::applicationVersion();
    state.qtVersion=QString::fromLatin1(qVersion());
    state.cpuLogical=int(QThread::idealThreadCount());
    state.clock=readClock();
    state.volumes=readVolumes();
    state.opticalDrive=opticalDrive();
    state.adapters=readAdapters();
    state.displays=readDisplays();
    state.printers=readPrinters();
    state.pads=readPads();
    state.keyboard=readKeyboard();
    state.mouse=readMouse();
    rawInputCounts(state.keyboards,state.mice);
    state.battery=readBattery();
#ifdef Q_OS_WIN
    state.monitorTimer.acMinutes=powerTimerValue(GUID_VIDEO_SUBGROUP,GUID_VIDEO_POWERDOWN_TIMEOUT,true);
    state.monitorTimer.dcMinutes=powerTimerValue(GUID_VIDEO_SUBGROUP,GUID_VIDEO_POWERDOWN_TIMEOUT,false);
    state.sleepTimer.acMinutes=powerTimerValue(GUID_SLEEP_SUBGROUP,GUID_STANDBY_TIMEOUT,true);
    state.sleepTimer.dcMinutes=powerTimerValue(GUID_SLEEP_SUBGROUP,GUID_STANDBY_TIMEOUT,false);
    state.powerScheme=powerSchemeName();
#endif
    state.volume=masterVolume();
    state.muted=masterMuted();
    state.defaultBrowser=defaultBrowserName();
    state.dlnaService=serviceState(L"WMPNetworkSvc");
    state.audioOutputs=readAudio(eRender);
    state.audioInputs=readAudio(eCapture);
    for(const auto& camera:QMediaDevices::videoInputs())
        state.cameras<<(camera.description().isEmpty()?QString::fromUtf8(camera.id()):camera.description());
    state.windowsAdmin=elevated();
#ifdef Q_OS_WIN
    for(const auto& printer:state.printers)if(printer.isDefault)state.defaultPrinter=printer.name;
    wchar_t account[257]{};DWORD accountSize=257;
    if(GetUserNameW(account,&accountSize))state.windowsAccount=wide(account);
    state.windowsDisplayName=state.windowsAccount;
    state.windowsDomain=qEnvironmentVariable("USERDOMAIN");
    HANDLE token=nullptr;
    if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) {
        DWORD needed=0;
        GetTokenInformation(token,TokenUser,nullptr,0,&needed);
        if(needed) {
            QByteArray buffer(int(needed),char(0));
            if(GetTokenInformation(token,TokenUser,reinterpret_cast<LPVOID>(buffer.data()),needed,&needed)) {
                auto* user=reinterpret_cast<TOKEN_USER*>(buffer.data());
                LPWSTR sid=nullptr;
                if(ConvertSidToStringSidW(user->User.Sid,&sid) && sid) {state.windowsSid=wide(sid);LocalFree(sid);}
                wchar_t name[256]{};wchar_t domain[256]{};DWORD nameSize=256,domainSize=256;SID_NAME_USE use{};
                if(LookupAccountSidW(nullptr,user->User.Sid,name,&nameSize,domain,&domainSize,&use)) {
                    state.windowsDisplayName=wide(name);
                    state.windowsDomain=wide(domain);
                }
            }
        }
        CloseHandle(token);
    }
    const QString versionKey=QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion");
    const auto productName=regString(versionKey,QStringLiteral("ProductName"));
    const auto displayVersion=regString(versionKey,QStringLiteral("DisplayVersion"));
    const auto build=regString(versionKey,QStringLiteral("CurrentBuild"));
    const auto revision=regString(versionKey,QStringLiteral("UBR"));
    if(!productName.isEmpty())
        state.osName=productName+(displayVersion.isEmpty()?QString{}:QStringLiteral(" ")+displayVersion);
    if(!build.isEmpty())state.osVersion=build+(revision.isEmpty()?QString{}:QLatin1Char('.')+revision);
    const QString cpuKey=QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0");
    state.cpuName=regString(cpuKey,QStringLiteral("ProcessorNameString"));
    const auto megahertz=QSettings(cpuKey,QSettings::NativeFormat).value(QStringLiteral("~MHz")).toInt();
    if(megahertz>0)state.cpuClock=QString::number(double(megahertz)/1000.,'f',2)+QStringLiteral(" GHz");
    DWORD returned=0;
    GetLogicalProcessorInformation(nullptr,&returned);
    if(returned) {
        QVector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> entries(int(returned/sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION))+1);
        if(GetLogicalProcessorInformation(entries.data(),&returned)) {
            for(const auto& entry:entries)if(entry.Relationship==RelationProcessorCore)++state.cpuCores;
        }
    }
    MEMORYSTATUSEX memoryState{};memoryState.dwLength=sizeof(memoryState);
    if(GlobalMemoryStatusEx(&memoryState)) {
        state.ramTotal=memoryState.ullTotalPhys;
        state.ramFree=memoryState.ullAvailPhys;
    }
    const QString gpuClass=QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}");
    for(const auto* index:{"0000","0001","0002"}) {
        const QString key=gpuClass+QLatin1Char('\\')+QLatin1String(index);
        const auto description=regString(key,QStringLiteral("DriverDesc"));
        if(description.isEmpty())continue;
        if(state.gpuName.isEmpty()) {
            state.gpuName=description;
            state.gpuDriver=regString(key,QStringLiteral("DriverVersion"));
            const auto memory=QSettings(key,QSettings::NativeFormat).value(QStringLiteral("HardwareInformation.qwMemorySize")).toLongLong();
            if(memory>0)state.gpuMemory=bytesText(memory);
            else {
                const auto adapterRam=QSettings(key,QSettings::NativeFormat).value(QStringLiteral("HardwareInformation.MemorySize")).toLongLong();
                if(adapterRam>0)state.gpuMemory=bytesText(adapterRam);
            }
        }
    }
    if(state.gpuName.isEmpty() && !state.displays.isEmpty())state.gpuName=state.displays.first().adapter;
    const QString biosKey=QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS");
    const auto boardVendor=regString(biosKey,QStringLiteral("BaseBoardManufacturer"));
    const auto boardProduct=regString(biosKey,QStringLiteral("BaseBoardProduct"));
    state.motherboard=(boardVendor+QLatin1Char(' ')+boardProduct).trimmed();
    state.bios=regString(biosKey,QStringLiteral("BIOSVersion"));
    if(!state.bios.isEmpty()) {
        const auto date=regString(biosKey,QStringLiteral("BIOSReleaseDate"));
        if(!date.isEmpty())state.bios+=QStringLiteral(" (")+date+QLatin1Char(')');
    }
#endif
    state.dataDirectory=gDataDirectory;
    state.dataDirectorySize=dataDirectoryText(gDataDirectory);
    return state;
}
}  // namespace

const Snapshot& snapshot() {
    const qint64 now=QDateTime::currentMSecsSinceEpoch();
    const qint64 taken=gSnapshotAt.load();
    if(taken==0 || now-taken>2000) {
        gCached=readSnapshot();
        gSnapshotAt.store(now);
    }
    return gCached;
}
void invalidateSnapshot() { gSnapshotAt.store(0); }
void setDataDirectory(const QString& path) { gDataDirectory=path; }
QStringList privilegeNotes() {
    return {(elevated()?QStringLiteral("elevated (administrator token): administrator reads run normally")
                       :QStringLiteral("not elevated (standard user): administrator-only reads are reported as such")),
        QStringLiteral("writes withheld by design: host clock, display mode, network configuration, time service")};
}

// ---- the console choice rows that write a Windows setting -----------------------------------------
// The Accessory page's four rows keep the firmware's own lists (Short/Normal/Long, Fast/Normal/Slow,
// Right-Handed/Left-Handed). This is the whole mapping, and it is the same table the read uses, so the row's
// value and the write cannot disagree. Every write is a documented SystemParametersInfo call (or
// SwapMouseButton), applies to this user, takes effect immediately and is undone by picking another item.
int hostChoiceIndex(const QString& key) {
    const auto& state=snapshot();
    if(key==QLatin1String("host/keyRepeatDelay")) {
        // SPI_GETKEYBOARDDELAY is 0..3 (250/500/750/1000 ms); the console's list has three items.
        if(state.keyboard.repeatDelay<=0)return 0;
        if(state.keyboard.repeatDelay==1)return 1;
        return 2;
    }
    if(key==QLatin1String("host/keyRepeatSpeed")) {
        // SPI_GETKEYBOARDSPEED is 0..31, higher is faster; Fast/Normal/Slow are three bands of it.
        if(state.keyboard.repeatSpeed>=24)return 0;
        if(state.keyboard.repeatSpeed>=8)return 1;
        return 2;
    }
    if(key==QLatin1String("host/mouseButtons"))return state.mouse.swapped?1:0;
    if(key==QLatin1String("host/pointerSpeed")) {
        // SPI_GETMOUSESPEED is 1..20; Fast/Normal/Slow are three bands of it.
        if(state.mouse.speed>=14)return 0;
        if(state.mouse.speed>=7)return 1;
        return 2;
    }
    return -1;
}
QString hostChoiceName(const QString& key) {
    if(key==QLatin1String("host/keyRepeatDelay"))return QStringLiteral("Key Repeat Interval");
    if(key==QLatin1String("host/keyRepeatSpeed"))return QStringLiteral("Key Repeat Speed");
    if(key==QLatin1String("host/mouseButtons"))return QStringLiteral("Mouse Type");
    if(key==QLatin1String("host/pointerSpeed"))return QStringLiteral("Pointer Speed");
    return {};
}
bool setHostChoice(const QString& key,int index) {
#ifdef Q_OS_WIN
    if(index<0 || index>3)return false;
    const auto& before=snapshot();
    bool ok=false;
    QString detail;
    if(key==QLatin1String("host/keyRepeatDelay")) {
        // The console's Short/Normal/Long are 250/500/1000 ms; Windows' four steps are 250/500/750/1000, so
        // Long takes the last one and Normal the second.
        static const int steps[]{0,1,3};
        const UINT delay=UINT(steps[std::clamp(index,0,2)]);
        ok=SystemParametersInfoW(SPI_SETKEYBOARDDELAY,delay,nullptr,SPIF_UPDATEINIFILE|SPIF_SENDCHANGE)!=FALSE;
        detail=QStringLiteral("SPI_SETKEYBOARDDELAY %1 (was %2)").arg(delay).arg(before.keyboard.repeatDelay);
    } else if(key==QLatin1String("host/keyRepeatSpeed")) {
        static const int speeds[]{31,16,4};
        const UINT speed=UINT(speeds[std::clamp(index,0,2)]);
        ok=SystemParametersInfoW(SPI_SETKEYBOARDSPEED,speed,nullptr,SPIF_UPDATEINIFILE|SPIF_SENDCHANGE)!=FALSE;
        detail=QStringLiteral("SPI_SETKEYBOARDSPEED %1 (was %2)").arg(speed).arg(before.keyboard.repeatSpeed);
    } else if(key==QLatin1String("host/mouseButtons")) {
        const bool swapped=index==1;
        ok=SwapMouseButton(swapped?TRUE:FALSE)!=FALSE || swapped==before.mouse.swapped;
        detail=QStringLiteral("SwapMouseButton %1 (was %2)").arg(swapped?1:0).arg(before.mouse.swapped?1:0);
    } else if(key==QLatin1String("host/pointerSpeed")) {
        static const int speeds[]{18,10,4};
        const UINT speed=UINT(speeds[std::clamp(index,0,2)]);
        ok=SystemParametersInfoW(SPI_SETMOUSESPEED,speed,nullptr,SPIF_UPDATEINIFILE|SPIF_SENDCHANGE)!=FALSE;
        detail=QStringLiteral("SPI_SETMOUSESPEED %1 (was %2)").arg(speed).arg(before.mouse.speed);
    } else return false;
    if(ok) {
        invalidateSnapshot();
        qInfo().noquote()<<QStringLiteral("accessory: %1 -> %2").arg(hostChoiceName(key),detail);
    } else {
        qWarning().noquote()<<QStringLiteral("accessory: %1 could not be changed (%2)").arg(hostChoiceName(key),detail);
    }
    return ok;
#else
    Q_UNUSED(key);Q_UNUSED(index);return false;
#endif
}

// ==== Core Audio volume, the existing host path ====================================================
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
    const bool ok=withEndpoint([&](IAudioEndpointVolume* v){return SUCCEEDED(v->SetMasterVolumeLevelScalar(level,nullptr));});
    if(ok)invalidateSnapshot();
    return ok;
#else
    Q_UNUSED(percent);return false;
#endif
}
bool masterMuted() {
#ifdef Q_OS_WIN
    BOOL muted=FALSE;withEndpoint([&](IAudioEndpointVolume* v){return SUCCEEDED(v->GetMute(&muted));});return muted!=FALSE;
#else
    return false;
#endif
}
bool setMasterMuted(bool muted) {
#ifdef Q_OS_WIN
    const bool ok=withEndpoint([&](IAudioEndpointVolume* v){return SUCCEEDED(v->SetMute(muted,nullptr));});
    if(ok)invalidateSnapshot();
    return ok;
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

// ==== the reports the Information pages show =======================================================
namespace {
QString adapterLine(const AdapterInfo& adapter) {
    QStringList parts;
    parts<<adapter.kind;
    if(!adapter.up)parts<<QStringLiteral("not connected");
    else {
        if(!adapter.ipv4.isEmpty())parts<<adapter.ipv4.first();
        if(!linkSpeedText(adapter.linkSpeed).isEmpty())parts<<linkSpeedText(adapter.linkSpeed);
    }
    if(!adapter.ssid.isEmpty())parts<<QStringLiteral("SSID ")+adapter.ssid;
    return adapter.name+QStringLiteral(": ")+parts.join(QStringLiteral(", "));
}
QString displayLine(const DisplayInfo& display) {
    QStringList parts;
    parts<<QStringLiteral("%1 x %2").arg(display.resolution.width()).arg(display.resolution.height());
    if(display.refresh>0)parts<<QString::number(std::lround(display.refresh))+QStringLiteral(" Hz");
    if(display.depth>0)parts<<QStringLiteral("%1-bit").arg(display.depth);
    if(display.primary)parts<<QStringLiteral("primary");
    return (display.monitor.isEmpty()?display.name:display.monitor)+QStringLiteral(": ")+parts.join(QStringLiteral(", "));
}
QString firstOutputName(const Snapshot& state) {
    for(const auto& device:state.audioOutputs)if(device.isDefault)return device.name;
    return state.audioOutputs.isEmpty()?QString{}:state.audioOutputs.first().name;
}
QString firstInputName(const Snapshot& state) {
    for(const auto& device:state.audioInputs)if(device.isDefault)return device.name;
    return state.audioInputs.isEmpty()?QString{}:state.audioInputs.first().name;
}
QString volumeText(const Snapshot& state) {
    if(state.volume<0)return QStringLiteral("not readable");
    return QString::number(state.volume)+QLatin1Char('%')+(state.muted?QStringLiteral(" (muted)"):QString{});
}
QString freeSpaceText(const Snapshot& state) {
    QStringList parts;
    for(const auto& volume:state.volumes) {
        const QString letter=volume.mount.left(2);
        if(letter.size()<2)continue;
        parts<<letter+QLatin1Char(' ')+bytesText(volume.freeBytes)+QStringLiteral(" free");
        if(parts.size()>=3)break;
    }
    return parts.isEmpty()?QStringLiteral("no mounted volume"):parts.join(QStringLiteral(", "));
}
QString padText(const Snapshot& state) {
    if(state.pads.isEmpty())return QStringLiteral("no PlayStation controller");
    QStringList parts;
    for(const auto& pad:state.pads) {
        QString line=pad.name+QStringLiteral(" (")+pad.transport+QLatin1Char(')');
        if(pad.batteryPercent>=0)line+=" "+QString::number(pad.batteryPercent)+QLatin1Char('%');
        parts<<line;
    }
    return parts.join(QStringLiteral(", "));
}
QString opticalText(const Snapshot& state) {
    return state.opticalDrive.isEmpty()?QStringLiteral("no BD/DVD drive"):state.opticalDrive;
}
QString displayProfileText(const Snapshot& state) {
    if(state.displays.isEmpty())return QStringLiteral("no display");
    const auto& first=state.displays.first();
    return QStringLiteral("%1x%2 at %3 Hz").arg(first.resolution.width()).arg(first.resolution.height())
        .arg(first.refresh>0?QString::number(std::lround(first.refresh)):QStringLiteral("?"));
}
}  // namespace

QString systemInformation(const QString& renderer,const Label& label,const QString& systemName) {
    // The PS3 page lists System Software, IP Address, MAC Address and Free Space (firmware labels); the host's
    // processor, memory, graphics, storage and displays follow. Every value is read here and now.
    const auto& state=snapshot();
    const auto text=[&](const char* key,const char* fallback){return label(QString::fromLatin1(key),QString::fromLatin1(fallback));};
    const QString none=QStringLiteral("-");
    QString address=none,mac=none;
    for(const auto& adapter:state.adapters)if(adapter.up && !adapter.ipv4.isEmpty()) {
        address=adapter.ipv4.first();
        mac=adapter.mac;
        break;
    }
    QStringList lines{text("msg_console_info","System Information"),QString{},
        text("msg_ps3_name","System Name")+": "+
            (systemName.trimmed().isEmpty()?state.hostName:systemName.trimmed()),
        text("msg_system_software","System Software")+": "+state.osName+
            (state.osVersion.isEmpty()?QString{}:QStringLiteral(" (build ")+state.osVersion+QLatin1Char(')')),
        text("msg_ipaddress","IP Address")+": "+address,
        text("msg_macaddress","MAC Address")+": "+mac,
        text("msg_space","Free Space")+": "+freeSpaceText(state),
        QStringLiteral("Processor: ")+(state.cpuName.isEmpty()?none:state.cpuName)+
            (state.cpuClock.isEmpty()?QString{}:QStringLiteral(" at ")+state.cpuClock)+
            (state.cpuCores?QStringLiteral(", %1 cores").arg(state.cpuCores):QString{}),
        QStringLiteral("Memory: ")+bytesText(qint64(state.ramTotal))+QStringLiteral(" (")+bytesText(qint64(state.ramFree))+
            QStringLiteral(" available)"),
        QStringLiteral("Graphics: ")+(state.gpuName.isEmpty()?none:state.gpuName)+
            (state.gpuDriver.isEmpty()?QString{}:QStringLiteral(" driver ")+state.gpuDriver),
        QStringLiteral("Host name: ")+state.hostName+QStringLiteral(", ") +
            QStringLiteral("%1-bit, locale %2").arg(state.architecture).arg(state.clock.language)};
    if(!renderer.isEmpty())lines<<QStringLiteral("Renderer: ")+renderer;
    for(const auto& display:state.displays)lines<<QStringLiteral("Display: ")+displayLine(display);
    for(const auto& volume:state.volumes)
        lines<<QStringLiteral("Storage: ")+volume.label+QStringLiteral(" ")+bytesText(volume.totalBytes)+
            QStringLiteral(" (")+bytesText(volume.freeBytes)+QStringLiteral(" free, ")+volume.filesystem+QLatin1Char(')');
    return lines.join(QLatin1Char('\n'));
}

QString networkStatus(const Label& label) {
    // The console's "Settings and Connection Status List" is a read-only table (SETTINGS 1.18/12.2): the
    // renderer draws one row per line on the measured 40 px grid, so the body is the firmware's heading,
    // a blank, and the 13 rows in the console's order - the same vector the report prints, so the page and
    // the report cannot disagree.
    const auto& state=snapshot();
    const auto text=[&](const char* key,const char* fallback){return label(QString::fromLatin1(key),QString::fromLatin1(fallback));};
    QStringList lines{text("msg_netlist_current","Settings and Connection Status List")};
    if(state.adapters.isEmpty()) {
        lines<<QString{}<<text("msg_netlist_current_explanation","The host reports no network adapter.");
        return lines.join(QLatin1Char('\n'));
    }
    lines<<QString{};
    for(const auto& row:networkStatusList(label))
        lines<<row.label+QStringLiteral(": ")+(row.value.isEmpty()?QStringLiteral("-"):row.value);
    // A second or third adapter is listed after the table when the grid has room; the console's table is
    // one connection, so the first block is the connection this machine actually uses.
    int extra=0;
    for(int index=1;index<state.adapters.size() && extra<1;++index,++extra) {
        lines<<QString{}<<adapterLine(state.adapters[index]);
    }
    return lines.join(QLatin1Char('\n'));
}

// ---- the 13-row status table (SETTINGS 1.18) ------------------------------------------------------
namespace {
#ifdef Q_OS_WIN
// The user's Internet Explorer/WinHTTP proxy configuration, read through winhttp.dll at run time so the
// build links no extra library:
//   lpszProxy set                    -> the proxy string Windows holds ("host:port")
//   auto-detect or an auto-config URL -> "Automatic"
//   neither                           -> "Do Not Use" (the console's own label for no proxy)
QString proxyText() {
    // The struct's layout is winhttp.h's WINHTTP_CURRENT_USER_IE_PROXY_CONFIG, declared here so the reader
    // does not depend on that header's version guard; the strings are GlobalAlloc'ed and freed below.
    struct IeProxyConfig { BOOL autoDetect; LPWSTR autoConfigUrl; LPWSTR proxy; LPWSTR proxyBypass; };
    using GetProxyFn=BOOL(WINAPI*)(IeProxyConfig*);
    HMODULE module=LoadLibraryW(L"winhttp.dll");
    if(!module)return QStringLiteral("winhttp.dll is missing");
    const auto getProxy=reinterpret_cast<GetProxyFn>(GetProcAddress(module,"WinHttpGetIEProxyConfigForCurrentUser"));
    if(!getProxy) {FreeLibrary(module);return QStringLiteral("the proxy reader is not exported");}
    IeProxyConfig config{};
    QString text;
    if(!getProxy(&config))text=QStringLiteral("not readable");
    else if(config.proxy && *config.proxy)text=wide(config.proxy);
    else if(config.autoDetect || config.autoConfigUrl)text=QStringLiteral("Automatic");
    else text=QStringLiteral("Do Not Use");
    if(config.proxy)GlobalFree(config.proxy);
    if(config.proxyBypass)GlobalFree(config.proxyBypass);
    if(config.autoConfigUrl)GlobalFree(config.autoConfigUrl);
    FreeLibrary(module);
    return text;
}
// The UPnP row's host fact: Windows' SSDP discovery service is what finds UPnP devices on this machine.
QString upnpText(QString& note) {
    const QString ssdp=serviceState(L"SSDPSRV");
    const QString host=serviceState(L"upnphost");
    note=QStringLiteral("the SSDP Discovery service (SSDPSRV) is ")+ssdp+
        QStringLiteral(", the UPnP Device Host service (upnphost) is ")+host;
    if(ssdp==QLatin1String("running"))return QStringLiteral("Enabled");
    if(ssdp==QLatin1String("stopped"))return QStringLiteral("Disabled");
    return {};
}
#endif
}  // namespace

QVector<StatusRow> networkStatusList(const Label& label) {
    const auto& state=snapshot();
    const auto text=[&](const char* key,const char* fallback){return label(QString::fromLatin1(key),QString::fromLatin1(fallback));};
    const AdapterInfo* adapter=nullptr;
    for(const auto& candidate:state.adapters)
        if(candidate.up && !candidate.ipv4.isEmpty()){adapter=&candidate;break;}
    if(!adapter)for(const auto& candidate:state.adapters)if(candidate.up){adapter=&candidate;break;}
    const auto ipv4=[&] {
        if(!adapter || adapter->ipv4.isEmpty())return QString{};
        const auto first=adapter->ipv4.first();
        const int space=first.indexOf(QLatin1Char(' '));
        return space>0?first.left(space):first;
    };
    const auto mac=[](const QString& value) {return value.isEmpty()?QString{}:value;};
    QVector<StatusRow> rows;
    // One row: id, the firmware label, the host value (empty when there is none), the source, the note.
    const auto row=[&](const char* id,const QString& name,const QString& value,const char* source,const QString& note) {
        StatusRow entry;
        entry.id=QString::fromLatin1(id);entry.label=name;entry.value=value;
        entry.source=QString::fromLatin1(source);entry.ok=!value.isEmpty();
        entry.note=note;
        rows.push_back(entry);
    };
    const QString noAdapter=QStringLiteral("the host reports no network adapter");
    QString upnpNote;
    row("internet_connection",text("msg_internet_connect","Internet Connection"),
        adapter?QStringLiteral("Enabled"):QString{},
        "win32",adapter?QStringLiteral("the interface's link state (IP_ADAPTER_ADDRESSES.OperStatus); "
            "the console's own Internet Connection switch has no host counterpart, so this is the link"):noAdapter);
    row("connection_method",text("msg_connection_method","Connection Method"),
        adapter?(adapter->wireless?text("msg_wless","Wireless"):text("msg_wired","Wired Connection")):QString{},
        "win32",adapter?QStringLiteral("IP_ADAPTER_ADDRESSES.IfType"):noAdapter);
    row("speed_duplex",text("msg_ethernetmode","Speed and Duplex"),
        adapter?linkSpeedText(adapter->linkSpeed):QString{},
        "win32",adapter?QStringLiteral("the negotiated transmit rate; the host exposes no duplex field"):noAdapter);
    row("address_settings",text("msg_address_settings","Address Settings"),
        adapter?(adapter->dhcp?QStringLiteral("Automatic"):QStringLiteral("Manual")):QString{},
        "win32",adapter?QStringLiteral("the interface's DHCP flag (IP_ADAPTER_DHCP_ENABLED)"):noAdapter);
    row("ip",text("msg_ipaddress","IP Address"),ipv4(),"win32",
        adapter?QStringLiteral("IP_ADAPTER_ADDRESSES.FirstUnicastAddress"):noAdapter);
    row("subnet",text("msg_subnetmask","Subnet Mask"),adapter?adapter->subnet:QString{},"win32",
        adapter?QStringLiteral("computed from the address's OnLinkPrefixLength"):noAdapter);
    row("router",text("msg_default_router","Default Router"),
        adapter?adapter->gateways.value(0):QString{},"win32",
        adapter?QStringLiteral("GAA_FLAG_INCLUDE_GATEWAYS, first gateway on the interface"):noAdapter);
    row("dns1",text("msg_primedns","Primary DNS"),adapter?adapter->dns.value(0):QString{},"win32",
        adapter?QStringLiteral("IP_ADAPTER_ADDRESSES.FirstDnsServerAddress"):noAdapter);
    row("dns2",text("msg_seconddns","Secondary DNS"),
        adapter?(adapter->dns.size()>1?adapter->dns.value(1):QStringLiteral("Not set")):QString{},"win32",
        adapter?QStringLiteral("the second address in the DNS chain; \"Not set\" means the interface "
            "reports one server"):noAdapter);
    row("mac",text("msg_macaddress","MAC Address"),adapter?mac(adapter->mac):QString{},"win32",
        adapter?QStringLiteral("IP_ADAPTER_ADDRESSES.PhysicalAddress"):noAdapter);
    row("mtu",text("msg_mtu","MTU"),adapter?QString::number(adapter->mtu):QString{},"win32",
        adapter?QStringLiteral("IP_ADAPTER_ADDRESSES.Mtu"):noAdapter);
    row("proxy",text("msg_proxyserver","Proxy Server"),
#ifdef Q_OS_WIN
        proxyText(),
#else
        QString{},
#endif
        "winhttp",QStringLiteral("WinHttpGetIEProxyConfigForCurrentUser (winhttp.dll)"));
    row("upnp",text("msg_upnp","UPnP"),
#ifdef Q_OS_WIN
        upnpText(upnpNote),
#else
        QString{},
#endif
        "win32",upnpNote.isEmpty()?QStringLiteral("Windows' UPnP service state"):upnpNote);
    return rows;
}

QJsonObject networkTest(bool probe) {
    const auto& state=snapshot();
    QJsonArray adapters;
    QString detail;
    bool ok=false;
    for(const auto& adapter:state.adapters) {
        adapters.push_back(QJsonObject{{"name",adapter.name},{"up",adapter.up},{"kind",adapter.kind},
            {"ipv4",adapter.ipv4.value(0)},{"gateway",adapter.gateways.value(0)}});
        if(!ok && adapter.up && !adapter.gateways.isEmpty()) {
            ok=true;
            detail=adapter.kind+QStringLiteral(": ")+adapter.ipv4.value(0)+QStringLiteral(" via ")+adapter.gateways.first();
        }
    }
    const QString interfaceDetail=ok?detail:QStringLiteral("no interface is up with a default gateway");
    bool reached=false;
    QString probeDetail;
    if(probe && ok) {
        // One bounded TCP connection to a public resolver: read-only, no request payload, no configuration.
        QTcpSocket socket;
        socket.connectToHost(QStringLiteral("1.1.1.1"),53);
        reached=socket.waitForConnected(1200);
        probeDetail=reached?QStringLiteral("a TCP connection to 1.1.1.1:53 was accepted")
                           :QStringLiteral("no TCP connection to 1.1.1.1:53 within 1200 ms");
        socket.abort();
    }
    return QJsonObject{{"ok",probe?reached:ok},
        {"detail",probe?(reached?QStringLiteral("Connected. ")+interfaceDetail+QStringLiteral("; ")+probeDetail
                            :QStringLiteral("Not connected. ")+interfaceDetail+QStringLiteral("; ")+probeDetail)
                       :interfaceDetail},
        {"interfaceOk",ok},{"interface",interfaceDetail},
        {"reachable",probe?reached:false},
        {"method",probe?"interface state (GetAdaptersAddresses) and one TCP connect to 1.1.1.1:53"
                       :"interface state (GetAdaptersAddresses)"},
        {"adapters",adapters},
        {"wroteConfiguration",false}};
}

QString audioOutput() {
    const auto& state=snapshot();
    const QString name=firstOutputName(state);
    QStringList lines{QStringLiteral("Audio Output Settings"),QString{},
        QStringLiteral("Output Device: ")+(name.isEmpty()?QStringLiteral("no playback device"):name)};
    for(const auto& device:state.audioOutputs) {
        QStringList detail;
        if(device.channels)detail<<QString::number(device.channels)+QStringLiteral(" channels");
        if(device.sampleRate)detail<<kilohertz(device.sampleRate);
        if(device.bits)detail<<QString::number(device.bits)+QStringLiteral("-bit");
        if(!detail.isEmpty())lines<<QStringLiteral("Format: ")+detail.join(QStringLiteral(", "));
        break;
    }
    lines<<QStringLiteral("Volume: ")+(state.volume<0?QStringLiteral("not readable"):volumeText(state));
    lines<<QStringLiteral("Devices: ")+QString::number(state.audioOutputs.size());
    for(int index=1;index<state.audioOutputs.size() && index<4;++index)
        lines<<QStringLiteral("Also: ")+state.audioOutputs[index].name;
    if(!state.audioInputs.isEmpty())lines<<QStringLiteral("Input: ")+firstInputName(state);
    return lines.join(QLatin1Char('\n'));
}

QString displayOutput() {
    const auto& state=snapshot();
    QStringList lines{QStringLiteral("Video Output Settings"),QString{}};
    if(state.displays.isEmpty())lines<<QStringLiteral("No display was reported.");
    for(const auto& display:state.displays) {
        lines<<(display.primary?QStringLiteral("Primary display"):QStringLiteral("Display"));
        lines<<QStringLiteral("Output: ")+QStringLiteral("%1 x %2").arg(display.resolution.width()).arg(display.resolution.height())+
            QStringLiteral(" at ")+(display.refresh>0?QString::number(std::lround(display.refresh))+QStringLiteral(" Hz"):QStringLiteral("a refresh rate the host did not report"));
        if(display.monitor.isEmpty()==false)lines<<QStringLiteral("Monitor: ")+display.monitor;
        if(display.adapter.isEmpty()==false)lines<<QStringLiteral("Adapter: ")+display.adapter;
        if(display.edid.isEmpty()==false)lines<<QStringLiteral("EDID: ")+display.edid;
        if(display.bitsPerPixel)lines<<QStringLiteral("Desktop depth: ")+QString::number(display.bitsPerPixel)+QStringLiteral("-bit");
        lines<<QStringLiteral("DPI: ")+QString::number(std::lround(display.dpi));
    }
    return lines.join(QLatin1Char('\n'));
}

// ==== the settings source layer ====================================================================
QJsonObject Reading::toJson() const {
    return QJsonObject{{"text",text},{"source",source},{"status",status},{"note",note},{"ok",ok}};
}
namespace {
Reading value(const QString& text,const char* source="win32",const char* status="live",const QString& note={}) {
    Reading reading;
    reading.text=text;
    reading.source=QString::fromLatin1(source);
    reading.status=QString::fromLatin1(status);
    reading.note=note;
    reading.ok=true;
    return reading;
}
const char* kAdminNote="changing it requires administrator rights and is withheld: the console's own change is not reversible from here";
// A row whose value this build must not invent: no text, the reason in `note`, the host facts on the row's
// own page (host::rowDetail). SETTINGS-COVERAGE section 4: no value rather than a plausible one.
Reading noValue(const QString& note,const char* status="no-source") {
    Reading reading;
    reading.source=QStringLiteral("none");
    reading.status=QString::fromLatin1(status);
    reading.note=note;
    return reading;
}
}  // namespace

Reading rowValue(const QString& pageId,const QString& rowId) {
    const auto& state=snapshot();
    // ---- system -----------------------------------------------------------------------------------
    if(pageId==QLatin1String("system")) {
        if(rowId==QLatin1String("console_lang"))return value(state.clock.language,"qt","read-only");
        if(rowId==QLatin1String("name"))return {};                 // the app's own preference
        if(rowId==QLatin1String("disc_utility"))
            return value(state.volumes.isEmpty()?QStringLiteral("no fixed volume"):
                QStringLiteral("%1 volumes").arg(state.volumes.size()),"win32","read-only");
        if(rowId==QLatin1String("backup_utility"))
            return value(state.dataDirectorySize.isEmpty()?QStringLiteral("no data directory"):state.dataDirectorySize,
                         "qt","read-only");
        if(rowId==QLatin1String("init_setting"))return {};         // a real action, no value
        if(rowId==QLatin1String("init_console"))
            return noValue(QStringLiteral("the console row wipes the system storage and every user's data; on a "
                "computer that would delete the data directory, so it is withheld and the row's page says so"));
        if(rowId==QLatin1String("ex_os_install"))
            return noValue(QStringLiteral("the console row installs a second operating system into a reserved "
                "partition; Windows has no equivalent and this build ships no partition writer"));
        if(rowId==QLatin1String("information"))
            return value(state.osName+QStringLiteral(", ")+bytesText(qint64(state.ramTotal))+QStringLiteral(" RAM"),
                         "win32","read-only");
        // The 17 System rows the native tree did not have (SETTINGS 1.7): the firmware row set is known from
        // the 25-row walk (ref2 tails/system-page-top + system-down1..20) and the RCO's own strings, and none
        // of them has an honest host value, so each is listed with the reason and an informative page.
        if(rowId==QLatin1String("auto_update"))
            return noValue(QStringLiteral("no PS3 update channel: the console row starts the system to fetch game "
                "patches and sync trophies, and this build has neither"));
        if(rowId==QLatin1String("characterset"))
            return noValue(QStringLiteral("the console row sets the character code for text in music files; this "
                "build reads tags as Unicode and has no such setting"));
        if(rowId==QLatin1String("dictionary_type"))
            return noValue(QStringLiteral("the console row chooses a predictive-text dictionary for its on-screen "
                "keyboard; this build ships none"));
        if(rowId==QLatin1String("dict_edit"))
            return noValue(QStringLiteral("the console row edits the predictive-text dictionary; this build has none"));
        if(rowId==QLatin1String("dict_delete"))
            return noValue(QStringLiteral("the console row deletes the predictive-text dictionary; this build has none"));
        if(rowId==QLatin1String("notification_message"))
            return noValue(QStringLiteral("the console row gates notification pop-ups; this build shows its own "
                "notifications and keeps no stored setting for them"));
        if(rowId==QLatin1String("trophy_notification"))
            return noValue(QStringLiteral("trophies are PSN-backed and this build has no trophy notifications"));
        if(rowId==QLatin1String("whats_new"))
            return noValue(QStringLiteral("the console row gates the [What's New] channel; this build has no such surface"));
        if(rowId==QLatin1String("disc_auto_start"))
            return noValue(QStringLiteral("this build has no optical drive and starts nothing when media is inserted"));
        if(rowId==QLatin1String("control_hdmi"))
            return noValue(QStringLiteral("the row is HDMI-CEC device control, and Windows exposes no public CEC API"));
        if(rowId==QLatin1String("divx_code"))
            return noValue(QStringLiteral("the registration code belongs to the console's DivX VOD player; this "
                "build has no such player"));
        if(rowId==QLatin1String("atrac"))
            return noValue(QStringLiteral("the console row authenticates ATRAC playback; this build's media stack "
                "reports no ATRAC decoder (its codec list is on the row's page)"));
        if(rowId==QLatin1String("wma"))
            return noValue(QStringLiteral("the console row authenticates WMA playback; this build decodes files "
                "through Qt and needs no enable step"));
        if(rowId==QLatin1String("vita_network"))
            return noValue(QStringLiteral("the console row lets a PS Vita use Content Manager over the network; "
                "this build runs no Content Manager"));
        if(rowId==QLatin1String("vita_list"))
            return noValue(QStringLiteral("no PS Vita system is registered because this build registers none"));
        if(rowId==QLatin1String("vita_backup_delete"))
            return noValue(QStringLiteral("the console row deletes PS Vita backup files from the system storage; "
                "this build keeps none"));
        if(rowId==QLatin1String("data_transfer"))
            return noValue(QStringLiteral("the console row copies data between two PS3 systems; this build "
                "transfers nothing"));
        if(rowId==QLatin1String("aboutps3"))
            return value(QStringLiteral("%1 (Qt %2)").arg(state.appVersion,state.qtVersion),"qt","read-only");
    }
    // ---- network ----------------------------------------------------------------------------------
    if(pageId==QLatin1String("network")) {
        const AdapterInfo* primary=nullptr;
        for(const auto& adapter:state.adapters)if(adapter.up) {primary=&adapter;break;}
        if(rowId==QLatin1String("net_netlist")) {
            // The row opens the 13-row status table (host::networkStatusList); the value is its summary.
            return value(state.adapters.isEmpty()?QStringLiteral("no adapter"):
                primary?QStringLiteral("%1 connected").arg(primary->kind):QStringLiteral("no link"),
                "win32","read-only",QStringLiteral("the row's page is the console's 13-row status table, filled "
                "with this computer's readings"));
        }
        if(rowId==QLatin1String("net_internet"))
            return value(primary?QStringLiteral("Enabled"):QStringLiteral("Disabled"),"win32","read-only",
                primary?QStringLiteral("an interface is up (IP_ADAPTER_ADDRESSES.OperStatus)"):
                       QStringLiteral("no interface is up"));
        if(rowId==QLatin1String("net_internet_setting"))
            return noValue(QStringLiteral("the console row's wizard writes the console's own network "
                "configuration; writing this computer's is out of scope, and the row's page reports the current "
                "setting (automatic or manual addressing) instead"));
        if(rowId==QLatin1String("net_internet_test")) {
            const auto test=networkTest(false);
            return value(test.value("ok").toBool()?QStringLiteral("Connected"):QStringLiteral("Not connected"),
                "win32","live",test.value("method").toString()+QStringLiteral(": ")+test.value("detail").toString());
        }
        if(rowId==QLatin1String("net_media_server")) {
            if(state.dlnaService.isEmpty())
                return noValue(QStringLiteral("the Windows media-sharing service was not reported by the host"));
            const bool sharing=state.dlnaService==QLatin1String("running");
            return value(sharing?QStringLiteral("Enabled"):QStringLiteral("Disabled"),"win32","read-only",
                QStringLiteral("the Windows Media Player Network Sharing Service (WMPNetworkSvc) is ")+state.dlnaService);
        }
    }
    // ---- display ----------------------------------------------------------------------------------
    if(pageId==QLatin1String("display")) {
        if(rowId==QLatin1String("display_output_setting"))return value(displayProfileText(state),"win32","read-only");
        if(rowId==QLatin1String("screensaver"))return {};          // the app's own preference
        // The three HDMI colour options (SETTINGS section 4's no-invention list): the host reports a desktop
        // colour space and bit depth, but exposes no switch for the console's output options.
        if(rowId==QLatin1String("cross_color_filter"))
            return noValue(QStringLiteral("the console row filters cross colour on its analogue output; this "
                "computer's video path is digital and the host exposes no such control"));
        if(rowId==QLatin1String("rgb_full_range"))
            return noValue(QStringLiteral("the console row chooses limited or full RGB on its HDMI output; "
                "Windows exposes no public HDMI range switch"));
        if(rowId==QLatin1String("super_white"))
            return noValue(QStringLiteral("the console row allows super-white levels on its HDMI output; the "
                "host exposes no such control"));
        if(rowId==QLatin1String("deep_color")) {
            if(state.displays.isEmpty())return noValue(QStringLiteral("the host reports no display"));
            const int bits=state.displays.first().bitsPerPixel?state.displays.first().bitsPerPixel:
                state.displays.first().depth;
            if(!bits)return noValue(QStringLiteral("the host reports no desktop bit depth"));
            return value(QStringLiteral("%1-bit desktop").arg(bits),"win32","read-only",
                QStringLiteral("QScreen's desktop depth; the console's Deep Color is an HDMI output option"));
        }
    }
    // ---- sound ------------------------------------------------------------------------------------
    if(pageId==QLatin1String("sound")) {
        if(rowId==QLatin1String("sound_output_setting")) {
            const QString name=firstOutputName(state);
            return value(name.isEmpty()?QStringLiteral("no output device"):name,"com","read-only");
        }
        if(rowId==QLatin1String("audio_multi_output"))
            return noValue(QStringLiteral("the console row sends one stream to several outputs at once; this "
                "build plays to the default endpoint only and has no fan-out setting"));
        if(rowId==QLatin1String("keytone"))return {};              // the app's own preference
        if(rowId==QLatin1String("host_volume"))return value(volumeText(state),"com","live");
        if(rowId==QLatin1String("host_mute"))return value(state.muted?QStringLiteral("On"):QStringLiteral("Off"),"com","live");
    }
    // ---- power ------------------------------------------------------------------------------------
    if(pageId==QLatin1String("power")) {
        if(rowId==QLatin1String("idle"))return {};                 // the app's own timer
        if(rowId==QLatin1String("controller"))
            return noValue(QStringLiteral("the console row turns an idle controller off; this build has no "
                "controller idle timer, and the attached pad's own state is on the row's page"));
    }
    // ---- accessory --------------------------------------------------------------------------------
    if(pageId==QLatin1String("device")) {
        // The console's motion-calibration, reassignment, vibration, BD-remote and Bluetooth rows have no
        // honest host value: the first three are console-driver operations, and the last two list console
        // devices. The attached pad's own state is on each row's page.
        if(rowId==QLatin1String("pd_calibrate_motion_controller"))
            return noValue(QStringLiteral("motion calibration is the controller driver's own; the attached pad "
                "is on the row's page"));
        if(rowId==QLatin1String("pd_pad_assign"))
            return noValue(QStringLiteral("the console row reassigns controller slots; this build opens no "
                "controller slot table, and the attached pad is on the row's page"));
        if(rowId==QLatin1String("pd_pad_vibration"))
            return noValue(QStringLiteral("the console row switches controller vibration; this build's pad "
                "reader does not expose a force-feedback setting"));
        if(rowId==QLatin1String("pd_bdremote_reg"))
            return noValue(QStringLiteral("the console row registers a BD remote; this build pairs no remote"));
        if(rowId==QLatin1String("pd_bt_utility"))
            return noValue(QStringLiteral("the console row lists the console's Bluetooth devices; this build "
                "reads only the PlayStation controllers plugged in or paired to this computer"));
        if(rowId==QLatin1String("pd_camera"))
            return value(state.cameras.isEmpty()?QStringLiteral("no camera"):state.cameras.first(),"qt","read-only");
        if(rowId==QLatin1String("pd_audio_setting")) {
            const QString name=firstInputName(state);
            return value(name.isEmpty()?QStringLiteral("no input device"):name,"com","read-only");
        }
        if(rowId==QLatin1String("pd_voice_changer"))
            return noValue(QStringLiteral("the console row applies a voice effect to chat audio; this build has "
                "no chat audio path"));
        if(rowId==QLatin1String("pd_keyboard_type"))
            return value(state.keyboard.layoutName.isEmpty()?QStringLiteral("no layout reported"):state.keyboard.layoutName,"win32","read-only");
        if(rowId==QLatin1String("pd_keyboard_input"))
            return noValue(QStringLiteral("the console row chooses an input method for its on-screen keyboard; "
                "this build's keyboard has no IME, and the installed layouts are on the row's page"));
        if(rowId==QLatin1String("pd_keyrepeat_start") || rowId==QLatin1String("pd_keyrepeat_speed") ||
           rowId==QLatin1String("pd_mouse_type") || rowId==QLatin1String("pd_pointer_speed"))return {};  // host choice rows
    }
    // ---- date and time ----------------------------------------------------------------------------
    if(pageId==QLatin1String("date")) {
        if(rowId==QLatin1String("datetime"))return {};             // the shell's own clock, formats included
        if(rowId==QLatin1String("autodate"))
            return value(state.clock.automatic?QStringLiteral("On"):QStringLiteral("Off"),"win32","read-only");
        if(rowId==QLatin1String("date_fmt") || rowId==QLatin1String("time_fmt") || rowId==QLatin1String("timezone"))
            return {};                                             // the app's own preferences
        if(rowId==QLatin1String("summertime"))
            return value(state.clock.daylight?QStringLiteral("Daylight"):QStringLiteral("Standard"),"qt","read-only");
    }
    // ---- theme ------------------------------------------------------------------------------------
    if(pageId==QLatin1String("theme")) {
        if(rowId==QLatin1String("theme") || rowId==QLatin1String("color") ||
           rowId==QLatin1String("background") || rowId==QLatin1String("font"))return {};
        if(rowId==QLatin1String("themefile") || rowId==QLatin1String("wallpaper"))return {};
    }
    // ---- security ---------------------------------------------------------------------------------
    // None of the six rows has an honest host source (SETTINGS 1.15: Windows' Family Safety is per-MSA and
    // not app-scoped), so none gets a value. Each page reports what the host does hold.
    if(pageId==QLatin1String("security")) {
        if(rowId==QLatin1String("pw_change"))
            return noValue(QStringLiteral("the console row sets a console PIN; this build has no PIN gate and "
                "does not touch the Windows account"));
        if(rowId==QLatin1String("bd_pc") || rowId==QLatin1String("dvd_pc") || rowId==QLatin1String("gameetc_pc"))
            return noValue(QStringLiteral("the console enforces its own parental levels; this build applies no "
                "restriction and Windows' Family Safety is bound to a Microsoft account, not to this app"));
        if(rowId==QLatin1String("bd_dvd_pcregion"))
            return noValue(QStringLiteral("the console row stores the region code its BD/DVD player uses; this "
                "build plays files, not discs"));
        if(rowId==QLatin1String("pc_browser"))
            return noValue(QStringLiteral("the console row blocks its own browser; this build's browser row "
                "hands the page to the default browser, which Windows' own controls govern"));
    }
    // ---- remote play ------------------------------------------------------------------------------
    // No PS3/Vita counterpart exists, so these rows report no value and say why on their pages.
    if(pageId==QLatin1String("remote")) {
        if(rowId==QLatin1String("device_registration") || rowId==QLatin1String("device_registration_list"))
            return noValue(QStringLiteral("Remote Play needs a PS Vita, PSP or Sony phone as the client and a "
                "console to connect to; this build runs no remote-play server"));
        if(rowId==QLatin1String("custom_connection_setting"))
            return noValue(QStringLiteral("the row's manual connection settings belong to the console's remote "
                "play service, which this build does not run"));
        if(rowId==QLatin1String("remote_start"))
            return noValue(QStringLiteral("remote start would let a remote client wake this system; the app "
                "opens no listener and reserves no port"));
        if(rowId==QLatin1String("audio_output_device"))
            return noValue(QStringLiteral("the row chooses where a remote session's audio goes; with no remote "
                "session the host's default output plays, and the Sound page reports it"));
    }
    // ---- printer ----------------------------------------------------------------------------------
    if(pageId==QLatin1String("print")) {
        if(rowId==QLatin1String("printer_selection"))
            return value(state.defaultPrinter.isEmpty()?QStringLiteral("no printer installed"):state.defaultPrinter,"winspool","read-only");
        if(rowId==QLatin1String("printer_manager"))
            return value(state.printers.isEmpty()?QStringLiteral("no printer installed"):QStringLiteral("%1 printers").arg(state.printers.size()),
                         "winspool","read-only");
    }
    // ---- chat -------------------------------------------------------------------------------------
    if(pageId==QLatin1String("chat")) {
        if(rowId==QLatin1String("av_chat_start"))
            return noValue(QStringLiteral("the row picks what a voice or video chat sends when it starts; this "
                "build has no chat service, so the choice has no event to act on"));
        if(rowId==QLatin1String("audio_device_setting")) {
            const QString name=firstInputName(state);
            return value(name.isEmpty()?QStringLiteral("no input device"):name,"com","read-only");
        }
        if(rowId==QLatin1String("camera_device_setting"))
            return value(state.cameras.isEmpty()?QStringLiteral("no camera"):state.cameras.first(),"qt","read-only");
    }
    // ---- game -------------------------------------------------------------------------------------
    // The console's PSP Remaster and PS upscaler rows: this build runs no such emulation, so no value.
    if(pageId==QLatin1String("game")) {
        if(rowId==QLatin1String("game_psp_emu_viewmode") || rowId==QLatin1String("game_psp_emu_3d") ||
           rowId==QLatin1String("game_psp_emu_adhoc_ch") || rowId==QLatin1String("game_psp_emu_adhoc_mode"))
            return noValue(QStringLiteral("the row configures the console's PSP Remaster emulator; this build "
                "runs no PSP emulation"));
        if(rowId==QLatin1String("game_ps_emu_upcon") || rowId==QLatin1String("game_ps_emu_smooth"))
            return noValue(QStringLiteral("the console upscales and smooths PlayStation and PS2 titles in "
                "hardware; this build launches PS3 titles through RPCS3, whose per-title settings hold its own "
                "scaling"));
    }
    // ---- video ------------------------------------------------------------------------------------
    // The BD/DVD player's 19 rows (SETTINGS 1.4). None has a host value: this build plays media files, not
    // discs, and the machine's own player options are the desktop's. Each row's page says what the row would
    // set and lists the RCO's own options; no value is invented (SETTINGS section 4).
    if(pageId==QLatin1String("video")) {
        if(rowId==QLatin1String("bd_menu_lang") || rowId==QLatin1String("bd_audio_lang") ||
           rowId==QLatin1String("bd_subtitle_lang") || rowId==QLatin1String("dvd_menu_lang") ||
           rowId==QLatin1String("dvd_audio_lang") || rowId==QLatin1String("dvd_subtitle_lang"))
            return noValue(QStringLiteral("the row sets a BD or DVD disc player's menu, audio or subtitle "
                "language; this build plays media files and has no disc player"));
        if(rowId==QLatin1String("bd_internet_connection"))
            return noValue(QStringLiteral("the row asks before a Blu-ray disc uses the Internet; this build "
                "opens no disc and runs no BD-Java"));
        if(rowId==QLatin1String("cinema_conversion"))
            return noValue(QStringLiteral("the row picks how interlaced film is converted for progressive "
                "output; this build's video output is the desktop's"));
        if(rowId==QLatin1String("dvd_wide_display"))
            return noValue(QStringLiteral("the row sets how 16:9 DVD content is shown on a 4:3 display; this "
                "build shows video in the XMB's own viewport"));
        if(rowId==QLatin1String("upscaler"))
            return noValue(QStringLiteral("the row upscales SD disc video to HD; the host's video pipeline "
                "does that itself and exposes no equivalent switch"));
        if(rowId==QLatin1String("video_output_format"))
            return noValue(QStringLiteral("the row chooses the colour signal a BD/DVD player sends over HDMI; "
                "Windows owns the display's colour output"));
        if(rowId==QLatin1String("hdmi_1080_24"))
            return noValue(QStringLiteral("the row plays 24 Hz disc content at 24 Hz over HDMI; the desktop's "
                "refresh rate is the host's, and this build never changes a display mode"));
        if(rowId==QLatin1String("dynamic_range_control"))
            return noValue(QStringLiteral("the row compresses a disc's Dolby dynamic range; this build's audio "
                "path applies no such processing"));
        if(rowId==QLatin1String("audio_format_hdmi") || rowId==QLatin1String("audio_format_optical"))
            return noValue(QStringLiteral("the row chooses how a disc's Dolby and DTS audio is sent to an HDMI "
                "or optical output; this build decodes files to the default endpoint"));
        if(rowId==QLatin1String("storage_audio_lang"))
            return noValue(QStringLiteral("the row sets the audio language for content on the system storage; "
                "this build plays each file's own first audio track"));
        if(rowId==QLatin1String("storage_subtitle_lang"))
            return noValue(QStringLiteral("the row sets the subtitle language for content on the system "
                "storage; this build selects subtitles per file from the sidecar"));
        if(rowId==QLatin1String("sequential_play"))
            return noValue(QStringLiteral("the row plays a storage device's items in sequence; this build's "
                "queue advances only from the player itself"));
        if(rowId==QLatin1String("hz_50"))
            return noValue(QStringLiteral("the row plays 50 Hz content at 50 Hz; this build renders the XMB at "
                "the desktop's rate and plays files at their own rate"));
        if(rowId==QLatin1String("repeat") || rowId==QLatin1String("shuffle"))return {};   // app preferences
        // The Volume row writes audio/volume, this build's own playback level; it is NOT the Windows
        // endpoint (Sound's Host Volume row is), so it must resolve through the store (AT-33).
        if(rowId==QLatin1String("volume"))return {};
    }
    // ---- music ------------------------------------------------------------------------------------
    if(pageId==QLatin1String("music")) {
        if(rowId==QLatin1String("import_musiccd"))
            return noValue(QStringLiteral("the row sets the codec and bit rate an audio CD is imported at; "
                "this build has no CD ripper and no disc player"));
        if(rowId==QLatin1String("crossfade_play"))
            return noValue(QStringLiteral("the row overlaps the end of one track with the next; this build's "
                "player does not crossfade"));
        if(rowId==QLatin1String("output_frequency")) {
            // The one Music row with a matrix-named host source: the endpoint's own mix rate.
            for(const auto& device:state.audioOutputs)if(device.sampleRate)
                return value(kilohertz(device.sampleRate),"com","read-only",
                    QStringLiteral("the default endpoint's mix format; Windows and the driver set it"));
            return noValue(QStringLiteral("the host did not report an endpoint sample rate"));
        }
        if(rowId==QLatin1String("bitmapping"))
            return noValue(QStringLiteral("the row shapes Super Audio CD noise; this build plays PCM and "
                "compressed files"));
        if(rowId==QLatin1String("dnormal"))
            return noValue(QStringLiteral("the row equalises track loudness; this build applies no loudness "
                "processing"));
        if(rowId==QLatin1String("repeat") || rowId==QLatin1String("shuffle"))return {};
        if(rowId==QLatin1String("volume"))return {};   // audio/volume: the app's own level, not the endpoint
    }
    // ---- system update ----------------------------------------------------------------------------
    // The console's two update methods are actions with no value on the console either; this build has no
    // PS3 system software to fetch or install, so neither row carries a value.
    if(pageId==QLatin1String("update")) {
        if(rowId==QLatin1String("net"))
            return noValue(QStringLiteral("the row downloads PS3 system software; this build is a native "
                "application with no update channel and no firmware to install"));
        if(rowId==QLatin1String("media"))
            return noValue(QStringLiteral("the row installs an update package from storage media; there is no "
                "system software here to update, so no medium can carry one"));
    }
    // ---- photo ------------------------------------------------------------------------------------
    if(pageId==QLatin1String("photo")) {
        if(rowId==QLatin1String("interval") || rowId==QLatin1String("fit") ||
           rowId==QLatin1String("repeat"))return {};
    }
    return {};
}

QString rowDetail(const QString& pageId,const QString& rowId) {
    const auto& state=snapshot();
    const auto lines=[&](const QStringList& body) {return body.join(QLatin1Char('\n'));};
    // ---- system -----------------------------------------------------------------------------------
    if(pageId==QLatin1String("system")) {
        if(rowId==QLatin1String("console_lang"))
            return lines({QStringLiteral("System Language"),
                QStringLiteral("Language: ")+state.clock.language,
                QStringLiteral("Locale: ")+state.clock.localeName,
                QStringLiteral("Region: ")+state.clock.country,
                QStringLiteral("Date pattern: ")+state.clock.datePattern,
                QStringLiteral("Time pattern: ")+state.clock.timePattern,
                QString{},
                QStringLiteral("The XMB language follows this computer's Windows display language. This build")
                ,QStringLiteral("ships one interface language, so the row reports the host's and changes nothing.")
                });
        if(rowId==QLatin1String("disc_utility")) {
            QStringList body{QStringLiteral("Format Utility")};
            for(const auto& volume:state.volumes)
                body<<volume.label+QStringLiteral(": ")+bytesText(volume.totalBytes)+QStringLiteral(" (")+
                    volume.kind+QStringLiteral(", ")+volume.filesystem+QStringLiteral(", ")+bytesText(volume.freeBytes)+
                    QStringLiteral(" free)");
            body<<QString{}
                <<QStringLiteral("Formatting a volume destroys every file on it and cannot be undone from this")
                <<QStringLiteral("build. Windows' own Disk Management is the tool for it, so this row reports the")
                <<QStringLiteral("drives and performs nothing.");
            return lines(body);
        }
        if(rowId==QLatin1String("backup_utility"))
            return lines({QStringLiteral("Backup Utility"),
                QStringLiteral("Data directory: ")+QDir::toNativeSeparators(state.dataDirectory),
                QStringLiteral("Size: ")+state.dataDirectorySize,
                QString{},
                QStringLiteral("This directory holds every preference, profile and resume point this build keeps."),
                QStringLiteral("It is the only state a backup would need to copy; no backup was made from here.")});
        if(rowId==QLatin1String("init_console"))
            return lines({QStringLiteral("Restore PS3 System"),
                QStringLiteral("Withheld: not performed"),
                QString{},
                QStringLiteral("The console's Restore PS3 System deletes the system storage and every user's data."),
                QStringLiteral("On this computer that would mean deleting ")+QDir::toNativeSeparators(state.dataDirectory),
                QStringLiteral("and the user profiles, which is destructive and irreversible."),
                QStringLiteral("Settings > Restore Default Settings resets this app's own settings instead.")});
        if(rowId==QLatin1String("ex_os_install"))
            return lines({QStringLiteral("Install Other OS"),
                QStringLiteral("This row has no equivalent on a computer"),
                QString{},
                QStringLiteral("The console row installs a second operating system into a reserved partition."),
                QStringLiteral("Windows has no such partition, this build ships no boot loader or partition writer,"),
                QStringLiteral("and a failure would leave the machine unable to start.")});
        if(rowId==QLatin1String("aboutps3"))
            return lines({QStringLiteral("About PlayStation 3"),
                QStringLiteral("Application: re-ps3-native ")+state.appVersion,
                QStringLiteral("Qt: ")+state.qtVersion,
                QStringLiteral("OS: ")+state.osName+QStringLiteral(" ")+state.osVersion,
                QStringLiteral("Architecture: ")+state.architecture,
                QStringLiteral("Data directory: ")+QDir::toNativeSeparators(state.dataDirectory),
                QString{},
                QStringLiteral("A native implementation of the 4.93 XMB. No Sony code runs and no firmware is installed;"),
                QStringLiteral("the labels and layout come from the local dev_flash the app reads at start-up.")});
    }
    // ---- network ----------------------------------------------------------------------------------
    if(pageId==QLatin1String("network")) {
        if(rowId==QLatin1String("net_internet")) {
            QStringList body{QStringLiteral("Internet Connection")};
            if(state.adapters.isEmpty())body<<QStringLiteral("No network adapter was found.");
            for(const auto& adapter:state.adapters)body<<adapterLine(adapter);
            body<<QString{}
                <<QStringLiteral("The row reports the host's link state. Windows owns the connection, and changing")
                <<QStringLiteral("it requires administrator rights, so no write is offered here.");
            return lines(body);
        }
        if(rowId==QLatin1String("net_internet_setting")) {
            QStringList body{QStringLiteral("Internet Connection Settings")};
            for(const auto& adapter:state.adapters) {
                body<<adapter.name+QStringLiteral(" (")+adapter.description+QLatin1Char(')');
                body<<QStringLiteral("Addressing: ")+(adapter.dhcp?QStringLiteral("DHCP"):QStringLiteral("Static"));
                if(!adapter.ipv4.isEmpty())body<<QStringLiteral("IPv4: ")+adapter.ipv4.first();
                if(!adapter.ipv6.isEmpty())body<<QStringLiteral("IPv6: ")+adapter.ipv6.first();
                if(!adapter.gateways.isEmpty())body<<QStringLiteral("Gateway: ")+adapter.gateways.join(QStringLiteral(", "));
                if(!adapter.dns.isEmpty())body<<QStringLiteral("DNS: ")+adapter.dns.join(QStringLiteral(", "));
                if(!adapter.dhcpServer.isEmpty())body<<QStringLiteral("DHCP server: ")+adapter.dhcpServer;
                if(!adapter.dhcpLease.isEmpty())body<<QStringLiteral("Lease: ")+adapter.dhcpLease;
                body<<QStringLiteral("MAC: ")+adapter.mac;
            }
            body<<QString{}<<QStringLiteral("Writing an address, gateway or DNS server needs administrator rights and")
                <<QStringLiteral("could disconnect the machine, so the row stays read-only by design.");
            return lines(body);
        }
        if(rowId==QLatin1String("net_internet_test")) {
            // The row's own job: test the connection and report the result. The interface state is read from a
            // fresh snapshot (not the two-second cache) and one bounded TCP connection to a public resolver is
            // opened, so a "Connected" answer means a packet really made the round trip. Nothing is written:
            // no address, gateway, DNS server or adapter state changes (SETTINGS 3.5/AT-21).
            invalidateSnapshot();
            const auto test=networkTest(true);
            QStringList body{QStringLiteral("Internet Connection Test"),
                QStringLiteral("Result: ")+(test.value("ok").toBool()?QStringLiteral("Connected"):QStringLiteral("Not connected")),
                QStringLiteral("Detail: ")+test.value("detail").toString(),
                QStringLiteral("Interface: ")+test.value("interface").toString(),
                QString{},
                QStringLiteral("Method: ")+test.value("method").toString(),
                QStringLiteral("No network configuration was written: the test reads the interface and opens one"),
                QStringLiteral("TCP connection, then reports the result of both.")};
            return lines(body);
        }
        if(rowId==QLatin1String("net_media_server"))
            return lines({QStringLiteral("Media Server Connection"),
                QStringLiteral("Windows media sharing service (WMPNetworkSvc): ")+state.dlnaService,
                QString{},
                QStringLiteral("The console row joins a DLNA media server. On this computer that service is"),
                QStringLiteral("Windows' own; starting it or changing its sharing settings needs administrator"),
                QStringLiteral("rights, so the row only reports its state.")});
    }
    // ---- display ----------------------------------------------------------------------------------
    if(pageId==QLatin1String("display")) {
        if(rowId==QLatin1String("cross_color_filter"))
            return lines({QStringLiteral("Cross Color Reduction Filter"),
                QStringLiteral("A composite-video filter with no digital counterpart"),
                QString{},
                QStringLiteral("The console filter suppresses cross-colour artefacts of a composite (CVBS) cable."),
                QStringLiteral("A PC drives its display over DisplayPort, HDMI, DVI or the panel's own link, where"),
                QStringLiteral("that artefact does not exist, so there is nothing to filter and no value to show.")});
        if(rowId==QLatin1String("rgb_full_range"))
            return lines({QStringLiteral("RGB Full Range (HDMI)"),
                QStringLiteral("The display driver holds this setting; Windows exposes no API for it"),
                QStringLiteral("Display: ")+displayProfileText(state),
                QString{},
                QStringLiteral("The full/limited RGB range is an HDMI output setting owned by the display driver."),
                QStringLiteral("Windows exposes no public per-output API for it, so the row cannot read the value"),
                QStringLiteral("the graphics driver holds. The driver's control panel is where it is set.")});
        if(rowId==QLatin1String("super_white"))
            return lines({QStringLiteral("Y Pb/Cb Pr/Cr Super-White (HDMI)"),
                QStringLiteral("The display driver holds this setting; Windows exposes no API for it"),
                QString{},
                QStringLiteral("Super-white passes the values above video white (235) in a YCbCr signal."),
                QStringLiteral("Like the RGB range it is a driver-owned HDMI setting with no Windows API, so the"),
                QStringLiteral("row reports that it cannot be read rather than inventing a value.")});
        if(rowId==QLatin1String("deep_color")) {
            QStringList body{QStringLiteral("Deep Color Output (HDMI)")};
            for(const auto& display:state.displays) {
                body<<QStringLiteral("Mode: ")+QStringLiteral("%1 x %2 at %3 Hz").arg(display.resolution.width())
                    .arg(display.resolution.height())
                    .arg(display.refresh>0?QString::number(std::lround(display.refresh)):QStringLiteral("?"));
                body<<QStringLiteral("Desktop depth: ")+(display.bitsPerPixel?QString::number(display.bitsPerPixel)+QStringLiteral("-bit"):
                    QStringLiteral("not reported"));
            }
            body<<QString{}<<QStringLiteral("Deep Colour raises the HDMI link above 8 bits per channel. Windows")
                <<QStringLiteral("reports the desktop's depth, and the link's own depth is the driver's setting;")
                <<QStringLiteral("the row shows the depth this session really runs at.");
            return lines(body);
        }
    }
    // ---- sound ------------------------------------------------------------------------------------
    if(pageId==QLatin1String("sound")) {
        if(rowId==QLatin1String("sound_output_setting"))return audioOutput();
        if(rowId==QLatin1String("audio_multi_output"))
            return lines({QStringLiteral("Audio Multi-Output"),
                QStringLiteral("This build plays to one endpoint"),
                QString{},
                QStringLiteral("The console can send one stream to two outputs at once. Windows Core Audio plays to"),
                QStringLiteral("one default endpoint, and routing to several at once needs a virtual audio driver"),
                QStringLiteral("this build does not ship, so the row reports that and offers no write.")});
        if(rowId==QLatin1String("host_volume"))
            return lines({QStringLiteral("Volume"),
                QStringLiteral("Windows default output: ")+volumeText(state),
                QStringLiteral("Device: ")+firstOutputName(state),
                QString{},
                QStringLiteral("Cross opens the console's 0-100 step list; the write goes straight to the default"),
                QStringLiteral("endpoint through Core Audio, which is the same path the media keys use.")});
        if(rowId==QLatin1String("host_mute"))
            return lines({QStringLiteral("Mute"),
                QStringLiteral("Windows default output: ")+(state.muted?QStringLiteral("muted"):QStringLiteral("unmuted")),
                QString{},
                QStringLiteral("Cross toggles the default endpoint's mute through Core Audio.")});
    }
    // ---- power ------------------------------------------------------------------------------------
    if(pageId==QLatin1String("power")) {
        if(rowId==QLatin1String("idle")) {
            QStringList body{QStringLiteral("System Auto-Off"),
                QStringLiteral("Power scheme: ")+(state.powerScheme.isEmpty()?QStringLiteral("not readable"):state.powerScheme),
                QStringLiteral("Monitor off (plugged in): ")+(state.monitorTimer.acMinutes<0?QStringLiteral("not readable"):
                    state.monitorTimer.acMinutes?QStringLiteral("%1 minutes").arg(state.monitorTimer.acMinutes):QStringLiteral("never")),
                QStringLiteral("Sleep (plugged in): ")+(state.sleepTimer.acMinutes<0?QStringLiteral("not readable"):
                    state.sleepTimer.acMinutes?QStringLiteral("%1 minutes").arg(state.sleepTimer.acMinutes):QStringLiteral("never")),
                QString{},
                QStringLiteral("The app's own Auto-Off turns the panel off and leaves the XMB running. The host's"),
                QStringLiteral("power plan timers are read here for reference; changing them is an administrator"),
                QStringLiteral("action and is not performed.")};
            return lines(body);
        }
        if(rowId==QLatin1String("controller"))
            return lines({QStringLiteral("Controller Auto-Off"),
                QStringLiteral("Attached: ")+padText(state),
                QString{},
                QStringLiteral("The console powers a controller down after an idle period. A PlayStation controller's"),
                QStringLiteral("power state belongs to its own firmware and to Windows' HID stack; neither offers"),
                QStringLiteral("a documented way to switch one off from an application.")});
    }
    // ---- accessory --------------------------------------------------------------------------------
    if(pageId==QLatin1String("device")) {
        if(rowId==QLatin1String("pd_keyboard_type"))
            return lines({QStringLiteral("Keyboard Type"),
                QStringLiteral("Active layout: ")+(state.keyboard.layoutName.isEmpty()?QStringLiteral("not reported"):state.keyboard.layoutName),
                QStringLiteral("Layout id: ")+(state.keyboard.layoutId.isEmpty()?QStringLiteral("not reported"):state.keyboard.layoutId),
                QStringLiteral("Locale: ")+(state.keyboard.layoutLocale.isEmpty()?QStringLiteral("not reported"):state.keyboard.layoutLocale),
                QStringLiteral("Installed layouts: ")+QString::number(state.keyboard.installed),
                QString{},
                QStringLiteral("Windows switches keyboard layouts per window and per user; an application cannot"),
                QStringLiteral("change the system layout, so the row reports the active one and writes nothing.")});
        if(rowId==QLatin1String("pd_keyboard_input"))
            return lines({QStringLiteral("Keyboard Entry Method"),
                QStringLiteral("Active layout: ")+(state.keyboard.layoutName.isEmpty()?QStringLiteral("not reported"):state.keyboard.layoutName),
                QString{},
                QStringLiteral("The console's row chooses direct kana input or romaji for Japanese. The text this"),
                QStringLiteral("build receives is whatever the focused Windows layout produces.")});
        if(rowId==QLatin1String("pd_keyrepeat_start"))
            return lines({QStringLiteral("Key Repeat Interval"),
                QStringLiteral("Current: ")+QStringLiteral("%1 of 3 (SPI_GETKEYBOARDDELAY %2)").arg(QString::number(state.keyboard.repeatDelay+1))
                    .arg(state.keyboard.repeatDelay),
                QString{},
                QStringLiteral("This row writes the Windows keyboard delay (250/500/750/1000 ms) through"),
                QStringLiteral("SystemParametersInfo. The change is immediate, applies to this user and needs no"),
                QStringLiteral("administrator rights; picking the current value writes it again unchanged.")});
        if(rowId==QLatin1String("pd_keyrepeat_speed"))
            return lines({QStringLiteral("Key Repeat Speed"),
                QStringLiteral("Current: SPI_GETKEYBOARDSPEED %1 of 31").arg(state.keyboard.repeatSpeed),
                QString{},
                QStringLiteral("This row writes the Windows keyboard repeat rate through SystemParametersInfo."),
                QStringLiteral("Fast is 31, Normal 16 and Slow 4 on the 0-31 scale; the change is immediate and needs"),
                QStringLiteral("no administrator rights.")});
        if(rowId==QLatin1String("pd_mouse_type"))
            return lines({QStringLiteral("Mouse Type"),
                QStringLiteral("Current: ")+(state.mouse.swapped?QStringLiteral("Left-Handed"):QStringLiteral("Right-Handed")),
                QStringLiteral("Buttons: ")+QString::number(state.mouse.buttons),
                QString{},
                QStringLiteral("This row swaps the primary and secondary mouse buttons through SwapMouseButton."),
                QStringLiteral("The change is immediate, system-wide and reversible by picking the other item.")});
        if(rowId==QLatin1String("pd_pointer_speed"))
            return lines({QStringLiteral("Pointer Speed"),
                QStringLiteral("Current: SPI_GETMOUSESPEED %1 of 20").arg(state.mouse.speed),
                QString{},
                QStringLiteral("This row sets the pointer speed through SystemParametersInfo (Fast 18, Normal 10,"),
                QStringLiteral("Slow 4 on Windows' 1-20 scale). The change is immediate and needs no administrator"),
                QStringLiteral("rights.")});
        if(rowId==QLatin1String("pd_calibrate_motion_controller"))
            return lines({QStringLiteral("Calibrate Motion Controller"),
                QStringLiteral("Attached: ")+padText(state),
                QString{},
                QStringLiteral("Motion calibration is stored in the controller's own firmware and is performed by"),
                QStringLiteral("Windows' HID stack when the pad enumerates. This build reads the pad's input and"),
                QStringLiteral("battery only, so the row reports what is attached.")});
        if(rowId==QLatin1String("pd_pad_assign"))
            return lines({QStringLiteral("Reassign Controllers"),
                QStringLiteral("Attached: ")+padText(state),
                QString{},
                QStringLiteral("Controller numbering is the XInput slot Windows assigns on enumeration.")});
        if(rowId==QLatin1String("pd_pad_vibration"))
            return lines({QStringLiteral("Controller Vibration Function"),
                QStringLiteral("Attached: ")+padText(state),
                QString{},
                QStringLiteral("The app sends no vibration reports, so the function is off for every pad. Windows"),
                QStringLiteral("does not let an application switch another application's vibration off.")});
        if(rowId==QLatin1String("pd_bdremote_reg"))
            return lines({QStringLiteral("Register BD Remote Control"),
                QStringLiteral("Registered: none"),
                QString{},
                QStringLiteral("A BD remote pairs over Bluetooth as a HID device; this build enumerates HID pads"),
                QStringLiteral("only and has no pairing flow. Windows Settings owns Bluetooth pairing.")});
        if(rowId==QLatin1String("pd_bt_utility"))
            return lines({QStringLiteral("Manage Bluetooth Devices"),
                QStringLiteral("Attached: ")+padText(state),
                QString{},
                QStringLiteral("Bluetooth pairing is Windows' (Settings > Bluetooth and devices). This build reads"),
                QStringLiteral("the HID devices that are already paired.")});
        if(rowId==QLatin1String("pd_camera"))
            return lines({QStringLiteral("Camera Device Settings"),
                QStringLiteral("Cameras reported: ")+(state.cameras.isEmpty()?QStringLiteral("none"):state.cameras.join(QStringLiteral(", "))),
                QString{},
                QStringLiteral("The list is what Windows' media devices enumerate for this session.")});
        if(rowId==QLatin1String("pd_audio_setting"))
            return lines({QStringLiteral("Audio Device Settings"),
                QStringLiteral("Default input: ")+(firstInputName(state).isEmpty()?QStringLiteral("no input device"):firstInputName(state)),
                QStringLiteral("Input devices: ")+QString::number(state.audioInputs.size()),
                QString{},
                QStringLiteral("Windows owns the default recording device; it is set in Settings > System > Sound.")});
        if(rowId==QLatin1String("pd_voice_changer"))
            return lines({QStringLiteral("Voice Changer"),
                QStringLiteral("This build applies no voice effect"),
                QString{},
                QStringLiteral("The console row processes microphone audio for chat. This build plays media and has")});
    }
    // ---- date and time ----------------------------------------------------------------------------
    if(pageId==QLatin1String("date")) {
        if(rowId==QLatin1String("autodate"))
            return lines({QStringLiteral("Set Automatically"),
                QStringLiteral("State: ")+(state.clock.automatic?QStringLiteral("On"):QStringLiteral("Off")),
                QStringLiteral("Detail: ")+state.clock.autoDetail,
                QStringLiteral("Time zone: ")+state.clock.timeZoneId+QStringLiteral(" (")+clockOffsetText(state.clock.utcOffsetSeconds)+QLatin1Char(')'),
                QString{},
                QStringLiteral("Windows keeps the clock in sync through the Windows Time service. Starting or"),
                QStringLiteral("stopping that service needs administrator rights, so this row reports the state"),
                QStringLiteral("and never changes it. The console's own automatic setting has no host equivalent.")});
        if(rowId==QLatin1String("summertime"))
            return lines({QStringLiteral("Daylight Saving"),
                QStringLiteral("Now: ")+(state.clock.daylight?QStringLiteral("Daylight Saving"):QStringLiteral("Standard")),
                QStringLiteral("Zone: ")+state.clock.timeZoneId,
                QStringLiteral("Standard name: ")+(state.clock.standardName.isEmpty()?QStringLiteral("not reported"):state.clock.standardName),
                QStringLiteral("Daylight name: ")+(state.clock.daylightName.isEmpty()?QStringLiteral("not reported"):state.clock.daylightName),
                QString{},
                QStringLiteral("The host applies daylight saving through its time zone, automatically. The console's"),
                QStringLiteral("switch is a manual override; on Windows the zone's own rule replaces it.")});
    }
    // ---- security ---------------------------------------------------------------------------------
    if(pageId==QLatin1String("security")) {
        if(rowId==QLatin1String("pw_change"))
            return lines({QStringLiteral("Change Password"),
                QStringLiteral("Windows account: ")+(state.windowsAccount.isEmpty()?QStringLiteral("not reported"):state.windowsAccount),
                QStringLiteral("Display name: ")+(state.windowsDisplayName.isEmpty()?QStringLiteral("not reported"):state.windowsDisplayName),
                QStringLiteral("Account SID: ")+(state.windowsSid.isEmpty()?QStringLiteral("not reported"):state.windowsSid),
                QStringLiteral("Domain: ")+(state.windowsDomain.isEmpty()?QStringLiteral("local"):state.windowsDomain),
                QString{},
                QStringLiteral("The password this row would change is the Windows account's. Changing it needs the"),
                QStringLiteral("account's own flow (Windows Settings or Ctrl+Alt+Del) and would lock this session out"),
                QStringLiteral("if it failed, so the row names the account and changes nothing.")});
        if(rowId==QLatin1String("bd_pc") || rowId==QLatin1String("dvd_pc") || rowId==QLatin1String("gameetc_pc"))
            return lines({QStringLiteral("Parental Control"),
                QStringLiteral("This build applies no content restriction"),
                QString{},
                QStringLiteral("The console's level list rates disc and game content it launches itself. This build"),
                QStringLiteral("launches titles through their own launchers, applies no rating of its own, and"),
                QStringLiteral("Windows' family controls live in the Microsoft account's settings.")});
        if(rowId==QLatin1String("bd_dvd_pcregion"))
            return lines({QStringLiteral("BD/DVD - Parental Control Region Code"),
                QStringLiteral("Optical drive: ")+opticalText(state),
                QString{},
                QStringLiteral("The region code is stored in the disc drive's firmware and can be changed only a"),
                QStringLiteral("few times. This computer has no drive to hold one, so the row reports that.")});
        if(rowId==QLatin1String("pc_browser"))
            return lines({QStringLiteral("Internet Browser Start Control"),
                QStringLiteral("Default browser: ")+(state.defaultBrowser.isEmpty()?QStringLiteral("not set"):state.defaultBrowser),
                QString{},
                QStringLiteral("The console row blocks the built-in browser. On this computer the Network column's"),
                QStringLiteral("Internet Browser row opens the default browser above, which Windows' own controls"),
                QStringLiteral("govern; the app adds no restriction on top.")});
    }
    // ---- remote play ------------------------------------------------------------------------------
    if(pageId==QLatin1String("remote")) {
        if(rowId==QLatin1String("device_registration") || rowId==QLatin1String("device_registration_list"))
            return lines({QStringLiteral("Remote Play Device"),
                QStringLiteral("Registered devices: none"),
                QString{},
                QStringLiteral("Remote Play needs a PS Vita, PSP or Sony phone as the client, and this build runs no"),
                QStringLiteral("remote-play server. No device can be registered, so the list is empty.")});
        if(rowId==QLatin1String("custom_connection_setting")) {
            QStringList body{QStringLiteral("Custom Connection Settings")};
            for(const auto& adapter:state.adapters)body<<adapterLine(adapter);
            body<<QString{}
                <<QStringLiteral("Remote Play's manual connection settings have no host counterpart: the app runs no")
                <<QStringLiteral("server to connect to. The host's own addressing is shown above.");
            return lines(body);
        }
        if(rowId==QLatin1String("remote_start"))
            return lines({QStringLiteral("Remote Start"),
                QStringLiteral("The app opens no listener and reserves no port"),
                QString{},
                QStringLiteral("Remote start would let a remote client wake and start this system. Nothing on this"),
                QStringLiteral("computer listens for a remote-play connection, so there is no state to enable.")});
        if(rowId==QLatin1String("audio_output_device"))
            return lines({QStringLiteral("Audio Output Device"),
                QStringLiteral("Current: ")+(firstOutputName(state).isEmpty()?QStringLiteral("no output device"):firstOutputName(state)),
                QStringLiteral("Devices: ")+QString::number(state.audioOutputs.size()),
                QString{},
                QStringLiteral("The console row chooses whether a remote session's audio goes to the PSP or the"),
                QStringLiteral("PS3. With no remote session, the host's default output is what plays.")});
    }
    // ---- printer ----------------------------------------------------------------------------------
    if(pageId==QLatin1String("print")) {
        if(rowId==QLatin1String("printer_selection") || rowId==QLatin1String("printer_manager")) {
            QStringList body{QStringLiteral("Printer Settings")};
            if(state.printers.isEmpty())body<<QStringLiteral("No printer is installed for this account.");
            for(const auto& printer:state.printers) {
                body<<printer.name+(printer.isDefault?QStringLiteral(" (default)"):QString{});
                body<<QStringLiteral("Driver: ")+printer.driver+QStringLiteral(", port ")+printer.port+
                    (printer.offline?QStringLiteral(", offline"):QString{});
            }
            body<<QString{}
                <<QStringLiteral("The printers are Windows'. Adding, removing or choosing a default printer is a")
                <<QStringLiteral("Windows action (Settings > Bluetooth and devices > Printers), so the rows report")
                <<QStringLiteral("the installed devices and change nothing.");
            return lines(body);
        }
    }
    // ---- chat -------------------------------------------------------------------------------------
    if(pageId==QLatin1String("chat")) {
        if(rowId==QLatin1String("av_chat_start"))
            return lines({QStringLiteral("Voice/Video Chat Start"),
                QStringLiteral("This build has no chat transport"),
                QString{},
                QStringLiteral("The console row chooses whether a chat sends video or the avatar when it starts."),
                QStringLiteral("This build has no voice or video chat service, so the choice has no event to act"),
                QStringLiteral("on and nothing is stored.")});
        if(rowId==QLatin1String("audio_device_setting"))
            return lines({QStringLiteral("Audio Device Settings"),
                QStringLiteral("Default input: ")+(firstInputName(state).isEmpty()?QStringLiteral("no input device"):firstInputName(state)),
                QStringLiteral("Input devices: ")+QString::number(state.audioInputs.size()),
                QString{},
                QStringLiteral("Windows owns the default recording device; there is no chat client here to bind it.")});
        if(rowId==QLatin1String("camera_device_setting"))
            return lines({QStringLiteral("Camera Device Settings"),
                QStringLiteral("Cameras: ")+(state.cameras.isEmpty()?QStringLiteral("none"):state.cameras.join(QStringLiteral(", "))),
                QString{},
                QStringLiteral("The list is what Windows reports; this build has no video-chat consumer for it.")});
    }
    // ---- game -------------------------------------------------------------------------------------
    if(pageId==QLatin1String("game")) {
        if(rowId.startsWith(QLatin1String("game_psp_emu")))
            return lines({QStringLiteral("PSP Remaster"),
                QStringLiteral("No PSP emulator is installed or supported by this build"),
                QString{},
                QStringLiteral("These rows configure the console's PSP remaster emulator: its view mode, 3D display"),
                QStringLiteral("and ad-hoc wireless channel. The app launches RPCS3 for PS3 titles and runs no"),
                QStringLiteral("PSP emulation, so the rows have no target to configure.")});
        if(rowId.startsWith(QLatin1String("game_ps_emu")))
            return lines({QStringLiteral("PS / PS2 Upscaler"),
                QStringLiteral("No PS1 or PS2 emulator is installed or supported by this build"),
                QString{},
                QStringLiteral("The console upscales and smooths PlayStation and PS2 titles in hardware. This"),
                QStringLiteral("build launches PS3 titles through RPCS3, whose own per-title settings hold any"),
                QStringLiteral("upscaling it offers.")});
    }
    // ---- video ------------------------------------------------------------------------------------
    // Each of the BD/DVD player's 19 rows (SETTINGS 1.4) opens this page: what the row sets, the state of the
    // machine's disc player, and why no value is shown. The RCO's own option list is appended by
    // settings_menu's row-body builder where the firmware carries one.
    if(pageId==QLatin1String("video")) {
        if(rowId==QLatin1String("storage_audio_lang") || rowId==QLatin1String("storage_subtitle_lang"))
            return lines({QStringLiteral("System Storage Language"),
                QStringLiteral("Applies to content saved on the console's system storage"),
                QString{},
                QStringLiteral("The console row chooses which audio or subtitle language to prefer for content on"),
                QStringLiteral("the system storage. This build plays each file's own tracks and selects a"),
                QStringLiteral("sidecar subtitle file per title, so there is no stored preference to set.")});
        if(rowId.endsWith(QLatin1String("_lang")) || rowId==QLatin1String("bd_internet_connection"))
            return lines({QStringLiteral("BD/DVD Settings"),
                QStringLiteral("Optical drive: ")+opticalText(state),
                QString{},
                QStringLiteral("These rows choose a Blu-ray or DVD disc player's menu, audio and subtitle languages"),
                QStringLiteral("and whether a disc may use the Internet. This build plays media files, not discs,"),
                QStringLiteral("so the row reports the drive and configures nothing.")});
        return lines({QStringLiteral("BD/DVD Player Settings"),
            QStringLiteral("Optical drive: ")+opticalText(state),
            QString{},
            QStringLiteral("The console row sets an option of its Blu-ray/DVD player: the video or audio format it"),
            QStringLiteral("sends over HDMI or optical digital, the cadence it plays film at, or how it upscales."),
            QStringLiteral("This build plays video files through the host's own pipeline, which owns those"),
            QStringLiteral("choices, so the row reports that and shows the console's options.")});
    }
    // ---- music ------------------------------------------------------------------------------------
    if(pageId==QLatin1String("music")) {
        if(rowId==QLatin1String("import_musiccd"))
            return lines({QStringLiteral("Audio CD Import"),
                QStringLiteral("Optical drive: ")+opticalText(state),
                QString{},
                QStringLiteral("The row sets the codec and bit rate an audio CD is imported at. This build has no CD"),
                QStringLiteral("ripper and the machine has no optical drive, so there is nothing to import.")});
        if(rowId==QLatin1String("crossfade_play"))
            return lines({QStringLiteral("Crossfade Playback"),
                QStringLiteral("No crossfade is applied"),
                QString{},
                QStringLiteral("The console overlaps the end of one track with the next. This build's player stops the"),
                QStringLiteral("current file and starts the next, so there is no crossfade length to set.")});
        if(rowId==QLatin1String("output_frequency")) {
            QStringList body{QStringLiteral("Output Frequency")};
            for(const auto& device:state.audioOutputs) {
                body<<device.name;
                if(device.sampleRate)body<<QStringLiteral("Mix format: ")+kilohertz(device.sampleRate)+
                    (device.channels?QStringLiteral(", %1 channels").arg(device.channels):QString{})+
                    (device.bits?QStringLiteral(", %1-bit").arg(device.bits):QString{});
            }
            body<<QString{}
                <<QStringLiteral("The sample rate is the endpoint's own mix format, which Windows and the driver")
                <<QStringLiteral("set. Changing it is the device's control panel, not this app.");
            return lines(body);
        }
        if(rowId==QLatin1String("bitmapping"))
            return lines({QStringLiteral("Bitmapping"),
                QStringLiteral("No bitmapping mode applies"),
                QString{},
                QStringLiteral("Bitmapping shapes the noise of a Super Audio CD's DSD stream. This build plays"),
                QStringLiteral("PCM and compressed files, so there is no DSD stream to shape.")});
        if(rowId==QLatin1String("dnormal"))
            return lines({QStringLiteral("Dynamic Normalizer"),
                QStringLiteral("No loudness processing is applied"),
                QString{},
                QStringLiteral("The console row equalises the loudness of the tracks it plays. This build plays each"),
                QStringLiteral("file at the level the file carries.")});
    }
    // ---- system update ----------------------------------------------------------------------------
    if(pageId==QLatin1String("update")) {
        if(rowId==QLatin1String("net"))
            return lines({QStringLiteral("Update via Internet"),
                QStringLiteral("Application: re-ps3-native ")+state.appVersion,
                QString{},
                QStringLiteral("The console row downloads PS3 system software. This build is a native application with"),
                QStringLiteral("no update channel and no firmware to install; it is updated by replacing it.")});
        if(rowId==QLatin1String("media"))
            return lines({QStringLiteral("Update via Storage Media"),
                QStringLiteral("There is no system software here to update"),
                QString{},
                QStringLiteral("The console row installs an update package from a storage device. This build is a"),
                QStringLiteral("native application with no firmware, so no medium can carry an update for it.")});
    }
    return {};
}

// ==== the JSON report ==============================================================================
QJsonObject snapshotJson(const Snapshot& state) {
    QJsonArray volumes;
    for(const auto& volume:state.volumes)
        volumes.push_back(QJsonObject{{"label",volume.label},{"mount",volume.mount},{"filesystem",volume.filesystem},
            {"kind",volume.kind},{"totalBytes",double(volume.totalBytes)},{"freeBytes",double(volume.freeBytes)},
            {"removable",volume.removable},{"readOnly",volume.readOnly},{"system",volume.system}});
    QJsonArray adapters;
    for(const auto& adapter:state.adapters)
        adapters.push_back(QJsonObject{{"name",adapter.name},{"description",adapter.description},{"kind",adapter.kind},
            {"up",adapter.up},{"wireless",adapter.wireless},{"dhcp",adapter.dhcp},{"mac",adapter.mac},
            {"linkSpeed",double(adapter.linkSpeed)},{"mtu",adapter.mtu},{"ssid",adapter.ssid},
            {"prefixLength",adapter.prefixLength},{"subnet",adapter.subnet},
            {"signalQuality",adapter.signalQuality},{"ipv4",QJsonArray::fromStringList(adapter.ipv4)},
            {"ipv6",QJsonArray::fromStringList(adapter.ipv6)},{"gateways",QJsonArray::fromStringList(adapter.gateways)},
            {"dns",QJsonArray::fromStringList(adapter.dns)},{"dhcpServer",adapter.dhcpServer},
            {"dhcpLease",adapter.dhcpLease}});
    QJsonArray displays;
    for(const auto& display:state.displays)
        displays.push_back(QJsonObject{{"name",display.name},{"monitor",display.monitor},{"adapter",display.adapter},
            {"deviceId",display.deviceId},{"width",display.resolution.width()},{"height",display.resolution.height()},
            {"refreshHz",display.refresh},{"depth",display.depth},{"bitsPerPixel",display.bitsPerPixel},
            {"dpi",display.dpi},{"primary",display.primary},{"edid",display.edid}});
    const auto audioArray=[](const QVector<AudioInfo>& devices) {
        QJsonArray array;
        for(const auto& device:devices)
            array.push_back(QJsonObject{{"name",device.name},{"adapter",device.adapter},{"id",device.id},
                {"default",device.isDefault},{"channels",device.channels},{"sampleRate",device.sampleRate},
                {"bits",device.bits}});
        return array;
    };
    QJsonArray printers;
    for(const auto& printer:state.printers)
        printers.push_back(QJsonObject{{"name",printer.name},{"driver",printer.driver},{"port",printer.port},
            {"default",printer.isDefault},{"offline",printer.offline}});
    QJsonArray pads;
    for(const auto& pad:state.pads)
        pads.push_back(QJsonObject{{"name",pad.name},{"transport",pad.transport},{"vendor",pad.vendor},
            {"product",pad.product},{"wireless",pad.wireless},{"batteryPercent",pad.batteryPercent},
            {"charging",pad.charging}});
    const QJsonObject clock{{"timeZoneId",state.clock.timeZoneId},{"timeZoneName",state.clock.timeZoneName},
        {"standardName",state.clock.standardName},{"daylightName",state.clock.daylightName},
        {"utcOffsetSeconds",state.clock.utcOffsetSeconds},{"utcOffset",clockOffsetText(state.clock.utcOffsetSeconds)},
        {"daylight",state.clock.daylight},{"now",state.clock.now.toString(Qt::ISODate)},
        {"locale",state.clock.localeName},{"language",state.clock.language},{"country",state.clock.country},
        {"datePattern",state.clock.datePattern},{"timePattern",state.clock.timePattern},
        {"automatic",state.clock.automatic},{"timeServiceRunning",state.clock.timeServiceRunning},
        {"autoDetail",state.clock.autoDetail}};
    const QJsonObject system{{"hostName",state.hostName},{"osName",state.osName},{"osVersion",state.osVersion},
        {"kernel",state.kernel},{"architecture",state.architecture},{"cpu",state.cpuName},{"cpuClock",state.cpuClock},
        {"cpuCores",state.cpuCores},{"cpuLogical",state.cpuLogical},{"ramTotalBytes",double(state.ramTotal)},
        {"ramFreeBytes",double(state.ramFree)},{"gpu",state.gpuName},{"gpuDriver",state.gpuDriver},
        {"gpuMemory",state.gpuMemory},{"motherboard",state.motherboard},{"bios",state.bios},
        {"windowsAccount",state.windowsAccount},{"windowsDisplayName",state.windowsDisplayName},
        {"windowsDomain",state.windowsDomain},{"windowsSid",state.windowsSid},{"administrator",state.windowsAdmin},
        {"opticalDrive",state.opticalDrive},{"defaultBrowser",state.defaultBrowser},{"dlnaService",state.dlnaService},
        {"dataDirectory",QDir::toNativeSeparators(state.dataDirectory)},{"dataDirectorySize",state.dataDirectorySize},
        {"appVersion",state.appVersion},{"qtVersion",state.qtVersion}};
    return QJsonObject{
        {"elevation",QJsonObject{{"elevated",elevated()},{"detail",elevationText()}}},
        {"privileges",QJsonArray::fromStringList(privilegeNotes())},
        {"system",system},
        {"clock",clock},
        {"storage",volumes},
        {"network",QJsonObject{{"adapters",adapters}}},
        {"display",QJsonObject{{"outputs",displays}}},
        {"audio",QJsonObject{{"outputs",audioArray(state.audioOutputs)},{"inputs",audioArray(state.audioInputs)},
            {"volume",state.volume},{"muted",state.muted},{"cameras",QJsonArray::fromStringList(state.cameras)}}},
        {"power",QJsonObject{{"battery",QJsonObject{{"present",state.battery.present},{"percent",state.battery.percent},
            {"state",state.battery.state},{"acOnline",state.battery.acOnline},{"secondsLeft",double(state.battery.secondsLeft)}}},
            {"scheme",state.powerScheme},
            {"monitorTimer",QJsonObject{{"acMinutes",state.monitorTimer.acMinutes},{"dcMinutes",state.monitorTimer.dcMinutes}}},
            {"sleepTimer",QJsonObject{{"acMinutes",state.sleepTimer.acMinutes},{"dcMinutes",state.sleepTimer.dcMinutes}}}}},
        {"accessory",QJsonObject{{"pads",pads},
            {"keyboard",QJsonObject{{"layoutName",state.keyboard.layoutName},{"layoutId",state.keyboard.layoutId},
                {"layoutLocale",state.keyboard.layoutLocale},{"repeatDelay",state.keyboard.repeatDelay},
                {"repeatSpeed",state.keyboard.repeatSpeed},{"installed",state.keyboard.installed}}},
            {"mouse",QJsonObject{{"swapped",state.mouse.swapped},{"speed",state.mouse.speed},{"buttons",state.mouse.buttons}}},
            {"keyboards",state.keyboards},{"mice",state.mice}}},
        {"printers",printers},
        {"displaysProfiles",QJsonObject{{"summary",displayProfileText(state)}}}};
}
}
