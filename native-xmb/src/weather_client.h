#pragma once
// Weather for the XMB (plan item P5, audit/PHASES-2026-11-07.md).
//
// A keyless client for Open-Meteo: the forecast API (https://api.open-meteo.com/v1/forecast) for current
// conditions and a daily forecast, and the geocoding API (https://geocoding-api.open-meteo.com/v1/search) to turn
// a place name into coordinates.
//
// Configuration: <data>/weather.json
//   {"place": "Chicago", "region": "Illinois", "country": "US", "latitude": 41.85, "longitude": -87.65,
//    "units": "metric" | "imperial"}
// setPlace() geocodes a name and rewrites the file (keeping the units). With no file, or no valid coordinates,
// the client is "not configured": refresh() reports that and never guesses a location (no IP geolocation).
//
// Cache: every successful forecast is written to <data>/weather/last.json with the time it was fetched (the raw
// Open-Meteo answer plus the place, coordinates and units it was asked for). refresh() serves that cache instead
// of the network while it is younger than the minimum interval (15 minutes), and serves it marked stale, with its
// age, when the network fails. A cache made for other coordinates or units is never served.
//
// Bounds: https only (also for test endpoints), redirects are not followed, 10 s per request, responses capped
// at 1 MB, latitude/longitude range-checked on input and in answers. Logs are one line each, "weather: ...".
#include <QDate>
#include <QDateTime>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

namespace xmb {

enum class WeatherIcon { Clear, Partly, Cloudy, Fog, Drizzle, Rain, Snow, Thunder };

struct WeatherDay {
    QDate date;                 // local date at the place
    int code = -1;              // WMO weather code (most severe of the day)
    WeatherIcon icon = WeatherIcon::Cloudy;
    QString text;               // short description ("Light rain")
    double high = qQNaN(), low = qQNaN();
    int precipitationChance = -1;   // percent, -1 when the model has none
    QDateTime sunrise, sunset;  // with the place's UTC offset; invalid when absent (polar day/night)
};

struct WeatherReport {
    bool valid = false;
    // What was asked for.
    QString place, region, country;
    double latitude = 0, longitude = 0;
    bool imperial = false;
    QString temperatureUnit, windUnit;   // "°C"/"°F", "km/h"/"mph"
    QString timezone;                    // IANA name Open-Meteo resolved ("America/Chicago")
    int utcOffsetSeconds = 0;
    // When.
    QDateTime fetched;     // UTC, when this answer came from the network
    QDateTime observed;    // the model time of the current values, with the place's offset
    bool fromCache = false;  // served from last.json rather than a request made just now
    bool stale = false;      // served from cache because the network failed
    QString error;           // the network failure when stale
    // Now.
    double temperature = qQNaN(), apparent = qQNaN(), windSpeed = qQNaN();
    int humidity = -1, windDirection = -1;   // percent, degrees
    int code = -1;
    QString text;
    WeatherIcon icon = WeatherIcon::Cloudy;
    bool night = false;
    // Today and the next days: days[0] is today, days has up to 5 entries (today plus four).
    WeatherDay today;
    QVector<WeatherDay> days;

    qint64 ageSeconds() const { return fetched.isValid() ? fetched.secsTo(QDateTime::currentDateTimeUtc()) : -1; }
};

class WeatherClient final : public QObject {
    Q_OBJECT
public:
    explicit WeatherClient(QObject* parent = nullptr);
    ~WeatherClient() override;

    // Reads <data>/weather.json and loads <data>/weather/last.json (when it matches). Returns configured().
    bool configure(const QString& dataDirectory);
    bool configured() const { return configured_; }
    QString place() const { return place_; }
    bool imperial() const { return imperial_; }
    // Changes the units, rewrites weather.json and drops the cache gate (the next refresh fetches).
    bool setImperial(bool imperial);
    // Geocodes `name` ("Chicago", "Springfield, Illinois", "Paris, FR"; text after a comma picks among the
    // matches by region, country or country code). On success stores the place and coordinates in weather.json.
    // -> placeResolved(ok, displayName, message).
    void setPlace(const QString& name);

    // Fetches the forecast, or serves the cache while it is younger than minimumInterval().
    // -> weatherReady(report), or failed(message) when there is nothing to show ("not configured", or the
    // network failed with no usable cache). A network failure with a usable cache emits weatherReady with
    // stale = true and error set.
    void refresh();
    // The last report (from the network or last.json); valid = false when there is none.
    WeatherReport lastReport() const { return last_; }

    // Default 900 s. Tests may lower it; values below 0 are taken as 0.
    void setMinimumInterval(int seconds) { minimumInterval_ = qMax(0, seconds); }
    int minimumInterval() const { return minimumInterval_; }
    // Test hook: other https endpoints. Returns false (and keeps the old ones) for anything but https.
    bool setEndpoints(const QUrl& forecast, const QUrl& geocoding);

    static QString describe(int wmoCode);          // short English text, "Unknown" for codes outside the table
    static WeatherIcon iconFor(int wmoCode);
    static QString iconName(WeatherIcon kind);     // "clear", "partly", ...
    static bool validCoordinates(double latitude, double longitude);
    // White line weather glyph in the XMB glyph style (12 px stroke at 256 px over a soft dark shadow), on a
    // transparent size x size image. Night changes clear (moon) and partly (moon behind cloud); the rest are
    // the same by day and night.
    static QImage icon(WeatherIcon kind, bool night, int size = 256);

signals:
    void weatherReady(xmb::WeatherReport report);
    void failed(QString message);
    void placeResolved(bool ok, QString place, QString message);

private:
    QNetworkAccessManager* network_ = nullptr;
    QString dataDir_;
    bool configured_ = false, imperial_ = false;
    QString place_, region_, country_;
    double latitude_ = 0, longitude_ = 0;
    QUrl forecastUrl_, geocodeUrl_;
    int minimumInterval_ = 900;
    WeatherReport last_;
    QNetworkReply* pending_ = nullptr;
    QNetworkReply* pendingPlace_ = nullptr;
    bool refreshAfterPlace_ = false;
    QDateTime lastFailure_;   // UTC; a failed fetch is not retried for 60 s (the cache is served meanwhile)
    QString lastError_;

    QNetworkReply* get(const QUrl& url);
    void finishForecast(QNetworkReply* reply);
    void finishPlace(QNetworkReply* reply, const QString& name, const QString& qualifier);
    bool readConfig();
    bool writeConfig() const;
    bool loadCache();
    bool cacheMatches(const QJsonObject& cache) const;
    void serveStale(const QString& error);
    WeatherReport parse(const QJsonObject& response, const QDateTime& fetched, QString* error) const;
    QString configPath() const;
    QString cachePath() const;
};

}

Q_DECLARE_METATYPE(xmb::WeatherReport)
