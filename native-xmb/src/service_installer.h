#pragma once
#include <QString>
#include <QVector>
#include <functional>

namespace xmb {
struct ServiceAvailability { bool installed=false; QString name; QString installerUrl; };
QVector<ServiceAvailability> serviceAvailability();
bool downloadInstaller(const QString& url, const QString& name, QString* path, QString* error,
                       const std::function<void(qint64,qint64)>& progress = {});
}
