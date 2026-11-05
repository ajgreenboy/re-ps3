#pragma once

// Virtual PS3-style packages: the XMB Manager's bridge between a ROM file and
// an apps.json row that the native XMB launches through an emulator.
//
// Package layout (<data>/pkg/<id>/):
//   ICON0.PNG   PNG rendition of the discovered art, at most 512x288
//   PARAM.SFO   SFO v1.1 with TITLE, CATEGORY, APP_VER and ICON0
//   pkg.json    schema xmb-virtual-pkg/1 (the manager's own record)
// EBOOT.BIN is deliberately NOT fabricated: the emulator executable in
// apps.json is the launch target, and the package is presentation + metadata.
//
// All registry and package writes are atomic; a failed registry upsert removes
// the package directory this call created (or restores the replaced one).

#include "content_registry.h"
#include "manager_core.h"
#include "manager_emulators.h"
#include "manager_scanner.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace xmb::manager {

struct PackageInput {
    DetectedGame game;
    QString emulatorId;
    QStringList extraArgs;
    QString collection;
    // Set by --replace. Not part of the original four fields; defaulted so
    // aggregate initialisation with those four still compiles.
    bool replace = false;
    // Optional tags written into pkg.json. On --replace an empty list keeps the
    // existing tags (tags are preserved across a re-import).
    QStringList tags;
    // Per-platform argument override from platform-map.json. When non-empty it
    // replaces the profile's argumentTemplate for this import.
    QStringList argumentsOverride;
    // True when the emulator came from platform-map.json: an explicit mapping
    // wins even when the profile does not list the platform.
    bool emulatorFromMapping = false;
};

struct PackageRecord {
    QString id;
    QString title;
    QString platform;
    QString romPath;
    QString romKind;
    QString emulatorId;
    QStringList arguments;
    QString collection;
    QString art;
    QString createdAt;
    QString sourceNote;
    QString packagePath;
    bool packagePresent = false;
    bool registryPresent = false;
    bool orphaned = false;       // pkg.json exists, no apps.json entry
    bool missingPackage = false; // manager-owned apps.json entry, no pkg.json
    QString issue;               // malformed pkg.json or other per-record note
    // Optional record extensions (backward compatible; absent means empty/0).
    QStringList tags;
    qint64 romSize = 0;
    QString romMtime;
    QString hashLabel;
    QString hashValue;
};

QString virtualPackageSchema();

QString packageDirectoryFor(const Settings& settings, const QString& appId);

// Creates the package and upserts its apps.json entry. Returns false and leaves
// no partial package behind on any failure. outApp (optional) receives the
// exact registration written to apps.json.
bool createPackage(Settings& settings, const PackageInput& input, EmulatorProfile profile,
                   ContentApp* outApp, QString& error);

// Removes the apps.json entry and the package directory. Only a direct child of
// <data>/pkg is ever touched; the ROM and the emulator are never touched.
bool removePackage(Settings& settings, const QString& appId, QString& error);

// Reads every <data>/pkg/<id>/pkg.json and merges it with apps.json, reporting
// orphaned packages (no registration) and missing packages (manager-owned
// registration with no package directory).
QVector<PackageRecord> listPackages(Settings& settings, QString& error);

// Detects a real PS3 .pkg. Metadata is only used when it is readable without
// decryption (debug/non-finalized package or a plaintext container). A retail
// encrypted package is refused with its header/encryption evidence; no package
// is created and nothing is modified.
bool importRealPkg(Settings& settings, const QString& pkgPath, QString& error,
                   QStringList* notes);

// Reads and validates one package directory's pkg.json.
bool readPackageRecord(const QString& packageDirectory, PackageRecord& record, QString& error);

// ---------------------------------------------------------------------------
// Tags (pkg.json "tags", default empty when absent).
// add/remove are normalised (trim, non-empty, <= kMaxTagLength, dedupe). The
// pkg.json is rewritten atomically; the registry is untouched.
// ---------------------------------------------------------------------------
bool updatePackageTags(Settings& settings, const QString& appId, const QStringList& add,
                       const QStringList& remove, QStringList& outTags, QString& error);

