#include "weather_client.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QSaveFile>
#include <QTimeZone>
#include <QTimer>
#include <QTransform>
#include <QUrlQuery>
#include <QtMath>
#include <cmath>
#include <functional>

namespace xmb {
namespace {

constexpr int RequestTimeoutMs = 10000;
constexpr qint64 MaxResponseBytes = 1024 * 1024;
constexpr int RetryAfterFailureSeconds = 60;
constexpr int ForecastDays = 5;

const char* ForecastEndpoint = "https://api.open-meteo.com/v1/forecast";
const char* GeocodeEndpoint = "https://geocoding-api.open-meteo.com/v1/search";

double number(const QJsonValue& v) { return v.isDouble() ? v.toDouble() : qQNaN(); }
int integer(const QJsonValue& v, int fallback = -1) {
    return v.isDouble() && std::isfinite(v.toDouble()) ? qRound(v.toDouble()) : fallback;
}

QDateTime localTime(const QString& text, int offsetSeconds) {
    if (text.isEmpty()) return {};
    QDateTime t = QDateTime::fromString(text, Qt::ISODate);
    if (!t.isValid()) return {};
    return QDateTime(t.date(), t.time(), QTimeZone::fromSecondsAheadOfUtc(offsetSeconds));
}

// ---- glyphs: the XMB line style (src/glyphs.cpp line()), drawn in a 256 x 256 space and scaled to `size` ----

QImage line(int size, const std::function<void(QPainter&)>& draw) {
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    p.scale(size / 256.0, size / 256.0);
    QPen shadow(QColor(0, 0, 0, 90)); shadow.setWidthF(14); shadow.setCapStyle(Qt::RoundCap); shadow.setJoinStyle(Qt::RoundJoin);
    p.save(); p.translate(3, 5); p.setPen(shadow); draw(p); p.restore();
    QPen pen(QColor(250, 250, 250)); pen.setWidthF(12); pen.setCapStyle(Qt::RoundCap); pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen); draw(p);
    return image;
}

// A cloud whose outline spans x 40..216, y 64..176 before `t`.
QPainterPath cloud(const QTransform& t = {}) {
    QPainterPath base; base.addRoundedRect(QRectF(40, 116, 176, 60), 30, 30);
    QPainterPath big; big.addEllipse(QPointF(116, 112), 48, 48);
    QPainterPath right; right.addEllipse(QPointF(166, 126), 34, 34);
    QPainterPath left; left.addEllipse(QPointF(78, 132), 28, 28);
    return t.map(base.united(big).united(right).united(left).simplified());
}

QPainterPath moon(QPointF c, qreal r) {
    QPainterPath disc; disc.addEllipse(c, r, r);
    QPainterPath bite; bite.addEllipse(c + QPointF(r * 0.55, -r * 0.42), r * 0.82, r * 0.82);
    return disc.subtracted(bite).simplified();
}

// `front` filled plus the clearance gap a shape behind it keeps from its outline.
// ZF4: the path clipper behind keepOut() and drawBehind() costs ~140 ms per call here, and one icon asks for the same `front` four times
// (the shadow and the main pass, each in sun() and drawBehind()). The last answer is remembered per thread; the result is the same path.
struct LastPath { QPainterPath in, out; bool valid = false; };
QPainterPath keepOut(const QPainterPath& front) {
    thread_local LastPath last;
    if (last.valid && last.in == front) return last.out;
    QPainterPathStroker gap; gap.setWidth(44); gap.setCapStyle(Qt::RoundCap); gap.setJoinStyle(Qt::RoundJoin);
    last.in = front; last.out = front.united(gap.createStroke(front)); last.valid = true;
    return last.out;
}

// A sun; with `behind`, rays that would touch it are left out rather than cut into stubs.
QPainterPath sun(QPointF c, qreal r, qreal rayFrom, qreal rayTo, const QPainterPath& behind = {}) {
    QPainterPath path; path.addEllipse(c, r, r);
    const QPainterPath zone = behind.isEmpty() ? QPainterPath() : keepOut(behind);
    for (int i = 0; i < 8; ++i) {
        const qreal a = i * M_PI / 4;
        const QPointF from = c + QPointF(std::cos(a) * rayFrom, std::sin(a) * rayFrom);
        const QPointF to = c + QPointF(std::cos(a) * rayTo, std::sin(a) * rayTo);
        if (!zone.isEmpty() && (zone.contains(from) || zone.contains(to) || zone.contains((from + to) / 2))) continue;
        path.moveTo(from);
        path.lineTo(to);
    }
    return path;
}

// Draws `path` except where it would cross `front` (plus a gap), so `front` reads as in front of it.
void drawBehind(QPainter& p, const QPainterPath& path, const QPainterPath& front) {
    thread_local LastPath lastClip;
    if (!lastClip.valid || !(lastClip.in == front)) {
        QPainterPath everything; everything.addRect(QRectF(-64, -64, 384, 384));
        lastClip.in = front; lastClip.out = everything.subtracted(keepOut(front)); lastClip.valid = true;
    }
    p.save();
    p.setClipPath(lastClip.out, Qt::IntersectClip);
    p.drawPath(path);
    p.restore();
}

void segments(QPainter& p, std::initializer_list<QLineF> lines) { for (const QLineF& l : lines) p.drawLine(l); }

} // namespace

