#include "steam_library.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QVector>
#include <algorithm>

namespace xmb {
namespace {

constexpr qsizetype MaximumConfigBytes = 2 * 1024 * 1024;
constexpr qsizetype MaximumManifestBytes = 1024 * 1024;
constexpr qsizetype MaximumTokens = 100000;
constexpr qsizetype MaximumEntries = 20000;
constexpr int MaximumDepth = 32;
constexpr int MaximumLibraries = 64;
constexpr qsizetype MaximumGames = 4096;
constexpr int MaximumManifestsPerLibrary = 4096;

struct KvEntry {
    QString key;
    QString value;
    QVector<KvEntry> children;
    bool block = false;
};

struct KvDocument {
    QVector<KvEntry> entries;
    QString error;
};

bool readBounded(const QString& path, qsizetype maximum, QByteArray* bytes) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 0 || file.size() > maximum)
        return false;
    *bytes = file.read(maximum + 1);
    return bytes->size() <= maximum && !file.error();
}

class KeyValuesParser final {
public:
    explicit KeyValuesParser(const QByteArray& input) : input_(input) {}

    KvDocument parse() {
        KvDocument document;
        if (!tokenize()) {
            document.error = error_;
            return document;
        }
        qsizetype totalEntries = 0;
        if (!parseEntries(document.entries, 0, false, totalEntries) || position_ != tokens_.size()) {
            document.entries.clear();
            document.error = error_.isEmpty() ? QStringLiteral("Unexpected trailing KeyValues data") : error_;
        }
        return document;
    }

private:
    bool tokenize() {
        qsizetype i = 0;
        while (i < input_.size()) {
            const auto c = static_cast<unsigned char>(input_.at(i));
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
                ++i;
                continue;
            }
            if (c == '/' && i + 1 < input_.size() && input_.at(i + 1) == '/') {
                i += 2;
                while (i < input_.size() && input_.at(i) != '\n') ++i;
                continue;
            }
            if (c == '{' || c == '}') {
                tokens_.append(QString(QChar(c)));
                ++i;
            } else if (c == '"') {
                ++i;
                QByteArray token;
                bool closed = false;
                while (i < input_.size()) {
                    const char current = input_.at(i++);
                    if (current == '"') {
                        closed = true;
                        break;
                    }
                    if (current == '\\' && i < input_.size()) {
                        const char next = input_.at(i);
                        if (next == '"' || next == '\\') {
                            token.append(next);
                            ++i;
                        } else {
                            token.append(current);
                        }
                    } else {
                        token.append(current);
                    }
                    if (token.size() > MaximumConfigBytes) {
                        error_ = QStringLiteral("KeyValues token exceeds limit");
                        return false;
                    }
                }
                if (!closed) {
                    error_ = QStringLiteral("Unterminated KeyValues string");
                    return false;
                }
                tokens_.append(QString::fromUtf8(token));
            } else {
                const qsizetype start = i;
                while (i < input_.size()) {
                    const char current = input_.at(i);
                    if (current == ' ' || current == '\t' || current == '\r' || current == '\n' ||
                        current == '\v' || current == '\f' || current == '{' || current == '}')
                        break;
                    if (current == '/' && i + 1 < input_.size() && input_.at(i + 1) == '/') break;
                    ++i;
                }
                if (start == i) {
                    error_ = QStringLiteral("Invalid KeyValues token");
                    return false;
                }
                tokens_.append(QString::fromUtf8(input_.constData() + start, i - start));
            }
            if (tokens_.size() > MaximumTokens) {
                error_ = QStringLiteral("KeyValues token count exceeds limit");
                return false;
            }
        }
        return true;
    }

    bool parseEntries(QVector<KvEntry>& entries, int depth, bool nested, qsizetype& total) {
        if (depth > MaximumDepth) {
            error_ = QStringLiteral("KeyValues nesting exceeds limit");
            return false;
        }
        while (position_ < tokens_.size()) {
            if (tokens_.at(position_) == QStringLiteral("}")) {
                if (!nested) {
                    error_ = QStringLiteral("Unexpected KeyValues closing brace");
                    return false;
                }
                ++position_;
                return true;
            }
            if (tokens_.at(position_) == QStringLiteral("{")) {
                error_ = QStringLiteral("Unexpected KeyValues opening brace");
                return false;
            }
            if (position_ + 1 >= tokens_.size()) {
                error_ = QStringLiteral("KeyValues key has no value");
                return false;
            }
            KvEntry entry;
            entry.key = tokens_.at(position_++);
            const QString value = tokens_.at(position_++);
            if (value == QStringLiteral("{")) {
                entry.block = true;
                if (!parseEntries(entry.children, depth + 1, true, total)) return false;
            } else if (value == QStringLiteral("}")) {
                error_ = QStringLiteral("KeyValues key has no value");
                return false;
            } else {
                entry.value = value;
            }
            if (++total > MaximumEntries) {
                error_ = QStringLiteral("KeyValues entry count exceeds limit");
                return false;
            }
            entries.append(std::move(entry));
        }
        if (nested) {
            error_ = QStringLiteral("Unclosed KeyValues block");
            return false;
        }
        return true;
    }

    const QByteArray& input_;
    QVector<QString> tokens_;
    qsizetype position_ = 0;
    QString error_;
};

