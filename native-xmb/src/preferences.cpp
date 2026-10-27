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
    if(key=="appearance/colour" || key=="appearance/background" || key=="appearance/font" || key=="photo/fit")return 0;
    // A fresh data directory is a US console: MM/DD/YYYY (the console's own middle option) and the
    // 12-hour clock. Both are stored preferences, so Settings > Date and Time can still change them.
    if(key=="clock/dateFormat" || key=="clock/hourFormat")return 1;
    if(key=="audio/volume")return 60;
    if(key=="photo/interval")return 5000;
    if(key=="media/repeatMode")return 0;
    if(key=="audio/keyTone")return true;
    if(key=="power/idleMinutes" || key=="display/screensaverMinutes")return 0;
    if(key=="audio/muted" || key=="media/repeat" || key=="media/shuffle" || key=="display/fullscreen")return false;
    if(key=="system/name")return QSysInfo::machineHostName().left(64);
    if(key=="clock/timeZone")return QString("System");
    if(key=="appearance/wallpaper" || key=="appearance/theme" || key.startsWith("library/") ||
       key=="media/resume")return QString{};
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
    if(key=="display/screensaverMinutes")return integer(0,30) && (value.toInt()==0 || value.toInt()==5 || value.toInt()==10 || value.toInt()==20 || value.toInt()==30);
    if(key=="audio/keyTone" || key=="audio/muted" || key=="media/repeat" || key=="media/shuffle" || key=="display/fullscreen")return value.metaType().id()==QMetaType::Bool;
    if(value.metaType().id()!=QMetaType::QString)return false;
    const auto text=value.toString();
    if(text.contains(QChar(0)))return false;
    if(key=="system/name")return !text.trimmed().isEmpty() && text.size()<=64 && !text.contains('\n') && !text.contains('\r');
    if(key=="clock/timeZone")return text=="System" || QTimeZone::isTimeZoneIdAvailable(text.toUtf8());
    if(key=="appearance/wallpaper" || key=="appearance/theme" || key=="library/photo" || key=="library/music" || key=="library/video")return text.size()<=32768;
    // Playback resume points: one JSON object keyed by the truncated SHA-1 of the file path. Two shapes
    // are accepted, because the second one is what this build writes and the first one is what an earlier
    // build wrote - rejecting it here would throw the value away before Shell::migrateResumePoints() could
    // resolve it (a preferences load keeps only the values that validate).
    //   new: {"<id>": {"path": "...", "ms": 8768, "dur": 20000, "title": "...", "at": 1761...}}
    //   old: {"<id>": 8768}
    // Bounded in size *and* shape: an unbounded document, an over-long key, a non-finite or absurd
    // timestamp and an over-long path all fail rather than reaching the shell. 64 records of at most
    // 512 (path) + 200 (title) + 80 (numbers and keys) bytes stay below the 65536-byte string bound.
    if(key=="media/resume") {
        if(text.size()>65536)return false;
        if(text.trimmed().isEmpty())return true;    // no entries yet: the empty value is a valid map
        const auto document=QJsonDocument::fromJson(text.toUtf8());
        if(!document.isObject())return false;
        const auto map=document.object();
        if(map.size()>64)return false;
        const auto number=[](const QJsonValue& value,double maximum) {
            if(!value.isDouble())return false;
            const double n=value.toDouble();
            return std::isfinite(n) && n>=0.0 && n<=maximum;
        };
        const auto milliseconds=[&](const QJsonValue& value) { return number(value,86400000.0); }; // one day past any title
        const auto timestamp=[&](const QJsonValue& value) { return number(value,4102444800.0); };  // 2100-01-01
        for(auto it=map.begin();it!=map.end();++it) {
            // The id is a truncated SHA-1 in lower-case hex: 24 characters in this build. The wider range
            // keeps an older or a later truncation loadable instead of silently dropping its data.
            if(it.key().size()<16 || it.key().size()>40)return false;
            for(const QChar c:it.key())if(!((c>='0' && c<='9') || (c>='a' && c<='f')))return false;
            if(milliseconds(it.value()))continue;   // the old pathless shape
            if(!it.value().isObject())return false;
            const auto record=it.value().toObject();
            if(record.size()>5)return false;
            for(auto field=record.begin();field!=record.end();++field) {
                const auto name=field.key();
                if(name=="path") {
                    if(!field.value().isString() || field.value().toString().size()>512)return false;
                } else if(name=="title") {
                    if(!field.value().isString() || field.value().toString().size()>200)return false;
                } else if(name=="ms" || name=="dur") {
                    if(!milliseconds(field.value()))return false;
                } else if(name=="at") {
                    if(!timestamp(field.value()))return false;
                } else return false;                // an unknown field is a format this build must not write back
            }
        }
        return true;
    }
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
