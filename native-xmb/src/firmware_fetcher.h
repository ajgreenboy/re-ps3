#pragma once
#include <QString>
#include <functional>

namespace xmb {
struct FirmwareRelease { QString version; QString url; };
FirmwareRelease latestFirmware(const QString& region = QStringLiteral("us"), QString* error = nullptr);
bool downloadPup(const QString& url, QString* path, QString* error = nullptr,
                 const std::function<void(qint64, qint64)>& progress = {});
QString expectedFirmwareVersion();
}