// ---------------------------------------------------------------------------------------------------------

WeatherClient::WeatherClient(QObject* parent)
    : QObject(parent), network_(new QNetworkAccessManager(this)),
      forecastUrl_(QString::fromLatin1(ForecastEndpoint)), geocodeUrl_(QString::fromLatin1(GeocodeEndpoint)) {
    qRegisterMetaType<WeatherReport>();
}

WeatherClient::~WeatherClient() = default;

QString WeatherClient::configPath() const { return QDir(dataDir_).filePath(QStringLiteral("weather.json")); }
QString WeatherClient::cachePath() const { return QDir(dataDir_).filePath(QStringLiteral("weather/last.json")); }

bool WeatherClient::validCoordinates(double latitude, double longitude) {
    return std::isfinite(latitude) && std::isfinite(longitude) && latitude >= -90 && latitude <= 90
        && longitude >= -180 && longitude <= 180;
}

bool WeatherClient::setEndpoints(const QUrl& forecast, const QUrl& geocoding) {
    if (forecast.scheme() != QLatin1String("https") || geocoding.scheme() != QLatin1String("https")
        || forecast.host().isEmpty() || geocoding.host().isEmpty()) {
        qInfo().noquote() << "weather: endpoints refused (https only)";
        return false;
    }
    forecastUrl_ = forecast; geocodeUrl_ = geocoding;
    lastFailure_ = {};
    return true;
}

bool WeatherClient::configure(const QString& dataDirectory) {
    dataDir_ = dataDirectory;
    last_ = {};
    configured_ = readConfig();
    if (configured_) loadCache();
    qInfo().noquote() << "weather:" << (configured_
        ? QStringLiteral("%1 (%2, %3) %4%5").arg(place_).arg(latitude_, 0, 'f', 4).arg(longitude_, 0, 'f', 4)
              .arg(imperial_ ? "imperial" : "metric", last_.valid ? QStringLiteral(", cached report %1 s old").arg(last_.ageSeconds()) : QString())
        : (place_.isEmpty() ? QStringLiteral("not configured") : QStringLiteral("place '%1' has no coordinates yet").arg(place_)));
    return configured_;
}

