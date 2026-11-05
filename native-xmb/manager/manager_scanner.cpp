#include "manager_scanner.h"

#include <QCryptographicHash>
#include <QByteArrayView>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>

#include <algorithm>

namespace xmb::manager {
namespace {

constexpr qint64 kMaxSfoBytes = 1024 * 1024;
constexpr qint64 kMaxPkgProbeBytes = 8 * 1024 * 1024;
constexpr int kMaxScanDepth = 32;
constexpr int kMaxScannedFiles = 250000;

quint16 be16(const QByteArray& data, qsizetype offset) {
    return static_cast<quint16>((static_cast<quint16>(static_cast<quint8>(data.at(offset))) << 8)
                                | static_cast<quint8>(data.at(offset + 1)));
}

quint32 be32(const QByteArray& data, qsizetype offset) {
    return (static_cast<quint32>(static_cast<quint8>(data.at(offset))) << 24)
        | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 16)
        | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 8)
        | static_cast<quint32>(static_cast<quint8>(data.at(offset + 3)));
}

quint64 be64(const QByteArray& data, qsizetype offset) {
    return (static_cast<quint64>(be32(data, offset)) << 32) | be32(data, offset + 4);
}

void putBe16(QByteArray& data, qsizetype offset, quint16 value) {
    data[offset] = static_cast<char>((value >> 8) & 0xff);
    data[offset + 1] = static_cast<char>(value & 0xff);
}

void putBe32(QByteArray& data, qsizetype offset, quint32 value) {
    data[offset] = static_cast<char>((value >> 24) & 0xff);
    data[offset + 1] = static_cast<char>((value >> 16) & 0xff);
    data[offset + 2] = static_cast<char>((value >> 8) & 0xff);
    data[offset + 3] = static_cast<char>(value & 0xff);
}

// PARAM.SFO (PSF) is little-endian; the PKG header is big-endian. Keep the two
// families of helpers separate so neither is used for the other format.
quint16 le16(const QByteArray& data, qsizetype offset) {
    return static_cast<quint16>(static_cast<quint8>(data.at(offset))
                                | (static_cast<quint16>(static_cast<quint8>(data.at(offset + 1)))
                                   << 8));
}

quint32 le32(const QByteArray& data, qsizetype offset) {
    return static_cast<quint32>(static_cast<quint8>(data.at(offset)))
        | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 8)
        | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 16)
        | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 3))) << 24);
}

void putLe16(QByteArray& data, qsizetype offset, quint16 value) {
    data[offset] = static_cast<char>(value & 0xff);
    data[offset + 1] = static_cast<char>((value >> 8) & 0xff);
}

void putLe32(QByteArray& data, qsizetype offset, quint32 value) {
    data[offset] = static_cast<char>(value & 0xff);
    data[offset + 1] = static_cast<char>((value >> 8) & 0xff);
    data[offset + 2] = static_cast<char>((value >> 16) & 0xff);
    data[offset + 3] = static_cast<char>((value >> 24) & 0xff);
}

bool fail(QString& error, const QString& message) {
    error = message;
    return false;
}

QString canonicalPath(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty())
        return QDir::cleanPath(canonical);
    return QDir::cleanPath(info.absoluteFilePath());
}

QString dedupeKey(const QString& path) {
    QString key = canonicalPath(path);
#ifdef Q_OS_WIN
    key = key.toLower();
#endif
    return key;
}

QByteArray readWindowBytes(QFile& file, qint64 offset, qint64 size, bool& ok) {
    ok = false;
    if (offset < 0 || size <= 0 || offset >= file.size())
        return QByteArray();
    if (!file.seek(offset))
        return QByteArray();
    const qint64 toRead = std::min(size, file.size() - offset);
    const QByteArray bytes = file.read(toRead);
    ok = bytes.size() == toRead;
    return bytes;
}

QString sniffIsoPlatform(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const qint64 size = file.size();
    if (size >= 0x20) {
        bool headerOk = false;
        const QByteArray header = readWindowBytes(file, 0, 0x20, headerOk);
        if (headerOk) {
            const quint32 gcMagic = be32(header, 0x1c);
            const quint32 wiiMagic = be32(header, 0x18);
            if (gcMagic == 0xc2339f3dU || wiiMagic == 0x5d1c9ea3U)
                return QStringLiteral("GameCube/Wii");
        }
    }
    if (size >= 0x8800) {
        bool ok = false;
        const QByteArray pvd = readWindowBytes(file, 0x8000, 0x800, ok);
        if (ok && pvd.size() >= 0x228 && pvd.mid(1, 5) == QByteArray("CD001", 5)) {
            const QByteArray systemId = pvd.mid(8, 32).trimmed().toUpper();
            const QByteArray volumeId = pvd.mid(0x28, 32).trimmed().toUpper();
            const auto describes = [&](const char* needle) {
                return systemId.contains(needle) || volumeId.contains(needle);
            };
            if (describes("PS3VOLUME") || describes("PS3 VOLUME"))
                return QStringLiteral("PS3");
            if (describes("PSPGAME") || describes("PSP GAME"))
                return QStringLiteral("PSP");
            if (describes("PLAYSTATION"))
                return QStringLiteral("PS2");
        }
    }
    return QString();
}

