#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace xmb {

struct RawgGame {
    int id = 0;
    QString name;
    QString slug;
    QString released;
    QStringList platforms;
    QStringList genres;
    QUrl imageUrl;
    QUrl pageUrl;
};

using RawgGameList = QVector<RawgGame>;

class RawgClient final : public QObject {
    Q_OBJECT
public:
    explicit RawgClient(QString cacheDirectory, QObject* parent = nullptr,
                        QNetworkAccessManager* network = nullptr);
    ~RawgClient() override;

    static QString apiKeyFilePath();
    static bool hasApiKey();
    static bool hasEnvironmentKey();
    static bool saveApiKey(const QString& apiKey, QString& error);

    void search(const QString& query);
    void downloadArtwork(const RawgGame& game);
    void cancel();

signals:
    void searchReady(QVector<xmb::RawgGame> games);
    void artworkReady(xmb::RawgGame game, QString path, QImage preview);
    void failed(QString safeError);

private:
    enum class RequestRole { Metadata, Image };

    void beginRequest(const QUrl& url, RequestRole role, quint64 generation, int redirects = 0);
    void finishRequest(QNetworkReply* reply, RequestRole role, quint64 generation,
                       int redirects, const QUrl& requestUrl);
    void finishMetadata(const QByteArray& bytes, quint64 generation);
    void finishImage(const QByteArray& bytes, quint64 generation);
    QString artworkCachePath(const RawgGame& game) const;
    void failIfCurrent(quint64 generation, const QString& message);

    QString cacheDirectory_;
    QNetworkAccessManager* network_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    QTimer* deadline_ = nullptr;
    QByteArray response_;
    RawgGame artworkGame_;
    quint64 generation_ = 0;
};

} // namespace xmb

Q_DECLARE_METATYPE(xmb::RawgGame)
Q_DECLARE_METATYPE(xmb::RawgGameList)