bool WeatherClient::readConfig() {
    place_.clear(); region_.clear(); country_.clear(); imperial_ = true; latitude_ = longitude_ = 0;   // Fahrenheit unless the file says metric
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly)) return false;
    if (f.size() > MaxResponseBytes) { qInfo().noquote() << "weather: weather.json too large, ignored"; return false; }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) { qInfo().noquote() << "weather: weather.json is not a json object, ignored"; return false; }
    const QJsonObject o = doc.object();
    place_ = o.value("place").toString().trimmed().left(100);
    region_ = o.value("region").toString().trimmed().left(100);
    country_ = o.value("country").toString().trimmed().left(100);
    imperial_ = o.value("units").toString().compare(QLatin1String("metric"), Qt::CaseInsensitive) != 0;   // missing or unknown = imperial, the documented default
    const double lat = number(o.value("latitude")), lon = number(o.value("longitude"));
    if (!validCoordinates(lat, lon)) {
        if (o.contains("latitude") || o.contains("longitude")) qInfo().noquote() << "weather: weather.json coordinates out of range, ignored";
        return false;
    }
    latitude_ = lat; longitude_ = lon;
    if (place_.isEmpty()) place_ = QStringLiteral("%1, %2").arg(lat, 0, 'f', 2).arg(lon, 0, 'f', 2);
    return true;
}

bool WeatherClient::writeConfig() const {
    if (dataDir_.isEmpty()) return false;
    QJsonObject o;
    QFile in(configPath());
    if (in.size() <= MaxResponseBytes && in.open(QIODevice::ReadOnly)) o = QJsonDocument::fromJson(in.readAll()).object();
    in.close();
    o["place"] = place_;
    if (region_.isEmpty()) o.remove("region"); else o["region"] = region_;
    if (country_.isEmpty()) o.remove("country"); else o["country"] = country_;
    if (configured_) { o["latitude"] = latitude_; o["longitude"] = longitude_; }
    o["units"] = imperial_ ? QStringLiteral("imperial") : QStringLiteral("metric");
    QDir().mkpath(dataDir_);
    QSaveFile f(configPath());
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    const bool ok = f.commit();
    if (!ok) qInfo().noquote() << "weather: could not write weather.json";
    return ok;
}

bool WeatherClient::cacheMatches(const QJsonObject& cache) const {
    return std::abs(number(cache.value("latitude")) - latitude_) < 1e-6
        && std::abs(number(cache.value("longitude")) - longitude_) < 1e-6
        && (cache.value("units").toString() == QLatin1String("imperial")) == imperial_;
}

bool WeatherClient::loadCache() {
    QFile f(cachePath());
    if (!f.open(QIODevice::ReadOnly) || f.size() > 2 * MaxResponseBytes) return false;
    const QJsonObject cache = QJsonDocument::fromJson(f.readAll()).object();
    if (cache.value("version").toInt() != 1 || !cacheMatches(cache)) return false;
    const QDateTime fetched = QDateTime::fromString(cache.value("fetched").toString(), Qt::ISODate).toUTC();
    if (!fetched.isValid()) return false;
    QString error;
    WeatherReport report = parse(cache.value("response").toObject(), fetched, &error);
    if (!report.valid) { qInfo().noquote() << "weather: cache unusable:" << error; return false; }
    report.fromCache = true;
    last_ = report;
    return true;
}

QNetworkReply* WeatherClient::get(const QUrl& url) {
    QNetworkRequest req(url);
    req.setTransferTimeout(RequestTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("native-xmb weather"));
    req.setRawHeader("Accept", "application/json");
    QNetworkReply* reply = network_->get(req);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64 total) {
        if (received > MaxResponseBytes || total > MaxResponseBytes) {
            reply->setProperty("tooLarge", true);
            reply->abort();
        }
    });
    return reply;
}

static QString replyError(QNetworkReply* reply, const QByteArray& body) {
    if (reply->property("tooLarge").toBool()) return QStringLiteral("response larger than 1 MB");
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::OperationCanceledError && status == 0) return QStringLiteral("timed out or cancelled");
    if (status >= 300 && status < 400) return QStringLiteral("unexpected redirect (HTTP %1)").arg(status);
    if (status != 0 && status != 200) {
        const QString reason = QJsonDocument::fromJson(body).object().value("reason").toString().left(200);
        return QStringLiteral("HTTP %1%2").arg(status).arg(reason.isEmpty() ? QString() : ": " + reason);
    }
    if (reply->error() != QNetworkReply::NoError) return reply->errorString().left(200);
    return {};
}