QString platformHint(const QString& path) {
    const QString lower = QDir::fromNativeSeparators(path).toLower();
    if (lower.contains(QStringLiteral("playstation 3")) || lower.contains(QStringLiteral("playstation3")))
        return QStringLiteral("PS3");
    if (lower.contains(QStringLiteral("playstation 2")) || lower.contains(QStringLiteral("playstation2")))
        return QStringLiteral("PS2");
    QStringList tokens;
    const QStringList components = lower.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QRegularExpression separator(QStringLiteral("[^a-z0-9]+"));
    for (const QString& component : components) {
        for (const QString& token : component.split(separator, Qt::SkipEmptyParts))
            tokens.append(token);
    }
    QSet<QString> set;
    for (const QString& token : tokens)
        set.insert(token);
    const auto has = [&set](const char* token) { return set.contains(QLatin1String(token)); };
    if (has("ps3"))
        return QStringLiteral("PS3");
    if (has("ps2"))
        return QStringLiteral("PS2");
    if (has("psp"))
        return QStringLiteral("PSP");
    if (has("ps1") || has("psx") || lower.contains(QStringLiteral("playstation")))
        return QStringLiteral("PS1");
    if (has("gamecube") || has("gc") || has("ngc") || has("wii"))
        return QStringLiteral("GameCube/Wii");
    if (has("switch"))
        return QStringLiteral("Switch");
    if (has("3ds") || has("nintendo3ds"))
        return QStringLiteral("3DS");
    if (has("nds"))
        return QStringLiteral("NDS");
    if (has("snes") || has("supernintendo"))
        return QStringLiteral("SNES");
    if (has("nes"))
        return QStringLiteral("NES");
    if (has("n64"))
        return QStringLiteral("N64");
    if (has("genesis") || has("megadrive"))
        return QStringLiteral("Genesis");
    if (has("gba"))
        return QStringLiteral("GBA");
    if (has("gbc") || has("gameboycolor"))
        return QStringLiteral("Game Boy Color");
    if (has("gb") || has("gameboy"))
        return QStringLiteral("Game Boy");
    if (has("saturn"))
        return QStringLiteral("Saturn");
    if (has("dreamcast") || has("dc"))
        return QStringLiteral("Dreamcast");
    if (has("arcade") || has("mame"))
        return QStringLiteral("Arcade");
    return QString();
}

bool isPs3PackageFolder(const QString& path) {
    const QFileInfo info(path);
    if (!info.isDir())
        return false;
    QDir directory(path);
    if (QFileInfo(directory.filePath(QStringLiteral("EBOOT.BIN"))).isFile())
        return true;
    if (QFileInfo(directory.filePath(QStringLiteral("PS3_GAME/USRDIR/EBOOT.BIN"))).isFile())
        return true;
    if (QFileInfo(directory.filePath(QStringLiteral("PS3_GAME/PS3_GAME/PARAM.SFO"))).isFile())
        return true;
    if (QFileInfo(directory.filePath(QStringLiteral("PS3_GAME/PARAM.SFO"))).isFile())
        return true;
    return false;
}

bool isTag(const QString& content) {
    QString text = content.trimmed().toLower();
    if (text.isEmpty() || text == QStringLiteral("!"))
        return true;
    static const QSet<QString> regions = {
        QStringLiteral("usa"), QStringLiteral("europe"), QStringLiteral("japan"),
        QStringLiteral("world"), QStringLiteral("australia"), QStringLiteral("korea"),
        QStringLiteral("china"), QStringLiteral("taiwan"), QStringLiteral("brazil"),
        QStringLiteral("canada"), QStringLiteral("france"), QStringLiteral("germany"),
        QStringLiteral("italy"), QStringLiteral("spain"), QStringLiteral("netherlands"),
        QStringLiteral("sweden"), QStringLiteral("russia"), QStringLiteral("eu"),
        QStringLiteral("us"), QStringLiteral("jp"), QStringLiteral("pal"),
        QStringLiteral("ntsc"), QStringLiteral("ntsc-u"), QStringLiteral("ntsc-j"),
        QStringLiteral("ntsc-5"), QStringLiteral("secam"), QStringLiteral("e"),
        QStringLiteral("u"), QStringLiteral("j"), QStringLiteral("k"), QStringLiteral("w"),
        QStringLiteral("g"), QStringLiteral("f"), QStringLiteral("i"), QStringLiteral("s"),
        QStringLiteral("nl"), QStringLiteral("kr"), QStringLiteral("tw"), QStringLiteral("cn"),
        QStringLiteral("br"), QStringLiteral("ca"), QStringLiteral("asia"),
        QStringLiteral("australia"),
    };
    QStringList regionParts = text.split(QRegularExpression(QStringLiteral("[,/]")),
                                         Qt::SkipEmptyParts);
    if (!regionParts.isEmpty()) {
        bool allRegions = true;
        for (QString part : regionParts) {
            part = part.trimmed();
            if (!regions.contains(part)) {
                allRegions = false;
                break;
            }
        }
        if (allRegions)
            return true;
    }
    static const QSet<QString> dumpTags = {
        QStringLiteral("beta"), QStringLiteral("proto"), QStringLiteral("demo"),
        QStringLiteral("kiosk"), QStringLiteral("debug"), QStringLiteral("unlicensed"),
        QStringLiteral("sample"), QStringLiteral("preview"), QStringLiteral("trial"),
        QStringLiteral("final"), QStringLiteral("pilot"), QStringLiteral("alt"),
        QStringLiteral("bad"), QStringLiteral("overdump"), QStringLiteral("underdump"),
    };
    if (dumpTags.contains(text))
        return true;
    static const QRegularExpression revision(
        QStringLiteral("^(?:rev(?:ision)?|ver(?:sion)?|build|v|r|b)"
                       "[\\s._-]*[a-z0-9][a-z0-9._-]*$"));
    if (revision.match(text).hasMatch())
        return true;
    static const QRegularExpression disc(
        QStringLiteral("^(?:disc|disk|cd|dvd|side)[\\s._-]*[a-z0-9]+$"));
    if (disc.match(text).hasMatch())
        return true;
    static const QSet<QString> languages = {
        QStringLiteral("en"), QStringLiteral("fr"), QStringLiteral("de"),
        QStringLiteral("es"), QStringLiteral("it"), QStringLiteral("ja"),
        QStringLiteral("jp"), QStringLiteral("nl"), QStringLiteral("pt"),
        QStringLiteral("ru"), QStringLiteral("sv"), QStringLiteral("no"),
        QStringLiteral("da"), QStringLiteral("fi"), QStringLiteral("pl"),
        QStringLiteral("ko"), QStringLiteral("zh"), QStringLiteral("cs"),
        QStringLiteral("hu"), QStringLiteral("multi"), QStringLiteral("multi5"),
        QStringLiteral("multi6"), QStringLiteral("multi7"), QStringLiteral("multi8"),
    };
    const QStringList words = text.split(QRegularExpression(QStringLiteral("[\\s,]+")),
                                         Qt::SkipEmptyParts);
    if (words.size() >= 1 && words.size() <= 8) {
        bool allLanguages = true;
        for (const QString& word : words) {
            if (!languages.contains(word)) {
                allLanguages = false;
                break;
            }
        }
        if (allLanguages)
            return true;
    }
    return false;
}

