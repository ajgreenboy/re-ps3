#pragma once
#include <QString>
namespace xmb {
bool extractPup(const QString& pupPath, const QString& destDir, QString* error = nullptr);
}