void WeatherClient::setPlace(const QString& name) {
    QString text = name.simplified();
    QString qualifier;
    const qsizetype comma = text.indexOf(QLatin1Char(','));
    if (comma >= 0) { qualifier = text.mid(comma + 1).trimmed(); text = text.left(comma).trimmed(); }
    if (text.size() < 2 || text.size() > 100 || qualifier.size() > 100) {
        QTimer::singleShot(0, this, [this, name] {
            refreshAfterPlace_ = false;
            qInfo().noquote() << "weather: place name refused:" << name.left(100);
            emit placeResolved(false, name, QStringLiteral("place name must be 2 to 100 characters"));
        });
        return;
    }
    if (pendingPlace_) { pendingPlace_->disconnect(this); pendingPlace_->abort(); pendingPlace_->deleteLater(); }
    QUrl url = geocodeUrl_;
    QUrlQuery q;
    q.addQueryItem("name", text);
    q.addQueryItem("count", "10");
    q.addQueryItem("language", "en");
    q.addQueryItem("format", "json");
    url.setQuery(q);
    QNetworkReply* reply = get(url);
    pendingPlace_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, text, qualifier] { finishPlace(reply, text, qualifier); });
}

void WeatherClient::finishPlace(QNetworkReply* reply, const QString& name, const QString& qualifier) {
    reply->deleteLater();
    if (pendingPlace_ == reply) pendingPlace_ = nullptr;
    const QByteArray body = reply->readAll();
    QString error = replyError(reply, body);
    const QString asked = qualifier.isEmpty() ? name : name + ", " + qualifier;
    QJsonObject chosen;
    if (error.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject()) error = QStringLiteral("geocoding answer is not json");
        else {
            const QJsonArray results = doc.object().value("results").toArray();
            for (const QJsonValue& v : results) {
                const QJsonObject r = v.toObject();
                if (!validCoordinates(number(r.value("latitude")), number(r.value("longitude")))) continue;
                if (!qualifier.isEmpty()) {
                    bool match = false;
                    for (const char* key : {"admin1", "admin2", "country", "country_code"}) {
                        const QString value = r.value(QLatin1String(key)).toString();
                        if (value.compare(qualifier, Qt::CaseInsensitive) == 0) match = true;
                    }
                    if (!match && qualifier.size() >= 2)
                        match = r.value("admin1").toString().startsWith(qualifier, Qt::CaseInsensitive)
                             || r.value("country").toString().startsWith(qualifier, Qt::CaseInsensitive);
                    if (!match) continue;
                }
                chosen = r;
                break;
            }
            if (chosen.isEmpty()) error = QStringLiteral("no place found for '%1'").arg(asked);
        }
    }
    if (!error.isEmpty()) {
        qInfo().noquote() << "weather: geocoding failed:" << error;
        const bool chained = refreshAfterPlace_;
        refreshAfterPlace_ = false;
        emit placeResolved(false, asked, error);
        if (chained) emit failed(QStringLiteral("not configured: ") + error);
        return;
    }
    place_ = chosen.value("name").toString().left(100);
    region_ = chosen.value("admin1").toString().left(100);
    country_ = chosen.value("country_code").toString().left(8);
    latitude_ = chosen.value("latitude").toDouble();
    longitude_ = chosen.value("longitude").toDouble();
    configured_ = true;
    last_ = {};
    lastFailure_ = {};
    writeConfig();
    QStringList parts;
    for (const QString& part : {place_, region_, country_}) if (!part.isEmpty()) parts << part;
    const QString display = parts.join(", ");
    qInfo().noquote() << "weather: place set to" << display << QStringLiteral("(%1, %2)").arg(latitude_, 0, 'f', 4).arg(longitude_, 0, 'f', 4);
    emit placeResolved(true, display, {});
    if (refreshAfterPlace_) { refreshAfterPlace_ = false; refresh(); }
}

