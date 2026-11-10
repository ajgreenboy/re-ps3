#pragma once
#include <QString>
#include <QVector>

namespace xmb {
struct BluetoothDevice {
    QString name;
    QString address;
    bool paired = false;
};

// Both functions block on WinRT operations and must run on a worker thread.
QVector<BluetoothDevice> listBluetooth(QString* error = nullptr);
bool pairBluetooth(const QString& address, QString* error = nullptr);
}
