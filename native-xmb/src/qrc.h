#pragma once
#include <QByteArray>
#include <QHash>
#include <QStringList>

namespace xmb {
// Sony's CXML QRC container, unrelated to Qt's RCC resource format.
// Contents are read directly from the user's firmware; nothing is extracted.
class QrcArchive final {
public:
    explicit QrcArchive(const QString& path);
    QByteArray file(const QString& id) const;
    QStringList names() const { return entries_.keys(); }
private:
    struct Entry { quint32 offset=0,size=0,expanded=0; };
    QByteArray bytes_;
    quint32 data_=0;
    QHash<QString,Entry> entries_;
};
}
