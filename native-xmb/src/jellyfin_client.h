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
    QString collectionType;   // a library's kind ("movies", "tvshows", "music", ...); empty on items
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
    bool text = false;      // IsTextSubtitleStream: the server can deliver it as SRT (subtitle())
    bool isDefault = false;
};
struct JellyfinPlayback {
    QUrl url;
    QString itemId, mediaSourceId, playSessionId, playMethod;
    bool audio = false;
    int burnSubtitleIndex = -1;   // the subtitle stream the server burns into this transcode, -1 = none
    QVector<JellyfinTrack> streams;
};
// Whether a subtitle stream is English. The language field is Jellyfin's ISO 639-2 code (usually
// "eng"); the title is DisplayTitle (e.g. "English (Forced)"), so both are consulted. Covers text
// and image (PGS) subtitle streams alike.
inline bool isEnglishSubtitle(const JellyfinTrack& track) {
    const QString lang=track.language.trimmed().toLower();
    const QString title=track.title.trimmed().toLower();
    if(lang==QLatin1String("eng") || lang==QLatin1String("en"))return true;
    if(lang.startsWith(QLatin1String("en-")))return true;
    return title.contains(QLatin1String("english"));
}

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
    // maxBitrateKbps caps what the server may send: above the file's bitrate it direct-plays, below it the
    // server transcodes down (HLS) to fit.
    // burnSubtitleIndex >= 0 asks for that subtitle stream burned into a server transcode
    // (SubtitleStreamIndex + a SubtitleProfile whose method is Encode; direct play is switched off).
    quint64 playback(QString itemId, bool audio = false, int maxBitrateKbps = 20000,
                     int burnSubtitleIndex = -1, QString burnCodec = {});
    // Primary (poster) or Backdrop art for one item, authenticated, bounded, and cached under
    // the directory set by setImageCache(). A failure emits artworkReady with a null image:
    // missing art is not an error the user needs to see.
    quint64 artwork(QString itemId, QString tag, bool backdrop = false);
    void setImageCache(QString directory);
    // A text subtitle stream of the playing item, converted by the server to SRT:
    // GET /Videos/{itemId}/{mediaSourceId}/Subtitles/{streamIndex}/Stream.srt. Bounded like every
    // response; subtitleReady carries empty bytes when it fails.
    quint64 subtitle(QString itemId, QString mediaSourceId, int streamIndex);
    // An image-based (PGS) stream in its own format: GET .../Subtitles/{streamIndex}/Stream.sup.
    // Bounded at 64 MB with a 120 s deadline; answers through subtitleReady like subtitle().
    quint64 subtitleImage(QString itemId, QString mediaSourceId, int streamIndex);
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
    void subtitleReady(quint64 request, int streamIndex, QByteArray bytes);
private:
    enum class Operation { SignIn, Views, Items, Search, Playback, Artwork, Report, Subtitle, SubtitleImage };
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
