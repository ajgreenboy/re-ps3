#pragma once

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QUrl>
#include <atomic>
#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace xmb {

class SteamArtwork final : public QObject {
    Q_OBJECT
public:
    explicit SteamArtwork(QString cacheDirectory, QObject* parent = nullptr);
    ~SteamArtwork() override;

    // Cache path ends in <appId>.image; readers should detect the image format from content.
    static QString cachePath(QString cacheDirectory, QString appId);

    // Supersedes previous work. At most 32 unique numeric app IDs are fetched.
    void refresh(QStringList appIds);

signals:
    void loaded(QString appId, QImage image);

private:
    using ResponseHandler = std::function<void(const QByteArray&, int, int)>;

    bool isCurrent(quint64 generation) const;
    void cancelRequests();
    void pump(quint64 generation);
    void requestMetadata(const QString& appId, quint64 generation);
    void requestImage(const QString& appId, quint64 generation, const QUrl& url);
    void get(const QString& appId, quint64 generation, const QUrl& url, bool imageRequest,
             int redirectCount, ResponseHandler handler);
    void decodeAndCache(const QString& appId, quint64 generation, QByteArray bytes);
    void finishJob(const QString& appId, quint64 generation, QImage image = {});

    QString cacheDirectory_;
    QNetworkAccessManager* network_ = nullptr;
    QVector<QNetworkReply*> replies_;
    QStringList pendingAppIds_;
    std::shared_ptr<std::atomic<quint64>> workerGeneration_;
    quint64 generation_ = 0;
    int activeJobs_ = 0;
};

} // namespace xmb