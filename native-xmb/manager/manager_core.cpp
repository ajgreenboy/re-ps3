#include "manager_core.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QTextStream>

#include <algorithm>

namespace xmb::manager {
namespace {

constexpr const char* kIniGroup = "manager";
constexpr const char* kIniDataDirectoryKey = "manager/dataDirectory";
constexpr const char* kCollectionsSchema = "xmb-collections/1";
constexpr const char* kPlatformMapSchema = "xmb-platform-map/1";

QMutex& logMutex() {
    static QMutex mutex;
    return mutex;
}

void writeLog(const char* level, const QString& message) {
    QMutexLocker locker(&logMutex());
    QTextStream stream(stderr);
    stream << "xmb-manager: ";
    if (level && *level)
        stream << level << ": ";
    stream << message << Qt::endl;
}

bool fail(QString& error, const QString& message) {
    error = message;
    return false;
}

QString canonicalOrClean(const QString& path) {
    const QFileInfo info(path);
    if (info.exists()) {
        const QString canonical = info.canonicalFilePath();
        if (!canonical.isEmpty())
            return QDir::cleanPath(canonical);
    }
    return QDir::cleanPath(info.absoluteFilePath());
}

bool pathHasControlCharacters(const QString& path) {
    for (const QChar ch : path) {
        if (ch.unicode() < 0x20 || ch.unicode() == 0x7f)
            return true;
    }
    return false;
}

bool validMappingId(const QString& id) {
    if (id.isEmpty() || id.size() > 64)
        return false;
    for (const QChar ch : id) {
        const ushort c = ch.unicode();
        const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        if (!allowed)
            return false;
    }
    return id != QStringLiteral(".") && id != QStringLiteral("..");
}

QJsonObject mappingToJson(const PlatformMapping& mapping) {
    QJsonObject object;
    object.insert(QStringLiteral("platform"), mapping.platform);
    object.insert(QStringLiteral("emulatorId"), mapping.emulatorId);
    if (!mapping.arguments.isEmpty()) {
        QJsonArray arguments;
        for (const QString& argument : mapping.arguments)
            arguments.append(argument);
        object.insert(QStringLiteral("arguments"), arguments);
    }
    return object;
}

} // namespace

void logInfo(const QString& message) {
    writeLog("", message);
}

void logWarning(const QString& message) {
    writeLog("warning", message);
}

void logError(const QString& message) {
    writeLog("error", message);
}

QString managerExecutableDirectory() {
    if (QCoreApplication::instance())
        return QDir::cleanPath(QCoreApplication::applicationDirPath());
    return QDir::currentPath();
}

QString defaultSettingsPath() {
    return QDir(managerExecutableDirectory()).filePath(QStringLiteral("manager.ini"));
}

Settings defaultSettings() {
    Settings settings;
    settings.executableDirectory = managerExecutableDirectory();
    settings.iniPath = defaultSettingsPath();
    settings.dataDirectory = QDir(settings.executableDirectory).filePath(QStringLiteral("data"));
    return settings;
}

bool loadSettings(Settings& settings, QString& error) {
    error.clear();
    settings = defaultSettings();
    const QFileInfo iniInfo(settings.iniPath);
    if (!iniInfo.exists())
        return true;
    if (!iniInfo.isFile())
        return fail(error, QStringLiteral("Settings path is not a file: %1").arg(settings.iniPath));

    QSettings ini(settings.iniPath, QSettings::IniFormat);
    if (ini.status() != QSettings::NoError)
        return fail(error, QStringLiteral("Could not read settings '%1'").arg(settings.iniPath));
    const QString stored = ini.value(QLatin1String(kIniDataDirectoryKey)).toString().trimmed();
    if (stored.isEmpty())
        return true;
    if (pathHasControlCharacters(stored))
        return fail(error, QStringLiteral("Settings data directory contains control characters"));
    QString resolved = QDir::fromNativeSeparators(stored);
    if (!QDir::isAbsolutePath(resolved))
        resolved = QDir(settings.executableDirectory).absoluteFilePath(resolved);
    resolved = QDir::cleanPath(resolved);
    if (!QDir::isAbsolutePath(resolved))
        return fail(error, QStringLiteral("Settings data directory is not absolute: %1").arg(stored));
    settings.dataDirectory = resolved;
    return true;
}

bool saveSettings(const Settings& settings, QString& error) {
    error.clear();
    if (settings.iniPath.isEmpty())
        return fail(error, QStringLiteral("Settings path is empty"));
    if (settings.dataDirectory.isEmpty())
        return fail(error, QStringLiteral("Data directory is empty"));
    if (pathHasControlCharacters(settings.dataDirectory))
        return fail(error, QStringLiteral("Data directory contains control characters"));
    const QString stored = QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(settings.dataDirectory).absoluteFilePath()));
    QString content = QStringLiteral("[%1]\ndataDirectory=%2\n")
                          .arg(QLatin1String(kIniGroup), stored);
    return writeFileAtomic(settings.iniPath, content.toUtf8(), error);
}