QString cleanTitleText(const QString& raw) {
    QString text = raw;
    text.replace(QLatin1Char('_'), QLatin1Char(' '));
    static const QRegularExpression bracket(
        QStringLiteral("[\\[\\(]([^\\]\\)]{0,64})[\\]\\)]"));
    QString withoutTags;
    qsizetype last = 0;
    auto matches = bracket.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        withoutTags.append(text.mid(last, match.capturedStart() - last));
        if (!isTag(match.captured(1)))
            withoutTags.append(match.captured(0));
        last = match.capturedEnd();
    }
    withoutTags.append(text.mid(last));
    text = withoutTags;
    static const QRegularExpression trailingRevision(
        QStringLiteral("\\s+(?:rev(?:ision)?|ver(?:sion)?|build)"
                       "\\s*[._-]?\\s*[a-z0-9]+$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression trailingVersion(
        QStringLiteral("\\s+v\\d+(?:\\.\\d+)*[a-z]?$"),
        QRegularExpression::CaseInsensitiveOption);
    for (int pass = 0; pass < 3; ++pass) {
        const QString before = text;
        text.remove(trailingRevision);
        text.remove(trailingVersion);
        if (text == before)
            break;
    }
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    text = text.trimmed();
    while (text.endsWith(QLatin1Char('-')) || text.endsWith(QLatin1Char('.'))
           || text.endsWith(QLatin1Char('_')))
        text.chop(1);
    text = text.trimmed();
    return text;
}

QString findSidecarArt(const QFileInfo& rom) {
    const QDir directory = rom.absoluteDir();
    const QString base = rom.completeBaseName();
    const QStringList names = {
        QStringLiteral("ICON0.PNG"), QStringLiteral("icon0.png"),
        QStringLiteral("boxart.png"), QStringLiteral("cover.png"),
        base + QStringLiteral(".png"), base + QStringLiteral(".jpg"),
        base + QStringLiteral(".jpeg"), base + QStringLiteral(".webp"),
    };
    for (const QString& name : names) {
        const QFileInfo candidate(directory.filePath(name));
        if (candidate.isFile())
            return QDir::cleanPath(candidate.absoluteFilePath());
    }
    return QString();
}

QString findPs3FolderArt(const QString& directory, const QString& sfoDirectory,
                         const QString& iconName) {
    if (!iconName.isEmpty()) {
        const QString cleaned = QDir::fromNativeSeparators(iconName).trimmed();
        if (!cleaned.isEmpty() && !QDir::isAbsolutePath(cleaned)
            && !cleaned.startsWith(QStringLiteral(".."))) {
            QString resolved;
            if (isPathInside(directory, QDir(sfoDirectory).filePath(cleaned), resolved)
                && QFileInfo(resolved).isFile())
                return resolved;
        }
    }
    const QStringList relative = {
        QStringLiteral("ICON0.PNG"), QStringLiteral("icon0.png"),
        QStringLiteral("PS3_GAME/ICON0.PNG"), QStringLiteral("PS3_GAME/icon0.png"),
        QStringLiteral("PS3_GAME/USRDIR/ICON0.PNG"),
        QStringLiteral("boxart.png"), QStringLiteral("cover.png"),
    };
    for (const QString& name : relative) {
        const QFileInfo candidate(QDir(directory).filePath(name));
        if (candidate.isFile())
            return QDir::cleanPath(candidate.absoluteFilePath());
    }
    const QString folderName = QFileInfo(directory).fileName();
    for (const QString& suffix : {QStringLiteral(".png"), QStringLiteral(".jpg")}) {
        const QFileInfo candidate(QDir(directory).filePath(folderName + suffix));
        if (candidate.isFile())
            return QDir::cleanPath(candidate.absoluteFilePath());
    }
    return QString();
}

QString pkgArtCachePath(const QString& pkgPath, const QByteArray& png) {
    QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (cacheRoot.isEmpty())
        cacheRoot = QDir::tempPath();
    const QString directory = QDir(cacheRoot).filePath(QStringLiteral("xmb-manager-pkg-art"));
    if (png.isEmpty() || !QDir().mkpath(directory))
        return QString();
    const QFileInfo info(pkgPath);
    const QByteArray key = (canonicalPath(pkgPath) + QLatin1Char('|')
                            + QString::number(info.size()) + QLatin1Char('|')
                            + QString::number(info.lastModified().toSecsSinceEpoch()))
                               .toUtf8();
    const QString name = QString::fromLatin1(
        QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex());
    const QString path = QDir(directory).filePath(name + QStringLiteral(".png"));
    if (QFileInfo(path).isFile())
        return path;
    QString error;
    if (!writeFileAtomic(path, png, error)) {
        logWarning(QStringLiteral("Could not cache the package icon: %1").arg(error));
        return QString();
    }
    return path;
}

QString readSfoMetadata(const QString& sfoPath, QHash<QString, QString>& values) {
    QString error;
    QFile file(sfoPath);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("could not open %1").arg(sfoPath);
    if (file.size() > kMaxSfoBytes)
        return QStringLiteral("PARAM.SFO exceeds 1 MiB");
    const QByteArray bytes = file.read(kMaxSfoBytes + 1);
    if (bytes.size() > kMaxSfoBytes)
        return QStringLiteral("PARAM.SFO exceeds 1 MiB");
    if (!readParamSfo(bytes, values, error))
        return QStringLiteral("invalid PARAM.SFO: %1").arg(error);
    return QString();
}

QString firstExisting(const QStringList& paths) {
    for (const QString& path : paths) {
        if (QFileInfo(path).isFile())
            return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    }
    return QString();
}

QString sfoPathForFolder(const QString& directory) {
    return firstExisting({
        QDir(directory).filePath(QStringLiteral("PS3_GAME/PARAM.SFO")),
        QDir(directory).filePath(QStringLiteral("PARAM.SFO")),
        QDir(directory).filePath(QStringLiteral("USRDIR/PARAM.SFO")),
    });
}

struct ScanState {
    QVector<DetectedGame> games;
    QSet<QString> seen;
    QSet<QString> visitedDirectories;
    int filesVisited = 0;
    bool warnedFileLimit = false;
    QStringList* warnings = nullptr;
};

