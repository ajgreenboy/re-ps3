#pragma once
// Spotify for the Music column (audit/SPOTIFY.md).
//
// This build cannot decode Spotify audio: Spotify streams only to its own clients and to Spotify Connect
// devices. What it does is the Web API: browse the signed-in user's library (Liked Songs, playlists) and
// start, pause and skip playback on a Spotify Connect device - the Spotify app on this PC, a phone, a speaker.
//
// Sign-in is Spotify's Authorization Code flow with PKCE (no client secret, as Spotify requires for a public
// client). The browser opens accounts.spotify.com; Spotify redirects to a loopback listener this client runs
// on http://127.0.0.1:<port>/callback (Spotify accepts loopback IP literals, never "localhost"). The refresh
// token is sealed with DPAPI for the current Windows user, exactly like the Jellyfin session; the access token
// lives in memory only. Every response is bounded, every request times out, and a 401 refreshes once.
//
// The user supplies the client id of their own Spotify developer app (developer.spotify.com/dashboard) with
// the redirect URI registered exactly; see SpotifyClient::configure().
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;

namespace xmb {

// `image` is the URL of the cover Spotify lists closest to 300 px (empty when it has none).
struct SpotifyPlaylist { QString id, name, uri, owner, image; int tracks = 0; };
struct SpotifyTrack { QString id, name, uri, artists, album, image; qint64 durationMs = 0; bool playable = true; };
struct SpotifyDevice { QString id, name, type; bool active = false, restricted = false; int volume = -1; };
struct SpotifyNowPlaying { bool playing = false; QString track, artists, device, image; qint64 progressMs = 0, durationMs = 0; };

class SpotifyClient final : public QObject {
    Q_OBJECT
public:
    explicit SpotifyClient(QObject* parent = nullptr);
    ~SpotifyClient() override;

    // Reads the client id and the loopback port: the environment (SPOTIFY_CLIENT_ID, SPOTIFY_REDIRECT_PORT)
    // first, then <data>/spotify.json {"clientId": "...", "redirectPort": 8888}. Returns false, with
    // configurationError() saying why, when no valid client id is available.
    bool configure(const QString& dataDirectory);
    bool configured() const { return !clientId_.isEmpty(); }
    QString configurationError() const { return configError_; }
    QString redirectUri() const;
    void setSessionStore(QString path) { sessionPath_ = std::move(path); }
    void setImageCache(QString directory) { imageCache_ = std::move(directory); }
    // Fetches a cover from Spotify's image CDN (https *.scdn.co / *.spotifycdn.com only), from the disk cache
    // when it is there. Emits imageReady once, with a null image on failure.
    void fetchImage(const QString& url);

    bool signedIn() const { return !refreshToken_.isEmpty(); }
    QString displayName() const { return displayName_; }
    QString product() const { return product_; }   // "premium", "free", ... from /me
    // Opens the browser on Spotify's consent page and waits (up to 5 minutes) for the loopback redirect.
    void signIn();
    void cancelSignIn();
    void signOut();
    bool restoreSession();

    // Library. Each pages through the API's `next` links up to a bound and emits once.
    quint64 playlists();
    quint64 playlistTracks(const QString& playlistId);
    quint64 likedTracks();
    quint64 devices();
    quint64 nowPlaying();
    // Playback on a Connect device. `contextUri` plays a playlist from `offsetUri`; with an empty context
    // `uris` is the queue. An empty deviceId lets Spotify use the active device. With no offsetUri,
    // `offsetPosition` (0-based, -1 for none) picks the context track to start from.
    quint64 play(const QString& deviceId, const QString& contextUri, const QString& offsetUri, const QStringList& uris,
                 int offsetPosition = -1);
    quint64 shuffle(const QString& deviceId, bool on);
    quint64 pause(const QString& deviceId);
    quint64 next(const QString& deviceId);
    quint64 previous(const QString& deviceId);

signals:
    void signInStarted(QString url);
    void signedInChanged();
    void failed(quint64 request, QString message);
    void playlistsReady(quint64 request, QVector<xmb::SpotifyPlaylist> playlists);
    void tracksReady(quint64 request, QVector<xmb::SpotifyTrack> tracks);
    void devicesReady(quint64 request, QVector<xmb::SpotifyDevice> devices);
    void nowPlayingReady(quint64 request, xmb::SpotifyNowPlaying state);
    void commandDone(quint64 request);
    void imageReady(QString url, QImage image);

private:
    enum class Kind { Me, Playlists, PlaylistTracks, Liked, Devices, NowPlaying, Command };
    struct Pending {
        Kind kind; QString method; QUrl url; QByteArray body; quint64 request; int pages = 0; bool retried = false;
        QVector<SpotifyPlaylist> playlists; QVector<SpotifyTrack> tracks; QString fallbackPath;
    };
    QNetworkAccessManager* network_ = nullptr;
    QTcpServer* listener_ = nullptr;
    QString clientId_, configError_, sessionPath_, imageCache_;
    QSet<QString> imagesInFlight_;
    int port_ = 8888;
    QUrl apiBase_, accountsBase_;
    QByteArray verifier_, state_;
    QByteArray accessToken_, refreshToken_;
    QDateTime accessExpiry_;
    QString displayName_, product_;
    quint64 nextRequest_ = 0, generation_ = 0;
    bool refreshing_ = false;
    QVector<Pending> waiting_;   // requests parked while a token refresh runs

    quint64 enqueue(Kind kind, const QString& method, const QString& path, const QByteArray& body = {});
    void send(Pending pending);
    void handle(QNetworkReply* reply, Pending pending, quint64 generation);
    void refreshAccess(std::function<void(bool)> done);
    void exchangeCode(const QByteArray& code);
    void acceptToken(const QJsonObject& token);
    void fetchMe();
    void onCallback();
    bool saveSession();
    void forgetSession();
    void fail(quint64 request, const QString& message);
    QString apiError(int status, const QByteArray& body) const;
};

}

Q_DECLARE_METATYPE(xmb::SpotifyPlaylist)
Q_DECLARE_METATYPE(xmb::SpotifyTrack)
Q_DECLARE_METATYPE(xmb::SpotifyDevice)
Q_DECLARE_METATYPE(xmb::SpotifyNowPlaying)
