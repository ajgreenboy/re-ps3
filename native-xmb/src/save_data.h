#pragma once
#include <QString>
#include <QVector>

namespace xmb {
class Assets;
struct Entry;
// Presentation only: no save-data provider is registered on this host. These utility commands are
// not save records, and must never be treated as local files by the item menu.
QString saveDataTitle(Assets& assets,const QString& kind);
QVector<Entry> saveDataRows(Assets& assets,const QString& kind);
}
