#include "steam_downloads.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStringList>
#include <algorithm>

#ifdef Q_OS_WIN
#include <QSettings>
#else
#include <QStandardPaths>
#endif

namespace xmb {
namespace {

// Safety limits. Steam's own appmanifest and library files are a few kilobytes each; every read here
// is bounded so a corrupt or hostile file cannot make the scan unbounded.
constexpr qint64 kMaxConfigBytes = 1024 * 1024;
constexpr qint64 kMaxManifestBytes = 1024 * 1024;
constexpr int kMaxPairsPerFile = 4096;
constexpr int kMaxVdfString = 8192;
constexpr int kMaxVdfDepth = 32;
constexpr int kMaxLibraries = 64;
constexpr int kMaxManifestsPerLibrary = 4096;
constexpr int kMaxManifestsPerScan = 16384;
constexpr int kMaxRegistryApps = 8192;
constexpr int kMaxDownloads = 256;

// Steam app state bits (AppState StateFlags). Verified values are documented in
// audit/STEAM-DOWNLOADS.md together with the Steam content_log.txt name/hex evidence.
constexpr quint64 kStateUpdateRequired = 2;
constexpr quint64 kStateFullyInstalled = 4;
constexpr quint64 kStateUpdateQueued = 8;
constexpr quint64 kStateUpdateOptional = 16;
constexpr quint64 kStateFilesMissing = 32;
constexpr quint64 kStateFilesCorrupt = 128;
constexpr quint64 kStateUpdateRunning = 256; // documented, not observed in the local log masks
constexpr quint64 kStateUpdatePaused = 512;
constexpr quint64 kStateUpdateStarted = 1024;
constexpr quint64 kStateAppRunning = 8192;

struct VdfPair {
    QString key;
    QString value;
};

struct Manifest {
    SteamDownload download;
    quint64 flags = 0;
    bool valid = false;
};

struct Candidate {
    SteamDownload download;
    quint64 flags = 0;
    bool registryOnly = false;
};

struct RegistryApp {
    bool updating = false;
    QString title;
};

bool isAppId(const QString& text) {
    if (text.isEmpty() || text.size() > 10) return false;
    for (const QChar character : text)
        if (!character.isDigit()) return false;
    bool valid = false;
    const qulonglong value = text.toULongLong(&valid);
    return valid && value != 0;
}

QString normalizedPath(const QString& path) {
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) return {};
    return QDir::cleanPath(QDir::fromNativeSeparators(trimmed));
}

bool samePath(const QString& left, const QString& right) {
#ifdef Q_OS_WIN
    return left.compare(right, Qt::CaseInsensitive) == 0;
#else
    return left == right;
#endif
}

// Minimal bounded reader for Steam's KeyValues text: quoted tokens, \" and \\ escapes, // comments and
// {} blocks. Only key/value pairs at `depth` are kept (the value is whether they sit directly inside
// the file's top-level block), so nested sections such as InstalledDepots cost nothing.
bool readVdfPairs(const QString& path, qint64 maximumBytes, int depth, QVector<VdfPair>* pairs) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 0 || file.size() > maximumBytes) return false;
    const QByteArray bytes = file.read(maximumBytes + 1);
    if (bytes.size() > maximumBytes || file.error()) return false;
    const QString text = QString::fromUtf8(bytes); // ACF files are UTF-8 (verified in the audit)
    const int length = int(text.size());
    int position = 0;
    int level = 0;
    QString key;
    bool haveKey = false;
    while (position < length) {
        const QChar character = text.at(position);
        if (character.isSpace()) {
            ++position;
            continue;
        }
        if (character == QLatin1Char('/') && position + 1 < length &&
            text.at(position + 1) == QLatin1Char('/')) {
            while (position < length && text.at(position) != QLatin1Char('\n')) ++position;
            continue;
        }
        if (character == QLatin1Char('{')) {
            if (++level > kMaxVdfDepth) return false;
            haveKey = false;
            ++position;
            continue;
        }
        if (character == QLatin1Char('}')) {
            if (level > 0) --level;
            haveKey = false;
            ++position;
            continue;
        }
        QString token;
        if (character == QLatin1Char('"')) {
            ++position;
            bool closed = false;
            while (position < length) {
                const QChar current = text.at(position++);
                if (current == QLatin1Char('"')) {
                    closed = true;
                    break;
                }
                if (current == QLatin1Char('\\') && position < length) {
                    const QChar escaped = text.at(position);
                    if (escaped == QLatin1Char('"') || escaped == QLatin1Char('\\') ||
                        escaped == QLatin1Char('n') || escaped == QLatin1Char('t')) {
                        token.append(escaped == QLatin1Char('n')   ? QLatin1Char('\n')
                                     : escaped == QLatin1Char('t') ? QLatin1Char('\t')
                                                                   : escaped);
                        ++position;
                        continue;
                    }
                    token.append(current);
                    continue;
                }
                token.append(current);
                if (token.size() > kMaxVdfString) return false;
            }
            if (!closed) return false;
        } else {
            while (position < length) {
                const QChar current = text.at(position);
                if (current.isSpace() || current == QLatin1Char('{') || current == QLatin1Char('}'))
                    break;
                if (current == QLatin1Char('/') && position + 1 < length &&
                    text.at(position + 1) == QLatin1Char('/')) break;
                token.append(current);
                ++position;
                if (token.size() > kMaxVdfString) return false;
            }
            if (token.isEmpty()) {
                ++position;
                continue;
            }
        }
        if (!haveKey) {
            key = token;
            haveKey = true;
            continue;
        }
        if (level == depth) {
            if (pairs->size() >= kMaxPairsPerFile) return false;
            pairs->append({key, token});
        }
        haveKey = false;
    }
    return true;
}

