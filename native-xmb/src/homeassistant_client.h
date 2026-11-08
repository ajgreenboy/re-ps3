#pragma once
// Home Assistant for the XMB (plan item H7, audit/PHASES-2026-11-07.md).
//
// A REST client for one Home Assistant instance: list the entities worth showing on a TV (scenes, scripts,
// lights, switches, media players, climate, covers, fans, temperature/humidity sensors, input booleans) and
// call the services behind the obvious couch actions (activate a scene, run a script, toggle, dim, transport,
// set a thermostat).
//
// Configuration: the environment (HA_URL, HA_TOKEN) first, then <data>/homeassistant.json
// {"url": "http://192.168.1.51:8123", "token": "<long-lived access token>"}. A token found in the json is moved
// into <data>/homeassistant/session.bin, sealed with DPAPI for the current Windows user (the same scheme as the
// Spotify and Jellyfin sessions), and the json is rewritten without it. The sealed record is bound to the
// server's origin: a token is never sent to a server other than the one it was stored for. A token from HA_TOKEN
// is used as given and never written to disk.
//
// Safety: http is accepted only for a loopback, private-range or link-local address, or a LAN-style host name
// (single label, .local, .lan, .home.arpa, .internal); https is accepted for any host. The token travels only
// in the Authorization: Bearer header, redirects are never followed, every response is capped at 4 MB and
// every request at 10 seconds. The token is never logged.
#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

namespace xmb {

// `id` is the entity id ("light.living_room"), `domain` its prefix, `name` the friendly name (the id when HA
// has none), `icon` the "mdi:..." icon HA reports (often empty), `unit` the unit of measurement.
struct HaEntity {
    QString id, domain, name, state, icon, unit;
    QJsonObject attributes;
    QDateTime changed;   // last_changed, UTC
};

// A room (area) or, when areas are unavailable, a domain ("light", "scene", ...). `id` is the area id or the
// domain; entities keep the order of the input list.
struct HaGroup {
    QString id, name;
    QVector<HaEntity> entities;
};

class HomeAssistantClient final : public QObject {
    Q_OBJECT
public:
    explicit HomeAssistantClient(QObject* parent = nullptr);
    ~HomeAssistantClient() override;

    // Reads HA_URL / HA_TOKEN, then <data>/homeassistant.json, migrating a plain json token into the sealed
    // session (see above), then restores a sealed token. Returns false, with configurationError() saying why,
    // when there is no acceptable url. A url without a token still configures: check() then shows whether the
    // server is reachable (HA answers 401), and hasToken() is false.
    bool configure(const QString& dataDirectory);
    bool configured() const { return base_.isValid() && !base_.isEmpty(); }
    bool hasToken() const { return !token_.isEmpty(); }
    QString configurationError() const { return configError_; }
    QUrl baseUrl() const { return base_; }
    // Drops the token from memory and deletes the sealed session.
    void forgetToken();

    // True when `url` may carry a token (see the header comment); `why` gets the reason when it may not.
    static bool acceptableUrl(const QUrl& url, QString* why = nullptr);
    // True for the domains this client lists, and for sensors only when they are temperature or humidity.
    static bool usefulEntity(const HaEntity& entity);
    // Groups by domain in a fixed order (scenes, scripts, lights, ...); the fallback when areas are unavailable.
    static QVector<HaGroup> groupByDomain(const QVector<HaEntity>& entities);

    // GET /api/ -> checked(request, ok, message). Works without a token (then reports the 401).
    quint64 check();
    // GET /api/states -> statesReady(request, entities) with useful entities only, sorted by domain then name.
    quint64 states();
    // POST /api/template (areas(), area_entities(), area_devices(), device_entities()) -> groupsReady with the
    // entities grouped by room plus an "Other" group, byArea = true. When the template call fails (HA restricts
    // it to admin users) or no entity has an area, it emits domain groups instead, byArea = false.
    quint64 groups(const QVector<HaEntity>& entities);

    // Actions: POST /api/services/<domain>/<service> -> done(request, ok, message). Each checks the entity's
    // domain first and fails (via done, asynchronously) without a request when it does not fit.
    quint64 activateScene(const QString& sceneId);                        // scene.turn_on
    quint64 runScript(const QString& scriptId);                           // script.turn_on
    quint64 toggle(const QString& entityId);                              // homeassistant.toggle
    quint64 setBrightness(const QString& lightId, int percent);           // light.turn_on brightness_pct; 0 = light.turn_off
    // media_play_pause, media_play, media_pause, media_stop, media_next_track, media_previous_track,
    // volume_up, volume_down, turn_on, turn_off, toggle.
    quint64 mediaCommand(const QString& mediaPlayerId, const QString& command);
    quint64 setVolume(const QString& mediaPlayerId, double level);        // media_player.volume_set, 0..1
    quint64 setTemperature(const QString& climateId, double value);       // climate.set_temperature
    // Any service; domain and service must be [a-z0-9_]+, and data.entity_id (when present) a valid entity id.
    quint64 callService(const QString& domain, const QString& service, const QJsonObject& data);

signals:
    void checked(quint64 request, bool ok, QString message);
    void statesReady(quint64 request, QVector<xmb::HaEntity> entities);
    void groupsReady(quint64 request, QVector<xmb::HaGroup> groups, bool byArea);
    void done(quint64 request, bool ok, QString message);
    void failed(quint64 request, QString message);   // check, states: the request could not be answered

private:
    enum class Kind { Check, States, Template, Service };
    QNetworkAccessManager* network_ = nullptr;
    QUrl base_;
    QByteArray token_;
    QString configError_, sessionPath_;
    quint64 nextRequest_ = 0;

    quint64 send(Kind kind, const QString& method, const QString& path, const QByteArray& body, QString label = {},
                 QVector<HaEntity> entities = {});
    void finish(QNetworkReply* reply, Kind kind, quint64 request, const QString& label, const QVector<HaEntity>& entities);
    quint64 refuse(const QString& message);
    bool saveSession();
    bool restoreSession();
    QString origin() const;
};

}

Q_DECLARE_METATYPE(xmb::HaEntity)
Q_DECLARE_METATYPE(xmb::HaGroup)
