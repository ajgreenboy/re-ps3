#include "preferences.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSettings>
#include <QSysInfo>
#include <QTimeZone>
#include <cmath>

namespace xmb {
namespace {
// The id a version-1 file's single shared set migrates to. It is a valid profile id and it is
// reserved: Profiles never creates it (its ids are UUIDs), so it can only ever hold the pre-profile
// settings.
const QString LegacyProfile=QStringLiteral("local");
constexpr qsizetype MaximumProfiles=16;
constexpr qsizetype MaximumStoredKeys=2048;
bool hasOnlyKeys(const QJsonObject& object,const QStringList& allowed) {
    for(auto it=object.constBegin();it!=object.constEnd();++it)if(!allowed.contains(it.key()))return false;
    return true;
}
}
QStringList Preferences::sharedKeys() {
    // Settings the console keeps once for the whole system rather than once per user: the system
    // name, the time zone, fullscreen and the two power/idle timers. Everything else is per user.
    return {QStringLiteral("system/name"),QStringLiteral("clock/timeZone"),QStringLiteral("display/fullscreen"),
            QStringLiteral("power/idleMinutes"),QStringLiteral("display/screensaverMinutes")};
}
bool Preferences::isSharedKey(const QString& key) {
    return sharedKeys().contains(key);
}
bool Preferences::validProfileId(const QString& id) {
    // 1..40 ASCII characters of [a-z0-9-], at least one digit or letter, no leading/trailing dash.
    // A UUID without braces is 36 characters, which is the shape Profiles writes.
    if(id.isEmpty() || id.size()>40)return false;
    if(id.startsWith(QLatin1Char('-')) || id.endsWith(QLatin1Char('-')))return false;
    bool alphanumeric=false;
    for(const QChar c:id) {
        const bool digit=c>='0' && c<='9',lower=c>='a' && c<='z';
        if(!digit && !lower && c!=QLatin1Char('-'))return false;
        alphanumeric=alphanumeric||digit||lower;
    }
    return alphanumeric;
}
bool Preferences::splitKey(const QString& key,QString& profile,QString& base) {
    profile.clear();
    base=key;
    if(!key.startsWith(QLatin1String("profile/")))return true;
    const auto rest=key.mid(8);
    const auto slash=rest.indexOf(QLatin1Char('/'));
    if(slash<=0 || slash+1>=rest.size())return false;
    profile=rest.left(slash);
    base=rest.mid(slash+1);
    return validProfileId(profile);
}
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
bool Preferences::validBare(const QString& key,const QVariant& value) {
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
bool Preferences::valid(const QString& key,const QVariant& value) {
    QString profile,base;
    if(!splitKey(key,profile,base))return false;
    // A bare per-user key is reachable only while no profile is active (a data directory this build
    // has not switched yet); it stays a valid stored form so a shell that never selects a profile
    // keeps working, and value() ignores it once a profile is active, so there is one source of truth
    // per state. A shared key stored *under* a profile would be a second source of truth for a
    // console-wide setting, so that fails.
    if(!profile.isEmpty() && isSharedKey(base))return false;
    return validBare(base,value);
}
QString Preferences::absoluteKey(const QString& key) const {
    QString profile,base;
    if(!splitKey(key,profile,base))return key;          // a malformed absolute key: leave it to valid()
    if(!profile.isEmpty())return key;                   // already absolute
    if(profile_.isEmpty() || isSharedKey(key))return key;
    return QStringLiteral("profile/")+profile_+QLatin1Char('/')+key;
}
QStringList Preferences::profileIds() const {
    QStringList ids;
    for(auto it=values_.constBegin();it!=values_.constEnd();++it) {
        QString profile,base;
        if(!splitKey(it.key(),profile,base) || profile.isEmpty())continue;
        if(!ids.contains(profile))ids.push_back(profile);
    }
    if(!profile_.isEmpty() && !ids.contains(profile_))ids.push_back(profile_);
    ids.sort();
    return ids;
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
        const int version=root.value("version").toInt(-1);
        if(parse.error!=QJsonParseError::NoError || !document.isObject() || (version!=1 && version!=2) ||
           !root.value("values").isObject()) {
            writable_=false;error_="Preferences have an invalid or newer format. The existing file has been preserved.";return;
        }
        const auto stored=root.value("values").toObject();
        if(stored.size()>MaximumStoredKeys) {
            writable_=false;error_="Preferences hold more values than this build supports. The existing file has been preserved.";return;
        }
        if(version==2) {
            if(!hasOnlyKeys(root,{QStringLiteral("version"),QStringLiteral("active"),QStringLiteral("values")}) ||
               !root.value("active").isString()) {
                writable_=false;error_="Preferences have an invalid or newer format. The existing file has been preserved.";return;
            }
            const auto active=root.value("active").toString();
            if(!active.isEmpty() && !validProfileId(active)) {
                writable_=false;error_="Preferences name an invalid profile. The existing file has been preserved.";return;
            }
            for(auto it=stored.begin();it!=stored.end();++it)if(valid(it.key(),it.value().toVariant()))values_.insert(it.key(),it.value());
            profile_=active;
            return;
        }
        // Version 1: the single shared set. Every per-user key moves under the reserved legacy
        // profile, the console-wide keys stay unprefixed, and the file is rewritten as version 2 so
        // the migration happens exactly once. The read handle is closed first: QSaveFile replaces the
        // file by renaming, and Windows refuses that while another handle holds it.
        file.close();
        for(auto it=stored.begin();it!=stored.end();++it) {
            if(!validBare(it.key(),it.value().toVariant()))continue;
            const auto key=isSharedKey(it.key())?it.key():QStringLiteral("profile/")+LegacyProfile+QLatin1Char('/')+it.key();
            values_.insert(key,it.value());
        }
        profile_=LegacyProfile;
        if(write(values_,profile_))qInfo().noquote()<<"preferences: migrated version 1 to profile"<<LegacyProfile
            <<"("<<values_.size()<<"values)";
        return;
    }
    // Migrate the first native foundation's INI preferences without touching
    // window geometry or deleting the legacy file.
    QSettings legacy(QDir(directory).filePath("settings.ini"),QSettings::IniFormat);
    int migrated=0;
    for(const QString& key:QStringList{"appearance/colour","library/photo","library/music","library/video"}) {
        if(!legacy.contains(key))continue;
        QVariant value=legacy.value(key);
        if(key=="appearance/colour"){bool ok=false;const auto n=value.toInt(&ok);if(ok)value=n;}
        if(!validBare(key,value))continue;
        values_.insert(isSharedKey(key)?key:QStringLiteral("profile/")+LegacyProfile+QLatin1Char('/')+key,QJsonValue::fromVariant(value));
        ++migrated;
    }
    if(migrated)profile_=LegacyProfile;
    write(values_,profile_);
}
QVariant Preferences::value(const QString& key) const {
    const auto absolute=absoluteKey(key);
    const auto found=values_.constFind(absolute);
    if(found!=values_.cend())return found->toVariant();
    QString profile,base;
    if(splitKey(absolute,profile,base))return defaultValue(base);
    return defaultValue(key);
}
bool Preferences::contains(const QString& key) const {
    return values_.contains(absoluteKey(key));
}
bool Preferences::fail(const QString& message) {
    error_=message;emit failed(error_);return false;
}
bool Preferences::write(const QJsonObject& values,const QString& profile) {
    QSaveFile file(path_);file.setDirectWriteFallback(false);
    const auto bytes=QJsonDocument(QJsonObject{{"version",2},{"active",profile},{"values",values}}).toJson();
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
        const auto absolute=absoluteKey(it.key());
        if(!valid(absolute,it.value())){emit failed("The setting value is invalid.");return false;}
        if(value(it.key())==it.value())continue;
        next.insert(absolute,QJsonValue::fromVariant(it.value()));changedKeys.push_back(it.key());
    }
    if(changedKeys.isEmpty())return true;
    if(!write(next,profile_))return false;
    values_=std::move(next);for(const auto& key:changedKeys)emit changed(key);return true;
}
bool Preferences::setProfile(const QString& id,const QString& copyFrom) {
    if(!writable_){emit failed(error_);return false;}
    const auto target=id.trimmed();
    if(!validProfileId(target))return fail(QStringLiteral("A profile id is 1 to 40 lower-case letters, digits or dashes."));
    if(target==profile_){error_.clear();return true;}
    auto next=values_;
    const auto prefixFor=[](const QString& profile){return QStringLiteral("profile/")+profile+QLatin1Char('/');};
    const auto carriesValues=[&](const QString& profile) {
        if(profile.isEmpty())return false;
        const auto prefix=prefixFor(profile);
        for(auto it=next.constBegin();it!=next.constEnd();++it)if(it.key().startsWith(prefix))return true;
        return false;
    };
    // Which profile the target inherits from: an explicit copyFrom always, else only the pre-profile
    // set (the version-1 migration case) - a new user on the console starts from the defaults.
    QString source;
    if(!carriesValues(target)) {
        if(!copyFrom.trimmed().isEmpty())source=copyFrom.trimmed();
        else if(profile_==LegacyProfile && carriesValues(profile_))source=profile_;
    }
    if(!source.isEmpty() && !validProfileId(source))
        return fail(QStringLiteral("The profile to copy from is not a valid id."));
    if(!source.isEmpty() && source!=target) {
        const auto sourcePrefix=prefixFor(source),targetPrefix=prefixFor(target);
        QVector<QPair<QString,QJsonValue>> copies;
        for(auto it=next.constBegin();it!=next.constEnd();++it)
            if(it.key().startsWith(sourcePrefix))copies.push_back({targetPrefix+it.key().mid(sourcePrefix.size()),it.value()});
        // The 17th profile with stored values is refused, the same bound Profiles enforces.
        QStringList existing;
        for(auto it=next.constBegin();it!=next.constEnd();++it) {
            QString profile,base;
            if(splitKey(it.key(),profile,base) && !profile.isEmpty() && !existing.contains(profile))existing.push_back(profile);
        }
        if(!copies.isEmpty() && !existing.contains(target) && existing.size()>=MaximumProfiles)
            return fail(QStringLiteral("Up to 16 local profiles can store settings."));
        int copied=0;
        for(const auto& copy:copies) {
            if(next.contains(copy.first))continue;   // never overwrite what the target already has
            next.insert(copy.first,copy.second);++copied;
        }
        if(copied)qInfo().noquote()<<"preferences: copied"<<copied<<"settings from profile"<<source<<"to"<<target;
    }
    if(!write(next,target))return false;
    values_=std::move(next);
    profile_=target;
    error_.clear();
    emit profileChanged(profile_);
    return true;
}
bool Preferences::forgetProfile(const QString& id) {
    if(!writable_){emit failed(error_);return false;}
    const auto target=id.trimmed();
    if(!validProfileId(target))return fail(QStringLiteral("A profile id is 1 to 40 lower-case letters, digits or dashes."));
    if(target==profile_)return fail(QStringLiteral("The active profile cannot be forgotten. Switch to another profile first."));
    if(target==LegacyProfile)return fail(QStringLiteral("The pre-profile settings cannot be forgotten; they are the only copy of a migrated install."));
    const auto prefix=QStringLiteral("profile/")+target+QLatin1Char('/');
    auto next=values_;
    int removed=0;
    for(auto it=values_.constBegin();it!=values_.constEnd();++it) {
        if(!it.key().startsWith(prefix))continue;
        next.remove(it.key());++removed;
    }
    if(!removed)return true;
    if(!write(next,profile_))return false;
    values_=std::move(next);
    error_.clear();
    return true;
}
}
