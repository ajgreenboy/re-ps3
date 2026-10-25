#include "preferences.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSettings>
#include <QSysInfo>
#include <QTimeZone>
#include <cmath>

namespace xmb {
QVariant Preferences::defaultValue(const QString& key) {
    if(key=="appearance/colour" || key=="appearance/background" || key=="appearance/font" ||
       key=="clock/dateFormat" || key=="clock/hourFormat" || key=="photo/fit")return 0;
    if(key=="audio/volume")return 60;
    if(key=="photo/interval")return 5000;
    if(key=="media/repeatMode")return 0;
    if(key=="audio/keyTone")return true;
    if(key=="power/idleMinutes")return 0;
    if(key=="audio/muted" || key=="media/repeat" || key=="media/shuffle" || key=="display/fullscreen")return false;
    if(key=="system/name")return QSysInfo::machineHostName().left(64);
    if(key=="clock/timeZone")return QString("System");
    if(key=="appearance/wallpaper" || key=="appearance/theme" || key.startsWith("library/"))return QString{};
    return {};
}
bool Preferences::valid(const QString& key,const QVariant& value) {
    const auto integer=[&](int minimum,int maximum) {
        if(value.metaType().id()!=QMetaType::Int && value.metaType().id()!=QMetaType::LongLong && value.metaType().id()!=QMetaType::Double)return false;
        const double n=value.toDouble();return std::isfinite(n) && n==std::floor(n) && n>=minimum && n<=maximum;
    };
    if(key=="appearance/colour")return integer(0,5);
    if(key=="appearance/background" || key=="appearance/font" || key=="clock/dateFormat")return integer(0,2);
    if(key=="clock/hourFormat" || key=="photo/fit")return integer(0,1);
    if(key=="audio/volume")return integer(0,100);
    if(key=="photo/interval")return integer(1000,60000);
    if(key=="media/repeatMode")return integer(0,2);
    if(key=="power/idleMinutes")return integer(0,120);
    if(key=="audio/keyTone" || key=="audio/muted" || key=="media/repeat" || key=="media/shuffle" || key=="display/fullscreen")return value.metaType().id()==QMetaType::Bool;
    if(value.metaType().id()!=QMetaType::QString)return false;
    const auto text=value.toString();
    if(text.contains(QChar(0)))return false;
    if(key=="system/name")return !text.trimmed().isEmpty() && text.size()<=64 && !text.contains('\n') && !text.contains('\r');
    if(key=="clock/timeZone")return text=="System" || QTimeZone::isTimeZoneIdAvailable(text.toUtf8());
    if(key=="appearance/wallpaper" || key=="appearance/theme" || key=="library/photo" || key=="library/music" || key=="library/video")return text.size()<=32768;
    return false;
}
Preferences::Preferences(const QString& directory,QObject* parent):QObject(parent),path_(QDir(directory).filePath("preferences.json")) {
    QDir().mkpath(directory);
    QFile file(path_);
    if(file.exists()) {
        if(!file.open(QIODevice::ReadOnly) || file.size()>1024*1024) {
            writable_=false;error_="Preferences could not be read. The existing file has been preserved.";return;
        }
        QJsonParseError parse;const auto document=QJsonDocument::fromJson(file.readAll(),&parse);
        const auto root=document.object();
        if(parse.error!=QJsonParseError::NoError || !document.isObject() || root.value("version").toInt(-1)!=1 || !root.value("values").isObject()) {
            writable_=false;error_="Preferences have an invalid or newer format. The existing file has been preserved.";return;
        }
        const auto stored=root.value("values").toObject();
        for(auto it=stored.begin();it!=stored.end();++it)if(valid(it.key(),it.value().toVariant()))values_.insert(it.key(),it.value());
        return;
    }
    // Migrate the first native foundation's INI preferences without touching
    // window geometry or deleting the legacy file.
    QSettings legacy(QDir(directory).filePath("settings.ini"),QSettings::IniFormat);
    for(const QString& key:QStringList{"appearance/colour","library/photo","library/music","library/video"}) {
        if(!legacy.contains(key))continue;
        QVariant value=legacy.value(key);
        if(key=="appearance/colour"){bool ok=false;const auto n=value.toInt(&ok);if(ok)value=n;}
        if(valid(key,value))values_.insert(key,QJsonValue::fromVariant(value));
    }
    write(values_);
}
QVariant Preferences::value(const QString& key) const {
    const auto found=values_.constFind(key);
    return found==values_.cend()?defaultValue(key):found->toVariant();
}
bool Preferences::write(const QJsonObject& values) {
    QSaveFile file(path_);file.setDirectWriteFallback(false);
    const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"values",values}}).toJson();
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) {
        error_="Settings could not be saved: "+file.errorString();emit failed(error_);return false;
    }
    error_.clear();return true;
}
bool Preferences::setValue(const QString& key,const QVariant& value) {
    return setValues({{key,value}});
}
bool Preferences::setValues(const QVariantMap& values) {
    if(!writable_){emit failed(error_);return false;}
    auto next=values_;QStringList changedKeys;
    for(auto it=values.cbegin();it!=values.cend();++it) {
        if(!valid(it.key(),it.value())){emit failed("The setting value is invalid.");return false;}
        if(value(it.key())==it.value())continue;
        next.insert(it.key(),QJsonValue::fromVariant(it.value()));changedKeys.push_back(it.key());
    }
    if(changedKeys.isEmpty())return true;
    if(!write(next))return false;
    values_=std::move(next);for(const auto& key:changedKeys)emit changed(key);return true;
}
}
