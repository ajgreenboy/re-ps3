#include "wifi_client.h"
#include <QDebug>
#include <QByteArray>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <wlanapi.h>
#endif

namespace xmb {
namespace {
void fail(QString* error, const QString& message) { if(error)*error = message; }
#ifdef Q_OS_WIN
QString winError(const QString& operation, DWORD code) {
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    const QString detail = text ? QString::fromWCharArray(text).trimmed() : QStringLiteral("unknown error");
    if(text)LocalFree(text);
    return QStringLiteral("%1: %2 (Windows error %3)").arg(operation, detail).arg(code);
}
struct WlanSession {
    HANDLE handle = nullptr;
    PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
    ~WlanSession() { if(interfaces)WlanFreeMemory(interfaces); if(handle)WlanCloseHandle(handle, nullptr); }
    bool open(QString* error) {
        DWORD version = 0;
        DWORD result = WlanOpenHandle(2, nullptr, &version, &handle);
        if(result != ERROR_SUCCESS) { fail(error, winError(QStringLiteral("Wi-Fi service unavailable"), result)); return false; }
        result = WlanEnumInterfaces(handle, nullptr, &interfaces);
        if(result != ERROR_SUCCESS) { fail(error, winError(QStringLiteral("Cannot enumerate Wi-Fi interfaces"), result)); return false; }
        return true;
    }
    const WLAN_INTERFACE_INFO* active(QString* error) const {
        const WLAN_INTERFACE_INFO* fallback = nullptr;
        for(DWORD i = 0; i < interfaces->dwNumberOfItems; ++i) {
            const auto& iface = interfaces->InterfaceInfo[i];
            if(iface.isState == wlan_interface_state_connected)return &iface;
            if(!fallback && iface.isState != wlan_interface_state_not_ready)fallback = &iface;
        }
        if(!fallback)fail(error, QStringLiteral("No active Wi-Fi interface available"));
        return fallback;
    }
};
QString authName(DOT11_AUTH_ALGORITHM auth) {
    switch(auth) {
    case DOT11_AUTH_ALGO_80211_OPEN: return QStringLiteral("Open");
    case DOT11_AUTH_ALGO_80211_SHARED_KEY: return QStringLiteral("Shared");
    case DOT11_AUTH_ALGO_WPA: return QStringLiteral("WPA-Enterprise");
    case DOT11_AUTH_ALGO_WPA_PSK: return QStringLiteral("WPA-PSK");
    case DOT11_AUTH_ALGO_WPA_NONE: return QStringLiteral("WPA-None");
    case DOT11_AUTH_ALGO_RSNA: return QStringLiteral("WPA2-Enterprise");
    case DOT11_AUTH_ALGO_RSNA_PSK: return QStringLiteral("WPA2-PSK");
    default: return QStringLiteral("Unknown (%1)").arg(int(auth));
    }
}
#endif
}

QVector<WifiNetwork> listNetworks(QString* error) {
    if(error)error->clear();
    QVector<WifiNetwork> networks;
#ifdef Q_OS_WIN
    WlanSession session;
    if(!session.open(error))return networks;
    const auto* iface = session.active(error);
    if(!iface)return networks;
    PWLAN_AVAILABLE_NETWORK_LIST list = nullptr;
    const DWORD result = WlanGetAvailableNetworkList(session.handle, &iface->InterfaceGuid, 0, nullptr, &list);
    if(result != ERROR_SUCCESS) { fail(error, winError(QStringLiteral("Cannot list Wi-Fi networks"), result)); return networks; }
    for(DWORD i = 0; i < list->dwNumberOfItems; ++i) {
        const auto& item = list->Network[i];
        networks.append({QString::fromUtf8(reinterpret_cast<const char*>(item.dot11Ssid.ucSSID), int(item.dot11Ssid.uSSIDLength)),
                         int(item.wlanSignalQuality), item.bSecurityEnabled != FALSE, authName(item.dot11DefaultAuthAlgorithm),
                         (item.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0});
    }
    WlanFreeMemory(list);
#else
    fail(error, QStringLiteral("Wi-Fi is only available on Windows"));
#endif
    return networks;
}

bool connect(const QString& ssid, const QString& passphrase, QString* error) {
    if(error)error->clear();
    if(qEnvironmentVariable("XMB_WIFI_DRY_RUN") == QStringLiteral("1")) {
        qInfo().noquote() << "wifi: would connect to" << ssid;
        return true;
    }
#ifdef Q_OS_WIN
    const QByteArray ssidBytes = ssid.toUtf8();
    if(ssidBytes.isEmpty() || ssidBytes.size() > 32 || ssid.contains(QChar::Null)) {
        fail(error, QStringLiteral("SSID must be 1 to 32 UTF-8 bytes")); return false;
    }
    if(passphrase.size() < 8 || passphrase.size() > 63 || passphrase.contains(QChar::Null)) {
        fail(error, QStringLiteral("WPA2 passphrase must be 8 to 63 characters")); return false;
    }
    WlanSession session;
    if(!session.open(error))return false;
    const auto* iface = session.active(error);
    if(!iface)return false;
    const QString xml = QStringLiteral("<?xml version=\"1.0\"?><WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
        "<name>%1</name><SSIDConfig><SSID><hex>%2</hex></SSID></SSIDConfig><connectionType>ESS</connectionType>"
        "<connectionMode>manual</connectionMode><MSM><security><authEncryption><authentication>WPA2PSK</authentication>"
        "<encryption>AES</encryption><useOneX>false</useOneX></authEncryption><sharedKey><keyType>passPhrase</keyType>"
        "<protected>false</protected><keyMaterial>%3</keyMaterial></sharedKey></security></MSM></WLANProfile>")
        .arg(ssid.toHtmlEscaped(), QString::fromLatin1(ssidBytes.toHex()), passphrase.toHtmlEscaped());
    DWORD reason = 0;
    DWORD result = WlanSetProfile(session.handle, &iface->InterfaceGuid, WLAN_PROFILE_USER,
                                  reinterpret_cast<LPCWSTR>(xml.utf16()), nullptr, FALSE, nullptr, &reason);
    if(result != ERROR_SUCCESS) {
        fail(error, winError(QStringLiteral("Cannot create Wi-Fi profile"), result) + QStringLiteral(" (reason %1)").arg(reason)); return false;
    }
    WLAN_CONNECTION_PARAMETERS params{};
    params.wlanConnectionMode = wlan_connection_mode_profile;
    params.strProfile = reinterpret_cast<LPCWSTR>(ssid.utf16());
    params.dot11BssType = dot11_BSS_type_infrastructure;
    result = WlanConnect(session.handle, &iface->InterfaceGuid, &params, nullptr);
    if(result != ERROR_SUCCESS) { fail(error, winError(QStringLiteral("Cannot request Wi-Fi connection"), result)); return false; }
    return true;
#else
    Q_UNUSED(ssid); Q_UNUSED(passphrase);
    fail(error, QStringLiteral("Wi-Fi is only available on Windows")); return false;
#endif
}
}