void addGame(ScanState& state, DetectedGame game) {
    if (game.romPath.isEmpty() || game.platform.isEmpty())
        return;
    const QString key = dedupeKey(game.romPath);
    if (state.seen.contains(key))
        return;
    state.seen.insert(key);
    state.games.append(std::move(game));
}

void scanFile(ScanState& state, const QFileInfo& info) {
    if (!info.isFile())
        return;
    const QString path = QDir::cleanPath(info.absoluteFilePath());
    const QString suffix = info.suffix().toLower();
    if (suffix.isEmpty())
        return;
    if (!defaultExtensions().contains(suffix))
        return;
    if (suffix == QStringLiteral("bin")) {
        const QString cue = QDir(info.absolutePath()).filePath(info.completeBaseName()
                                                                + QStringLiteral(".cue"));
        if (QFileInfo(cue).isFile()) {
            // The descriptor represents the disc; do not list the raw track twice.
            return;
        }
    }
    if (++state.filesVisited > kMaxScannedFiles) {
        if (!state.warnedFileLimit && state.warnings) {
            state.warnings->append(QStringLiteral("Stopped after %1 files; the tree is larger "
                                                  "than the scan limit").arg(kMaxScannedFiles));
            state.warnedFileLimit = true;
        }
        return;
    }
    const QString platform = platformForPath(path);
    if (platform.isEmpty()) {
        if (state.warnings)
            state.warnings->append(QStringLiteral("Unrecognised ROM type: %1").arg(path));
        return;
    }
    DetectedGame game;
    game.platform = platform;
    game.romPath = path;
    game.romKind = suffix;
    game.size = info.size();
    game.title = titleFromFileName(info.fileName());
    game.artPath = findSidecarArt(info);
    game.sourceNote = QStringLiteral("file .%1").arg(suffix);
    if (suffix == QStringLiteral("iso")) {
        const QString sniffed = sniffIsoPlatform(path);
        if (sniffed.isEmpty())
            game.sourceNote = QStringLiteral("file .iso (no platform marker read)");
        else
            game.sourceNote = QStringLiteral("file .iso (%1 marker)").arg(sniffed);
    } else if (suffix == QStringLiteral("pkg")) {
        PackageProbe probe;
        QString probeError;
        if (probePs3Package(path, probe, probeError) && probe.sfoReadable) {
            const QString title = probe.sfo.value(QStringLiteral("TITLE")).trimmed();
            if (!title.isEmpty())
                game.title = cleanTitleText(title);
            game.sourceNote = QStringLiteral("PS3 package, readable PARAM.SFO");
            if (!probe.iconPng.isEmpty())
                game.artPath = pkgArtCachePath(path, probe.iconPng);
        } else if (probe.pkgMagic) {
            game.sourceNote = QStringLiteral("PS3 package, header readable, payload not "
                                             "decrypted");
        } else {
            game.sourceNote = QStringLiteral("PS3 package, header not readable");
        }
    }
    addGame(state, std::move(game));
}

void scanDirectory(ScanState& state, const QString& directory, int depth) {
    if (depth > kMaxScanDepth) {
        if (state.warnings)
            state.warnings->append(QStringLiteral("Skipped deep directory: %1").arg(directory));
        return;
    }
    const QString canonical = canonicalPath(directory);
    const QString key =
#ifdef Q_OS_WIN
        canonical.toLower();
#else
        canonical;
#endif
    if (state.visitedDirectories.contains(key))
        return;
    state.visitedDirectories.insert(key);

    if (isPs3PackageFolder(directory)) {
        DetectedGame game;
        game.platform = QStringLiteral("PS3");
        game.romPath = canonical;
        game.romKind = QStringLiteral("folder");
        game.size = 0;
        const QString sfoPath = sfoPathForFolder(directory);
        QString sfoDirectory = directory;
        QString iconName;
        if (!sfoPath.isEmpty()) {
            sfoDirectory = QFileInfo(sfoPath).absolutePath();
            QHash<QString, QString> values;
            const QString sfoError = readSfoMetadata(sfoPath, values);
            if (!sfoError.isEmpty()) {
                if (state.warnings)
                    state.warnings->append(QStringLiteral("%1: %2").arg(directory, sfoError));
            } else {
                const QString title = values.value(QStringLiteral("TITLE")).trimmed();
                if (!title.isEmpty())
                    game.title = cleanTitleText(title);
                iconName = values.value(QStringLiteral("ICON0")).trimmed();
            }
        }
        if (game.title.isEmpty())
            game.title = cleanTitleText(QFileInfo(directory).fileName());
        game.artPath = findPs3FolderArt(directory, sfoDirectory, iconName);
        game.sourceNote = QStringLiteral("PS3 package folder (%1)")
                              .arg(sfoPath.isEmpty()
                                       ? QStringLiteral("EBOOT.BIN")
                                       : QDir(directory).relativeFilePath(sfoPath));
        addGame(state, std::move(game));
        return;
    }

    QDir dir(directory);
    const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
                                                    QDir::Name);
    for (const QFileInfo& entry : entries) {
        const QString name = entry.fileName();
        if (name == QStringLiteral("$RECYCLE.BIN")
            || name.compare(QStringLiteral("System Volume Information"), Qt::CaseInsensitive) == 0)
            continue;
        if (entry.isSymLink()) {
            if (entry.isFile())
                scanFile(state, entry);
            continue;
        }
        if (entry.isDir())
            scanDirectory(state, entry.absoluteFilePath(), depth + 1);
        else if (entry.isFile())
            scanFile(state, entry);
    }
}

} // namespace

QStringList defaultExtensions() {
    static const QStringList extensions = {
        QStringLiteral("iso"), QStringLiteral("pkg"), QStringLiteral("gcm"),
        QStringLiteral("cso"), QStringLiteral("rvz"), QStringLiteral("wbfs"),
        QStringLiteral("nsp"), QStringLiteral("xci"), QStringLiteral("3ds"),
        QStringLiteral("cia"), QStringLiteral("nds"), QStringLiteral("sfc"),
        QStringLiteral("smc"), QStringLiteral("nes"), QStringLiteral("n64"),
        QStringLiteral("z64"), QStringLiteral("v64"), QStringLiteral("md"),
        QStringLiteral("gen"), QStringLiteral("smd"), QStringLiteral("gba"),
        QStringLiteral("gb"), QStringLiteral("gbc"), QStringLiteral("gdi"),
        QStringLiteral("cdi"), QStringLiteral("cue"), QStringLiteral("bin"),
        QStringLiteral("chd"), QStringLiteral("img"), QStringLiteral("mdf"),
        QStringLiteral("zip"),
    };
    return extensions;
}