bool setDataDirectory(Settings& settings, const QString& directory, QString& error,
                      bool persist) {
    error.clear();
    if (directory.trimmed().isEmpty())
        return fail(error, QStringLiteral("--data-dir needs a directory"));
    if (pathHasControlCharacters(directory))
        return fail(error, QStringLiteral("Data directory contains control characters"));
    QString resolved = QDir::fromNativeSeparators(directory);
    if (!QDir::isAbsolutePath(resolved))
        return fail(error, QStringLiteral("Data directory must be an absolute path: %1").arg(directory));
    resolved = QDir::cleanPath(resolved);
    if (resolved.isEmpty())
        return fail(error, QStringLiteral("Data directory is empty"));
    settings.dataDirectory = resolved;
    if (persist && !saveSettings(settings, error))
        return false;
    return true;
}

bool ensureDataLayout(const Settings& settings, QString& error) {
    error.clear();
    if (settings.dataDirectory.isEmpty())
        return fail(error, QStringLiteral("Data directory is not configured"));
    const QFileInfo dataInfo(settings.dataDirectory);
    if (dataInfo.exists() && !dataInfo.isDir())
        return fail(error, QStringLiteral("Data directory is not a directory: %1")
                              .arg(settings.dataDirectory));
    if (!QDir().mkpath(settings.dataDirectory))
        return fail(error, QStringLiteral("Could not create the data directory: %1")
                              .arg(settings.dataDirectory));
    if (!QFileInfo(settings.dataDirectory).isWritable())
        return fail(error, QStringLiteral("Data directory is not writable: %1")
                              .arg(settings.dataDirectory));
    if (!QDir().mkpath(packageRoot(settings)))
        return fail(error, QStringLiteral("Could not create the package directory: %1")
                              .arg(packageRoot(settings)));
    return true;
}

QString registryPath(const Settings& settings) {
    return QDir(settings.dataDirectory).filePath(QStringLiteral("apps.json"));
}

QString packageRoot(const Settings& settings) {
    return QDir(settings.dataDirectory).filePath(QStringLiteral("pkg"));
}

QString emulatorsPath(const Settings& settings) {
    return QDir(settings.dataDirectory).filePath(QStringLiteral("emulators.json"));
}

QString collectionsPath(const Settings& settings) {
    return QDir(settings.dataDirectory).filePath(QStringLiteral("collections.json"));
}

QString platformMapPath(const Settings& settings) {
    return QDir(settings.dataDirectory).filePath(QStringLiteral("platform-map.json"));
}

QString platformMapSchema() {
    return QLatin1String(kPlatformMapSchema);
}

bool validateCollectionName(const QString& name, QString& error) {
    if (name.isEmpty())
        return fail(error, QStringLiteral("Collection name must not be empty"));
    if (name.size() > 64)
        return fail(error, QStringLiteral("Collection name must be at most 64 characters"));
    for (const QChar ch : name) {
        if (ch.unicode() < 0x20 || ch.unicode() == 0x7f)
            return fail(error,
                        QStringLiteral("Collection name must not contain control characters"));
    }
    return true;
}

QVector<PlatformMapping> defaultPlatformMappings() {
    static const struct {
        const char* platform;
        const char* emulatorId;
    } defaults[] = {
        {"PS3", "rpcs3"},
        {"PS1", "duckstation"},
        {"PS2", "pcsx2"},
        {"PSP", "ppsspp"},
        {"GameCube/Wii", "dolphin"},
        {"Switch", "ryujinx"},
        {"SNES", "retroarch"},
        {"NES", "retroarch"},
        {"N64", "retroarch"},
        {"Genesis", "retroarch"},
        {"GBA", "retroarch"},
        {"Game Boy", "retroarch"},
        {"Game Boy Color", "retroarch"},
        {"Saturn", "mednafen"},
        {"Dreamcast", "retroarch"},
        {"Arcade", "mame"},
        {"NDS", "melonds"},
    };
    QVector<PlatformMapping> mappings;
    mappings.reserve(int(sizeof(defaults) / sizeof(defaults[0])));
    for (const auto& entry : defaults) {
        PlatformMapping mapping;
        mapping.platform = QLatin1String(entry.platform);
        mapping.emulatorId = QLatin1String(entry.emulatorId);
        mappings.append(mapping);
    }
    return mappings;
}

