#pragma once
#include <QByteArray>
#include <QImage>
#include <QString>

namespace xmb {
QImage decodeGtf(const QByteArray& gtf,QString* error=nullptr);
}