const KvEntry* findEntry(const QVector<KvEntry>& entries, const QString& key) {
    for (const auto& entry : entries) {
        if (entry.key.compare(key, Qt::CaseInsensitive) == 0) return &entry;
    }
    return nullptr;
}

QString stringValue(const QVector<KvEntry>& entries, const QString& key) {
    const auto* entry = findEntry(entries, key);
    return entry && !entry->block ? entry->value : QString{};
}

QString canonicalDirectory(const QString& path) {
    const QFileInfo info(QDir::cleanPath(path));
    return info.isDir() ? QDir::fromNativeSeparators(info.canonicalFilePath()) : QString{};
}

QString normalizedPath(const QString& path) {
    QString result = QDir::fromNativeSeparators(QDir::cleanPath(path.trimmed()));
#ifdef Q_OS_WIN
    if (result.size() >= 2 && result.at(1) == QLatin1Char(':')) result[0] = result.at(0).toUpper();
#endif
    return result;
}

bool isSafeAppId(const QString& appId) {
    if (appId.isEmpty() || appId.size() > 12) return false;
    return std::all_of(appId.cbegin(), appId.cend(), [](QChar c) {
        return c >= QLatin1Char('0') && c <= QLatin1Char('9');
    });
}

bool isSafeNumericKey(const QString& key) {
    if (key.isEmpty() || key.size() > 20) return false;
    return std::all_of(key.cbegin(), key.cend(), [](QChar c) {
        return c >= QLatin1Char('0') && c <= QLatin1Char('9');
    });
}

bool isSafeInstallDirectory(const QString& directory) {
    if (directory.isEmpty() || directory.size() > 180 || directory == QStringLiteral(".") ||
        directory == QStringLiteral("..")) return false;
    return std::none_of(directory.cbegin(), directory.cend(), [](QChar c) {
        return c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':') ||
               c == QLatin1Char('<') || c == QLatin1Char('>') || c == QLatin1Char('"') ||
               c == QLatin1Char('|') || c == QLatin1Char('?') || c == QLatin1Char('*') || c.isNull() ||
               c.category() == QChar::Other_Control;
    });
}

bool isInside(const QString& child, const QString& parent) {
    QString normalizedChild = normalizedPath(child);
    QString normalizedParent = normalizedPath(parent);
#ifdef Q_OS_WIN
    normalizedChild = normalizedChild.toCaseFolded();
    normalizedParent = normalizedParent.toCaseFolded();
#endif
    if (!normalizedParent.endsWith(QLatin1Char('/'))) normalizedParent.append(QLatin1Char('/'));
    return normalizedChild.startsWith(normalizedParent);
}

