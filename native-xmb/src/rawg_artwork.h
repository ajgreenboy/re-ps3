#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QTimer;

namespace xmb {

class RawgClient;

class RawgArtwork final : public QObject {
    Q_OBJECT
public:
    explicit RawgArtwork(QString cacheDirectory, QObject* parent = nullptr,
                         QNetworkAccessManager* network = nullptr);
    void refresh(const QVector<QPair<QString, QString>>& targets);
    void resetBackoff();
    static QString cachedArtwork(QString cacheDirectory, QString id, QString title);

signals:
    void ready(QString id, QString title, QString path, QImage preview);
    void status(QString safeMessage);

private:
    using Target = QPair<QString, QString>;
    void next();
    void deferTarget(qint64 seconds);
    void readPreview(const Target& target, const QString& path);
    void report(const QString& message);

    QString cacheDirectory_;
    RawgClient* client_;
    QTimer* timer_;
    QVector<Target> pending_;
    QVector<Target> needsNetwork_;
    QSet<Target> delivered_;
    Target active_;
    QString query_;
    QString lastStatus_;
    quint64 generation_ = 0;
    QHash<Target, qint64> retryAfter_;
    qint64 networkRetryAfter_ = 0;
};

} // namespace xmb