QString platformForPath(const QString& path) {
    const QFileInfo info(path);
    if (info.isDir())
        return isPs3PackageFolder(path) ? QStringLiteral("PS3") : QString();
    const QString suffix = info.suffix().toLower();
    if (suffix.isEmpty())
        return QString();
    if (suffix == QStringLiteral("pkg") || suffix == QStringLiteral("gcm"))
        return QStringLiteral("PS3");
    if (suffix == QStringLiteral("cso"))
        return QStringLiteral("PSP");
    if (suffix == QStringLiteral("rvz") || suffix == QStringLiteral("wbfs"))
        return QStringLiteral("GameCube/Wii");
    if (suffix == QStringLiteral("nsp") || suffix == QStringLiteral("xci"))
        return QStringLiteral("Switch");
    if (suffix == QStringLiteral("3ds") || suffix == QStringLiteral("cia"))
        return QStringLiteral("3DS");
    if (suffix == QStringLiteral("nds"))
        return QStringLiteral("NDS");
    if (suffix == QStringLiteral("sfc") || suffix == QStringLiteral("smc"))
        return QStringLiteral("SNES");
    if (suffix == QStringLiteral("nes"))
        return QStringLiteral("NES");
    if (suffix == QStringLiteral("n64") || suffix == QStringLiteral("z64")
        || suffix == QStringLiteral("v64"))
        return QStringLiteral("N64");
    if (suffix == QStringLiteral("md") || suffix == QStringLiteral("gen")
        || suffix == QStringLiteral("smd"))
        return QStringLiteral("Genesis");
    if (suffix == QStringLiteral("gba"))
        return QStringLiteral("GBA");
    if (suffix == QStringLiteral("gb"))
        return QStringLiteral("Game Boy");
    if (suffix == QStringLiteral("gbc"))
        return QStringLiteral("Game Boy Color");
    if (suffix == QStringLiteral("gdi") || suffix == QStringLiteral("cdi"))
        return QStringLiteral("Dreamcast");
    if (suffix == QStringLiteral("zip"))
        return QStringLiteral("Arcade");
    if (suffix == QStringLiteral("iso")) {
        const QString hint = platformHint(path);
        if (!hint.isEmpty())
            return hint;
        const QString sniffed = sniffIsoPlatform(path);
        if (!sniffed.isEmpty())
            return sniffed;
        return QStringLiteral("Disc image");
    }
    if (suffix == QStringLiteral("cue") || suffix == QStringLiteral("bin")
        || suffix == QStringLiteral("chd") || suffix == QStringLiteral("img")
        || suffix == QStringLiteral("mdf")) {
        const QString hint = platformHint(path);
        if (!hint.isEmpty())
            return hint;
        return QStringLiteral("Disc image");
    }
    return QString();
}

QVector<DetectedGame> scanPaths(const QStringList& roots, QStringList* warnings) {
    ScanState state;
    state.warnings = warnings;
    for (const QString& root : roots) {
        const QFileInfo info(root);
        if (!info.exists()) {
            if (warnings)
                warnings->append(QStringLiteral("Path does not exist: %1").arg(root));
            continue;
        }
        if (info.isDir())
            scanDirectory(state, QDir::cleanPath(info.absoluteFilePath()), 0);
        else if (info.isFile())
            scanFile(state, info);
    }
    std::sort(state.games.begin(), state.games.end(),
              [](const DetectedGame& a, const DetectedGame& b) {
                  const int compare = QString::compare(a.title, b.title, Qt::CaseInsensitive);
                  if (compare != 0)
                      return compare < 0;
                  return a.romPath < b.romPath;
              });
    return state.games;
}

QString titleFromFileName(const QString& fileName) {
    const QFileInfo info(fileName);
    QString base = info.completeBaseName();
    if (base.isEmpty())
        base = info.fileName();
    const QString cleaned = cleanTitleText(base);
    return cleaned.isEmpty() ? info.fileName() : cleaned;
}

