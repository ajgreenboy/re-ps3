// Network > Weather (weather_client.h; audit/WEATHER.md). Pages are ordinary XMB rows; nothing here draws.
//
//   Weather (Network column)   "<temp> <conditions> - <place>" with the conditions icon; refreshed every 15 min
//   Weather page               Now, Today, the next four days, and Units (Fahrenheit / Celsius)
// The place comes from <data>/weather.json ({"place": "Chicago, IL"}): Open-Meteo, no key, never IP geolocation.
#include "shell.h"
#include "diagnostics.h"
#include "glyphs.h"
#include <QLocale>
#include <QTimer>
#include <cmath>

namespace xmb {
namespace {
Entry weatherRow(QString id,QString title,Action action,QString value={},QString detail={}) {
    Entry e;e.id=std::move(id);e.title=std::move(title);e.action=action;e.value=std::move(value);e.detail=std::move(detail);
    e.container=QStringLiteral("explore_plugin_full");e.texture=QStringLiteral("item_tex_plain_folder");e.source=QStringLiteral("native");
    return e;
}
Entry iconed(Entry e,const QImage& icon){if(!icon.isNull()){e.artwork=icon;e.iconSized=true;}return e;}
QString degrees(double v,const QString& unit){return std::isnan(v)?QStringLiteral("-"):QStringLiteral("%1%2").arg(qRound(v)).arg(unit);}
}

QString Shell::weatherSummary() const {
    if(!weather_.configured() && weather_.place().isEmpty())return tr("Set a place in weather.json");
    if(!weatherReport_.valid)return weatherError_.isEmpty()?tr("Reading the forecast..."):weatherError_;
    const auto& r=weatherReport_;
    QString text=degrees(r.temperature,r.temperatureUnit)+QStringLiteral(" ")+r.text+QStringLiteral(" - ")+r.place;
    if(r.stale)text+=tr(" (offline, %1 min old)").arg(r.ageSeconds()/60);
    return text;
}

QVector<Entry> Shell::weatherRows() const {
    QVector<Entry> rows;
    if(!weather_.configured() && weather_.place().isEmpty()) {
        rows.push_back(iconed(weatherRow(QStringLiteral("weather:setup"),tr("Set Up Weather"),Action::Information,
            tr("Put weather.json in the data folder with the place to show, for example {\"place\": \"Chicago, IL\"}. "
               "Add \"units\": \"metric\" for Celsius. The forecast comes from Open-Meteo; no key and no location lookup."),
            tr("No place is set")),WeatherClient::icon(WeatherIcon::Partly,false)));
        return rows;
    }
    const auto& r=weatherReport_;
    if(!r.valid) {
        rows.push_back(weatherRow(QStringLiteral("weather:wait"),tr("Weather"),Action::Information,weatherSummary(),weatherSummary()));
        return rows;
    }
    const QLocale locale(QLocale::English);
    QStringList now;
    if(!std::isnan(r.apparent))now<<tr("Feels like %1").arg(degrees(r.apparent,r.temperatureUnit));
    if(r.humidity>=0)now<<tr("Humidity %1%").arg(r.humidity);
    if(!std::isnan(r.windSpeed))now<<tr("Wind %1 %2").arg(qRound(r.windSpeed)).arg(r.windUnit);
    rows.push_back(iconed(weatherRow(QStringLiteral("weather:now"),degrees(r.temperature,r.temperatureUnit)+QStringLiteral(" ")+r.text,
        Action::Information,r.place+QStringLiteral("\n")+now.join(QStringLiteral("\n")),now.join(QStringLiteral(" - "))),
        WeatherClient::icon(WeatherClient::iconFor(r.code),r.night)));
    for(int i=0;i<r.days.size() && i<5;++i) {
        const auto& d=r.days[i];
        QStringList detail;
        detail<<tr("High %1").arg(degrees(d.high,r.temperatureUnit))<<tr("Low %1").arg(degrees(d.low,r.temperatureUnit));
        if(d.precipitationChance>=0)detail<<tr("Rain %1%").arg(d.precipitationChance);
        if(i==0 && d.sunrise.isValid())detail<<tr("Sun %1-%2").arg(locale.toString(d.sunrise.time(),QStringLiteral("h:mm"))).arg(locale.toString(d.sunset.time(),QStringLiteral("h:mm")));
        const QString title=(i==0?tr("Today"):locale.toString(d.date,QStringLiteral("dddd")))+QStringLiteral(" - ")+d.text;
        rows.push_back(iconed(weatherRow(QStringLiteral("weather:day:%1").arg(i),title,Action::Information,
            title+QStringLiteral("\n")+detail.join(QStringLiteral("\n")),detail.join(QStringLiteral(" - "))),
            WeatherClient::icon(WeatherClient::iconFor(d.code),false)));
    }
    rows.push_back(iconed(weatherRow(QStringLiteral("weather:units"),tr("Units"),Action::WeatherUnits,QString(),
        r.imperial?tr("Fahrenheit - Cross switches to Celsius"):tr("Celsius - Cross switches to Fahrenheit")),glyphs::more()));
    if(r.stale)rows.push_back(weatherRow(QStringLiteral("weather:stale"),tr("Showing the Last Forecast"),Action::Information,r.error,
        tr("Offline - %1 min old").arg(r.ageSeconds()/60)));
    return rows;
}

void Shell::refreshWeatherRows() {
    for(auto& page:pages_) {
        if(!page.weather)continue;
        const QString focused=page.entries.value(page.selection).id;
        page.entries=weatherRows();page.selection=0;
        for(int i=0;i<page.entries.size();++i)if(page.entries[i].id==focused){page.selection=i;break;}
    }
    for(auto& column:categories_)for(auto& row:column.entries)if(row.action==Action::WeatherApp) {
        row.detail=weatherSummary();
        row.artwork=weatherReport_.valid?WeatherClient::icon(WeatherClient::iconFor(weatherReport_.code),weatherReport_.night)
                                        :WeatherClient::icon(WeatherIcon::Partly,false);
        row.iconSized=true;
    }
}

void Shell::initWeather() {
    weather_.configure(dataDirectory_);
    for(auto& column:categories_) {
        if(column.id!=QLatin1String("network"))continue;
        auto row=iconed(weatherRow(QStringLiteral("weather"),tr("Weather"),Action::WeatherApp,QString(),weatherSummary()),
                        WeatherClient::icon(WeatherIcon::Partly,false));
        row.container=QStringLiteral("explore_category_network");
        column.entries.push_back(std::move(row));
    }
    connect(&weather_,&WeatherClient::weatherReady,this,[this](const WeatherReport& report){
        weatherReport_=report;weatherError_.clear();refreshWeatherRows();emit changed();
    });
    connect(&weather_,&WeatherClient::failed,this,[this](const QString& message){
        weatherError_=message;refreshWeatherRows();emit changed();
    });
    connect(&weather_,&WeatherClient::placeResolved,this,[this](bool ok,const QString& place,const QString& message){
        if(ok){qInfo().noquote()<<"weather: place resolved to"<<place;weather_.refresh();}
        else{weatherError_=message;refreshWeatherRows();emit changed();}
    });
    if(diagnostics::safeMode()){qInfo()<<"weather: not refreshed in safe mode";return;}   // D6
    // A weather.json with only a place name is geocoded once (Open-Meteo), then stored with its coordinates.
    if(!weather_.configured() && !weather_.place().isEmpty())weather_.setPlace(weather_.place());
    if(!weather_.configured() && weather_.place().isEmpty())return;
    if(weather_.configured())weather_.refresh();
    // The client serves its cache inside 15 minutes, so a refresh every 15 minutes is at most one request.
    auto* timer=new QTimer(this);timer->setInterval(15*60*1000);
    connect(timer,&QTimer::timeout,this,[this]{weather_.refresh();});
    timer->start();
}

bool Shell::activateWeather(const Entry& entry) {
    switch(entry.action) {
    case Action::WeatherApp: {
        if(pages_.size()>=32)return true;
        Page page;page.title=tr("Weather");page.weather=true;page.entries=weatherRows();
        pages_.push_back(std::move(page));
        if(weather_.configured())weather_.refresh();
        return true;
    }
    case Action::WeatherUnits:
        if(weather_.setImperial(!weather_.imperial()))weather_.refresh();
        return true;
    default:return false;
    }
}

}
