#pragma once
#include <QObject>
#include <QJsonObject>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace xmb {
// Application preferences only. Never writes Windows or firmware settings.
//
// Key namespace (Phase 3.6, profiles)
// -----------------------------------
// Every per-user setting is stored under "profile/<id>/<key>", where <key> is the same setting name
// this build always used ("appearance/colour", "audio/volume", "media/resume", ...) and <id> is the
// local profile id from Profiles (profiles.json: a lower-case UUID without braces). A small set of
// console-wide keys stays unprefixed and is shared by every profile: system/name,
// clock/timeZone, display/fullscreen, power/idleMinutes, display/screensaverMinutes.
//
// Callers keep using the *logical* key ("audio/volume"); value()/setValue()/contains() resolve it
// against the active profile and fall back to the shared set when the key is shared or when no
// profile has been selected yet. absoluteKey() exposes the resolution for tests and the audit.
//
// clock/timeZone's three accepted forms
// -------------------------------------
// The value is shared (one time zone for the console) and has three accepted forms, all bounded:
//   "System"          the default: the clock follows the host;
//   an IANA id        "America/Los_Angeles", the form this build's host list produces and every older file
//                     holds - still accepted, still resolved through QTimeZone, so no stored value is
//                     orphaned by the firmware list;
//   ""                the empty value, which behaves like "System";
//   a firmware token  "date_time_config_timezone_pacific_us_canada": the RCO MItem name without its
//                     "item_" namespace, which is exactly what the choice model's Zone values are
//                     (settings_menu.cpp, SETTINGS-FIRMWARE.md 11.3). The store validates the *form*
//                     (this one namespace, a 1..48 character lower-case name) because it holds no
//                     firmware tree; the choice model resolves the token to the item's own label and
//                     GMT offset, and a well-formed token this firmware does not ship resolves to
//                     nothing (host time, no row label) rather than to a guessed zone.
//
// File format
//   version 1 (before profiles): {"version":1,"values":{"audio/volume":60,...}}
//   version 2 (this build):      {"version":2,"active":"<id>","values":{"profile/<id>/audio/volume":60,
//                                                                   "system/name":"..."}}
// A version-1 file is migrated on load: every per-user key moves to profile/local/<key>, the shared
// keys stay bare, and the file is written back as version 2. A file this build cannot read or
// validate keeps its bytes and turns the object read-only (error(), failed()), exactly as before.
//
// The documented switch
// ---------------------
// setProfile(id) is the only way the active profile changes:
//  1. id must validate (1..40 characters of [a-z0-9-]); otherwise nothing is written and failed() fires;
//  2. switching to the active id is a no-op that succeeds;
//  3. when the target carries no value yet and a source profile is named - an explicit copyFrom, or
//     by default only the pre-profile set "local" after a version-1 migration - the source's values
//     are copied to the target first. That copy is the one-time migration from the single shared
//     set; it never overwrites a value the target already has, and it never deletes the source's
//     values. A brand-new profile therefore starts from the defaults unless the caller asks for a
//     copy, which is what the console's "Create New User" does;
//  4. the new active id and the values are committed atomically (QSaveFile); on any failure the
//     in-memory state is unchanged and failed() fires;
//  5. profileChanged(id) is emitted after the commit, so the caller can re-apply its settings view.
// There are at most 16 profiles with stored values (the same bound Profiles enforces), and a switch
// that would create a 17th fails cleanly. Switching back is exact: no other profile's values are
// touched, so a round trip restores the previous settings byte for byte.
class Preferences final:public QObject {
    Q_OBJECT
public:
    explicit Preferences(const QString& dataDirectory,QObject* parent=nullptr);
    QVariant value(const QString& key) const;
    bool contains(const QString& key) const;
    bool setValue(const QString& key,const QVariant& value);
    bool setValues(const QVariantMap& values);
    QString path() const { return path_; }
    QString error() const { return error_; }

    // The local profile whose values value()/setValue() resolve against; empty before the first
    // setProfile() call, which is the shared/legacy state.
    QString activeProfile() const { return profile_; }
    // Ids that carry at least one stored value, plus the active id, sorted.
    QStringList profileIds() const;
    // The documented switch, above. copyFrom names a profile to seed an empty target from.
    bool setProfile(const QString& id,const QString& copyFrom=QString());
    // Forget one profile's stored values. Refuses the active profile (switch away first) and the
    // pre-profile set "local" (it is the only copy of a migrated install's settings).
    bool forgetProfile(const QString& id);
    // The absolute key a logical key resolves to for the active profile.
    QString absoluteKey(const QString& key) const;
    static bool validProfileId(const QString& id);
    static bool isSharedKey(const QString& key);
    static QStringList sharedKeys();
    // The namespace the firmware's 110 time zones are stored in, and the bounded test for a value in it
    // (see the header note above). Exposed so the choice model produces and the store accepts one rule.
    static QString firmwareZoneTokenPrefix();
    static bool isFirmwareZoneToken(const QString& token);
signals:
    void changed(const QString& key);
    void failed(const QString& message);
    void profileChanged(const QString& id);
private:
    QString path_,error_;
    QJsonObject values_;
    QString profile_;
    bool writable_=true;
    static QVariant defaultValue(const QString& key);
    // The stored-key form: "key" for a shared key, "profile/<id>/key" otherwise.
    static bool splitKey(const QString& key,QString& profile,QString& base);
    static bool validBare(const QString& key,const QVariant& value);
    static bool valid(const QString& key,const QVariant& value);
    bool fail(const QString& message);
    bool write(const QJsonObject& values,const QString& profile);
};
}