QString existingArtwork(const QStringList& candidates) {
    for (const auto& path : candidates) {
        const QFileInfo info(path);
        if (info.isFile() && info.size() > 0 && info.size() <= 20 * 1024 * 1024)
            return QDir::fromNativeSeparators(info.absoluteFilePath());
    }
    return {};
}

void appendArtworkMatches(QStringList& candidates, const QString& directory, const QStringList& patterns) {
    constexpr qsizetype MaximumMatchesPerGroup = 16;
    const QDir dir(directory);
    const auto matches = dir.entryInfoList(patterns, QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::Name);
    const qsizetype count = std::min<qsizetype>(matches.size(), MaximumMatchesPerGroup);
    for (qsizetype i = 0; i < count; ++i) candidates.append(matches.at(i).absoluteFilePath());
}

QString artworkFor(const QString& steamRoot, const QString& appId, const QString& steamId) {
    const QString cache = QDir(steamRoot).filePath(QStringLiteral("appcache/librarycache"));
    QStringList candidates;
    QStringList userGrids;
    if (!steamId.isEmpty()) {
        QStringList userIds;
        bool converted = false;
        constexpr qulonglong SteamIdBase = 76561197960265728ULL;
        const qulonglong steamIdNumber = steamId.toULongLong(&converted);
        if (converted && steamIdNumber >= SteamIdBase)
            userIds.append(QString::number(steamIdNumber - SteamIdBase));
        userIds.append(steamId);
        for (const auto& userId : userIds)
            userGrids.append(QDir(steamRoot).filePath(QStringLiteral("userdata/%1/config/grid").arg(userId)));

        for (const auto& grid : userGrids)
            candidates << QDir(grid).filePath(appId + QStringLiteral(".png"))
                       << QDir(grid).filePath(appId + QStringLiteral(".jpg"));
        for (const auto& grid : userGrids)
            candidates << QDir(grid).filePath(appId + QStringLiteral("_hero.png"))
                       << QDir(grid).filePath(appId + QStringLiteral("_hero.jpg"))
                       << QDir(grid).filePath(appId + QStringLiteral("_library_hero.png"))
                       << QDir(grid).filePath(appId + QStringLiteral("_library_hero.jpg"));
    }

    const QString nested = QDir(cache).filePath(appId);
    // Wide header art contains the game's title/logo, so prefer it over hero art.
    candidates << QDir(cache).filePath(appId + QStringLiteral("_header_2x.jpg"))
               << QDir(cache).filePath(appId + QStringLiteral("_header_2x.png"))
               << QDir(cache).filePath(appId + QStringLiteral("_header.jpg"))
               << QDir(cache).filePath(appId + QStringLiteral("_header.png"));
    appendArtworkMatches(candidates, nested, {QStringLiteral("*_header_2x.jpg"), QStringLiteral("*_header_2x.png")});
    appendArtworkMatches(candidates, nested, {QStringLiteral("*_header.jpg"), QStringLiteral("*_header.png")});

    candidates << QDir(cache).filePath(appId + QStringLiteral("_hero.jpg"))
               << QDir(cache).filePath(appId + QStringLiteral("_hero.png"))
               << QDir(cache).filePath(appId + QStringLiteral("_library_hero.jpg"))
               << QDir(cache).filePath(appId + QStringLiteral("_library_hero.png"))
               << QDir(cache).filePath(appId + QStringLiteral("_library_hero_2x.jpg"))
               << QDir(cache).filePath(appId + QStringLiteral("_library_hero_2x.png"));
    appendArtworkMatches(candidates, nested, {QStringLiteral("*_library_hero_2x.jpg"), QStringLiteral("*_library_hero_2x.png")});
    appendArtworkMatches(candidates, nested, {QStringLiteral("*_library_hero.jpg"), QStringLiteral("*_library_hero.png")});
    appendArtworkMatches(candidates, nested, {QStringLiteral("*_hero.jpg"), QStringLiteral("*_hero.png")});

    // Portrait artwork and legacy icon files are last-resort fallbacks for the wide slot.
    candidates << QDir(cache).filePath(appId + QStringLiteral("_library_600x900_2x.jpg"))
               << QDir(cache).filePath(appId + QStringLiteral("_library_600x900_2x.png"))
               << QDir(cache).filePath(appId + QStringLiteral("_library_600x900.jpg"))
               << QDir(cache).filePath(appId + QStringLiteral("_library_600x900.png"));
    appendArtworkMatches(candidates, nested, {QStringLiteral("*_library_600x900_2x.jpg"), QStringLiteral("*_library_600x900_2x.png")});
    appendArtworkMatches(candidates, nested, {QStringLiteral("*_library_600x900.jpg"), QStringLiteral("*_library_600x900.png")});
    for (const auto& grid : userGrids)
        candidates << QDir(grid).filePath(appId + QStringLiteral("p.png"))
                   << QDir(grid).filePath(appId + QStringLiteral("p.jpg"));
    const QString legacy = QDir(steamRoot).filePath(QStringLiteral("steam/games"));
    candidates << QDir(legacy).filePath(appId + QStringLiteral(".jpg"))
               << QDir(legacy).filePath(appId + QStringLiteral(".png"));
    return existingArtwork(candidates);
}