QString pairValue(const QVector<VdfPair>& pairs, const QString& key) {
    for (const VdfPair& pair : pairs)
        if (pair.key.compare(key, Qt::CaseInsensitive) == 0) return pair.value;
    return {};
}

qint64 parseByteCount(const QString& text) {
    if (text.isEmpty()) return 0;
    bool valid = false;
    const qlonglong value = text.toLongLong(&valid);
    return valid && value > 0 ? qint64(value) : 0;
}

QStringList rootCandidates(const QString& explicitRoot) {
    if (!explicitRoot.trimmed().isEmpty()) return {explicitRoot};
    const QString environmentRoot = qEnvironmentVariable("XMB_STEAM_ROOT").trimmed();
    if (!environmentRoot.isEmpty()) return {environmentRoot};
    QStringList candidates;
#ifdef Q_OS_WIN
    const QSettings userSteam(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"),
                              QSettings::NativeFormat);
    const QString userPath = userSteam.value(QStringLiteral("SteamPath")).toString().trimmed();
    if (!userPath.isEmpty()) candidates.append(userPath);
    const QSettings machineSteam(
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam"),
        QSettings::NativeFormat);
    const QString machinePath = machineSteam.value(QStringLiteral("InstallPath")).toString().trimmed();
    if (!machinePath.isEmpty()) candidates.append(machinePath);
    const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)").trimmed();
    if (!programFilesX86.isEmpty())
        candidates.append(QDir(programFilesX86).filePath(QStringLiteral("Steam")));
    candidates.append(QStringLiteral("C:/Program Files (x86)/Steam"));
    candidates.append(QStringLiteral("C:/Program Files/Steam"));
#else
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (!home.isEmpty()) {
        candidates.append(QDir(home).filePath(QStringLiteral(".steam/steam")));
        candidates.append(QDir(home).filePath(QStringLiteral(".local/share/Steam")));
    }
#endif
    return candidates;
}

QString resolveSteamRoot(const QString& explicitRoot) {
    for (const QString& candidate : rootCandidates(explicitRoot)) {
        const QString path = normalizedPath(candidate);
        if (path.isEmpty()) continue;
        if (QFileInfo(QDir(path).filePath(QStringLiteral("steamapps"))).isDir()) return path;
    }
    return {};
}

