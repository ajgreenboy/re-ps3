#pragma once
#include <QObject>
#include <QJsonObject>
#include <QVariant>
#include <QVariantMap>

namespace xmb {
// Application preferences only. Never writes Windows or firmware settings.
class Preferences final:public QObject {
    Q_OBJECT
public:
    explicit Preferences(const QString& dataDirectory,QObject* parent=nullptr);
    QVariant value(const QString& key) const;
    bool setValue(const QString& key,const QVariant& value);
    bool setValues(const QVariantMap& values);
    QString path() const { return path_; }
    QString error() const { return error_; }
signals:
    void changed(const QString& key);
    void failed(const QString& message);
private:
    QString path_,error_;
    QJsonObject values_;
    bool writable_=true;
    static QVariant defaultValue(const QString& key);
    static bool valid(const QString& key,const QVariant& value);
    bool write(const QJsonObject& values);
};
}
