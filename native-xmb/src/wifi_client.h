#pragma once
#include <QString>
#include <QVector>

namespace xmb {
struct WifiNetwork {
    QString ssid;
    int signalQuality = 0;
    bool secured = false;
    QString auth;
    bool connected = false;
};

QVector<WifiNetwork> listNetworks(QString* error = nullptr);
// Starts an asynchronous WPA2-PSK connection; success means Windows accepted the request, not that it connected.
// Windows retains the per-user WLAN profile and protects its key; this app does not persist the passphrase.
bool connect(const QString& ssid, const QString& passphrase, QString* error = nullptr);
}