const PlatformMapping* findPlatformMapping(const QVector<PlatformMapping>& mappings,
                                           const QString& platform) {
    const QString wanted = platform.trimmed();
    if (wanted.isEmpty())
        return nullptr;
    for (const PlatformMapping& mapping : mappings) {
        if (mapping.platform.compare(wanted, Qt::CaseInsensitive) == 0)
            return &mapping;
    }
    return nullptr;
}

bool isValidTag(const QString& tag, QString& error) {
    const QString trimmed = tag.trimmed();
    if (trimmed.isEmpty())
        return fail(error, QStringLiteral("A tag must not be empty"));
    if (trimmed.size() > kMaxTagLength)
        return fail(error, QStringLiteral("Tag '%1' is longer than %2 characters")
                              .arg(trimmed)
                              .arg(kMaxTagLength));
    for (const QChar ch : trimmed) {
        if (ch.unicode() < 0x20 || ch.unicode() == 0x7f)
            return fail(error, QStringLiteral("Tag '%1' contains control characters").arg(trimmed));
    }
    return true;
}

QStringList normalizeTags(const QStringList& input, QString& error) {
    error.clear();
    QStringList normalized;
    QSet<QString> seen;
    for (const QString& raw : input) {
        QString tagError;
        if (!isValidTag(raw, tagError)) {
            error = tagError;
            return QStringList();
        }
        const QString tag = raw.trimmed();
        const QString key = tag.toCaseFolded();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        normalized.append(tag);
    }
    return normalized;
}

QString slugify(const QString& text, int maxLength) {
    QString out;
    out.reserve(text.size());
    bool lastDash = false;
    for (const QChar ch : text) {
        const QChar lower = ch.toLower();
        const ushort code = lower.unicode();
        const bool keep = (code >= 'a' && code <= 'z') || (code >= '0' && code <= '9');
        if (keep) {
            out.append(lower);
            lastDash = false;
        } else if (!out.isEmpty() && !lastDash) {
            out.append(QLatin1Char('-'));
            lastDash = true;
        }
    }
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    const int limit = std::max(1, maxLength);
    if (out.size() > limit)
        out.truncate(limit);
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    if (out.isEmpty())
        out = QStringLiteral("game");
    return out;
}

QString packageIdFor(const QString& romPath, const QString& platform) {
    QString canonical = canonicalOrClean(romPath);
#ifdef Q_OS_WIN
    canonical = canonical.toLower();
#endif
    quint64 hash = 1469598103934665603ULL;
    const QByteArray bytes = canonical.toUtf8();
    for (const char ch : bytes) {
        hash ^= static_cast<quint64>(static_cast<unsigned char>(ch));
        hash *= 1099511628211ULL;
    }
    const QString suffix = QStringLiteral("%1").arg(hash, 16, 16, QLatin1Char('0')).left(16);
    return QStringLiteral("pkg-%1-%2").arg(slugify(platform, 16), suffix);
}

QString timestampUtc() {
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'"));
}

bool isSafePackageId(const QString& id) {
    if (id.isEmpty() || id.size() > 80 || id == QStringLiteral(".") || id == QStringLiteral(".."))
        return false;
    for (const QChar ch : id) {
        const ushort c = ch.unicode();
        const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        if (!allowed)
            return false;
    }
    return true;
}

bool isPathInside(const QString& root, const QString& candidate, QString& resolved) {
    resolved.clear();
    if (root.trimmed().isEmpty() || candidate.trimmed().isEmpty())
        return false;
    const QString rootPath = canonicalOrClean(root);
    const QString candidatePath = canonicalOrClean(candidate);
    QString rootWithSlash = rootPath;
    if (!rootWithSlash.endsWith(QLatin1Char('/')))
        rootWithSlash.append(QLatin1Char('/'));
    const Qt::CaseSensitivity sensitivity =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    if (!candidatePath.startsWith(rootWithSlash, sensitivity))
        return false;
    resolved = candidatePath;
    return true;
}

