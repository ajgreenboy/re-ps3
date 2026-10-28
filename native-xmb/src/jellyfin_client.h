#pragma once

#include <QObject>
#include <QUrl>
#include <QString>
#include <QVector>
#include <QByteArray>
#include <QImage>
#include <QSet>
#include <QtGlobal>

class QNetworkAccessManager;
class QNetworkReply;

namespace xmb {
struct JellyfinItem {
    QString id, title, type, overview;
    bool folder = false;
    qint64 durationMs = 0, resumeMs = 0;
    bool played = false;
    // The server's image tags, for the artwork cache key: a changed poster gets a new file
    // instead of serving the previous one forever.
    QString primaryTag, backdropTag;
};
struct JellyfinPage { QVector<JellyfinItem> items; int startIndex = 0, total = 0; };
// One MediaStream from PlaybackInfo. The player cannot necessarily switch to it; the shell asks
// QMediaPlayer what it reports and disables the rest with the reason, rather than pretending.
struct JellyfinTrack {
    int index = -1;
    QString type;      // "Video", "Audio", "Subtitle", "EmbeddedImage"
    QString title;     // DisplayTitle, else Language, else Codec
    QString language, codec;
    int channels = 0;
    bool external = false;
    bool isDefault = false;
};
struct JellyfinPlayback {
    QUrl url;
    QString itemId, mediaSourceId, playSessionId, playMethod;
    bool audio = false;
    QVector<JellyfinTrack> streams;
};

class JellyfinClient final : public QObject {
    Q_OBJECT
public:
    explicit JellyfinClient(QObject* parent = nullptr);
    ~JellyfinClient() override;
    void signIn(QUrl server, QString user, QString password);
    void signOut();
    bool signedIn() const;
    QUrl serverUrl() const;
    QByteArray authorizationHeader() const;
    QString userId() const;
    quint64 views();
    quint64 items(QString parentId, int startIndex = 0);
    // A free-text library search: the same user-scoped /Items route with SearchTerm and
    // Recursive=true, so results carry the same UserData (resume/played) as a folder listing.
    quint64 search(QString term, int startIndex = 0);
    quint64 playback(QString itemId, bool audio = false);
    // Primary (poster) or Backdrop art for one item, authenticated, bounded, and cached under
    // the directory set by setImageCache(). A failure emits artworkReady with a null image:
    // missing art is not an error the user needs to see.
    quint64 artwork(QString itemId, QString tag, bool backdrop = false);
    void setImageCache(QString directory);
    void reportPlayback(const JellyfinPlayback& playback, qint64 positionMs, bool paused, QString event);
    // ---- Remembered sign-in (DPAPI, opt-in) ----------------------------------------------
    // The store keeps one record: server URL, user id and the access token, protected with
    // CryptProtectData for the current Windows user. The password is never stored. An empty
    // path disables the store entirely.
    void setSessionStore(QString path);
    bool saveSession();
    bool restoreSession();
    void forgetSession();
    // True when a record is on disk, without unprotecting it: this is the sign-in page's opt-in
    // state and the condition for trying a restore at all.
    bool hasStoredSession() const;
signals:
    void authenticated();
    void failed(quint64 request, QString message);
    void pageReady(quint64 request, xmb::JellyfinPage page);
    void playbackReady(quint64 request, xmb::JellyfinPlayback playback);
    void artworkReady(quint64 request, QString itemId, bool backdrop, QImage image);
private:
    enum class Operation { SignIn, Views, Items, Search, Playback, Artwork, Report };
    quint64 send(Operation operation, QNetworkReply* reply, QString context = {}, bool audio = false);
    void finish(QNetworkReply* reply, Operation operation, quint64 request,
                quint64 generation, QString context = {}, bool audio = false);
    QNetworkReply* get(const QUrl& url);
    QNetworkReply* post(const QUrl& url, const QByteArray& body);
    QNetworkReply* deleteResource(const QUrl& url);
    QUrl apiUrl(const QString& route) const;
    bool isCurrent(quint64 generation) const;
    void fail(quint64 request, const QString& message);
    void cancelRequests(bool includeReports = true);
    bool trustedServerUrl(const QUrl& url) const;
    void flushLogouts();
    QString cachedImagePath(const QString& itemId, const QString& tag, bool backdrop) const;
    void writeImageCache(const QString& path, const QByteArray& bytes) const;
    void trimImageCache() const;
    struct PendingLogout { QUrl url; QByteArray authorization; };
    QVector<PendingLogout> logouts_;
    bool destroying_=false;
    QNetworkAccessManager* network_ = nullptr;
    QVector<QNetworkReply*> replies_;
    QSet<QNetworkReply*> reportReplies_;
    QUrl server_;
    QByteArray accessToken_;
    QString userId_;
    QString deviceId_;
    QString imageCache_;
    QString sessionPath_;
    quint64 generation_ = 0;
    quint64 nextRequest_ = 0;
};
} // namespace xmb
Q_DECLARE_METATYPE(xmb::JellyfinItem)
Q_DECLARE_METATYPE(xmb::JellyfinPage)
Q_DECLARE_METATYPE(xmb::JellyfinPlayback)