bool WeatherClient::setImperial(bool imperial) {
    if (imperial == imperial_) return true;
    imperial_ = imperial;
    last_ = {};
    lastFailure_ = {};
    if (configured_ || !place_.isEmpty()) {
        if (!writeConfig()) return false;
        loadCache();   // a cache in the new units is fine to show
    }
    return true;
}

void WeatherClient::refresh() {
    if (!configured_) {
        if (!place_.isEmpty() && !pendingPlace_) {   // a hand-written {"place": "..."} without coordinates
            refreshAfterPlace_ = true;
            setPlace(place_);
            return;
        }
        if (pendingPlace_) { refreshAfterPlace_ = true; return; }
        QTimer::singleShot(0, this, [this] {
            qInfo().noquote() << "weather: not configured";
            emit failed(QStringLiteral("not configured"));
        });
        return;
    }
    if (pending_) return;   // the answer to the request in flight will be emitted
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (last_.valid && !last_.stale && last_.ageSeconds() >= 0 && last_.ageSeconds() < minimumInterval_) {
        WeatherReport report = last_;
        report.fromCache = true;
        QTimer::singleShot(0, this, [this, report] {
            qInfo().noquote() << "weather: served cached report," << report.ageSeconds() << "s old";
            emit weatherReady(report);
        });
        return;
    }
    if (lastFailure_.isValid() && lastFailure_.secsTo(now) < RetryAfterFailureSeconds) {
        const QString error = lastError_;
        QTimer::singleShot(0, this, [this, error] { serveStale(error); });
        return;
    }
    QUrl url = forecastUrl_;
    QUrlQuery q;
    q.addQueryItem("latitude", QString::number(latitude_, 'f', 4));
    q.addQueryItem("longitude", QString::number(longitude_, 'f', 4));
    q.addQueryItem("current", "temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,wind_speed_10m,wind_direction_10m");
    q.addQueryItem("daily", "weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset");
    q.addQueryItem("timezone", "auto");
    q.addQueryItem("forecast_days", QString::number(ForecastDays));
    q.addQueryItem("temperature_unit", imperial_ ? "fahrenheit" : "celsius");
    q.addQueryItem("wind_speed_unit", imperial_ ? "mph" : "kmh");
    q.addQueryItem("precipitation_unit", imperial_ ? "inch" : "mm");
    q.addQueryItem("timeformat", "iso8601");
    url.setQuery(q);
    QNetworkReply* reply = get(url);
    pending_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finishForecast(reply); });
}

void WeatherClient::finishForecast(QNetworkReply* reply) {
    reply->deleteLater();
    if (pending_ == reply) pending_ = nullptr;
    const QByteArray body = reply->readAll();
    QString error = replyError(reply, body);
    const QDateTime fetched = QDateTime::currentDateTimeUtc();
    QJsonObject response;
    WeatherReport report;
    if (error.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject()) error = QStringLiteral("forecast answer is not a json object");
        else { response = doc.object(); report = parse(response, fetched, &error); }
    }
    if (!report.valid) {
        if (error.isEmpty()) error = QStringLiteral("forecast answer unusable");
        lastFailure_ = fetched;
        lastError_ = error;
        qInfo().noquote() << "weather: fetch failed:" << error;
        serveStale(error);
        return;
    }
    lastFailure_ = {};
    lastError_.clear();
    QJsonObject cache;
    cache["version"] = 1;
    cache["fetched"] = fetched.toString(Qt::ISODateWithMs);
    cache["place"] = place_; cache["region"] = region_; cache["country"] = country_;
    cache["latitude"] = latitude_; cache["longitude"] = longitude_;
    cache["units"] = imperial_ ? QStringLiteral("imperial") : QStringLiteral("metric");
    cache["response"] = response;
    QDir().mkpath(QFileInfo(cachePath()).absolutePath());
    QSaveFile f(cachePath());
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(cache).toJson(QJsonDocument::Compact)) < 0 || !f.commit())
        qInfo().noquote() << "weather: could not write the cache" << cachePath();
    last_ = report;
    qInfo().noquote() << QStringLiteral("weather: %1 %2%3 %4, high %5 low %6").arg(report.place)
        .arg(report.temperature, 0, 'f', 1).arg(report.temperatureUnit, report.text)
        .arg(report.today.high, 0, 'f', 1).arg(report.today.low, 0, 'f', 1);
    emit weatherReady(report);
}