bool writeFileAtomic(const QString& path, const QByteArray& bytes, QString& error) {
    error.clear();
    if (path.trimmed().isEmpty())
        return fail(error, QStringLiteral("Write path is empty"));
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return fail(error, QStringLiteral("Could not create directory '%1'").arg(info.absolutePath()));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, QStringLiteral("Could not open '%1' for writing: %2")
                              .arg(path, file.errorString()));
    if (file.write(bytes) != bytes.size()) {
        const QString message = file.errorString();
        file.cancelWriting();
        return fail(error, QStringLiteral("Could not write '%1': %2").arg(path, message));
    }
    if (!file.commit())
        return fail(error, QStringLiteral("Could not atomically replace '%1': %2")
                              .arg(path, file.errorString()));
    return true;
}

bool writeJsonFileAtomic(const QString& path, const QJsonObject& object, QString& error) {
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (bytes.size() > kMaxJsonBytes)
        return fail(error, QStringLiteral("JSON document exceeds the %1 byte limit")
                              .arg(kMaxJsonBytes));
    return writeFileAtomic(path, bytes, error);
}

bool readFileLimited(const QString& path, qint64 limit, QByteArray& bytes, QString& error) {
    error.clear();
    bytes.clear();
    QFile file(path);
    if (!file.exists())
        return fail(error, QStringLiteral("File does not exist: %1").arg(path));
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Could not open '%1': %2").arg(path, file.errorString()));
    if (file.size() > limit)
        return fail(error, QStringLiteral("File '%1' exceeds the %2 byte limit").arg(path).arg(limit));
    bytes = file.read(limit + 1);
    if (bytes.size() > limit)
        return fail(error, QStringLiteral("File '%1' exceeds the %2 byte limit").arg(path).arg(limit));
    return true;
}

bool readJsonFile(const QString& path, QJsonObject& object, QString& error, qint64 limit) {
    error.clear();
    object = QJsonObject();
    QByteArray bytes;
    if (!readFileLimited(path, limit, bytes, error))
        return false;
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("Invalid JSON in '%1': %2")
                              .arg(path, parseError.errorString()));
    object = document.object();
    return true;
}