bool readParamSfo(const QByteArray& data, QHash<QString, QString>& values, QString& error) {
    error.clear();
    values.clear();
    if (data.size() < 20)
        return fail(error, QStringLiteral("PARAM.SFO is shorter than the 20-byte header"));
    const quint32 magic = le32(data, 0);
    if (magic != 0x46535000U)
        return fail(error, QStringLiteral("PARAM.SFO magic is 0x%1, expected 0x46535000")
                              .arg(magic, 8, 16, QLatin1Char('0')));
    const quint32 version = le32(data, 4);
    if (version != 0x00000101U)
        return fail(error, QStringLiteral("PARAM.SFO version is 0x%1, expected 0x00000101")
                              .arg(version, 8, 16, QLatin1Char('0')));
    const quint32 keyTableStart = le32(data, 8);
    const quint32 dataTableStart = le32(data, 12);
    const quint32 count = le32(data, 16);
    if (keyTableStart < 20)
        return fail(error, QStringLiteral("PARAM.SFO key table starts inside the header"));
    if (dataTableStart < keyTableStart || qint64(dataTableStart) > data.size())
        return fail(error, QStringLiteral("PARAM.SFO data table is out of bounds"));
    if (count > 4096)
        return fail(error, QStringLiteral("PARAM.SFO declares more than 4096 entries"));
    if (qint64(20) + qint64(16) * count > qint64(keyTableStart)
        || qint64(20) + qint64(16) * count > qint64(dataTableStart))
        return fail(error, QStringLiteral("PARAM.SFO index table overlaps the key table"));
    const qint64 keyTableLength = qint64(dataTableStart) - qint64(keyTableStart);
    for (quint32 index = 0; index < count; ++index) {
        const qsizetype entry = 20 + qsizetype(index) * 16;
        const quint16 keyOffset = le16(data, entry);
        const quint16 format = le16(data, entry + 2);
        const quint32 length = le32(data, entry + 4);
        const quint32 maxLength = le32(data, entry + 8);
        const quint32 dataOffset = le32(data, entry + 12);
        if (qint64(keyOffset) >= keyTableLength)
            return fail(error, QStringLiteral("PARAM.SFO entry %1 key offset is out of bounds")
                                  .arg(index));
        const qsizetype keyStart = qsizetype(keyTableStart) + keyOffset;
        const qsizetype keyLimit = qsizetype(dataTableStart);
        qsizetype keyEnd = keyStart;
        while (keyEnd < keyLimit && data.at(keyEnd) != '\0')
            ++keyEnd;
        if (keyEnd >= keyLimit)
            return fail(error, QStringLiteral("PARAM.SFO entry %1 key is not NUL terminated")
                                  .arg(index));
        const QByteArray keyBytes = data.mid(keyStart, keyEnd - keyStart);
        if (keyBytes.isEmpty())
            return fail(error, QStringLiteral("PARAM.SFO entry %1 has an empty key").arg(index));
        for (const char byte : keyBytes) {
            const int value = static_cast<unsigned char>(byte);
            if (value < 0x20 || value > 0x7e)
                return fail(error, QStringLiteral("PARAM.SFO entry %1 key is not ASCII")
                                      .arg(index));
        }
        const QString key = QString::fromLatin1(keyBytes);
        if (values.contains(key))
            return fail(error, QStringLiteral("PARAM.SFO has a duplicate key '%1'").arg(key));
        if (qint64(length) > maxLength && maxLength != 0)
            return fail(error, QStringLiteral("PARAM.SFO key '%1' length exceeds max length")
                                  .arg(key));
        if (qint64(dataOffset) + qint64(length)
            > qint64(data.size()) - qint64(dataTableStart))
            return fail(error, QStringLiteral("PARAM.SFO key '%1' data is out of bounds").arg(key));
        const QByteArray payload = data.mid(qsizetype(dataTableStart) + dataOffset,
                                            qsizetype(length));
        QString value;
        if (format == 0x0004 || format == 0x0204) {
            QByteArray bytes = payload;
            if (format == 0x0204) {
                while (bytes.endsWith('\0'))
                    bytes.chop(1);
            }
            if (bytes.contains('\0'))
                return fail(error, QStringLiteral("PARAM.SFO key '%1' contains an embedded NUL")
                                      .arg(key));
            QStringDecoder decoder(QStringDecoder::Utf8);
            value = decoder.decode(bytes);
            if (decoder.hasError())
                return fail(error, QStringLiteral("PARAM.SFO key '%1' is not valid UTF-8").arg(key));
        } else if (format == 0x0404) {
            if (length != 4)
                return fail(error, QStringLiteral("PARAM.SFO key '%1' uint32 length is not 4")
                                      .arg(key));
            value = QString::number(le32(payload, 0));
        } else {
            return fail(error, QStringLiteral("PARAM.SFO key '%1' uses unsupported format 0x%2")
                                  .arg(key).arg(format, 4, 16, QLatin1Char('0')));
        }
        values.insert(key, value);
    }
    return true;
}

bool writeParamSfo(const QHash<QString, QString>& values, QByteArray& data, QString& error) {
    error.clear();
    data.clear();
    if (values.isEmpty())
        return fail(error, QStringLiteral("PARAM.SFO needs at least one key"));
    if (values.size() > 1024)
        return fail(error, QStringLiteral("PARAM.SFO is limited to 1024 keys"));
    QStringList keys = values.keys();
    std::sort(keys.begin(), keys.end());
    QByteArray keyTable;
    QVector<quint16> keyOffsets;
    keyOffsets.reserve(keys.size());
    for (const QString& key : keys) {
        const QByteArray bytes = key.toUtf8();
        if (bytes.isEmpty() || bytes.size() > 127)
            return fail(error, QStringLiteral("PARAM.SFO key '%1' must be 1..127 bytes").arg(key));
        for (const char byte : bytes) {
            const int value = static_cast<unsigned char>(byte);
            if (value < 0x20 || value > 0x7e)
                return fail(error, QStringLiteral("PARAM.SFO key '%1' must be printable ASCII")
                                      .arg(key));
        }
        if (keyTable.size() > 0xffff)
            return fail(error, QStringLiteral("PARAM.SFO key table is too large"));
        keyOffsets.append(static_cast<quint16>(keyTable.size()));
        keyTable.append(bytes);
        keyTable.append('\0');
    }
    while (keyTable.size() % 4 != 0)
        keyTable.append('\0');
    if (keyTable.size() > 0xffff)
        return fail(error, QStringLiteral("PARAM.SFO key table is too large"));

    struct DataEntry {
        qsizetype offset = 0;
        quint32 length = 0;   // includes the terminating NUL
        quint32 maxLength = 0;
    };
    QVector<DataEntry> entries;
    entries.reserve(keys.size());
    QByteArray dataTable;
    for (const QString& key : keys) {
        const QByteArray payload = values.value(key).toUtf8() + '\0';
        if (payload.size() > 0x7fffffff)
            return fail(error, QStringLiteral("PARAM.SFO value for '%1' is too large").arg(key));
        while (dataTable.size() % 4 != 0)
            dataTable.append('\0');
        DataEntry entry;
        entry.offset = dataTable.size();
        entry.length = static_cast<quint32>(payload.size());
        dataTable.append(payload);
        while (dataTable.size() % 4 != 0)
            dataTable.append('\0');
        entry.maxLength = static_cast<quint32>(dataTable.size() - entry.offset);
        entries.append(entry);
    }
    const quint32 keyTableStart = static_cast<quint32>(20 + 16 * keys.size());
    const quint32 dataTableStart =
        static_cast<quint32>(keyTableStart + keyTable.size());
    if (qint64(dataTableStart) > 0x7fffffff)
        return fail(error, QStringLiteral("PARAM.SFO tables are too large"));
    data.resize(qsizetype(dataTableStart) + dataTable.size());
    data.fill('\0');
    putLe32(data, 0, 0x46535000U);
    putLe32(data, 4, 0x00000101U);
    putLe32(data, 8, keyTableStart);
    putLe32(data, 12, dataTableStart);
    putLe32(data, 16, static_cast<quint32>(keys.size()));
    qsizetype entryOffset = 20;
    for (qsizetype index = 0; index < keys.size(); ++index) {
        const DataEntry& entry = entries.at(qsizetype(index));
        putLe16(data, entryOffset, keyOffsets.at(index));
        putLe16(data, entryOffset + 2, 0x0204);
        putLe32(data, entryOffset + 4, entry.length);
        putLe32(data, entryOffset + 8, entry.maxLength);
        putLe32(data, entryOffset + 12, static_cast<quint32>(entry.offset));
        entryOffset += 16;
    }
    data.replace(qsizetype(keyTableStart), keyTable.size(), keyTable);
    data.replace(qsizetype(dataTableStart), dataTable.size(), dataTable);
    return true;
}

