#pragma once

// Emulator profiles for the XMB Manager.
//
// A profile describes how to hand one ROM to one emulator. Built-in profiles
// carry the platform list and the argument template; the executable path is
// filled in by detection (or by the user through emulators.json / the UI).
// User entries in <data>/emulators.json override built-ins by id.

#include "manager_core.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace xmb::manager {

struct EmulatorProfile {
    QString id;
    QString name;
    QString executable;
    QString workingDirectory;
    QStringList platforms;
    QString argumentTemplate;
    QHash<QString, QString> env;
    bool builtIn = false;
};

// The built-in catalogue (RPCS3, RetroArch, PCSX2, Dolphin, DuckStation,
// PPSSPP, Cemu, Ryujinx, Xenia, MAME, Mednafen, melonDS, DeSmuME). Executable
// and workingDirectory are empty until detection or a user override supplies
// them.
QVector<EmulatorProfile> builtInProfiles();

// Built-ins merged with <data>/emulators.json (user entries win by id).
// A missing emulators.json is not an error. On error the returned vector is
// empty and error explains why.
QVector<EmulatorProfile> loadProfiles(Settings& settings, QString& error);

// Atomically writes every profile to <data>/emulators.json. Profiles serialise
// builtIn as documentation of their origin; on load a known built-in id is
// still treated as built-in.
bool saveProfiles(Settings& settings, const QVector<EmulatorProfile>& profiles, QString& error);

// Scans known install locations plus the Windows uninstall registry for the
// built-in emulators. Only existing executables are reported; an install is
// never invented. Each detection appends one line to notes with its evidence
// path (install directory, registry entry, or the executable file itself).
// The returned profiles are the built-ins with executable/workingDirectory
// filled in for every detected install.
QVector<EmulatorProfile> detectInstalled(QStringList* notes);

// Expands argumentTemplate into an argv list. Placeholders: {rom}, {title},
// {id}, {dir}, {core}. keyValues are "name=value" entries. The template is
// lexed first and values are substituted into a token afterwards, so a value
// containing spaces (a ROM path, a title) always stays ONE argv element.
// Quoting rule: whitespace separates arguments; double or single quotes group
// literal text; there are no escape sequences and no shell interpretation.
// Unknown placeholders, unterminated quotes and malformed keyValues are errors.
QStringList expandArguments(const EmulatorProfile& profile, const QStringList& keyValues,
                            QString& error);

// Display-only rendering of an argv list. Arguments containing whitespace or
// the empty argument are wrapped in double quotes; no escaping is applied.
// Never pass the result to a shell.
QString formatArguments(const QStringList& arguments);

// Expands a literal argv list (a platform-map argument override): each element
// is exactly one argv element, with {rom}, {title}, {id}, {dir} and {core}
// substituted verbatim. No whitespace splitting and no quoting are applied.
QStringList expandArgumentList(const QStringList& elements, const QStringList& keyValues,
                               QString& error);

// Every platform id the scanner can produce, plus the platforms named by the
// built-in profiles and the default platform map. Used by --list-mappings so
// gaps are visible. Sorted case-insensitively.
QStringList knownPlatforms();

// First-class platform -> emulator resolution:
//   1. <data>/platform-map.json (including the seeded defaults);
//   2. the first profile whose "platforms" lists the platform (case-insensitive).
// Returns an empty string when the platform is not mapped. reason (optional)
// explains a gap or a load error. An explicit platform-map entry wins even when
// the profile does not list the platform.
QString resolveEmulatorForPlatform(Settings& settings, const QString& platform,
                                  QString* reason = nullptr);

// Full resolution used by import: emulator id, optional per-platform argument
// override from the map, and whether the map supplied the answer (fromMap).
// Returns false only when the map or profiles cannot be read.
bool resolvePlatformTarget(Settings& settings, const QString& platform, QString& emulatorId,
                           QStringList& argumentOverride, bool& fromMap, QString& reason);

// JSON round trip for one profile (the emulators.json entry shape).
QJsonObject emulatorProfileToJson(const EmulatorProfile& profile);
bool emulatorProfileFromJson(const QJsonObject& object, qsizetype index,
                             EmulatorProfile& profile, QString& error);

// Built-in RetroArch core for a platform, or an empty string when unknown.
QString defaultCoreFor(const QString& emulatorId, const QString& platform);

const EmulatorProfile* findProfile(const QVector<EmulatorProfile>& profiles, const QString& id);
EmulatorProfile* findProfile(QVector<EmulatorProfile>& profiles, const QString& id);

} // namespace xmb::manager
