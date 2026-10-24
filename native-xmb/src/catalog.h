#pragma once
#include <QMap>
#include <QString>
#include <QVector>
#include <QJsonObject>

namespace xmb {
struct XmbItem { QString kind; QMap<QString,QString> attributes; };
struct XmbView { QString id; QMap<QString,QMap<QString,QString>> tables; QVector<XmbItem> items; };
struct XmbDocument { QString path,sha256; QMap<QString,XmbView> views; QString error; int bareAmpersands=0; };
// XMBML is data. Selector/plugin URIs remain explicit references; parsing never
// executes a firmware query or treats it as a functioning native service.
XmbDocument readXmbDocument(const QString& path);
QJsonObject inventoryXmb(const QString& flashRoot);
}
