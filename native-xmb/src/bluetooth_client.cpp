// WinRT headers must precede Qt's (macro hygiene).
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#endif

#include "bluetooth_client.h"
#include <QDebug>
#include <QRegularExpression>

namespace xmb {
namespace {
void fail(QString* error, const QString& message) { if(error)*error = message; }
#ifdef _WIN32
namespace bt = winrt::Windows::Devices::Bluetooth;
namespace enumeration = winrt::Windows::Devices::Enumeration;
struct Apartment {
    Apartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
    ~Apartment() { winrt::uninit_apartment(); }
};
QString message(const winrt::hresult_error& e) {
    return QString::fromWCharArray(e.message().c_str());
}
QString formatAddress(uint64_t address) {
    const QString hex = QStringLiteral("%1").arg(address, 12, 16, QLatin1Char('0')).toUpper();
    QStringList parts;
    for(int i = 0; i < 12; i += 2)parts.append(hex.mid(i, 2));
    return parts.join(QLatin1Char(':'));
}
#endif
}

QVector<BluetoothDevice> listBluetooth(QString* error) {
    if(error)error->clear();
    QVector<BluetoothDevice> devices;
#ifdef _WIN32
    try {
        Apartment apartment;
        if(!bt::BluetoothAdapter::GetDefaultAsync().get()) {
            fail(error, QStringLiteral("No Bluetooth radio / unavailable")); return {};
        }
        const auto collect = [&](const winrt::hstring& selector, bool lowEnergy) {
            const auto results = enumeration::DeviceInformation::FindAllAsync(selector).get();
            for(const auto& item : results) try {
                // One device that cannot be opened (removed mid-scan, driver error) must not discard the whole list.
                uint64_t address = 0;
                if(lowEnergy) {
                    const auto device = bt::BluetoothLEDevice::FromIdAsync(item.Id()).get();
                    if(device)address = device.BluetoothAddress();
                } else {
                    const auto device = bt::BluetoothDevice::FromIdAsync(item.Id()).get();
                    if(device)address = device.BluetoothAddress();
                }
                if(!address)continue;
                const QString formatted = formatAddress(address);
                bool found = false;
                for(auto& existing : devices) {
                    if(existing.address == formatted) {
                        existing.paired = existing.paired || item.Pairing().IsPaired();
                        if(existing.name.isEmpty())existing.name = QString::fromWCharArray(item.Name().c_str());
                        found = true; break;
                    }
                }
                if(!found)devices.append({QString::fromWCharArray(item.Name().c_str()), formatted, item.Pairing().IsPaired()});
            } catch(const winrt::hresult_error& e) {
                qWarning().noquote() << "bluetooth: skipped a device:" << message(e);
            } catch(...) {
                qWarning() << "bluetooth: skipped a device";
            }
        };
        collect(bt::BluetoothDevice::GetDeviceSelector(), false);
        collect(bt::BluetoothLEDevice::GetDeviceSelector(), true);
    } catch(const winrt::hresult_error& e) {
        fail(error, QStringLiteral("Bluetooth unavailable: %1").arg(message(e))); return {};
    } catch(...) {
        fail(error, QStringLiteral("Bluetooth unavailable")); return {};
    }
#else
    fail(error, QStringLiteral("Bluetooth is only available on Windows"));
#endif
    return devices;
}

bool pairBluetooth(const QString& address, QString* error) {
    if(error)error->clear();
    if(qEnvironmentVariable("XMB_BLUETOOTH_DRY_RUN") == QStringLiteral("1")) {
        qInfo().noquote() << "bluetooth: would pair" << address;
        return true;
    }
#ifdef _WIN32
    static const QRegularExpression valid(QStringLiteral("^(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$"));
    if(!valid.match(address).hasMatch()) { fail(error, QStringLiteral("Invalid Bluetooth address")); return false; }
    QString hex = address; hex.remove(QLatin1Char(':'));
    const uint64_t numeric = hex.toULongLong(nullptr, 16);
    try {
        Apartment apartment;
        winrt::hstring id;
        const auto classic = bt::BluetoothDevice::FromBluetoothAddressAsync(numeric).get();
        if(classic)id = classic.DeviceId();
        if(id.empty()) {
            const auto le = bt::BluetoothLEDevice::FromBluetoothAddressAsync(numeric).get();
            if(le)id = le.DeviceId();
        }
        if(id.empty()) { fail(error, QStringLiteral("Bluetooth device not found; put it in pairing mode")); return false; }
        const auto info = enumeration::DeviceInformation::CreateFromIdAsync(id).get();
        if(!info) { fail(error, QStringLiteral("Bluetooth device unavailable")); return false; }
        const auto result = info.Pairing().PairAsync().get();
        const auto status = result.Status();
        if(status == enumeration::DevicePairingResultStatus::Paired || status == enumeration::DevicePairingResultStatus::AlreadyPaired)return true;
        fail(error, QStringLiteral("Bluetooth pairing failed (status %1)").arg(int(status))); return false;
    } catch(const winrt::hresult_error& e) {
        fail(error, QStringLiteral("Bluetooth pairing unavailable: %1").arg(message(e))); return false;
    } catch(...) {
        fail(error, QStringLiteral("Bluetooth pairing unavailable")); return false;
    }
#else
    Q_UNUSED(address);
    fail(error, QStringLiteral("Bluetooth is only available on Windows")); return false;
#endif
}
}