struct RootInfo {
    QString path;
    QString steamId;
};

struct LibraryInfo {
    QString path;
    RootInfo owner;
};

constexpr qulonglong SteamIdBase = 76561197960265728ULL;
constexpr qulonglong MaximumAccountId = 0xffffffffULL;

bool steamIdForAccountKey(const QString& key, qulonglong* steamId) {
    if (!isSafeNumericKey(key)) return false;
    bool valid = false;
    const qulonglong parsed = key.toULongLong(&valid);
    if (!valid || parsed < SteamIdBase || parsed - SteamIdBase == 0 ||
        parsed - SteamIdBase > MaximumAccountId) return false;
    if (steamId) *steamId = parsed;
    return true;
}

QString activeRegistrySteamId(const QString& steamRoot) {
#ifdef Q_OS_WIN
    QSettings steamRegistry(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
    const QString registryPath = steamRegistry.value(QStringLiteral("SteamPath")).toString().trimmed();
    const QString canonicalRegistryPath = canonicalDirectory(registryPath);
    const QString canonicalSteamRoot = canonicalDirectory(steamRoot);
    if (canonicalRegistryPath.isEmpty() || canonicalSteamRoot.isEmpty() ||
        canonicalRegistryPath.compare(canonicalSteamRoot, Qt::CaseInsensitive) != 0) return {};

    QSettings activeProcess(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam\\ActiveProcess"),
                            QSettings::NativeFormat);
    bool valid = false;
    const qulonglong accountId = activeProcess.value(QStringLiteral("ActiveUser")).toULongLong(&valid);
    if (!valid || accountId == 0 || accountId > MaximumAccountId) return {};
    return QString::number(SteamIdBase + accountId);
#else
    Q_UNUSED(steamRoot);
    return {};
#endif
}

QString selectedSteamId(const QString& steamRoot, bool allowActiveRegistry) {
    if (allowActiveRegistry) {
        const QString active = activeRegistrySteamId(steamRoot);
        if (!active.isEmpty()) return active;
    }

    const QString path = QDir(steamRoot).filePath(QStringLiteral("config/loginusers.vdf"));
    QByteArray bytes;
    if (!readBounded(path, MaximumConfigBytes, &bytes)) return {};
    const auto document = KeyValuesParser(bytes).parse();
    if (!document.error.isEmpty()) return {};
    const auto* users = findEntry(document.entries, QStringLiteral("users"));
    if (!users || !users->block) return {};

    struct Account {
        qulonglong steamId = 0;
        bool mostRecent = false;
        bool hasTimestamp = false;
        qulonglong timestamp = 0;
    };
    QVector<Account> accounts;
    for (const auto& user : users->children) {
        qulonglong steamId = 0;
        if (!user.block || !steamIdForAccountKey(user.key, &steamId)) continue;
        Account account;
        account.steamId = steamId;
        // The client writes lower-case "timestamp" today and has dropped MostRecent; accept the older
        // spellings so a different Steam version still resolves the active account.
        account.mostRecent = stringValue(user.children, QStringLiteral("MostRecent")) == QStringLiteral("1") ||
                             stringValue(user.children, QStringLiteral("AutoLogin")) == QStringLiteral("1") ||
                             stringValue(user.children, QStringLiteral("AllowAutoLogin")) == QStringLiteral("1");
        bool timestampValid = false;
        QString stamp = stringValue(user.children, QStringLiteral("timestamp"));
        if (stamp.isEmpty()) stamp = stringValue(user.children, QStringLiteral("Timestamp"));
        account.timestamp = stamp.toULongLong(&timestampValid);
        account.hasTimestamp = timestampValid;
        accounts.append(account);
    }

    for (const auto& account : accounts) {
        if (!account.mostRecent) continue;
        qulonglong selected = account.steamId;
        for (const auto& candidate : accounts)
            if (candidate.mostRecent && candidate.steamId > selected) selected = candidate.steamId;
        return QString::number(selected);
    }
    if (accounts.size() == 1) return QString::number(accounts.constFirst().steamId);

    const Account* selected = nullptr;
    for (const auto& account : accounts) {
        if (!account.hasTimestamp) continue;
        if (!selected || account.timestamp > selected->timestamp ||
            (account.timestamp == selected->timestamp && account.steamId > selected->steamId))
            selected = &account;
    }
    return selected ? QString::number(selected->steamId) : QString{};
}

QStringList rootsToTry(const QString& explicitRoot) {
    if (!explicitRoot.trimmed().isEmpty()) return {explicitRoot};
    QStringList candidates;
    const QString overrideRoot = qEnvironmentVariable("XMB_STEAM_ROOT").trimmed();
    if (!overrideRoot.isEmpty()) return {overrideRoot};

#ifdef Q_OS_WIN
    QSettings registry(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
    const QString registryRoot = registry.value(QStringLiteral("SteamPath")).toString().trimmed();
    if (!registryRoot.isEmpty()) candidates.append(registryRoot);
    const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)").trimmed();
    if (!programFilesX86.isEmpty()) candidates.append(QDir(programFilesX86).filePath(QStringLiteral("Steam")));
    else candidates.append(QStringLiteral("C:/Program Files (x86)/Steam"));
#else
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    candidates << QDir(home).filePath(QStringLiteral(".steam/steam"))
               << QDir(home).filePath(QStringLiteral(".local/share/Steam"));
#endif

    QStringList unique;
    for (const auto& candidate : candidates) {
        const QString cleaned = normalizedPath(candidate);
        bool found = false;
        for (const auto& prior : unique) {
#ifdef Q_OS_WIN
            found = prior.compare(cleaned, Qt::CaseInsensitive) == 0;
#else
            found = prior == cleaned;
#endif
            if (found) break;
        }
        if (!found) unique.append(cleaned);
    }
    return unique;
}

void addLibraries(const RootInfo& root, QVector<LibraryInfo>& libraries, QStringList& warnings) {
    auto add = [&](const QString& path) {
        const QString canonical = canonicalDirectory(path);
        if (canonical.isEmpty() || !QDir(QDir(canonical).filePath(QStringLiteral("steamapps"))).exists()) return;
        for (const auto& previous : libraries) {
#ifdef Q_OS_WIN
            if (previous.path.compare(canonical, Qt::CaseInsensitive) == 0) return;
#else
            if (previous.path == canonical) return;
#endif
        }
        if (libraries.size() >= MaximumLibraries) {
            if (!warnings.contains(QStringLiteral("Steam library count exceeds safety limit.")))
                warnings.append(QStringLiteral("Steam library count exceeds safety limit."));
            return;
        }
        libraries.append({canonical, root});
    };

    add(root.path);
    const QString config = QDir(root.path).filePath(QStringLiteral("steamapps/libraryfolders.vdf"));
    QByteArray bytes;
    if (!readBounded(config, MaximumConfigBytes, &bytes)) return;
    const auto document = KeyValuesParser(bytes).parse();
    if (!document.error.isEmpty()) {
        warnings.append(QStringLiteral("A Steam library configuration could not be parsed."));
        return;
    }
    const auto* folders = findEntry(document.entries, QStringLiteral("libraryfolders"));
    if (!folders || !folders->block) return;
    for (const auto& folder : folders->children) {
        if (!isSafeAppId(folder.key)) continue;
        const QString path = folder.block ? stringValue(folder.children, QStringLiteral("path")) : folder.value;
        if (!path.trimmed().isEmpty()) add(path);
    }
}

void scanLibrary(const QString& steamRoot, const QString& steamId, const QString& library,
                 SteamInstallation& result, QSet<QString>& seenApps) {
    const QString steamApps = QDir(library).filePath(QStringLiteral("steamapps"));
    const QFileInfo commonInfo(QDir(steamApps).filePath(QStringLiteral("common")));
    const QString common = canonicalDirectory(commonInfo.absoluteFilePath());
    if (common.isEmpty()) return;
    const QDir directory(steamApps);
    const auto manifests = directory.entryInfoList({QStringLiteral("appmanifest_*.acf")},
        QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::Name);
    const qsizetype count = std::min<qsizetype>(manifests.size(), MaximumManifestsPerLibrary);
    if (manifests.size() > MaximumManifestsPerLibrary)
        result.warnings.append(QStringLiteral("A Steam library has more manifests than the scan limit."));
    for (qsizetype i = 0; i < count; ++i) {
        if (result.games.size() >= MaximumGames) return;
        const QFileInfo& manifest = manifests.at(i);
        const QString expectedId = manifest.completeBaseName().mid(QStringLiteral("appmanifest_").size());
        if (!isSafeAppId(expectedId)) continue;
        QByteArray bytes;
        if (!readBounded(manifest.absoluteFilePath(), MaximumManifestBytes, &bytes)) continue;
        const auto document = KeyValuesParser(bytes).parse();
        if (!document.error.isEmpty()) continue;
        const auto* state = findEntry(document.entries, QStringLiteral("AppState"));
        if (!state || !state->block) continue;
        const QString appId = stringValue(state->children, QStringLiteral("appid"));
        if (appId != expectedId || !isSafeAppId(appId) || seenApps.contains(appId)) continue;
        if (appId == QStringLiteral("228980")) continue; // Steamworks Common Redistributables
        bool stateOk = false;
        const qulonglong flags = stringValue(state->children, QStringLiteral("StateFlags")).toULongLong(&stateOk);
        if (!stateOk || (flags & 4ULL) == 0) continue;
        const QString installDir = stringValue(state->children, QStringLiteral("installdir"));
        if (!isSafeInstallDirectory(installDir)) continue;
        const QString installPath = QDir(common).filePath(installDir);
        const QFileInfo installInfo(installPath);
        if (!installInfo.isDir() || installInfo.isSymLink()) continue;
        const QString actualDirectory = canonicalDirectory(installPath);
        if (actualDirectory.isEmpty() || !isInside(actualDirectory, common)) continue;

        SteamGame game;
        game.appId = appId;
        game.title = stringValue(state->children, QStringLiteral("name")).trimmed();
        if (game.title.isEmpty()) game.title = appId;
        game.installDirectory = actualDirectory;
        game.artwork = artworkFor(steamRoot, appId, steamId);
        result.games.append(std::move(game));
        seenApps.insert(appId);
    }
}

} // namespace

