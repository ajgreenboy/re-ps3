#include "homeassistant_client.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QtDebug>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dpapi.h>
#pragma comment(lib, "crypt32.lib")
#endif

namespace xmb {
namespace {
constexpr qint64 MaxResponseBytes = 4 * 1024 * 1024;
constexpr int RequestTimeoutMs = 10000;
constexpr qint64 MaxConfigBytes = 16 * 1024;
constexpr qint64 MaxSessionBytes = 16 * 1024;
constexpr int MaxEntities = 5000;
const QByteArray SessionMagic = QByteArrayLiteral("XMBHA1\n");

// Display order of the domains this client lists; it is also the domain-group order.
const QStringList Domains{QStringLiteral("scene"), QStringLiteral("script"), QStringLiteral("light"), QStringLiteral("switch"),
                          QStringLiteral("media_player"), QStringLiteral("climate"), QStringLiteral("cover"), QStringLiteral("fan"),
                          QStringLiteral("sensor"), QStringLiteral("input_boolean")};

QString domainTitle(const QString& domain) {
    static const QHash<QString, QString> titles{
        {QStringLiteral("scene"), QStringLiteral("Scenes")}, {QStringLiteral("script"), QStringLiteral("Scripts")},
        {QStringLiteral("light"), QStringLiteral("Lights")}, {QStringLiteral("switch"), QStringLiteral("Switches")},
        {QStringLiteral("media_player"), QStringLiteral("Media players")}, {QStringLiteral("climate"), QStringLiteral("Climate")},
        {QStringLiteral("cover"), QStringLiteral("Covers")}, {QStringLiteral("fan"), QStringLiteral("Fans")},
        {QStringLiteral("sensor"), QStringLiteral("Sensors")}, {QStringLiteral("input_boolean"), QStringLiteral("Toggles")}};
    return titles.value(domain, domain);
}

// Maps each entity to its area (an entity's own area, else its device's: HA's area_id()), and lists the areas.
// Built with namespace lists because HA's sandboxed Jinja cannot mutate a dict.
const char* const AreaTemplate =
    "{% set ar = namespace(l=[]) %}{% for a in areas() %}{% set ar.l = ar.l + [[a, area_name(a)]] %}{% endfor %}"
    "{% set en = namespace(l=[]) %}{% for s in states %}{% set a = area_id(s.entity_id) %}"
    "{% if a %}{% set en.l = en.l + [[s.entity_id, a]] %}{% endif %}{% endfor %}"
    "{{ {'areas': ar.l, 'entities': en.l} | tojson }}";

bool isSlug(const QString& text, int maxLength) {
    if (text.isEmpty() || text.size() > maxLength) return false;
    for (const QChar c : text)
        if (!((c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || c == QLatin1Char('_')))
            return false;
    return true;
}

bool validEntityId(const QString& id) {
    const qsizetype dot = id.indexOf(QLatin1Char('.'));
    return dot > 0 && id.size() <= 255 && isSlug(id.left(dot), 64) && isSlug(id.mid(dot + 1), 190);
}

QString domainOf(const QString& entityId) { return entityId.section(QLatin1Char('.'), 0, 0); }

// A long-lived access token is a JWT: base64url segments joined by dots.
bool validToken(const QString& token) {
    if (token.size() < 16 || token.size() > 4096) return false;
    for (const QChar c : token)
        if (!(c.isLetterOrNumber() && c.unicode() < 128) && c != QLatin1Char('-') && c != QLatin1Char('_') && c != QLatin1Char('.'))
            return false;
    return true;
}

bool lanHostName(const QString& host) {
    if (host == QLatin1String("localhost")) return true;
    if (!host.contains(QLatin1Char('.'))) return true;   // a single-label name resolves through the LAN's DNS
    for (const char* suffix : {".local", ".lan", ".home.arpa", ".internal", ".localhost"})
        if (host.endsWith(QLatin1String(suffix))) return true;
    return false;
}

QString normalizedBase(QUrl url) {
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/'))) path.chop(1);
    url.setPath(path);
    return url.toString(QUrl::FullyEncoded);
}

#ifdef Q_OS_WIN
QByteArray seal(const QByteArray& plain, bool protect) {
    const QByteArray entropy = QByteArrayLiteral("xmb-native-homeassistant-session-v1");
    DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()))};
    DATA_BLOB extra{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(const_cast<char*>(entropy.constData()))};
    DATA_BLOB out{0, nullptr};
    const BOOL ok = protect ? CryptProtectData(&in, L"XMB Home Assistant session", &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)
                            : CryptUnprotectData(&in, nullptr, &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (!ok) return {};
    QByteArray result(reinterpret_cast<const char*>(out.pbData), int(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}
#else
QByteArray seal(const QByteArray&, bool) { return {}; }
#endif

HaEntity parseEntity(const QJsonObject& object) {
    HaEntity entity;
    entity.id = object.value(QStringLiteral("entity_id")).toString();
    if (!validEntityId(entity.id)) return {};
    entity.domain = domainOf(entity.id);
    entity.state = object.value(QStringLiteral("state")).toString().left(255);
    entity.attributes = object.value(QStringLiteral("attributes")).toObject();
    entity.name = entity.attributes.value(QStringLiteral("friendly_name")).toString().simplified().left(120);
    if (entity.name.isEmpty()) entity.name = entity.id;
    entity.icon = entity.attributes.value(QStringLiteral("icon")).toString().left(64);
    entity.unit = entity.attributes.value(QStringLiteral("unit_of_measurement")).toString().left(16);
    entity.changed = QDateTime::fromString(object.value(QStringLiteral("last_changed")).toString(), Qt::ISODateWithMs).toUTC();
    return entity;
}

QString haMessage(int status, const QByteArray& body) {
    QString message = QJsonDocument::fromJson(body).object().value(QStringLiteral("message")).toString();
    if (message.isEmpty() && !body.trimmed().startsWith('{') && !body.trimmed().startsWith('<')) message = QString::fromUtf8(body.left(200));
    message = message.simplified().left(200);
    return message.isEmpty() ? QStringLiteral("HTTP %1").arg(status) : QStringLiteral("HTTP %1: %2").arg(status).arg(message);
}
}

HomeAssistantClient::HomeAssistantClient(QObject* parent) : QObject(parent), network_(new QNetworkAccessManager(this)) {
    qRegisterMetaType<HaEntity>();
    qRegisterMetaType<HaGroup>();
    qRegisterMetaType<QVector<HaEntity>>();
    qRegisterMetaType<QVector<HaGroup>>();
}

HomeAssistantClient::~HomeAssistantClient() { token_.fill('\0'); }

bool HomeAssistantClient::acceptableUrl(const QUrl& url, QString* why) {
    auto refuse = [why](const QString& reason) { if (why) *why = reason; return false; };
    if (!url.isValid() || url.host().isEmpty()) return refuse(tr("The Home Assistant url is not a valid http(s) url."));
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) return refuse(tr("The Home Assistant url must start with http:// or https://."));
    if (!url.userName().isEmpty() || !url.password().isEmpty() || url.hasQuery() || url.hasFragment())
        return refuse(tr("The Home Assistant url must not carry a user name, password, query or fragment."));
    if (scheme == QLatin1String("https")) return true;
    const QString host = url.host().toLower();
    QHostAddress address;
    if (address.setAddress(host)) {
        if (address.isLoopback() || address.isPrivateUse() || address.isLinkLocal() || address.isUniqueLocalUnicast()) return true;
        return refuse(tr("Plain http is accepted only for a home-network or loopback address; use https for %1.").arg(host));
    }
    if (lanHostName(host)) return true;
    return refuse(tr("Plain http is accepted only for a home-network host name (homeassistant.local, a .lan name); use https for %1.").arg(host));
}

bool HomeAssistantClient::usefulEntity(const HaEntity& entity) {
    if (!Domains.contains(entity.domain)) return false;
    if (entity.domain != QLatin1String("sensor")) return true;
    const QString deviceClass = entity.attributes.value(QStringLiteral("device_class")).toString();
    if (deviceClass == QLatin1String("temperature") || deviceClass == QLatin1String("humidity")) return true;
    // Older integrations set no device_class; a temperature unit is unambiguous.
    return deviceClass.isEmpty() && (entity.unit == QStringLiteral("°C") || entity.unit == QStringLiteral("°F"));
}

QVector<HaGroup> HomeAssistantClient::groupByDomain(const QVector<HaEntity>& entities) {
    QVector<HaGroup> groups;
    for (const QString& domain : Domains) {
        HaGroup group{domain, domainTitle(domain), {}};
        for (const HaEntity& entity : entities) if (entity.domain == domain) group.entities.push_back(entity);
        if (!group.entities.isEmpty()) groups.push_back(std::move(group));
    }
    return groups;
}

QString HomeAssistantClient::origin() const {
    return base_.scheme().toLower() + QStringLiteral("://") + base_.host().toLower() + QLatin1Char(':') +
           QString::number(base_.port(base_.scheme().toLower() == QLatin1String("https") ? 443 : 80));
}

bool HomeAssistantClient::configure(const QString& dataDirectory) {
    base_ = {};configError_.clear();token_.fill('\0');token_.clear();
    sessionPath_ = QDir(dataDirectory).filePath(QStringLiteral("homeassistant/session.bin"));
    const QString jsonPath = QDir(dataDirectory).filePath(QStringLiteral("homeassistant.json"));
    QString url = qEnvironmentVariable("HA_URL").trimmed();
    QString envToken = qEnvironmentVariable("HA_TOKEN").trimmed();
    QString jsonToken;
    QJsonObject config;
    QFile file(jsonPath);
    if (file.exists()) {
        if (file.size() > MaxConfigBytes || !file.open(QIODevice::ReadOnly)) {
            configError_ = tr("homeassistant.json is larger than this app reads or cannot be opened.");
            return false;
        }
        QByteArray bytes = file.readAll();
        file.close();
        config = QJsonDocument::fromJson(bytes).object();
        bytes.fill('\0');
        if (url.isEmpty()) url = config.value(QStringLiteral("url")).toString().trimmed();
        jsonToken = config.value(QStringLiteral("token")).toString().trimmed();
    }
    if (url.isEmpty()) {
        configError_ = tr("No Home Assistant url is set. Put {\"url\": \"http://<home assistant>:8123\", \"token\": \"<long-lived "
                          "access token>\"} in homeassistant.json in the data folder.");
        return false;
    }
    const QUrl parsed(url, QUrl::StrictMode);
    QString why;
    if (!acceptableUrl(parsed, &why)) { configError_ = why;qWarning().noquote() << "homeassistant:" << why;return false; }
    base_ = QUrl(normalizedBase(parsed));

    // A plain token in the json moves into the sealed session; the json keeps everything else.
    if (config.contains(QStringLiteral("token"))) {
        if (!jsonToken.isEmpty() && !validToken(jsonToken)) {
            qWarning().noquote() << "homeassistant: the token in homeassistant.json is not a long-lived access token; ignored";
        } else if (!jsonToken.isEmpty()) {
            token_ = jsonToken.toLatin1();
            if (saveSession()) {
                config.remove(QStringLiteral("token"));
                QSaveFile out(jsonPath);
                const QByteArray rewritten = QJsonDocument(config).toJson(QJsonDocument::Indented);
                if (out.open(QIODevice::WriteOnly) && out.write(rewritten) == rewritten.size() && out.commit())
                    qInfo().noquote() << "homeassistant: token moved from homeassistant.json into the sealed session";
                else
                    qWarning().noquote() << "homeassistant: token sealed, but homeassistant.json could not be rewritten without it";
            } else {
                qWarning().noquote() << "homeassistant: the token could not be sealed; it stays in homeassistant.json and is used from there";
            }
        }
        jsonToken.fill(QLatin1Char('\0'));
    }
    if (!envToken.isEmpty()) {
        if (validToken(envToken)) { token_.fill('\0');token_ = envToken.toLatin1(); }
        else qWarning().noquote() << "homeassistant: HA_TOKEN is not a long-lived access token; ignored";
        envToken.fill(QLatin1Char('\0'));
    }
    if (token_.isEmpty()) restoreSession();
    qInfo().noquote() << "homeassistant: server" << base_.toString() << (hasToken() ? "(token ready)" : "(no token)");
    return true;
}

bool HomeAssistantClient::saveSession() {
    if (sessionPath_.isEmpty() || token_.isEmpty() || !configured()) return false;
    QByteArray plain = QJsonDocument(QJsonObject{{"v", 1}, {"origin", origin()}, {"token", QString::fromLatin1(token_)}})
                           .toJson(QJsonDocument::Compact);
    const QByteArray sealed = seal(plain, true);
    plain.fill('\0');
    if (sealed.isEmpty()) return false;
    QDir().mkpath(QFileInfo(sessionPath_).absolutePath());
    QSaveFile file(sessionPath_);
    if (!file.open(QIODevice::WriteOnly) || file.write(SessionMagic) != SessionMagic.size() || file.write(sealed) != sealed.size() ||
        !file.commit())
        return false;
    return true;
}

bool HomeAssistantClient::restoreSession() {
    QFile file(sessionPath_);
    if (sessionPath_.isEmpty() || !file.exists() || file.size() <= SessionMagic.size() || file.size() > MaxSessionBytes ||
        !file.open(QIODevice::ReadOnly))
        return false;
    QByteArray bytes = file.readAll();
    file.close();
    if (!bytes.startsWith(SessionMagic)) { QFile::remove(sessionPath_);return false; }
    QByteArray plain = seal(bytes.mid(SessionMagic.size()), false);
    bytes.fill('\0');
    if (plain.isEmpty()) return false;   // another Windows user's record: leave it alone
    const QJsonObject record = QJsonDocument::fromJson(plain).object();
    plain.fill('\0');
    QString token = record.value(QStringLiteral("token")).toString();
    const QString stored = record.value(QStringLiteral("origin")).toString();
    if (!validToken(token)) { token.fill(QLatin1Char('\0'));return false; }
    if (stored != origin()) {
        // The url changed since the token was stored. Never send it to a different server.
        configError_ = tr("The stored Home Assistant token belongs to %1, not %2. Put a token for this server in homeassistant.json.")
                           .arg(stored.left(120), origin());
        qWarning().noquote() << QStringLiteral("homeassistant: stored token is for %1, not %2; not used").arg(stored.left(120), origin());
        token.fill(QLatin1Char('\0'));
        return false;
    }
    token_ = token.toLatin1();
    token.fill(QLatin1Char('\0'));
    qInfo().noquote() << "homeassistant: sealed token restored";
    return true;
}

void HomeAssistantClient::forgetToken() {
    token_.fill('\0');token_.clear();
    if (!sessionPath_.isEmpty()) QFile::remove(sessionPath_);
    qInfo().noquote() << "homeassistant: token forgotten";
}

// ---- Requests -------------------------------------------------------------------------------------------
quint64 HomeAssistantClient::check() { return send(Kind::Check, QStringLiteral("GET"), QStringLiteral("/api/"), {}); }
quint64 HomeAssistantClient::states() { return send(Kind::States, QStringLiteral("GET"), QStringLiteral("/api/states"), {}); }

quint64 HomeAssistantClient::groups(const QVector<HaEntity>& entities) {
    const QByteArray body = QJsonDocument(QJsonObject{{"template", QString::fromLatin1(AreaTemplate)}}).toJson(QJsonDocument::Compact);
    return send(Kind::Template, QStringLiteral("POST"), QStringLiteral("/api/template"), body, {}, entities);
}

quint64 HomeAssistantClient::refuse(const QString& message) {
    const quint64 request = ++nextRequest_;
    qInfo().noquote() << "homeassistant:" << message;
    QTimer::singleShot(0, this, [this, request, message] { emit done(request, false, message); });
    return request;
}

quint64 HomeAssistantClient::callService(const QString& domain, const QString& service, const QJsonObject& data) {
    if (!isSlug(domain, 64) || !isSlug(service, 64)) return refuse(tr("That Home Assistant service name is not valid."));
    if (data.contains(QStringLiteral("entity_id")) && !validEntityId(data.value(QStringLiteral("entity_id")).toString()))
        return refuse(tr("That Home Assistant entity id is not valid."));
    const QString label = domain + QLatin1Char('.') + service + QLatin1Char(' ') + data.value(QStringLiteral("entity_id")).toString();
    return send(Kind::Service, QStringLiteral("POST"), QStringLiteral("/api/services/%1/%2").arg(domain, service),
                QJsonDocument(data).toJson(QJsonDocument::Compact), label.trimmed());
}

quint64 HomeAssistantClient::activateScene(const QString& sceneId) {
    if (!validEntityId(sceneId) || domainOf(sceneId) != QLatin1String("scene")) return refuse(tr("%1 is not a scene.").arg(sceneId.left(80)));
    return callService(QStringLiteral("scene"), QStringLiteral("turn_on"), {{"entity_id", sceneId}});
}

quint64 HomeAssistantClient::runScript(const QString& scriptId) {
    if (!validEntityId(scriptId) || domainOf(scriptId) != QLatin1String("script")) return refuse(tr("%1 is not a script.").arg(scriptId.left(80)));
    return callService(QStringLiteral("script"), QStringLiteral("turn_on"), {{"entity_id", scriptId}});
}

quint64 HomeAssistantClient::toggle(const QString& entityId) {
    if (!validEntityId(entityId)) return refuse(tr("That Home Assistant entity id is not valid."));
    return callService(QStringLiteral("homeassistant"), QStringLiteral("toggle"), {{"entity_id", entityId}});
}

quint64 HomeAssistantClient::setBrightness(const QString& lightId, int percent) {
    if (!validEntityId(lightId) || domainOf(lightId) != QLatin1String("light")) return refuse(tr("%1 is not a light.").arg(lightId.left(80)));
    percent = std::clamp(percent, 0, 100);
    if (percent == 0) return callService(QStringLiteral("light"), QStringLiteral("turn_off"), {{"entity_id", lightId}});
    return callService(QStringLiteral("light"), QStringLiteral("turn_on"), {{"entity_id", lightId}, {"brightness_pct", percent}});
}

quint64 HomeAssistantClient::mediaCommand(const QString& mediaPlayerId, const QString& command) {
    static const QStringList allowed{QStringLiteral("media_play_pause"), QStringLiteral("media_play"), QStringLiteral("media_pause"),
                                     QStringLiteral("media_stop"), QStringLiteral("media_next_track"), QStringLiteral("media_previous_track"),
                                     QStringLiteral("volume_up"), QStringLiteral("volume_down"), QStringLiteral("turn_on"),
                                     QStringLiteral("turn_off"), QStringLiteral("toggle")};
    if (!validEntityId(mediaPlayerId) || domainOf(mediaPlayerId) != QLatin1String("media_player"))
        return refuse(tr("%1 is not a media player.").arg(mediaPlayerId.left(80)));
    if (!allowed.contains(command)) return refuse(tr("%1 is not a media player command this app sends.").arg(command.left(40)));
    return callService(QStringLiteral("media_player"), command, {{"entity_id", mediaPlayerId}});
}

quint64 HomeAssistantClient::setVolume(const QString& mediaPlayerId, double level) {
    if (!validEntityId(mediaPlayerId) || domainOf(mediaPlayerId) != QLatin1String("media_player"))
        return refuse(tr("%1 is not a media player.").arg(mediaPlayerId.left(80)));
    if (!std::isfinite(level)) return refuse(tr("That volume is not a number."));
    level = std::round(std::clamp(level, 0.0, 1.0) * 100.0) / 100.0;
    return callService(QStringLiteral("media_player"), QStringLiteral("volume_set"), {{"entity_id", mediaPlayerId}, {"volume_level", level}});
}

quint64 HomeAssistantClient::setTemperature(const QString& climateId, double value) {
    if (!validEntityId(climateId) || domainOf(climateId) != QLatin1String("climate")) return refuse(tr("%1 is not a thermostat.").arg(climateId.left(80)));
    if (!std::isfinite(value) || value < -50.0 || value > 120.0) return refuse(tr("That temperature is out of range."));
    value = std::round(value * 10.0) / 10.0;
    return callService(QStringLiteral("climate"), QStringLiteral("set_temperature"), {{"entity_id", climateId}, {"temperature", value}});
}

quint64 HomeAssistantClient::send(Kind kind, const QString& method, const QString& path, const QByteArray& body, QString label,
                                  QVector<HaEntity> entities) {
    const quint64 request = ++nextRequest_;
    auto fail = [this, kind, request, entities](const QString& message) {
        QTimer::singleShot(0, this, [this, kind, request, entities, message] {
            qInfo().noquote() << "homeassistant:" << message;
            if (kind == Kind::Service) emit done(request, false, message);
            else if (kind == Kind::Template) emit groupsReady(request, groupByDomain(entities), false);
            else emit failed(request, message);
        });
        return request;
    };
    if (!configured()) return fail(configError_.isEmpty() ? tr("Home Assistant is not set up.") : configError_);
    if (token_.isEmpty() && kind != Kind::Check)
        return fail(tr("No Home Assistant token is set. Create a long-lived access token in your Home Assistant profile and put it "
                       "in homeassistant.json."));
    QNetworkRequest req(QUrl(base_.toString() + path));
    if (!token_.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token_);
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(RequestTimeoutMs);
    // Never follow a redirect: it could carry the Authorization header to another host.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    if (method != QLatin1String("GET")) req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply* reply = network_->sendCustomRequest(req, method.toLatin1(), body);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) { if (received > MaxResponseBytes) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, kind, request, label, entities] { finish(reply, kind, request, label, entities); });
    return request;
}

void HomeAssistantClient::finish(QNetworkReply* reply, Kind kind, quint64 request, const QString& label, const QVector<HaEntity>& entities) {
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray bytes = reply->read(MaxResponseBytes + 1);
    QString error;
    if (bytes.size() > MaxResponseBytes) error = tr("Home Assistant sent a response larger than this app reads.");
    else if (reply->error() == QNetworkReply::TimeoutError || reply->error() == QNetworkReply::OperationCanceledError)
        error = tr("Home Assistant did not answer within 10 seconds.");
    else if (status == 0) error = tr("Home Assistant could not be reached (%1).").arg(reply->errorString().left(200));
    else if (status == 401)
        error = hasToken() ? tr("Home Assistant refused the token (HTTP 401). Create a new long-lived access token in your profile.")
                           : tr("Home Assistant is reachable and asks for a token (HTTP 401).");
    else if (status == 403) error = tr("Home Assistant does not let this user do that (%1).").arg(haMessage(status, bytes));
    else if (status >= 300 && status < 400) error = tr("Home Assistant answered with a redirect (HTTP %1); check the url.").arg(status);
    else if (status < 200 || status >= 300) error = tr("Home Assistant refused the request (%1).").arg(haMessage(status, bytes));

    switch (kind) {
    case Kind::Check:
        qInfo().noquote() << "homeassistant: GET /api/ ->" << status;
        if (!error.isEmpty()) { emit checked(request, false, error);return; }
        emit checked(request, true, QJsonDocument::fromJson(bytes).object().value(QStringLiteral("message")).toString().left(120));
        return;
    case Kind::States: {
        if (!error.isEmpty()) { qInfo().noquote() << "homeassistant: GET /api/states ->" << status;emit failed(request, error);return; }
        const QJsonArray array = QJsonDocument::fromJson(bytes).array();
        QVector<HaEntity> list;
        for (const auto& value : array) {
            if (list.size() >= MaxEntities) break;
            HaEntity entity = parseEntity(value.toObject());
            if (!entity.id.isEmpty() && usefulEntity(entity)) list.push_back(std::move(entity));
        }
        std::stable_sort(list.begin(), list.end(), [](const HaEntity& a, const HaEntity& b) {
            const auto da = Domains.indexOf(a.domain), db = Domains.indexOf(b.domain);
            if (da != db) return da < db;
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });
        qInfo().noquote() << "homeassistant: states" << array.size() << "entities," << list.size() << "listed";
        emit statesReady(request, list);
        return;
    }
    case Kind::Template: {
        const QJsonObject object = error.isEmpty() ? QJsonDocument::fromJson(bytes).object() : QJsonObject();
        QHash<QString, QString> areaOf;
        for (const auto& value : object.value(QStringLiteral("entities")).toArray()) {
            const QJsonArray pair = value.toArray();
            if (pair.size() == 2) areaOf.insert(pair.at(0).toString(), pair.at(1).toString());
        }
        QVector<HaGroup> groups;
        for (const auto& value : object.value(QStringLiteral("areas")).toArray()) {
            const QJsonArray pair = value.toArray();
            if (pair.size() != 2 || pair.at(0).toString().isEmpty()) continue;
            HaGroup group{pair.at(0).toString().left(120), pair.at(1).toString().simplified().left(120), {}};
            if (group.name.isEmpty()) group.name = group.id;
            for (const HaEntity& entity : entities) if (areaOf.value(entity.id) == group.id) group.entities.push_back(entity);
            if (!group.entities.isEmpty()) groups.push_back(std::move(group));
        }
        if (groups.isEmpty()) {
            qInfo().noquote() << QStringLiteral("homeassistant: areas unavailable (%1); grouping by domain")
                                     .arg(error.isEmpty() ? QStringLiteral("no entity has an area") : error);
            emit groupsReady(request, groupByDomain(entities), false);
            return;
        }
        std::stable_sort(groups.begin(), groups.end(), [](const HaGroup& a, const HaGroup& b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
        HaGroup other{QString(), tr("Other"), {}};
        for (const HaEntity& entity : entities) {
            bool placed = false;
            for (const HaGroup& group : groups) if (areaOf.value(entity.id) == group.id) { placed = true;break; }
            if (!placed) other.entities.push_back(entity);
        }
        if (!other.entities.isEmpty()) groups.push_back(std::move(other));
        qInfo().noquote() << "homeassistant: grouped" << entities.size() << "entities into" << groups.size() << "areas";
        emit groupsReady(request, groups, true);
        return;
    }
    case Kind::Service:
        qInfo().noquote() << "homeassistant:" << label << "->" << status;
        if (!error.isEmpty()) { emit done(request, false, error);return; }
        emit done(request, true, tr("Done"));
        return;
    }
}

}