// Every library root Steam knows about: the install itself plus libraryfolders.vdf entries. Handles
// both the current nested format and the pre-2020 numbered-at-top-level format, and drops folders
// whose drive or steamapps directory is missing (this machine's libraryfolders.vdf names a G: drive
// that no longer exists).
QStringList steamLibraries(const QString& root) {
    QStringList libraries;
    const auto add = [&libraries](const QString& candidate) {
        const QString path = normalizedPath(candidate);
        if (path.isEmpty() || !QFileInfo(QDir(path).filePath(QStringLiteral("steamapps"))).isDir())
            return;
        for (const QString& existing : libraries)
            if (samePath(existing, path)) return;
        if (libraries.size() < kMaxLibraries) libraries.append(path);
    };
    add(root);
    const QString config = QDir(root).filePath(QStringLiteral("steamapps/libraryfolders.vdf"));
    QVector<VdfPair> pairs;
    if (readVdfPairs(config, kMaxConfigBytes, 2, &pairs)) {
        for (const VdfPair& pair : pairs)
            if (pair.key.compare(QStringLiteral("path"), Qt::CaseInsensitive) == 0)
                add(pair.value);
    }
    pairs.clear();
    if (readVdfPairs(config, kMaxConfigBytes, 1, &pairs)) {
        for (const VdfPair& pair : pairs)
            if (isAppId(pair.key) &&
                (pair.value.contains(QLatin1Char(':')) || pair.value.startsWith(QLatin1Char('/'))))
                add(pair.value);
    }
    return libraries;
}

Manifest readManifest(const QString& path) {
    Manifest manifest;
    QVector<VdfPair> pairs;
    if (!readVdfPairs(path, kMaxManifestBytes, 1, &pairs)) return manifest;
    QString appId = pairValue(pairs, QStringLiteral("appid")).trimmed();
    if (!isAppId(appId)) {
        const QString base = QFileInfo(path).completeBaseName();
        const QString prefix = QStringLiteral("appmanifest_");
        if (base.startsWith(prefix, Qt::CaseInsensitive)) appId = base.mid(prefix.size());
        if (!isAppId(appId)) return manifest;
    }
    bool flagsValid = false;
    const quint64 flags = pairValue(pairs, QStringLiteral("StateFlags")).toULongLong(&flagsValid);
    if (!flagsValid) return manifest;
    SteamDownload& download = manifest.download;
    download.appId = appId;
    download.title = pairValue(pairs, QStringLiteral("name")).trimmed();
    download.bytesDownloaded = parseByteCount(pairValue(pairs, QStringLiteral("BytesDownloaded")));
    download.bytesToDownload = parseByteCount(pairValue(pairs, QStringLiteral("BytesToDownload")));
    download.bytesStaged = parseByteCount(pairValue(pairs, QStringLiteral("BytesStaged")));
    download.bytesToStage = parseByteCount(pairValue(pairs, QStringLiteral("BytesToStage")));
    manifest.flags = flags;
    manifest.valid = true;
    return manifest;
}

void addManifest(const QString& path, QHash<QString, Candidate>* candidates) {
    const Manifest manifest = readManifest(path);
    if (!manifest.valid) return;
    auto found = candidates->find(manifest.download.appId);
    if (found == candidates->end()) {
        Candidate candidate;
        candidate.download = manifest.download;
        candidate.flags = manifest.flags;
        candidates->insert(manifest.download.appId, candidate);
        return;
    }
    // The same app can be described by its install manifest and a staging copy; keep the union of the
    // flags and the most complete counters.
    Candidate& candidate = found.value();
    candidate.flags |= manifest.flags;
    SteamDownload& download = candidate.download;
    download.bytesDownloaded = std::max(download.bytesDownloaded, manifest.download.bytesDownloaded);
    download.bytesToDownload = std::max(download.bytesToDownload, manifest.download.bytesToDownload);
    download.bytesStaged = std::max(download.bytesStaged, manifest.download.bytesStaged);
    download.bytesToStage = std::max(download.bytesToStage, manifest.download.bytesToStage);
    if (download.title.isEmpty()) download.title = manifest.download.title;
}

QHash<QString, RegistryApp> readRegistryApps() {
    QHash<QString, RegistryApp> apps;
#ifdef Q_OS_WIN
    QSettings registry(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam\\Apps"),
                       QSettings::NativeFormat);
    const QStringList ids = registry.childGroups();
    for (const QString& id : ids) {
        if (apps.size() >= kMaxRegistryApps) break;
        if (!isAppId(id)) continue;
        registry.beginGroup(id);
        RegistryApp app;
        app.updating = registry.value(QStringLiteral("Updating")).toInt() == 1;
        app.title = registry.value(QStringLiteral("Name")).toString().trimmed();
        registry.endGroup();
        apps.insert(id, app);
    }
#endif
    return apps;
}