SteamInstallation scanSteam(const QString& explicitRoot) {
    SteamInstallation result;
    QVector<RootInfo> roots;
    const bool allowActiveRegistry = explicitRoot.trimmed().isEmpty() &&
        qEnvironmentVariable("XMB_STEAM_ROOT").trimmed().isEmpty();
    for (const auto& candidate : rootsToTry(explicitRoot)) {
        const QString canonical = canonicalDirectory(candidate);
        if (canonical.isEmpty() || !QDir(canonical).exists()) continue;
        bool duplicate = false;
        for (const auto& root : roots) {
#ifdef Q_OS_WIN
            duplicate = root.path.compare(canonical, Qt::CaseInsensitive) == 0;
#else
            duplicate = root.path == canonical;
#endif
            if (duplicate) break;
        }
        if (!duplicate) roots.append({canonical, selectedSteamId(canonical, allowActiveRegistry)});
    }

    if (roots.isEmpty()) {
        result.warnings.append(QStringLiteral("Steam was not found in the configured locations."));
        return result;
    }
    result.root = roots.constFirst().path;
    result.steamId = roots.constFirst().steamId;

    QVector<LibraryInfo> libraries;
    for (const auto& root : roots) addLibraries(root, libraries, result.warnings);
    QSet<QString> seenApps;
    for (const auto& library : libraries) {
        scanLibrary(library.owner.path, library.owner.steamId, library.path, result, seenApps);
        if (result.games.size() >= MaximumGames) {
            result.warnings.append(QStringLiteral("Steam game scan reached the result limit."));
            break;
        }
    }
    std::sort(result.games.begin(), result.games.end(), [](const SteamGame& left, const SteamGame& right) {
        const int titleOrder = QString::compare(left.title, right.title, Qt::CaseInsensitive);
        return titleOrder == 0 ? left.appId < right.appId : titleOrder < 0;
    });
    return result;
}

