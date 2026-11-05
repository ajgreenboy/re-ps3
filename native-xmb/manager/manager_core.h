#pragma once

// XMB Manager core: settings, data layout, path validation, atomic file IO,
// deterministic package ids, collections, and diagnostics.
//
// Everything in this module is independent of the Qt Widgets UI so the CLI and
// the in-XMB data files use exactly the same rules.

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace xmb::manager {

// Resolved manager configuration. The data directory defaults to
// <manager exe dir>/data and is persisted in <manager exe dir>/manager.ini.
// The ini path is always next to the manager executable.
struct Settings {
    QString dataDirectory;
    QString iniPath;
    QString executableDirectory;
};

// Hard ceiling for any JSON document this tool reads. The native XMB registry
// uses 1 MiB; manager-owned files are allowed a little more.
constexpr qint64 kMaxJsonBytes = 4 * 1024 * 1024;

// Diagnostics. Every line goes to stderr (never stdout, so --json stays
// machine-readable) and through qInfo/qWarning so a host log sink still sees it.
void logInfo(const QString& message);
void logWarning(const QString& message);
void logError(const QString& message);

// Absolute directory containing the running manager executable.
QString managerExecutableDirectory();

// <exe dir>/manager.ini (the persistent settings path).
QString defaultSettingsPath();

// Settings with defaults applied; does not touch disk.
Settings defaultSettings();

// Reads manager.ini next to the executable. A missing ini is not an error.
// A relative persisted data directory is resolved against the exe directory.
bool loadSettings(Settings& settings, QString& error);

// Atomically rewrites manager.ini with the current data directory.
bool saveSettings(const Settings& settings, QString& error);

// Resolves an absolute data directory and stores it in settings; optionally
// persists the choice. Relative paths are rejected.
bool setDataDirectory(Settings& settings, const QString& directory, QString& error,
                      bool persist);

// Creates <data>, <data>/pkg and validates that the data directory is a
// writable directory. Called before any mutating verb.
bool ensureDataLayout(const Settings& settings, QString& error);

// Well-known paths. All are absolute and derived only from settings.dataDirectory.
QString registryPath(const Settings& settings);      // <data>/apps.json
QString packageRoot(const Settings& settings);       // <data>/pkg
QString emulatorsPath(const Settings& settings);     // <data>/emulators.json
QString collectionsPath(const Settings& settings);   // <data>/collections.json
QString platformMapPath(const Settings& settings);   // <data>/platform-map.json

// ---------------------------------------------------------------------------
// Platform -> emulator map, <data>/platform-map.json, schema xmb-platform-map/1:
//   {"schema":"xmb-platform-map/1","version":1,"mappings":[
//     {"platform":"SNES","emulatorId":"retroarch",
//      "arguments":["-L","snes9x_libretro","{rom}"]}]}
// "arguments" is optional. When present it replaces the profile's
// argumentTemplate for that platform (each array element is exactly one argv
// element, with {rom}, {title}, {id}, {dir} and {core} substituted verbatim).
// When absent the profile template is used. A missing file yields the built-in
// defaults below; --map-platform writes the full effective map.
// ---------------------------------------------------------------------------
struct PlatformMapping {
    QString platform;
    QString emulatorId;
    QStringList arguments;   // empty => use the emulator profile's template
};

QString platformMapSchema();

// Sensible default mappings for the 13 built-in emulator profiles. These are
// the scanner's platform ids, not free-form names.
QVector<PlatformMapping> defaultPlatformMappings();

// Case-insensitive platform lookup; nullptr when the platform is not mapped.
const PlatformMapping* findPlatformMapping(const QVector<PlatformMapping>& mappings,
                                           const QString& platform);

// Missing file is not an error: the result starts from defaultPlatformMappings.
// Invalid JSON/fields, duplicate platforms and unsafe ids are errors.
bool loadPlatformMap(const Settings& settings, QVector<PlatformMapping>& mappings,
                     QString& error);

// Validates and atomically writes the map (sorted by platform). Entries with an
// empty "arguments" list omit the field.
bool savePlatformMap(const Settings& settings, const QVector<PlatformMapping>& mappings,
                     QString& error);

// Collection name rule shared by collections.json and the collection verbs.
bool validateCollectionName(const QString& name, QString& error);

// Tags: trimmed, non-empty, at most kMaxTagLength characters, no control
// characters, de-duplicated case-insensitively (the first spelling wins).
constexpr int kMaxTagLength = 32;
QStringList normalizeTags(const QStringList& input, QString& error);
bool isValidTag(const QString& tag, QString& error);

// Stable, filesystem-safe package id for one ROM path and platform:
// pkg-<platform-slug>-<16 hex chars of FNV-1a over the canonical, case-folded
// path>. The same ROM always produces the same id, so --replace is an update.
QString packageIdFor(const QString& romPath, const QString& platform);

// Lower-case ASCII slug: letters/digits kept, every other run becomes '-',
// leading/trailing '-' trimmed, truncated to maxLength (minimum 1).
QString slugify(const QString& text, int maxLength = 48);

// ISO-8601 UTC timestamp, e.g. 2026-11-04T23:12:07Z.
QString timestampUtc();

// True when id is a safe single path component accepted by the apps.json
// schema: ASCII letters, digits, '_', '.', '-', 1..80 bytes, not "." or "..".
bool isSafePackageId(const QString& id);

// True when candidate is strictly inside root (case-insensitive on Windows).
// When the path exists its canonical form is used, so a symlink that escapes
// the root is rejected. resolved receives the cleaned/canonical absolute path.
bool isPathInside(const QString& root, const QString& candidate, QString& resolved);

// Atomic write: parent directories created, bytes written to a temporary file
// in the same directory and renamed over the target (QSaveFile).
bool writeFileAtomic(const QString& path, const QByteArray& bytes, QString& error);
bool writeJsonFileAtomic(const QString& path, const QJsonObject& object, QString& error);

// Size-limited read. Missing and oversized files are errors.
bool readFileLimited(const QString& path, qint64 limit, QByteArray& bytes, QString& error);
bool readJsonFile(const QString& path, QJsonObject& object, QString& error,
                  qint64 limit = kMaxJsonBytes);

// Collections: <data>/collections.json, schema xmb-collections/1:
//   {"version":1,"collections":[{"name":"...","ids":["pkg-..."]}]}
// A missing file is an empty collection set. Names are trimmed, unique and
// 1..64 characters with no control characters.
bool readCollections(const Settings& settings, QHash<QString, QStringList>& collections,
                     QString& error);
bool writeCollections(const Settings& settings, const QHash<QString, QStringList>& collections,
                      QString& error);
bool assignToCollection(const Settings& settings, const QString& appId,
                        const QString& collection, QString& error);
bool removeFromCollections(const Settings& settings, const QString& appId, QString& error);

} // namespace xmb::manager
