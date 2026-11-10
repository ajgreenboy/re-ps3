#pragma once
#include <QByteArray>
namespace xmb::firmware {
QByteArray hmacSha1(QByteArray key, const QByteArray& data);
bool aesCtr(QByteArray& data, const QByteArray& key, QByteArray iv);
bool aesCbc(QByteArray& data, const QByteArray& key, QByteArray iv, bool encrypt = false);
}
