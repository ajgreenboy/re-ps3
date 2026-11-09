#pragma once
#include <QByteArray>
#include <QMap>
#include <QStringList>
#include <QVector4D>

namespace xmb {
struct VsmxKey {
    enum Kind { Alpha, Position, Scale, Colour } kind=Alpha;
    double time=0;
    QVector4D value;
};
struct VsmxTimeline {
    QMap<QString,double> constants;
    QMap<QString,QString> strings;
    QMap<QString,QVector<VsmxKey>> actors;
    QStringList notEvaluated;
};
bool evalVsmx(const QByteArray& data,VsmxTimeline* out,QString* error=nullptr);
QVector4D sample(const VsmxTimeline& timeline,const QString& actor,VsmxKey::Kind kind,double t);
}