bool probePs3Package(const QString& path, PackageProbe& probe, QString& error) {
    error.clear();
    probe = PackageProbe();
    QFileInfo info(path);
    if (!info.isFile())
        return fail(error, QStringLiteral("Package is not a file: %1").arg(path));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Could not open '%1': %2").arg(path, file.errorString()));
    if (file.size() < 0x100) {
        bool ok = false;
        const QByteArray head = readWindowBytes(file, 0, 0x40, ok);
        probe.zipContainer = head.startsWith(QByteArray("PK\x03\x04", 4));
        return fail(error, QStringLiteral("File is too small to be a PS3 package (%1 bytes)")
                              .arg(file.size()));
    }
    bool headerOk = false;
    const QByteArray header = readWindowBytes(file, 0, 0x280, headerOk);
    if (header.size() < 0x100)
        return fail(error, QStringLiteral("Could not read the package header"));
    probe.zipContainer = header.startsWith(QByteArray("PK\x03\x04", 4));
    const quint32 magic = be32(header, 0);
    if (magic != 0x7f504b47U) {
        return fail(error, QStringLiteral("Not a PS3 package: magic is 0x%1, expected 0x7F504B47")
                              .arg(magic, 8, 16, QLatin1Char('0')));
    }
    probe.pkgMagic = true;
    probe.revision = be16(header, 0x04);
    probe.type = be16(header, 0x06);
    probe.metadataOffset = be32(header, 0x08);
    probe.metadataCount = be32(header, 0x0c);
    probe.metadataSize = be32(header, 0x10);
    probe.itemCount = be32(header, 0x14);
    probe.totalSize = be64(header, 0x18);
    probe.dataOffset = be64(header, 0x20);
    probe.dataSize = be64(header, 0x28);
    const QByteArray contentId = header.mid(0x30, 0x24);
    qsizetype contentIdEnd = contentId.indexOf('\0');
    if (contentIdEnd < 0)
        contentIdEnd = contentId.size();
    probe.contentId = QString::fromLatin1(contentId.left(contentIdEnd)).trimmed();
    probe.digest = header.mid(0x60, 0x10);
    probe.aesIv = header.mid(0x70, 0x10);
    probe.headerDigest = header.mid(0x80, 0x40);

    const qint64 fileSize = file.size();
    const bool metadataInBounds = qint64(probe.metadataOffset) + qint64(probe.metadataSize)
        <= fileSize;
    const bool dataInBounds =
        qint64(probe.dataOffset) + qint64(probe.dataSize) <= fileSize;

    QStringList facts;
    facts.append(QStringLiteral("magic 0x7F504B47"));
    facts.append(probe.revision == 0x8000
                     ? QStringLiteral("revision 0x8000 (finalized retail)")
                     : QStringLiteral("revision 0x%1 (non-finalized/debug)")
                           .arg(probe.revision, 4, 16, QLatin1Char('0')));
    facts.append(QStringLiteral("type %1").arg(probe.type));
    facts.append(QStringLiteral("metadata offset 0x%1, %2 bytes, %3 entries%4")
                     .arg(probe.metadataOffset, 0, 16)
                     .arg(probe.metadataSize)
                     .arg(probe.metadataCount)
                     .arg(metadataInBounds ? QString() : QStringLiteral(" (out of bounds)")));
    facts.append(QStringLiteral("item count %1").arg(probe.itemCount));
    facts.append(QStringLiteral("data offset 0x%1, %2 bytes%3")
                     .arg(probe.dataOffset, 0, 16)
                     .arg(probe.dataSize)
                     .arg(dataInBounds ? QString() : QStringLiteral(" (out of bounds)")));
    facts.append(QStringLiteral("content id '%1'")
                     .arg(probe.contentId.isEmpty() ? QStringLiteral("(none)")
                                                    : probe.contentId));
    facts.append(QStringLiteral("AES-128-CTR IV %1")
                     .arg(QString::fromLatin1(probe.aesIv.toHex())));
    facts.append(QStringLiteral("header digest %1")
                     .arg(QString::fromLatin1(probe.headerDigest.toHex())));
    const bool isPs3 = probe.type == 1;
    if (!isPs3)
        facts.append(QStringLiteral("not a PS3 package (type 1 is PS3)"));
    probe.evidence = facts.join(QStringLiteral("; "));

    if (!isPs3) {
        error = QStringLiteral("Not a PS3 package: header type %1 is PSP/PS Vita or unknown; "
                               "evidence: %2").arg(probe.type).arg(probe.evidence);
        return false;
    }

    QVector<QPair<qint64, qint64>> windows;
    if (metadataInBounds && probe.metadataSize > 0) {
        windows.append({qint64(probe.metadataOffset),
                        std::min<qint64>(probe.metadataSize, kMaxPkgProbeBytes)});
    }
    if (dataInBounds && probe.dataSize > 0) {
        windows.append({qint64(probe.dataOffset),
                        std::min<qint64>(probe.dataSize, kMaxPkgProbeBytes)});
    }
    QByteArray dataWindow;
    for (const auto& window : windows) {
        bool ok = false;
        const QByteArray bytes = readWindowBytes(file, window.first, window.second, ok);
        if (!ok)
            continue;
        if (window.first == qint64(probe.dataOffset))
            dataWindow = bytes;
        const auto findSfo = [&](const QByteArray& source, qint64 sourceOffset) {
            qsizetype hit = source.indexOf(QByteArray("\0PSF", 4));
            while (hit >= 0 && !probe.sfoReadable) {
                bool readOk = false;
                const qint64 sfoStart = sourceOffset + hit;
                const QByteArray sfoBytes = readWindowBytes(
                    file, sfoStart, std::min<qint64>(kMaxSfoBytes, fileSize - sfoStart), readOk);
                if (readOk) {
                    QHash<QString, QString> values;
                    QString sfoError;
                    if (readParamSfo(sfoBytes, values, sfoError)) {
                        probe.sfoReadable = true;
                        probe.sfo = values;
                        break;
                    }
                }
                hit = source.indexOf(QByteArray("\0PSF", 4), hit + 4);
            }
        };
        findSfo(bytes, window.first);
    }
    if (!dataWindow.isEmpty()) {
        qsizetype pngStart = dataWindow.indexOf(QByteArray("\x89PNG\r\n\x1a\n", 8));
        while (pngStart >= 0) {
            const qsizetype iend = dataWindow.indexOf(QByteArray("IEND", 4), pngStart + 8);
            if (iend < 0 || iend + 8 > dataWindow.size())
                break;
            const QByteArray candidate = dataWindow.mid(pngStart, iend + 8 - pngStart);
            if (candidate.size() <= 4 * 1024 * 1024) {
                QImage image;
                if (image.loadFromData(candidate, "PNG")) {
                    probe.iconPng = candidate;
                    break;
                }
            }
            pngStart = dataWindow.indexOf(QByteArray("\x89PNG\r\n\x1a\n", 8), pngStart + 8);
        }
    }
    if (probe.sfoReadable) {
        QStringList keys = probe.sfo.keys();
        keys.sort();
        facts.append(QStringLiteral("readable PARAM.SFO keys: %1").arg(keys.join(QLatin1Char(','))));
        probe.evidence = facts.join(QStringLiteral("; "));
    }
    return true;
}