bool loadPlatformMap(const Settings& settings, QVector<PlatformMapping>& mappings,
                     QString& error) {
    error.clear();
    mappings = defaultPlatformMappings();
    const QString path = platformMapPath(settings);
    if (!QFileInfo::exists(path))
        return true;
    QJsonObject root;
    if (!readJsonFile(path, root, error))
        return false;
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.key() != QStringLiteral("version") && it.key() != QStringLiteral("schema")
            && it.key() != QStringLiteral("mappings"))
            return fail(error, QStringLiteral("platform-map.json has unknown field '%1'")
                                  .arg(it.key()));
    }
    const auto version = root.constFind(QStringLiteral("version"));
    if (version == root.constEnd() || !version->isDouble() || version->toDouble() != 1.0)
        return fail(error, QStringLiteral("platform-map.json version must be the number 1"));
    if (root.contains(QStringLiteral("schema"))) {
        const QString schema = root.value(QStringLiteral("schema")).toString();
        if (!schema.isEmpty() && schema != QLatin1String(kPlatformMapSchema))
            return fail(error, QStringLiteral("platform-map.json schema must be '%1'")
                                  .arg(QLatin1String(kPlatformMapSchema)));
    }
    const auto list = root.constFind(QStringLiteral("mappings"));
    if (list == root.constEnd() || !list->isArray())
        return fail(error, QStringLiteral("platform-map.json field 'mappings' must be an array"));
    const QJsonArray array = list->toArray();
    if (array.size() > 256)
        return fail(error, QStringLiteral("platform-map.json exceeds the 256-entry limit"));
    QSet<QString> seen;
    for (qsizetype index = 0; index < array.size(); ++index) {
        if (!array.at(index).isObject())
            return fail(error, QStringLiteral("mappings[%1] must be an object").arg(index));
        const QJsonObject item = array.at(index).toObject();
        for (auto it = item.begin(); it != item.end(); ++it) {
            if (it.key() != QStringLiteral("platform") && it.key() != QStringLiteral("emulatorId")
                && it.key() != QStringLiteral("arguments"))
                return fail(error, QStringLiteral("mappings[%1] has unknown field '%2'")
                                      .arg(index).arg(it.key()));
        }
        PlatformMapping mapping;
        const auto platformValue = item.constFind(QStringLiteral("platform"));
        if (platformValue == item.constEnd() || !platformValue->isString())
            return fail(error, QStringLiteral("mappings[%1].platform must be a string").arg(index));
        mapping.platform = platformValue->toString().trimmed();
        if (mapping.platform.isEmpty() || mapping.platform.size() > 64
            || pathHasControlCharacters(mapping.platform))
            return fail(error, QStringLiteral("mappings[%1].platform is not a valid platform id")
                                  .arg(index));
        const auto emulatorValue = item.constFind(QStringLiteral("emulatorId"));
        if (emulatorValue == item.constEnd() || !emulatorValue->isString())
            return fail(error, QStringLiteral("mappings[%1].emulatorId must be a string").arg(index));
        mapping.emulatorId = emulatorValue->toString().trimmed();
        if (!validMappingId(mapping.emulatorId))
            return fail(error, QStringLiteral("mappings[%1].emulatorId '%2' is not a valid "
                                              "emulator id")
                                  .arg(index).arg(mapping.emulatorId));
        const auto argumentsValue = item.constFind(QStringLiteral("arguments"));
        if (argumentsValue != item.constEnd()) {
            if (!argumentsValue->isArray())
                return fail(error, QStringLiteral("mappings[%1].arguments must be an array")
                                      .arg(index));
            const QJsonArray arguments = argumentsValue->toArray();
            if (arguments.size() > 64)
                return fail(error, QStringLiteral("mappings[%1].arguments exceeds the 64-element "
                                                  "limit")
                                      .arg(index));
            for (const QJsonValue& value : arguments) {
                if (!value.isString())
                    return fail(error, QStringLiteral("mappings[%1].arguments must be strings")
                                          .arg(index));
                const QString argument = value.toString();
                if (argument.contains(QChar::Null) || argument.size() > 4096)
                    return fail(error, QStringLiteral("mappings[%1].arguments has an invalid entry")
                                          .arg(index));
                mapping.arguments.append(argument);
            }
        }
        const QString key = mapping.platform.toCaseFolded();
        if (seen.contains(key))
            return fail(error, QStringLiteral("Duplicate platform mapping '%1'")
                                  .arg(mapping.platform));
        seen.insert(key);
        bool replaced = false;
        for (PlatformMapping& existing : mappings) {
            if (existing.platform.compare(mapping.platform, Qt::CaseInsensitive) == 0) {
                existing = mapping;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            mappings.append(mapping);
    }
    return true;
}

bool savePlatformMap(const Settings& settings, const QVector<PlatformMapping>& mappings,
                     QString& error) {
    error.clear();
    QVector<PlatformMapping> sorted = mappings;
    std::sort(sorted.begin(), sorted.end(), [](const PlatformMapping& a, const PlatformMapping& b) {
        return a.platform.compare(b.platform, Qt::CaseInsensitive) < 0;
    });
    QSet<QString> seen;
    QJsonArray array;
    for (const PlatformMapping& mapping : sorted) {
        if (mapping.platform.trimmed().isEmpty() || mapping.platform.size() > 64
            || pathHasControlCharacters(mapping.platform))
            return fail(error, QStringLiteral("Mapped platform '%1' is not valid")
                                  .arg(mapping.platform));
        if (!validMappingId(mapping.emulatorId))
            return fail(error, QStringLiteral("Mapped emulator id '%1' is not valid")
                                  .arg(mapping.emulatorId));
        const QString key = mapping.platform.trimmed().toCaseFolded();
        if (seen.contains(key))
            return fail(error, QStringLiteral("Duplicate platform mapping '%1'")
                                  .arg(mapping.platform));
        seen.insert(key);
        for (const QString& argument : mapping.arguments) {
            if (argument.contains(QChar::Null) || argument.size() > 4096)
                return fail(error, QStringLiteral("Mapping for '%1' has an invalid argument")
                                      .arg(mapping.platform));
        }
        array.append(mappingToJson(mapping));
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("schema"), QLatin1String(kPlatformMapSchema));
    root.insert(QStringLiteral("mappings"), array);
    return writeJsonFileAtomic(platformMapPath(settings), root, error);
}

bool readCollections(const Settings& settings, QHash<QString, QStringList>& collections,
                     QString& error) {
    error.clear();
    collections.clear();
    const QString path = collectionsPath(settings);
    if (!QFileInfo::exists(path))
        return true;
    QJsonObject root;
    if (!readJsonFile(path, root, error))
        return false;
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.key() != QStringLiteral("version") && it.key() != QStringLiteral("schema")
            && it.key() != QStringLiteral("collections"))
            return fail(error, QStringLiteral("collections.json has unknown field '%1'").arg(it.key()));
    }
    const auto version = root.constFind(QStringLiteral("version"));
    if (version == root.constEnd() || !version->isDouble() || version->toDouble() != 1.0)
        return fail(error, QStringLiteral("collections.json version must be the number 1"));
    if (root.contains(QStringLiteral("schema"))) {
        const QString schema = root.value(QStringLiteral("schema")).toString();
        if (!schema.isEmpty() && schema != QLatin1String(kCollectionsSchema))
            return fail(error, QStringLiteral("collections.json schema must be '%1'")
                                  .arg(QLatin1String(kCollectionsSchema)));
    }
    const auto list = root.constFind(QStringLiteral("collections"));
    if (list == root.constEnd() || !list->isArray())
        return fail(error, QStringLiteral("collections.json field 'collections' must be an array"));
    const QJsonArray array = list->toArray();
    for (qsizetype index = 0; index < array.size(); ++index) {
        if (!array.at(index).isObject())
            return fail(error, QStringLiteral("collections[%1] must be an object").arg(index));
        const QJsonObject item = array.at(index).toObject();
        for (auto it = item.begin(); it != item.end(); ++it) {
            if (it.key() != QStringLiteral("name") && it.key() != QStringLiteral("ids"))
                return fail(error, QStringLiteral("collections[%1] has unknown field '%2'")
                                      .arg(index).arg(it.key()));
        }
        const QString name = item.value(QStringLiteral("name")).toString().trimmed();
        QString nameError;
        if (!validateCollectionName(name, nameError))
            return fail(error, QStringLiteral("collections[%1]: %2").arg(index).arg(nameError));
        if (collections.contains(name))
            return fail(error, QStringLiteral("Duplicate collection name '%1'").arg(name));
        const QJsonValue idsValue = item.value(QStringLiteral("ids"));
        if (!idsValue.isArray())
            return fail(error, QStringLiteral("collections[%1].ids must be an array").arg(index));
        QStringList ids;
        for (const QJsonValue& value : idsValue.toArray()) {
            if (!value.isString())
                return fail(error, QStringLiteral("collections[%1].ids must contain strings").arg(index));
            const QString id = value.toString();
            if (!isSafePackageId(id))
                return fail(error, QStringLiteral("collections[%1] has an unsafe app id '%2'")
                                      .arg(index).arg(id));
            if (!ids.contains(id))
                ids.append(id);
        }
        collections.insert(name, ids);
    }
    return true;
}

