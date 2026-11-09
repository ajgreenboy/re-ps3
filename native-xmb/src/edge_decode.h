#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>
#include <QVector2D>
#include <QVector3D>

namespace xmb {
struct EdgeVertex { QVector3D position,normal; QVector2D uv; };
bool decodeEdge(const QByteArray& edge,QVector<EdgeVertex>& vertices,QVector<quint32>& indices,QString* error=nullptr);
}