bool steamStopApp(const QString& appId,const QString& steamRoot,QString* error) {
    if (error) error->clear();
#ifdef Q_OS_WIN
    bool numeric = false;
    appId.toULongLong(&numeric);
    if (!numeric) { if (error) *error = QStringLiteral("Not a Steam app id."); return false; }
    const QString roots[] = {steamRoot, rootsToTry(steamRoot).value(0)};
    const QString exe = QDir(roots[0].isEmpty() ? roots[1] : roots[0]).filePath(QStringLiteral("steam.exe"));
    if (!QFileInfo(exe).isFile()) { if (error) *error = QStringLiteral("Steam installation not found."); return false; }
    // The console command is undocumented and prints nothing; the registry Running flag is the proof.
    if (!QProcess::startDetached(exe, {QStringLiteral("+app_stop"), appId})) {
        if (error) *error = QStringLiteral("Steam could not be started."); return false;
    }
    return true;
#else
    Q_UNUSED(appId); Q_UNUSED(steamRoot);
    if (error) *error = QStringLiteral("Steam is only supported on Windows.");
    return false;
#endif
}

SteamAppState steamAppState(const QString& appId) {
    SteamAppState state;
#ifdef Q_OS_WIN
    bool numeric = false;
    appId.toULongLong(&numeric);
    if (!numeric) return state;
    // Steam keeps these live while it runs: RunningAppID names the title in the foreground of its
    // lifecycle, Apps\<id>\Running stays 1 from launch (including any launcher) until the last
    // process exits, and Apps\<id>\Updating is 1 while a pre-launch update downloads.
    const QString base = QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam");
    const QSettings steam(base, QSettings::NativeFormat);
    const QSettings app(base + QStringLiteral("\\Apps\\") + appId, QSettings::NativeFormat);
    state.running = steam.value(QStringLiteral("RunningAppID")).toString() == appId ||
                    app.value(QStringLiteral("Running")).toInt() == 1;
    state.updating = app.value(QStringLiteral("Updating")).toInt() == 1;
#else
    Q_UNUSED(appId);
#endif
    return state;
}

} // namespace xmb