bool writeCollections(const Settings& settings, const QHash<QString, QStringList>& collections,
                      QString& error) {
    QStringList names = collections.keys();
    names.sort();
    QJsonArray array;
    for (const QString& name : names) {
        QString nameError;
        if (!validateCollectionName(name, nameError))
            return fail(error, nameError);
        QStringList ids = collections.value(name);
        ids.sort();
        QJsonArray idArray;
        for (const QString& id : ids) {
            if (!isSafePackageId(id))
                return fail(error, QStringLiteral("Refusing to write unsafe app id '%1'").arg(id));
            idArray.append(id);
        }
        array.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("ids"), idArray}});
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("schema"), QLatin1String(kCollectionsSchema));
    root.insert(QStringLiteral("collections"), array);
    return writeJsonFileAtomic(collectionsPath(settings), root, error);
}

bool assignToCollection(const Settings& settings, const QString& appId,
                        const QString& collection, QString& error) {
    error.clear();
    if (!isSafePackageId(appId))
        return fail(error, QStringLiteral("Unsafe app id '%1'").arg(appId));
    const QString name = collection.trimmed();
    QString nameError;
    if (!validateCollectionName(name, nameError))
        return fail(error, nameError);
    QHash<QString, QStringList> collections;
    if (!readCollections(settings, collections, error))
        return false;
    QStringList ids = collections.value(name);
    if (ids.contains(appId))
        return true;
    ids.append(appId);
    collections.insert(name, ids);
    return writeCollections(settings, collections, error);
}

bool removeFromCollections(const Settings& settings, const QString& appId, QString& error) {
    error.clear();
    QHash<QString, QStringList> collections;
    if (!readCollections(settings, collections, error))
        return false;
    bool changed = false;
    for (auto it = collections.begin(); it != collections.end();) {
        if (it.value().removeAll(appId) > 0)
            changed = true;
        if (it.value().isEmpty()) {
            it = collections.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (!changed)
        return true;
    return writeCollections(settings, collections, error);
}

} // namespace xmb::manager