void classify(SteamDownload* download, quint64 flags, bool updatingHint) {
    download->paused = (flags & kStateUpdatePaused) != 0;
    download->active = !download->paused &&
        (((flags & (kStateUpdateStarted | kStateUpdateRunning)) != 0) || updatingHint);
    // Steam keeps Update Queued set while an update runs, so only report "queued" when the app is
    // actually waiting to start.
    download->queued = !download->paused && !download->active && (flags & kStateUpdateQueued) != 0;
    if (download->paused) {
        download->state = QStringLiteral("paused");
        return;
    }
    if (download->active) {
        if (download->bytesToDownload > 0 && download->bytesDownloaded < download->bytesToDownload) {
            download->state = QStringLiteral("downloading");
            return;
        }
        if (download->bytesToStage > 0 && download->bytesStaged < download->bytesToStage) {
            download->state = QStringLiteral("staging");
            return;
        }
        download->state = QStringLiteral("updating");
        return;
    }
    if (download->queued) {
        download->state = QStringLiteral("queued");
        return;
    }
    const quint64 pending = kStateUpdateRequired | kStateUpdateOptional | kStateFilesMissing |
                            kStateFilesCorrupt;
    if ((flags & pending) != 0) {
        download->state = QStringLiteral("update-pending");
        return;
    }
    download->state = QStringLiteral("idle");
}

bool worthShowing(const Candidate& candidate) {
    if (candidate.registryOnly) return candidate.download.active;
    if (candidate.download.active || candidate.download.paused) return true;
    const quint64 interesting = kStateUpdateRequired | kStateUpdateQueued | kStateUpdateOptional |
                                kStateFilesMissing | kStateFilesCorrupt | kStateUpdatePaused |
                                kStateUpdateStarted | kStateUpdateRunning;
    return (candidate.flags & interesting) != 0;
}

int stateRank(const SteamDownload& download) {
    if (download.active) return 0;
    if (download.paused) return 1;
    if (download.queued) return 2;
    return 3;
}

} // namespace

double SteamDownload::fraction() const {
    const auto ratio = [](qint64 done, qint64 total) {
        if (total <= 0) return 0.0;
        const double value = double(done) / double(total);
        return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
    };
    if (state == QLatin1String("staging")) return ratio(bytesStaged, bytesToStage);
    if (bytesToDownload > 0) return ratio(bytesDownloaded, bytesToDownload);
    return ratio(bytesStaged, bytesToStage);
}