void WeatherClient::serveStale(const QString& error) {
    if (!last_.valid) {
        if (!loadCache()) { emit failed(error); return; }
    }
    WeatherReport report = last_;
    report.fromCache = true;
    report.stale = true;
    report.error = error;
    qInfo().noquote() << "weather: offline, served cached report" << report.ageSeconds() << "s old";
    emit weatherReady(report);
}

WeatherReport WeatherClient::parse(const QJsonObject& root, const QDateTime& fetched, QString* error) const {
    WeatherReport r;
    auto fail = [&](const QString& why) { if (error) *error = why; return WeatherReport{}; };
    if (root.value("error").toBool()) return fail(QStringLiteral("Open-Meteo: ") + root.value("reason").toString().left(200));
    const double lat = number(root.value("latitude")), lon = number(root.value("longitude"));
    if (!validCoordinates(lat, lon)) return fail(QStringLiteral("answer has no valid coordinates"));
    // Open-Meteo snaps to its grid; anything more than ~1 degree away is not an answer for this place.
    if (std::abs(lat - latitude_) > 1.0 || std::abs(lon - longitude_) > 1.0) return fail(QStringLiteral("answer is for another place"));
    r.place = place_; r.region = region_; r.country = country_;
    r.latitude = latitude_; r.longitude = longitude_;
    r.imperial = imperial_;
    r.temperatureUnit = imperial_ ? QStringLiteral("°F") : QStringLiteral("°C");
    r.windUnit = imperial_ ? QStringLiteral("mph") : QStringLiteral("km/h");
    r.timezone = root.value("timezone").toString().left(64);
    r.utcOffsetSeconds = integer(root.value("utc_offset_seconds"), 0);
    if (std::abs(r.utcOffsetSeconds) > 14 * 3600) r.utcOffsetSeconds = 0;
    r.fetched = fetched;

    const QJsonObject current = root.value("current").toObject();
    if (current.isEmpty()) return fail(QStringLiteral("answer has no current values"));
    r.observed = localTime(current.value("time").toString(), r.utcOffsetSeconds);
    r.temperature = number(current.value("temperature_2m"));
    r.apparent = number(current.value("apparent_temperature"));
    r.windSpeed = number(current.value("wind_speed_10m"));
    r.humidity = integer(current.value("relative_humidity_2m"));
    if (r.humidity > 100) r.humidity = 100;
    r.windDirection = integer(current.value("wind_direction_10m"));
    if (r.windDirection >= 0) r.windDirection %= 360;
    r.code = integer(current.value("weather_code"));
    r.text = describe(r.code);
    r.icon = iconFor(r.code);
    r.night = integer(current.value("is_day"), 1) == 0;
    if (!std::isfinite(r.temperature)) return fail(QStringLiteral("answer has no current temperature"));

    const QJsonObject daily = root.value("daily").toObject();
    const QJsonArray dates = daily.value("time").toArray();
    const QJsonArray codes = daily.value("weather_code").toArray(), highs = daily.value("temperature_2m_max").toArray(),
        lows = daily.value("temperature_2m_min").toArray(), chances = daily.value("precipitation_probability_max").toArray(),
        sunrises = daily.value("sunrise").toArray(), sunsets = daily.value("sunset").toArray();
    for (qsizetype i = 0; i < dates.size() && r.days.size() < ForecastDays; ++i) {
        WeatherDay d;
        d.date = QDate::fromString(dates.at(i).toString(), Qt::ISODate);
        if (!d.date.isValid()) continue;
        d.code = integer(codes.at(i));
        d.text = describe(d.code);
        d.icon = iconFor(d.code);
        d.high = number(highs.at(i));
        d.low = number(lows.at(i));
        d.precipitationChance = integer(chances.at(i));
        if (d.precipitationChance > 100) d.precipitationChance = 100;
        d.sunrise = localTime(sunrises.at(i).toString(), r.utcOffsetSeconds);
        d.sunset = localTime(sunsets.at(i).toString(), r.utcOffsetSeconds);
        r.days.push_back(d);
    }
    if (r.days.isEmpty()) return fail(QStringLiteral("answer has no daily forecast"));
    r.today = r.days.first();
    r.valid = true;
    return r;
}