QString fullHashLabel() {
    return QStringLiteral("full-sha256-v1");
}

QString partialHashLabel() {
    return QStringLiteral("partial-sha256-v1");
}

QString folderHashLabel() {
    return QStringLiteral("folder-v1");
}

namespace {

QString fingerprintMtime(const QFileInfo& info) {
    const QDateTime modified = info.lastModified().toUTC();
    return modified.isValid() ? modified.toString(Qt::ISODate) : QString();
}

bool hashRange(QCryptographicHash& hash, QFile& file, qint64 offset, qint64 length,
               QString& error) {
    if (!file.seek(offset)) {
        error = QStringLiteral("Could not seek in '%1': %2").arg(file.fileName(), file.errorString());
        return false;
    }
    QByteArray buffer(1024 * 1024, '\0');
    qint64 remaining = length;
    while (remaining > 0) {
        const qint64 wanted = std::min<qint64>(remaining, buffer.size());
        const qint64 read = file.read(buffer.data(), wanted);
        if (read < 0) {
            error = QStringLiteral("Could not read '%1': %2")
                        .arg(file.fileName(), file.errorString());
            return false;
        }
        if (read == 0) {
            error = QStringLiteral("Unexpected end of file while hashing '%1'").arg(file.fileName());
            return false;
        }
        hash.addData(QByteArrayView(buffer.constData(), read));
        remaining -= read;
    }
    return true;
}

bool folderFingerprint(const QString& path, RomFingerprint& fingerprint, QString& error) {
    QVector<QPair<QString, qint64>> entries;
    QDirIterator iterator(path, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir root(path);
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        if (entries.size() >= 200000) {
            error = QStringLiteral("The ROM folder '%1' contains more than 200000 files")
                        .arg(path);
            return false;
        }
        const QString relative = QDir::fromNativeSeparators(root.relativeFilePath(
            info.absoluteFilePath()));
        entries.append({relative, info.size()});
    }
    std::sort(entries.begin(), entries.end(),
              [](const QPair<QString, qint64>& a, const QPair<QString, qint64>& b) {
                  return a.first < b.first;
              });
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 total = 0;
    for (const auto& entry : entries) {
        const QByteArray pathBytes = entry.first.toUtf8();
        const QByteArray sizeBytes = QByteArray::number(entry.second);
        hash.addData(QByteArrayView(pathBytes));
        hash.addData(QByteArrayView("\t", 1));
        hash.addData(QByteArrayView(sizeBytes));
        hash.addData(QByteArrayView("\n", 1));
        total += entry.second;
    }
    fingerprint.size = total;
    fingerprint.mtime = fingerprintMtime(QFileInfo(path));
    fingerprint.hashLabel = folderHashLabel();
    fingerprint.hashValue = QString::fromLatin1(hash.result().toHex());
    return true;
}

} // namespace

bool computeRomFingerprint(const QString& path, RomFingerprint& fingerprint, QString& error) {
    error.clear();
    fingerprint = RomFingerprint();
    const QFileInfo info(path);
    if (!info.exists()) {
        error = QStringLiteral("ROM does not exist: %1").arg(path);
        return false;
    }
    if (info.isDir())
        return folderFingerprint(path, fingerprint, error);
    if (!info.isFile()) {
        error = QStringLiteral("ROM is neither a file nor a directory: %1").arg(path);
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Could not open '%1' for hashing: %2")
                    .arg(path, file.errorString());
        return false;
    }
    fingerprint.size = file.size();
    fingerprint.mtime = fingerprintMtime(info);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (fingerprint.size > kFullHashLimit) {
        const QByteArray sizeBytes = QByteArray::number(fingerprint.size);
        hash.addData(QByteArrayView(sizeBytes));
        if (!hashRange(hash, file, 0, kHashChunk, error))
            return false;
        const qint64 tailOffset = fingerprint.size - kHashChunk;
        if (!hashRange(hash, file, tailOffset, kHashChunk, error))
            return false;
        fingerprint.hashLabel = partialHashLabel();
    } else {
        if (!hashRange(hash, file, 0, fingerprint.size, error))
            return false;
        fingerprint.hashLabel = fullHashLabel();
    }
    fingerprint.hashValue = QString::fromLatin1(hash.result().toHex());
    return true;
}

} // namespace xmb::manager