QVector<SteamDownload> scanSteamDownloads(const QString& steamRoot, QString* error) {
    if (error) error->clear();
    const QString root = resolveSteamRoot(steamRoot);
    if (root.isEmpty()) {
        if (error)
            *error = QStringLiteral("Steam installation not found (no steamapps directory under the "
                                    "configured roots).");
        return {};
    }
    const QHash<QString, RegistryApp> registry = readRegistryApps();
    QHash<QString, Candidate> candidates;
    int scanned = 0;
    for (const QString& library : steamLibraries(root)) {
        const QDir steamApps(QDir(library).filePath(QStringLiteral("steamapps")));
        const QFileInfoList manifests = steamApps.entryInfoList(
            {QStringLiteral("appmanifest_*.acf")}, QDir::Files | QDir::Readable, QDir::Name);
        for (const QFileInfo& manifest : manifests) {
            if (scanned >= kMaxManifestsPerScan) break;
            ++scanned;
            addManifest(manifest.absoluteFilePath(), &candidates);
        }
        // Steam stages partial content under steamapps/downloading/<appid>; if it ever leaves the
        // app manifest with it, read it too so a paused download still reports its counters.
        const QDir downloading(steamApps.filePath(QStringLiteral("downloading")));
        const QFileInfoList folders = downloading.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        int foldersSeen = 0;
        for (const QFileInfo& folder : folders) {
            if (foldersSeen++ >= kMaxManifestsPerLibrary || scanned >= kMaxManifestsPerScan) break;
            const QFileInfoList staged = QDir(folder.absoluteFilePath()).entryInfoList(
                {QStringLiteral("appmanifest_*.acf")}, QDir::Files | QDir::Readable, QDir::Name);
            for (const QFileInfo& manifest : staged) {
                if (scanned >= kMaxManifestsPerScan) break;
                ++scanned;
                addManifest(manifest.absoluteFilePath(), &candidates);
            }
        }
    }
    // Apps Steam reports as updating but that have no manifest yet (typically a first install that has
    // not committed). Steam's registry flag is the only state available for those.
    for (auto it = registry.constBegin(); it != registry.constEnd(); ++it) {
        if (!it.value().updating || candidates.contains(it.key())) continue;
        Candidate candidate;
        candidate.download.appId = it.key();
        candidate.download.title = it.value().title;
        candidate.registryOnly = true;
        candidates.insert(it.key(), candidate);
    }
    QVector<SteamDownload> downloads;
    downloads.reserve(candidates.size());
    for (auto it = candidates.constBegin(); it != candidates.constEnd(); ++it) {
        Candidate candidate = it.value();
        const auto registryApp = registry.constFind(it.key());
        const bool registryUpdating = registryApp != registry.constEnd() && registryApp.value().updating;
        if (candidate.download.title.isEmpty() && registryApp != registry.constEnd())
            candidate.download.title = registryApp.value().title;
        if (candidate.download.title.isEmpty()) candidate.download.title = candidate.download.appId;
        classify(&candidate.download, candidate.flags, candidate.registryOnly || registryUpdating);
        if (!worthShowing(candidate)) continue;
        downloads.append(candidate.download);
    }
    std::sort(downloads.begin(), downloads.end(),
              [](const SteamDownload& left, const SteamDownload& right) {
                  const int leftRank = stateRank(left);
                  const int rightRank = stateRank(right);
                  if (leftRank != rightRank) return leftRank < rightRank;
                  const int titleOrder = QString::compare(left.title, right.title, Qt::CaseInsensitive);
                  return titleOrder == 0 ? left.appId < right.appId : titleOrder < 0;
              });
    if (downloads.size() > kMaxDownloads) downloads.resize(kMaxDownloads);
    return downloads;
}

QString steamDownloadStateText(const SteamDownload& download) {
    const auto percent = [&download]() {
        const int value = qRound(download.fraction() * 100.0);
        return value > 99 ? 99 : value; // 100% only once the state leaves downloading/staging
    };
    if (download.paused) return QStringLiteral("Paused");
    if (download.state == QLatin1String("downloading")) {
        if (download.bytesToDownload > 0)
            return QStringLiteral("Downloading %1%").arg(percent());
        return QStringLiteral("Downloading");
    }
    if (download.state == QLatin1String("staging")) {
        if (download.bytesToStage > 0) return QStringLiteral("Staging %1%").arg(percent());
        return QStringLiteral("Staging");
    }
    if (download.state == QLatin1String("updating")) return QStringLiteral("Updating");
    if (download.queued) return QStringLiteral("Queued");
    return QStringLiteral("Update pending");
}

void logSteamDownloads(const QVector<SteamDownload>& downloads) {
    if (!qEnvironmentVariableIsSet("XMB_STEAM_DOWNLOADS_LOG")) return;
    if (downloads.isEmpty()) {
        qInfo().noquote() << "steam downloads: none";
        return;
    }
    for (const SteamDownload& download : downloads) {
        QString line = QStringLiteral(
            "steam download: app=%1 title=\"%2\" state=%3 active=%4 paused=%5 queued=%6 "
            "downloaded=%7/%8 staged=%9/%10 progress=%11% text=\"%12\"");
        line = line.arg(download.appId, download.title, download.state)
                   .arg(download.active ? 1 : 0)
                   .arg(download.paused ? 1 : 0)
                   .arg(download.queued ? 1 : 0)
                   .arg(qlonglong(download.bytesDownloaded))
                   .arg(qlonglong(download.bytesToDownload))
                   .arg(qlonglong(download.bytesStaged))
                   .arg(qlonglong(download.bytesToStage))
                   .arg(qRound(download.fraction() * 100.0))
                   .arg(steamDownloadStateText(download));
        qInfo().noquote() << line;
    }
}

} // namespace xmb