QString WeatherClient::describe(int code) {
    switch (code) {
    case 0: return QStringLiteral("Clear");
    case 1: return QStringLiteral("Mainly clear");
    case 2: return QStringLiteral("Partly cloudy");
    case 3: return QStringLiteral("Overcast");
    case 45: return QStringLiteral("Fog");
    case 48: return QStringLiteral("Rime fog");
    case 51: return QStringLiteral("Light drizzle");
    case 53: return QStringLiteral("Drizzle");
    case 55: return QStringLiteral("Heavy drizzle");
    case 56: return QStringLiteral("Freezing drizzle");
    case 57: return QStringLiteral("Heavy freezing drizzle");
    case 61: return QStringLiteral("Light rain");
    case 63: return QStringLiteral("Rain");
    case 65: return QStringLiteral("Heavy rain");
    case 66: return QStringLiteral("Freezing rain");
    case 67: return QStringLiteral("Heavy freezing rain");
    case 71: return QStringLiteral("Light snow");
    case 73: return QStringLiteral("Snow");
    case 75: return QStringLiteral("Heavy snow");
    case 77: return QStringLiteral("Snow grains");
    case 80: return QStringLiteral("Light showers");
    case 81: return QStringLiteral("Showers");
    case 82: return QStringLiteral("Heavy showers");
    case 85: return QStringLiteral("Snow showers");
    case 86: return QStringLiteral("Heavy snow showers");
    case 95: return QStringLiteral("Thunderstorm");
    case 96: return QStringLiteral("Thunderstorm, hail");
    case 97: return QStringLiteral("Heavy thunderstorm");
    case 99: return QStringLiteral("Thunderstorm, heavy hail");
    default: return QStringLiteral("Unknown");
    }
}

WeatherIcon WeatherClient::iconFor(int code) {
    if (code == 0 || code == 1) return WeatherIcon::Clear;
    if (code == 2) return WeatherIcon::Partly;
    if (code == 45 || code == 48) return WeatherIcon::Fog;
    if (code >= 51 && code <= 57) return WeatherIcon::Drizzle;
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return WeatherIcon::Rain;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return WeatherIcon::Snow;
    if (code >= 95 && code <= 99) return WeatherIcon::Thunder;
    return WeatherIcon::Cloudy;   // 3 and anything unknown
}

QString WeatherClient::iconName(WeatherIcon kind) {
    switch (kind) {
    case WeatherIcon::Clear: return QStringLiteral("clear");
    case WeatherIcon::Partly: return QStringLiteral("partly");
    case WeatherIcon::Cloudy: return QStringLiteral("cloudy");
    case WeatherIcon::Fog: return QStringLiteral("fog");
    case WeatherIcon::Drizzle: return QStringLiteral("drizzle");
    case WeatherIcon::Rain: return QStringLiteral("rain");
    case WeatherIcon::Snow: return QStringLiteral("snow");
    case WeatherIcon::Thunder: return QStringLiteral("thunder");
    }
    return QStringLiteral("cloudy");
}