// ---------------------------------------------------------------------------
// Collections as first-class organisation. pkg.json "collection" and
// collections.json membership are kept in step. An empty name clears the
// membership. rename/remove update every member package's pkg.json.
// ---------------------------------------------------------------------------
bool setPackageCollection(Settings& settings, const QString& appId, const QString& collection,
                          QString& error);
bool renameCollection(Settings& settings, const QString& from, const QString& to, int& affected,
                      QString& error);
bool removeCollection(Settings& settings, const QString& name, int& affected, QString& error);

// ---------------------------------------------------------------------------
// Bulk operations. Every id is validated before anything is written; on an
// unexpected write failure the touched files and directories are restored, so
// a failed request does not leave a half-updated library.
// ---------------------------------------------------------------------------
struct BulkRequest {
    QStringList ids;
    bool setCollection = false;
    QString collection;      // empty clears membership
    bool setEmulator = false;
    QString emulatorId;
    QStringList addTags;
    QStringList removeTags;
    bool remove = false;
};

struct BulkResult {
    QString id;
    bool ok = false;
    QString action;
    QString error;
};

bool applyBulk(Settings& settings, const BulkRequest& request, QVector<BulkResult>& results,
               QString& error);

// ---------------------------------------------------------------------------
// ROM identity and relink.
// ---------------------------------------------------------------------------
struct HashVerification {
    QString id;
    QString romPath;
    QString state;        // ok | mismatch | missing-rom | missing-hash | unreadable
    QString storedLabel;
    QString storedHash;
    QString currentHash;
    qint64 romSize = 0;
    QString error;
};

bool verifyPackageHashes(Settings& settings, QVector<HashVerification>& results, QString& error);

// Updates the ROM path and re-expands the argv. Refuses a different content hash
// unless force is true. summary receives the before/after evidence.
bool relinkPackage(Settings& settings, const QString& appId, const QString& newPath, bool force,
                   QJsonObject& summary, QString& error);

// ---------------------------------------------------------------------------
// Launch checking. argv comes from the exact apps.json registration; QProcess
// is used with an argument list, never a shell. On timeout the process is
// terminated and then killed.
// ---------------------------------------------------------------------------
struct LaunchOutcome {
    QString appId;
    QString executable;
    QString workingDirectory;
    QStringList arguments;
    qint64 pid = 0;
    bool started = false;
    bool finished = false;
    bool timedOut = false;
    bool terminated = false;
    bool killed = false;
    int timeoutSeconds = 8;
    int exitCode = -1;
    QString exitStatus;   // normal | crash | timeout | spawn-error
    QString spawnError;
    QString stdoutText;
    QString stderrText;
};

bool packageArgv(Settings& settings, const QString& appId, QString& executable,
                 QString& workingDirectory, QStringList& arguments, QString& error);
bool launchPackage(Settings& settings, const QString& appId, int timeoutSeconds,
                   LaunchOutcome& outcome, QString& error);

// ---------------------------------------------------------------------------
// Backup / restore / migrate. Bundle schema xmb-manager-backup/1.
// ---------------------------------------------------------------------------
struct BackupOptions {
    bool withPackagedArt = false;
};

// Writes a portable bundle: manager-owned game registrations (pkg.json
// presence), package records, tags, collections, the effective platform map and
// the emulator overlay. Summary reports what was written.
bool writeBackup(Settings& settings, const QString& file, const BackupOptions& options,
                 QJsonObject& summary, QString& error);

struct PathRemap {
    QString from;
    QString to;
};

bool parsePathRemap(const QString& text, PathRemap& remap, QString& error);

// Restores a bundle. Every path field and every argv element is remapped with
// the given prefix rules. Existing package ids are conflicts and abort the
// restore before anything is written unless replace is true. actions reports
// every applied step; conflicts is filled when the restore is refused.
bool restoreBackup(Settings& settings, const QString& file, const QVector<PathRemap>& remaps,
                   bool replace, QJsonArray& actions, QJsonArray& conflicts, QString& error);

} // namespace xmb::manager