static QImage drawIcon(WeatherIcon kind, bool night, int size) {
    size = qBound(8, size, 2048);
    // Clouds with weather under them sit higher so the precipitation fits below.
    const QPainterPath high = cloud(QTransform::fromTranslate(0, -34));
    switch (kind) {
    case WeatherIcon::Clear:
        return line(size, [night](QPainter& p) {
            if (night) p.drawPath(QTransform().translate(128, 128).rotate(-12).translate(-128, -128).map(moon(QPointF(124, 132), 78)));
            else p.drawPath(sun(QPointF(128, 128), 46, 72, 100));
        });
    case WeatherIcon::Partly:
        return line(size, [night](QPainter& p) {
            const QPainterPath front = cloud(QTransform(0.8, 0, 0, 0.8, 68, 66));
            const QPainterPath back = night ? moon(QPointF(94, 96), 50) : sun(QPointF(92, 92), 32, 50, 70, front);
            drawBehind(p, back, front);
            p.drawPath(front);
        });
    case WeatherIcon::Cloudy:
        return line(size, [](QPainter& p) {
            const QPainterPath front = cloud(QTransform::fromTranslate(-8, 22));
            const QPainterPath back = cloud(QTransform(0.62, 0, 0, 0.62, 104, 4));
            drawBehind(p, back, front);
            p.drawPath(front);
        });
    case WeatherIcon::Fog:
        return line(size, [high](QPainter& p) {
            p.drawPath(high);
            segments(p, {QLineF(52, 176, 204, 176), QLineF(80, 212, 176, 212)});
        });
    case WeatherIcon::Drizzle:
        return line(size, [high](QPainter& p) {
            p.drawPath(high);
            segments(p, {QLineF(92, 172, 88, 182), QLineF(132, 172, 128, 182), QLineF(172, 172, 168, 182),
                         QLineF(112, 206, 108, 216), QLineF(152, 206, 148, 216)});
        });
    case WeatherIcon::Rain:
        return line(size, [high](QPainter& p) {
            p.drawPath(high);
            segments(p, {QLineF(94, 170, 80, 214), QLineF(134, 170, 120, 214), QLineF(174, 170, 160, 214)});
        });
    case WeatherIcon::Snow:
        return line(size, [high](QPainter& p) {
            p.drawPath(high);
            for (const QPointF c : {QPointF(88, 186), QPointF(168, 186), QPointF(128, 218)}) {
                for (int i = 0; i < 3; ++i) {
                    const qreal a = M_PI / 2 + i * M_PI / 3;
                    const QPointF d(std::cos(a) * 15, std::sin(a) * 15);
                    p.drawLine(c - d, c + d);
                }
            }
        });
    case WeatherIcon::Thunder:
        return line(size, [high](QPainter& p) {
            p.drawPath(high);
            p.drawPolyline(QPolygonF{{140, 160}, {112, 198}, {142, 198}, {116, 236}});
        });
    }
    return {};
}

// ZF4: the same drawing, remembered. The start-up path asks for the same icons the shell builds its rows from, and a worker
// thread starts drawing them before the shell exists (Shell::initWeather then finds them done). One lock covers the drawing,
// so a request that meets a drawing in progress waits for it instead of repeating it.
QImage WeatherClient::icon(WeatherIcon kind, bool night, int size) {
    static QMutex lock;
    static QHash<quint64, QImage> memo;
    size = qBound(8, size, 2048);
    const quint64 key = (quint64(kind) << 40) | (quint64(night ? 1 : 0) << 32) | quint64(size);
    const QMutexLocker guard(&lock);
    const auto found = memo.constFind(key);
    if (found != memo.cend()) return *found;
    const QImage image = drawIcon(kind, night, size);
    memo.insert(key, image);
    return image;
}

} // namespace xmb
