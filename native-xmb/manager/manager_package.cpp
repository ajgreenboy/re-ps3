#include "manager_package.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLinearGradient>
#include <QPainter>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>

namespace xmb::manager {
namespace {

constexpr const char* kVirtualPackageSchema = "xmb-virtual-pkg/1";
constexpr const char* kPkgJsonName = "pkg.json";
constexpr const char* kIconName = "ICON0.PNG";
constexpr const char* kSfoName = "PARAM.SFO";
constexpr int kMaxIconWidth = 512;
constexpr int kMaxIconHeight = 288;
constexpr int kMaxTitleBytes = 127;

bool fail(QString& error, const QString& message) {
    error = message;
    return false;
}

QString canonical(const QString& path) {
    const QFileInfo info(path);
    const QString resolved = info.canonicalFilePath();
    if (!resolved.isEmpty())
        return QDir::cleanPath(resolved);
    return QDir::cleanPath(info.absoluteFilePath());
}

QString pathKey(const QString& path) {
    QString key = canonical(path);
#ifdef Q_OS_WIN
    key = key.toLower();
#endif
    return key;
}

bool samePath(const QString& a, const QString& b) {
    return !a.isEmpty() && !b.isEmpty() && pathKey(a) == pathKey(b);
}

QString newSiblingName(const QString& root, const QString& prefix) {
    return QDir(root).filePath(prefix
                               + QUuid::createUuid().toString(QUuid::WithoutBraces));
}

bool removeDirectoryInside(const QString& root, const QString& directory, QString& error) {
    QString resolved;
    if (!isPathInside(root, directory, resolved))
        return fail(error, QStringLiteral("Refusing to remove a path outside the package root: %1")
                              .arg(directory));
    if (!QFileInfo::exists(resolved))
        return true;
    if (!QFileInfo(resolved).isDir())
        return fail(error, QStringLiteral("Refusing to remove a non-directory: %1").arg(resolved));
    if (!QDir(resolved).removeRecursively())
        return fail(error, QStringLiteral("Could not remove directory '%1'").arg(resolved));
    return true;
}

bool readStringField(const QJsonObject& object, const QString& key, QString& output,
                     bool required, const QString& where, QString& error) {
    const auto value = object.constFind(key);
    if (value == object.constEnd()) {
        if (required)
            return fail(error, QStringLiteral("%1 is missing '%2'").arg(where, key));
        output.clear();
        return true;
    }
    if (!value->isString())
        return fail(error, QStringLiteral("%1 field '%2' must be a string").arg(where, key));
    output = value->toString();
    if (output.contains(QChar::Null))
        return fail(error, QStringLiteral("%1 field '%2' must not contain NUL").arg(where, key));
    return true;
}

bool writeIcon(const QString& artPath, const QString& title, const QString& platform,
               const QString& outputPath, QString& error) {
    QImage image;
    if (!artPath.isEmpty()) {
        QImageReader reader(artPath);
        reader.setDecideFormatFromContent(true);
        reader.setAutoTransform(true);
        image = reader.read();
        if (image.isNull())
            logWarning(QStringLiteral("Artwork could not be decoded (%1); using a generated icon")
                           .arg(reader.errorString()));
    }
    if (!image.isNull()) {
        if (image.width() > kMaxIconWidth || image.height() > kMaxIconHeight) {
            image = image.scaled(kMaxIconWidth, kMaxIconHeight, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
        }
        image = image.convertToFormat(QImage::Format_ARGB32);
    } else {
        image = QImage(kMaxIconWidth, kMaxIconHeight, QImage::Format_ARGB32);
        if (image.isNull())
            return fail(error, QStringLiteral("Could not allocate the package icon"));
        image.fill(QColor(10, 14, 21));
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QLinearGradient gradient(0, 0, image.width(), image.height());
        gradient.setColorAt(0.0, QColor(29, 42, 62));
        gradient.setColorAt(1.0, QColor(9, 12, 18));
        painter.fillRect(image.rect(), gradient);
        painter.fillRect(QRect(28, 36, 6, image.height() - 72), QColor(76, 137, 205));
        painter.setPen(QColor(233, 240, 250));
        QFont titleFont(QStringLiteral("Segoe UI"), 26, QFont::Bold);
        painter.setFont(titleFont);
        painter.drawText(QRect(56, 70, image.width() - 100, image.height() - 150),
                         Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, title);
        painter.setPen(QColor(125, 163, 211));
        QFont labelFont(QStringLiteral("Segoe UI"), 12);
        painter.setFont(labelFont);
        painter.drawText(QRect(56, 34, image.width() - 100, 28),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("%1  |  XMB MANAGER").arg(platform.toUpper()));
        painter.end();
    }
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly))
        return fail(error, QStringLiteral("Could not open the icon buffer"));
    if (!image.save(&buffer, "PNG"))
        return fail(error, QStringLiteral("Could not encode the package icon as PNG"));
    return writeFileAtomic(outputPath, png, error);
}

QString limitedTitle(const QString& title) {
    QString limited = title.trimmed();
    if (limited.isEmpty())
        limited = QStringLiteral("Untitled");
    while (!limited.isEmpty() && limited.toUtf8().size() > kMaxTitleBytes)
        limited.chop(1);
    if (limited.isEmpty())
        limited = QStringLiteral("Untitled");
    return limited;
}

bool writePackageMetadata(const QString& staging, const QString& finalDirectory,
                          const PackageInput& input, const QString& id, const QString& title,
                          const QString& platform, const QString& romPath,
                          const QString& emulatorId, const QStringList& arguments,
                          const QStringList& tags, const RomFingerprint& fingerprint,
                          QString& error) {
    QHash<QString, QString> sfo;
    sfo.insert(QStringLiteral("TITLE"), limitedTitle(title));
    sfo.insert(QStringLiteral("CATEGORY"), QStringLiteral("HG"));
    sfo.insert(QStringLiteral("APP_VER"), QStringLiteral("01.00"));
    sfo.insert(QStringLiteral("ICON0"), QLatin1String(kIconName));
    QByteArray sfoBytes;
    if (!writeParamSfo(sfo, sfoBytes, error))
        return false;
    if (!writeFileAtomic(QDir(staging).filePath(QLatin1String(kSfoName)), sfoBytes, error))
        return false;

    QJsonObject record;
    record.insert(QStringLiteral("schema"), QLatin1String(kVirtualPackageSchema));
    record.insert(QStringLiteral("id"), id);
    record.insert(QStringLiteral("title"), title);
    record.insert(QStringLiteral("platform"), platform);
    record.insert(QStringLiteral("romPath"), romPath);
    record.insert(QStringLiteral("romKind"), input.game.romKind);
    record.insert(QStringLiteral("emulatorId"), emulatorId);
    QJsonArray argumentArray;
    for (const QString& argument : arguments)
        argumentArray.append(argument);
    record.insert(QStringLiteral("arguments"), argumentArray);
    record.insert(QStringLiteral("collection"), input.collection.trimmed());
    record.insert(QStringLiteral("art"), QDir(finalDirectory).filePath(QLatin1String(kIconName)));
    record.insert(QStringLiteral("createdAt"), timestampUtc());
    record.insert(QStringLiteral("sourceNote"), input.game.sourceNote);
    QJsonArray tagArray;
    for (const QString& tag : tags)
        tagArray.append(tag);
    record.insert(QStringLiteral("tags"), tagArray);
    record.insert(QStringLiteral("romSize"), double(fingerprint.size));
    record.insert(QStringLiteral("romMtime"), fingerprint.mtime);
    record.insert(QStringLiteral("romHash"), fingerprint.hashValue);
    record.insert(QStringLiteral("romHashLabel"), fingerprint.hashLabel);
    return writeJsonFileAtomic(QDir(staging).filePath(QLatin1String(kPkgJsonName)), record,
                               error);
}

bool loadRegistry(const Settings& settings, QVector<ContentApp>& apps, QString& error) {
    return ContentRegistry::load(registryPath(settings), apps, error);
}

bool loadPackageRecords(const Settings& settings, QVector<PackageRecord>& records,
                        QString& error) {
    error.clear();
    records.clear();
    const QString root = packageRoot(settings);
    if (!QFileInfo(root).isDir())
        return true;
    const QFileInfoList directories = QDir(root).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& directory : directories) {
        if (directory.fileName().startsWith(QLatin1Char('.')))
            continue;
        if (!QFileInfo(QDir(directory.absoluteFilePath()).filePath(QLatin1String(kPkgJsonName)))
                 .isFile())
            continue;
        PackageRecord record;
        QString recordError;
        if (!readPackageRecord(directory.absoluteFilePath(), record, recordError)) {
            record = PackageRecord();
            record.id = directory.fileName();
            record.title = directory.fileName();
            record.packagePath = QDir::cleanPath(directory.absoluteFilePath());
            record.packagePresent = true;
            record.issue = recordError;
            logWarning(QStringLiteral("Package '%1' is unreadable: %2")
                           .arg(directory.fileName(), recordError));
        }
        records.append(record);
    }
    return true;
}

bool managerOwnedApp(const Settings& settings, const ContentApp& app) {
    if (app.id.startsWith(QStringLiteral("pkg-")))
        return true;
    if (app.artwork.isEmpty())
        return false;
    QString resolved;
    return isPathInside(packageRoot(settings), app.artwork, resolved);
}

} // namespace

QString virtualPackageSchema() {
    return QLatin1String(kVirtualPackageSchema);
}

QString packageDirectoryFor(const Settings& settings, const QString& appId) {
    if (!isSafePackageId(appId))
        return QString();
    return QDir(packageRoot(settings)).filePath(appId);
}

bool readPackageRecord(const QString& packageDirectory, PackageRecord& record, QString& error) {
    error.clear();
    record = PackageRecord();
    const QFileInfo directoryInfo(packageDirectory);
    if (!directoryInfo.isDir())
        return fail(error, QStringLiteral("Package directory does not exist: %1")
                              .arg(packageDirectory));
    const QString path = QDir(packageDirectory).filePath(QLatin1String(kPkgJsonName));
    QJsonObject object;
    if (!readJsonFile(path, object, error))
        return false;
    static const QStringList known = {
        QStringLiteral("schema"), QStringLiteral("id"), QStringLiteral("title"),
        QStringLiteral("platform"), QStringLiteral("romPath"), QStringLiteral("romKind"),
        QStringLiteral("emulatorId"), QStringLiteral("arguments"),
        QStringLiteral("collection"), QStringLiteral("art"), QStringLiteral("createdAt"),
        QStringLiteral("sourceNote"), QStringLiteral("tags"), QStringLiteral("romSize"),
        QStringLiteral("romMtime"), QStringLiteral("romHash"), QStringLiteral("romHashLabel")
    };
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!known.contains(it.key()))
            return fail(error, QStringLiteral("pkg.json has unknown field '%1'").arg(it.key()));
    }
    const QString where = QStringLiteral("pkg.json");
    QString schema;
    if (!readStringField(object, QStringLiteral("schema"), schema, true, where, error))
        return false;
    if (schema != QLatin1String(kVirtualPackageSchema))
        return fail(error, QStringLiteral("pkg.json schema must be '%1'")
                              .arg(QLatin1String(kVirtualPackageSchema)));
    if (!readStringField(object, QStringLiteral("id"), record.id, true, where, error)
        || !readStringField(object, QStringLiteral("title"), record.title, true, where, error)
        || !readStringField(object, QStringLiteral("platform"), record.platform, true, where, error)
        || !readStringField(object, QStringLiteral("romPath"), record.romPath, true, where, error)
        || !readStringField(object, QStringLiteral("romKind"), record.romKind, true, where, error)
        || !readStringField(object, QStringLiteral("emulatorId"), record.emulatorId, true, where,
                            error)
        || !readStringField(object, QStringLiteral("collection"), record.collection, false, where,
                            error)
        || !readStringField(object, QStringLiteral("art"), record.art, false, where, error)
        || !readStringField(object, QStringLiteral("createdAt"), record.createdAt, true, where,
                            error)
        || !readStringField(object, QStringLiteral("sourceNote"), record.sourceNote, false, where,
                            error))
        return false;
    if (!isSafePackageId(record.id))
        return fail(error, QStringLiteral("pkg.json id '%1' is not a safe package id")
                              .arg(record.id));
    if (record.id != directoryInfo.fileName())
        return fail(error, QStringLiteral("pkg.json id '%1' does not match its directory '%2'")
                              .arg(record.id, directoryInfo.fileName()));
    if (record.title.trimmed().isEmpty())
        return fail(error, QStringLiteral("pkg.json title must not be empty"));
    if (record.platform.trimmed().isEmpty())
        return fail(error, QStringLiteral("pkg.json platform must not be empty"));
    if (record.emulatorId.trimmed().isEmpty())
        return fail(error, QStringLiteral("pkg.json emulatorId must not be empty"));
    if (!QDir::isAbsolutePath(record.romPath))
        return fail(error, QStringLiteral("pkg.json romPath must be absolute"));
    if (!record.art.isEmpty() && !QDir::isAbsolutePath(record.art))
        return fail(error, QStringLiteral("pkg.json art must be absolute"));
    const auto arguments = object.constFind(QStringLiteral("arguments"));
    if (arguments == object.constEnd() || !arguments->isArray())
        return fail(error, QStringLiteral("pkg.json field 'arguments' must be an array"));
    for (const QJsonValue& value : arguments->toArray()) {
        if (!value.isString())
            return fail(error, QStringLiteral("pkg.json arguments must be strings"));
        const QString argument = value.toString();
        if (argument.contains(QChar::Null))
            return fail(error, QStringLiteral("pkg.json arguments must not contain NUL"));
        record.arguments.append(argument);
    }
    const auto tagsValue = object.constFind(QStringLiteral("tags"));
    record.tags.clear();
    if (tagsValue != object.constEnd()) {
        if (!tagsValue->isArray())
            return fail(error, QStringLiteral("pkg.json field 'tags' must be an array"));
        QStringList rawTags;
        for (const QJsonValue& value : tagsValue->toArray()) {
            if (!value.isString())
                return fail(error, QStringLiteral("pkg.json tags must be strings"));
            rawTags.append(value.toString());
        }
        QString tagError;
        record.tags = normalizeTags(rawTags, tagError);
        if (!tagError.isEmpty())
            return fail(error, QStringLiteral("pkg.json tags are invalid: %1").arg(tagError));
    }
    const auto sizeValue = object.constFind(QStringLiteral("romSize"));
    record.romSize = 0;
    if (sizeValue != object.constEnd()) {
        if (!sizeValue->isDouble() || sizeValue->toDouble() < 0)
            return fail(error, QStringLiteral("pkg.json field 'romSize' must be a non-negative "
                                              "number"));
        record.romSize = static_cast<qint64>(sizeValue->toDouble());
    }
    const auto mtimeValue = object.constFind(QStringLiteral("romMtime"));
    record.romMtime.clear();
    if (mtimeValue != object.constEnd()) {
        if (!mtimeValue->isString())
            return fail(error, QStringLiteral("pkg.json field 'romMtime' must be a string"));
        record.romMtime = mtimeValue->toString();
        if (record.romMtime.contains(QChar::Null))
            return fail(error, QStringLiteral("pkg.json field 'romMtime' must not contain NUL"));
    }
    const auto hashValue = object.constFind(QStringLiteral("romHash"));
    record.hashValue.clear();
    if (hashValue != object.constEnd()) {
        if (!hashValue->isString())
            return fail(error, QStringLiteral("pkg.json field 'romHash' must be a string"));
        record.hashValue = hashValue->toString();
        for (const QChar ch : record.hashValue) {
            const ushort code = ch.unicode();
            if (!((code >= '0' && code <= '9') || (code >= 'a' && code <= 'f')
                  || (code >= 'A' && code <= 'F')))
                return fail(error, QStringLiteral("pkg.json field 'romHash' must be hexadecimal"));
        }
    }
    const auto labelValue = object.constFind(QStringLiteral("romHashLabel"));
    record.hashLabel.clear();
    if (labelValue != object.constEnd()) {
        if (!labelValue->isString())
            return fail(error, QStringLiteral("pkg.json field 'romHashLabel' must be a string"));
        record.hashLabel = labelValue->toString();
        if (record.hashLabel != fullHashLabel() && record.hashLabel != partialHashLabel()
            && record.hashLabel != folderHashLabel())
            return fail(error, QStringLiteral("pkg.json field 'romHashLabel' must be '%1', '%2' "
                                              "or '%3'")
                                  .arg(fullHashLabel(), partialHashLabel(), folderHashLabel()));
    }
    if (record.hashValue.isEmpty() != record.hashLabel.isEmpty())
        return fail(error, QStringLiteral("pkg.json must record both romHash and romHashLabel, "
                                          "or neither"));
    record.packagePath = QDir::cleanPath(directoryInfo.absoluteFilePath());
    record.packagePresent = true;
    return true;
}

bool createPackage(Settings& settings, const PackageInput& input, EmulatorProfile profile,
                   ContentApp* outApp, QString& error) {
    error.clear();
    if (outApp)
        *outApp = ContentApp();
    if (!ensureDataLayout(settings, error))
        return false;
    const QString requestedEmulator = input.emulatorId.trimmed();    if (requestedEmulator.isEmpty())
        return fail(error, QStringLiteral("An emulator id is required"));
    if (profile.id.compare(requestedEmulator, Qt::CaseInsensitive) != 0)
        return fail(error, QStringLiteral("Emulator profile '%1' does not match the requested "
                                          "id '%2'").arg(profile.id, requestedEmulator));
    QString romPath = canonical(input.game.romPath);
    if (romPath.isEmpty())
        return fail(error, QStringLiteral("The ROM path is empty"));
    const QFileInfo romInfo(romPath);
    if (!romInfo.exists())
        return fail(error, QStringLiteral("The ROM does not exist: %1").arg(input.game.romPath));
    QString platform = input.game.platform.trimmed();
    if (platform.isEmpty())
        platform = platformForPath(romPath);
    if (platform.isEmpty())
        return fail(error, QStringLiteral("Cannot determine the platform of '%1'").arg(romPath));
    if (!input.emulatorFromMapping && !profile.platforms.isEmpty()
        && !profile.platforms.contains(platform, Qt::CaseInsensitive))
        return fail(error, QStringLiteral("Emulator '%1' does not list platform '%2' "
                                          "(platforms: %3)")
                              .arg(profile.id, platform, profile.platforms.join(QStringLiteral(", "))));
    const QString executable = profile.executable.trimmed();
    if (executable.isEmpty())
        return fail(error, QStringLiteral("Emulator '%1' has no executable configured; run "
                                          "--detect-emulators or edit emulators.json")
                              .arg(profile.id));
    if (!QDir::isAbsolutePath(executable))
        return fail(error, QStringLiteral("Emulator '%1' executable is not absolute: %2")
                              .arg(profile.id, executable));
    const QFileInfo executableInfo(executable);
    if (!executableInfo.isFile())
        return fail(error, QStringLiteral("Emulator '%1' executable does not exist: %2")
                              .arg(profile.id, executable));
#ifdef Q_OS_WIN
    if (executableInfo.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0)
        return fail(error, QStringLiteral("Emulator '%1' executable must end in .exe: %2")
                              .arg(profile.id, executable));
#endif
    QString workingDirectory = profile.workingDirectory.trimmed();
    if (workingDirectory.isEmpty())
        workingDirectory = executableInfo.absolutePath();
    if (!QDir::isAbsolutePath(workingDirectory))
        return fail(error, QStringLiteral("Emulator '%1' working directory is not absolute: %2")
                              .arg(profile.id, workingDirectory));
    if (!QFileInfo(workingDirectory).isDir())
        return fail(error, QStringLiteral("Emulator '%1' working directory does not exist: %2")
                              .arg(profile.id, workingDirectory));

    QString title = input.game.title.trimmed();
    if (title.isEmpty())
        title = titleFromFileName(romInfo.fileName());
    if (title.isEmpty())
        title = QStringLiteral("Untitled");

    const QString id = packageIdFor(romPath, platform);
    if (!isSafePackageId(id))
        return fail(error, QStringLiteral("Could not derive a safe package id"));

    QString tagError;
    QStringList tags = normalizeTags(input.tags, tagError);
    if (!tagError.isEmpty())
        return fail(error, tagError);

    RomFingerprint fingerprint;
    if (!computeRomFingerprint(romPath, fingerprint, error))
        return false;

    const QString keyValuesCore = defaultCoreFor(profile.id, platform);
    QStringList keyValues = {
        QStringLiteral("rom=%1").arg(romPath),
        QStringLiteral("title=%1").arg(title),
        QStringLiteral("id=%1").arg(id),
        QStringLiteral("dir=%1").arg(QFileInfo(romPath).absolutePath()),
        QStringLiteral("core=%1").arg(keyValuesCore),
    };
    QStringList arguments;
    if (input.argumentsOverride.isEmpty()) {
        arguments = expandArguments(profile, keyValues, error);
        if (!error.isEmpty())
            return false;
    } else {
        arguments = expandArgumentList(input.argumentsOverride, keyValues, error);
        if (!error.isEmpty())
            return false;
    }
    for (const QString& extra : input.extraArgs) {
        if (extra.contains(QChar::Null))
            return fail(error, QStringLiteral("Extra arguments must not contain NUL"));
        arguments.append(extra);
    }

    QVector<PackageRecord> records;
    if (!loadPackageRecords(settings, records, error))
        return false;
    QStringList preservedTags;
    for (const PackageRecord& record : records) {
        if (!samePath(record.romPath, romPath))
            continue;
        if (!input.replace) {
            return fail(error, QStringLiteral("This ROM is already imported as '%1' "
                                              "(%2). Use --replace to update it.")
                                  .arg(record.id, record.title));
        }
        if (record.id != id) {
            return fail(error, QStringLiteral("This ROM is already imported as '%1' for another "
                                              "platform; remove that package first")
                                  .arg(record.id));
        }
        if (input.tags.isEmpty())
            preservedTags = record.tags;
    }
    if (tags.isEmpty() && !preservedTags.isEmpty())
        tags = preservedTags;
    QVector<ContentApp> apps;
    if (!loadRegistry(settings, apps, error))
        return false;
    const bool appIdExists = std::any_of(apps.cbegin(), apps.cend(),
                                         [&id](const ContentApp& app) { return app.id == id; });
    const bool packageRecordExists = std::any_of(records.cbegin(), records.cend(),
                                                 [&id](const PackageRecord& record) {
                                                     return record.id == id;
                                                 });
    if (appIdExists && !packageRecordExists && !input.replace)
        return fail(error, QStringLiteral("apps.json already has an entry with id '%1'; "
                                          "use --replace to overwrite it").arg(id));

    const QString root = packageRoot(settings);
    const QString finalDirectory = QDir(root).filePath(id);
    if (QFileInfo::exists(finalDirectory) && !input.replace) {
        return fail(error, QStringLiteral("A package directory already exists at '%1'; "
                                          "use --replace to update it").arg(finalDirectory));
    }
    const QString staging = newSiblingName(root, QStringLiteral(".staging-"));
    if (!QDir().mkpath(staging)) {
        return fail(error, QStringLiteral("Could not create the staging directory: %1")
                              .arg(staging));
    }
    const auto discardStaging = [&]() {
        QString ignored;
        removeDirectoryInside(root, staging, ignored);
    };
    if (!writeIcon(input.game.artPath, title, platform,
                   QDir(staging).filePath(QLatin1String(kIconName)), error)) {
        discardStaging();
        return false;
    }
    if (!writePackageMetadata(staging, finalDirectory, input, id, title, platform, romPath,
                              profile.id, arguments, tags, fingerprint, error)) {
        discardStaging();
        return false;
    }

    QString backup;
    if (QFileInfo::exists(finalDirectory)) {
        backup = newSiblingName(root, QStringLiteral(".backup-"));
        if (!QDir().rename(finalDirectory, backup)) {
            discardStaging();
            return fail(error, QStringLiteral("Could not move the existing package aside: %1")
                                  .arg(finalDirectory));
        }
    }
    if (!QDir().rename(staging, finalDirectory)) {
        QString ignored;
        if (!backup.isEmpty())
            QDir().rename(backup, finalDirectory);
        discardStaging();
        return fail(error, QStringLiteral("Could not commit the package directory: %1")
                              .arg(finalDirectory));
    }

    ContentApp app;
    app.id = id;
    app.title = title;
    app.kind = QStringLiteral("game");
    app.category = QStringLiteral("game");
    app.executable = executableInfo.absoluteFilePath();
    app.workingDirectory = QFileInfo(workingDirectory).absoluteFilePath();
    app.artwork = QDir(finalDirectory).filePath(QLatin1String(kIconName));
    app.arguments = arguments;

    ContentApp previousApp;
    bool hadPreviousApp = false;
    for (const ContentApp& candidate : apps) {
        if (candidate.id == id) {
            previousApp = candidate;
            hadPreviousApp = true;
            break;
        }
    }
    bool registryWritten = false;
    const auto rollback = [&](const QString& message) -> bool {
        QString ignored;
        if (registryWritten) {
            ContentRegistry::remove(registryPath(settings), id, ignored);
            if (hadPreviousApp) {
                QString restoreError;
                if (!ContentRegistry::upsert(registryPath(settings), previousApp, restoreError))
                    logError(QStringLiteral("Could not restore the previous registration: %1")
                                 .arg(restoreError));
            }
        }
        removeDirectoryInside(root, finalDirectory, ignored);
        if (!backup.isEmpty() && QFileInfo::exists(backup)
            && !QDir().rename(backup, finalDirectory))
            logError(QStringLiteral("Could not restore the previous package directory '%1'")
                         .arg(finalDirectory));
        return fail(error, message);
    };

    QString registryError;
    if (!ContentRegistry::upsert(registryPath(settings), app, registryError)) {
        return rollback(QStringLiteral("The registry update failed: %1. The package directory "
                                       "was removed%s.")
                            .arg(registryError,
                                 backup.isEmpty() ? QString()
                                                  : QStringLiteral(" and the previous package "
                                                                   "restored")));
    }
    registryWritten = true;
    QString collectionError;
    const bool collectionOk = input.collection.trimmed().isEmpty()
        ? removeFromCollections(settings, id, collectionError)
        : assignToCollection(settings, id, input.collection, collectionError);
    if (!collectionOk) {
        return rollback(QStringLiteral("The collection update failed: %1. The package and "
                                       "registration were rolled back.")
                            .arg(collectionError));
    }
    if (!backup.isEmpty()) {
        QString ignored;
        if (!removeDirectoryInside(root, backup, ignored))
            logWarning(QStringLiteral("Could not remove the previous package backup: %1")
                           .arg(ignored));
    }
    if (outApp)
        *outApp = app;
    return true;
}

bool removePackage(Settings& settings, const QString& appId, QString& error) {
    error.clear();
    if (!isSafePackageId(appId))
        return fail(error, QStringLiteral("'%1' is not a safe package id").arg(appId));
    const QString root = packageRoot(settings);
    if (!QFileInfo(settings.dataDirectory).isDir())
        return fail(error, QStringLiteral("Data directory does not exist: %1")
                              .arg(settings.dataDirectory));
    const QString directory = QDir(root).filePath(appId);
    const QFileInfo directoryInfo(directory);
    if (directoryInfo.exists()) {
        if (!directoryInfo.isDir())
            return fail(error, QStringLiteral("Package path is not a directory: %1")
                                  .arg(directory));
        const QString parent = QDir::cleanPath(directoryInfo.absolutePath());
        if (QString::compare(parent, QDir::cleanPath(QFileInfo(root).absoluteFilePath()),
                             Qt::CaseInsensitive) != 0)
            return fail(error, QStringLiteral("Refusing to remove a package outside the "
                                              "package root: %1").arg(directory));
        QString resolved;
        if (!isPathInside(root, directory, resolved))
            return fail(error, QStringLiteral("Refusing to resolve a package outside the "
                                              "package root: %1").arg(directory));
    }

    QVector<ContentApp> apps;
    if (!loadRegistry(settings, apps, error))
        return false;
    const auto existing = std::find_if(apps.cbegin(), apps.cend(),
                                       [&appId](const ContentApp& app) {
                                           return app.id == appId;
                                       });
    const bool registryHas = existing != apps.cend();
    const bool directoryExists = directoryInfo.exists();
    if (!registryHas && !directoryExists)
        return fail(error, QStringLiteral("No package or registration with id '%1'").arg(appId));

    QString trash;
    if (directoryExists) {
        trash = newSiblingName(root, QStringLiteral(".trash-"));
        if (!QDir().rename(directory, trash))
            return fail(error, QStringLiteral("Could not move the package directory aside: %1")
                                  .arg(directory));
    }
    const auto restoreDirectory = [&]() {
        if (!trash.isEmpty() && QFileInfo::exists(trash))
            QDir().rename(trash, directory);
    };
    if (registryHas) {
        QString removeError;
        if (!ContentRegistry::remove(registryPath(settings), appId, removeError)) {
            restoreDirectory();
            return fail(error, QStringLiteral("Could not remove the registration: %1")
                                  .arg(removeError));
        }
    }
    QString collectionError;
    if (!removeFromCollections(settings, appId, collectionError)) {
        // Restore the directory first: the previous registration's artwork lives
        // inside it, and ContentRegistry::upsert validates that path.
        restoreDirectory();
        if (registryHas) {
            QString ignored;
            if (!ContentRegistry::upsert(registryPath(settings), *existing, ignored))
                logError(QStringLiteral("Could not restore the registration after a collection "
                                        "failure"));
        }
        return fail(error, QStringLiteral("Could not update collections: %1").arg(collectionError));
    }
    if (!trash.isEmpty()) {
        QString removeError;
        if (!removeDirectoryInside(root, trash, removeError))
            logWarning(QStringLiteral("The package was unregistered but its directory could not "
                                      "be deleted: %1").arg(removeError));
    }
    return true;
}

QVector<PackageRecord> listPackages(Settings& settings, QString& error) {
    error.clear();
    QVector<PackageRecord> records;
    if (!loadPackageRecords(settings, records, error))
        return QVector<PackageRecord>();
    QVector<ContentApp> apps;
    if (!loadRegistry(settings, apps, error))
        return QVector<PackageRecord>();
    QSet<QString> registeredIds;
    for (const ContentApp& app : apps)
        registeredIds.insert(app.id);
    for (PackageRecord& record : records) {
        record.registryPresent = registeredIds.contains(record.id);
        record.orphaned = !record.registryPresent;
    }
    for (const ContentApp& app : apps) {
        if (!managerOwnedApp(settings, app))
            continue;
        const bool hasRecord = std::any_of(records.cbegin(), records.cend(),
                                           [&app](const PackageRecord& record) {
                                               return record.id == app.id;
                                           });
        if (hasRecord)
            continue;
        PackageRecord record;
        record.id = app.id;
        record.title = app.title;
        record.romPath.clear();
        record.romKind.clear();
        record.emulatorId.clear();
        record.arguments = app.arguments;
        record.art = app.artwork;
        record.packagePath = QDir(packageRoot(settings)).filePath(app.id);
        record.registryPresent = true;
        record.missingPackage = true;
        record.issue = QStringLiteral("apps.json entry has no pkg.json under %1")
                           .arg(record.packagePath);
        records.append(record);
    }
    std::sort(records.begin(), records.end(), [](const PackageRecord& a, const PackageRecord& b) {
        const int compare = QString::compare(a.title, b.title, Qt::CaseInsensitive);
        if (compare != 0)
            return compare < 0;
        return a.id < b.id;
    });
    return records;
}

bool importRealPkg(Settings& settings, const QString& pkgPath, QString& error,
                   QStringList* notes) {
    error.clear();
    PackageProbe probe;
    QString probeError;
    if (!probePs3Package(pkgPath, probe, probeError)) {
        if (probe.zipContainer)
            error = QStringLiteral("This file is a ZIP archive, not a PS3 package. Unpack it "
                                   "and scan the folder instead.");
        else
            error = probeError;
        if (notes)
            notes->append(QStringLiteral("pkg: %1").arg(error));
        return false;
    }
    if (notes)
        notes->append(QStringLiteral("pkg header: %1").arg(probe.evidence));
    if (!probe.sfoReadable) {
        error = QStringLiteral("Encrypted retail PS3 package: the header is readable but no "
                               "PARAM.SFO is readable without decryption, so no title or icon "
                               "can be trusted. Nothing was imported and the file was not "
                               "modified. Evidence: %1").arg(probe.evidence);
        if (notes)
            notes->append(QStringLiteral("pkg: refused (metadata encrypted)"));
        return false;
    }
    const QString title = probe.sfo.value(QStringLiteral("TITLE")).trimmed();
    if (title.isEmpty()) {
        error = QStringLiteral("The readable PARAM.SFO has no TITLE; nothing was imported. "
                               "Evidence: %1").arg(probe.evidence);
        return false;
    }
    if (notes && !probe.sfo.value(QStringLiteral("CATEGORY")).isEmpty()) {
        notes->append(QStringLiteral("pkg PARAM.SFO CATEGORY=%1")
                          .arg(probe.sfo.value(QStringLiteral("CATEGORY"))));
    }

    const QVector<EmulatorProfile> profiles = loadProfiles(settings, error);
    if (!error.isEmpty())
        return false;
    const auto usable = [](const EmulatorProfile& profile) {
        if (profile.executable.trimmed().isEmpty())
            return false;
        const QFileInfo exe(profile.executable);
        return exe.isFile()
            && exe.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) == 0;
    };
    const EmulatorProfile* chosen = findProfile(profiles, QStringLiteral("rpcs3"));
    if (!chosen || !usable(*chosen)) {
        chosen = nullptr;
        for (const EmulatorProfile& profile : profiles) {
            if (usable(profile)
                && (profile.platforms.isEmpty()
                    || profile.platforms.contains(QStringLiteral("PS3"), Qt::CaseInsensitive))) {
                chosen = &profile;
                break;
            }
        }
    }
    if (!chosen) {
        error = QStringLiteral("The package metadata is readable, but no PS3 emulator with a "
                               "configured executable was found. Run --detect-emulators or set "
                               "the RPCS3 executable in emulators.json. Nothing was imported.");
        return false;
    }

    QTemporaryDir temporary;
    QString artPath;
    if (!probe.iconPng.isEmpty() && temporary.isValid()) {
        const QString iconPath = QDir(temporary.path()).filePath(QStringLiteral("ICON0.PNG"));
        QString writeError;
        if (writeFileAtomic(iconPath, probe.iconPng, writeError))
            artPath = iconPath;
        else
            logWarning(QStringLiteral("Could not extract the package icon: %1").arg(writeError));
    }

    DetectedGame game;
    game.title = title;
    game.platform = QStringLiteral("PS3");
    game.romPath = canonical(pkgPath);
    game.romKind = QStringLiteral("pkg");
    game.size = QFileInfo(pkgPath).size();
    game.artPath = artPath;
    game.sourceNote = QStringLiteral("real PS3 package (%1 metadata read, payload not "
                                     "decrypted)")
                          .arg(probe.revision == 0x8000 ? QStringLiteral("retail header")
                                                        : QStringLiteral("debug package"));
    PackageInput input;
    input.game = game;
    input.emulatorId = chosen->id;
    if (!createPackage(settings, input, *chosen, nullptr, error))
        return false;
    if (notes) {
        notes->append(QStringLiteral("pkg: imported '%1' with emulator '%2'")
                          .arg(title, chosen->id));
        notes->append(QStringLiteral("pkg: TITLE and ICON0 came from readable data; the payload "
                                     "itself was not decrypted"));
        notes->append(QStringLiteral("pkg: the emulator is launched with the .pkg path, exactly "
                                     "like any other virtual package argument"));
    }
    return true;
}

namespace {

QJsonArray stringArray(const QStringList& values) {
    QJsonArray array;
    for (const QString& value : values)
        array.append(value);
    return array;
}

QJsonObject recordToJsonObject(const PackageRecord& record) {
    QJsonObject object;
    object.insert(QStringLiteral("schema"), virtualPackageSchema());
    object.insert(QStringLiteral("id"), record.id);
    object.insert(QStringLiteral("title"), record.title);
    object.insert(QStringLiteral("platform"), record.platform);
    object.insert(QStringLiteral("romPath"), record.romPath);
    object.insert(QStringLiteral("romKind"), record.romKind);
    object.insert(QStringLiteral("emulatorId"), record.emulatorId);
    object.insert(QStringLiteral("arguments"), stringArray(record.arguments));
    object.insert(QStringLiteral("collection"), record.collection);
    object.insert(QStringLiteral("art"), record.art);
    object.insert(QStringLiteral("createdAt"), record.createdAt);
    object.insert(QStringLiteral("sourceNote"), record.sourceNote);
    object.insert(QStringLiteral("tags"), stringArray(record.tags));
    object.insert(QStringLiteral("romSize"), double(record.romSize));
    object.insert(QStringLiteral("romMtime"), record.romMtime);
    object.insert(QStringLiteral("romHash"), record.hashValue);
    object.insert(QStringLiteral("romHashLabel"), record.hashLabel);
    return object;
}

bool writeRecordFile(const PackageRecord& record, QString& error) {
    if (record.packagePath.isEmpty())
        return fail(error, QStringLiteral("Package '%1' has no directory").arg(record.id));
    return writeJsonFileAtomic(QDir(record.packagePath).filePath(QLatin1String(kPkgJsonName)),
                               recordToJsonObject(record), error);
}

bool findPackageRecord(Settings& settings, const QString& appId, PackageRecord& record,
                       QString& error, bool requireRegistration) {
    if (!isSafePackageId(appId))
        return fail(error, QStringLiteral("'%1' is not a safe package id").arg(appId));
    QVector<PackageRecord> records;
    if (!loadPackageRecords(settings, records, error))
        return false;
    bool found = false;
    for (const PackageRecord& candidate : records) {
        if (candidate.id != appId)
            continue;
        if (!candidate.packagePresent)
            break;
        if (!candidate.issue.isEmpty())
            return fail(error, QStringLiteral("Package '%1' is unreadable: %2")
                                  .arg(appId, candidate.issue));
        record = candidate;
        found = true;
        break;
    }
    if (!found)
        return fail(error, QStringLiteral("No package with id '%1'").arg(appId));
    if (requireRegistration) {
        QVector<ContentApp> apps;
        if (!ContentRegistry::load(registryPath(settings), apps, error))
            return false;
        const bool registered = std::any_of(apps.cbegin(), apps.cend(),
                                            [&appId](const ContentApp& app) {
                                                return app.id == appId;
                                            });
        if (!registered)
            return fail(error, QStringLiteral("Package '%1' has no apps.json registration; "
                                              "relink needs one").arg(appId));
    }
    return true;
}

struct FileSnapshot {
    QString path;
    bool existed = false;
    QByteArray bytes;
};

bool snapshotFile(const QString& path, FileSnapshot& snapshot, QString& error) {
    snapshot.path = path;
    snapshot.bytes.clear();
    snapshot.existed = QFileInfo::exists(path);
    if (!snapshot.existed)
        return true;
    return readFileLimited(path, kMaxJsonBytes, snapshot.bytes, error);
}

bool restoreFileSnapshot(const FileSnapshot& snapshot, QString& error) {
    if (!snapshot.existed) {
        if (QFileInfo::exists(snapshot.path) && !QFile::remove(snapshot.path))
            return fail(error, QStringLiteral("Could not remove '%1' while rolling back")
                                  .arg(snapshot.path));
        return true;
    }
    return writeFileAtomic(snapshot.path, snapshot.bytes, error);
}

struct SnapshotSet {
    QVector<FileSnapshot> files;

    bool add(const QString& path, QString& error) {
        for (const FileSnapshot& existing : files) {
            if (existing.path.compare(path, Qt::CaseInsensitive) == 0)
                return true;
        }
        FileSnapshot snapshot;
        if (!snapshotFile(path, snapshot, error))
            return false;
        files.append(snapshot);
        return true;
    }

    bool restore(QString& error) const {
        for (auto it = files.crbegin(); it != files.crend(); ++it) {
            if (!restoreFileSnapshot(*it, error))
                return false;
        }
        return true;
    }
};

QString remappedText(const QString& text, const QVector<PathRemap>& remaps) {
    QString result = QDir::fromNativeSeparators(text);
    for (const PathRemap& remap : remaps) {
        const QString from = QDir::fromNativeSeparators(remap.from.trimmed());
        const QString to = QDir::fromNativeSeparators(remap.to.trimmed());
        if (from.isEmpty())
            continue;
        int position = result.indexOf(from, 0, Qt::CaseInsensitive);
        while (position >= 0) {
            result.replace(position, from.size(), to);
            position = result.indexOf(from, position + to.size(), Qt::CaseInsensitive);
        }
    }
    return result;
}

QString remappedPath(const QString& path, const QVector<PathRemap>& remaps) {
    if (path.isEmpty())
        return path;
    return QDir::cleanPath(remappedText(path, remaps));
}

bool writeSfoAndIcon(const QString& directory, const QString& title, const QString& platform,
                     const QString& artPath, QString& error) {
    QHash<QString, QString> sfo;
    sfo.insert(QStringLiteral("TITLE"), limitedTitle(title));
    sfo.insert(QStringLiteral("CATEGORY"), QStringLiteral("HG"));
    sfo.insert(QStringLiteral("APP_VER"), QStringLiteral("01.00"));
    sfo.insert(QStringLiteral("ICON0"), QLatin1String(kIconName));
    QByteArray bytes;
    if (!writeParamSfo(sfo, bytes, error))
        return false;
    if (!writeFileAtomic(QDir(directory).filePath(QLatin1String(kSfoName)), bytes, error))
        return false;
    return writeIcon(artPath, title, platform, QDir(directory).filePath(QLatin1String(kIconName)),
                     error);
}

} // namespace

bool updatePackageTags(Settings& settings, const QString& appId, const QStringList& add,
                       const QStringList& remove, QStringList& outTags, QString& error) {
    error.clear();
    outTags.clear();
    PackageRecord record;
    if (!findPackageRecord(settings, appId, record, error, false))
        return false;
    QString tagError;
    const QStringList normalizedAdd = normalizeTags(add, tagError);
    if (!tagError.isEmpty())
        return fail(error, tagError);
    const QStringList normalizedRemove = normalizeTags(remove, tagError);
    if (!tagError.isEmpty())
        return fail(error, tagError);
    QStringList tags = record.tags;
    for (const QString& tag : normalizedAdd) {
        const bool present = std::any_of(tags.cbegin(), tags.cend(), [&tag](const QString& existing) {
            return existing.compare(tag, Qt::CaseInsensitive) == 0;
        });
        if (!present)
            tags.append(tag);
    }
    for (const QString& tag : normalizedRemove)
        tags.erase(std::remove_if(tags.begin(), tags.end(), [&tag](const QString& existing) {
                       return existing.compare(tag, Qt::CaseInsensitive) == 0;
                   }),
                   tags.end());
    record.tags = tags;
    if (!writeRecordFile(record, error))
        return false;
    outTags = tags;
    return true;
}

bool setPackageCollection(Settings& settings, const QString& appId, const QString& collection,
                          QString& error) {
    error.clear();
    const QString name = collection.trimmed();
    if (!name.isEmpty() && !validateCollectionName(name, error))
        return false;
    PackageRecord record;
    if (!findPackageRecord(settings, appId, record, error, false))
        return false;
    FileSnapshot snapshot;
    if (!snapshotFile(collectionsPath(settings), snapshot, error))
        return false;
    QHash<QString, QStringList> collections;
    if (!readCollections(settings, collections, error))
        return false;
    for (auto it = collections.begin(); it != collections.end();) {
        it.value().removeAll(appId);
        if (it.value().isEmpty())
            it = collections.erase(it);
        else
            ++it;
    }
    if (!name.isEmpty())
        collections[name].append(appId);
    if (!writeCollections(settings, collections, error))
        return false;
    record.collection = name;
    if (!writeRecordFile(record, error)) {
        QString ignored;
        if (!restoreFileSnapshot(snapshot, ignored))
            logError(QStringLiteral("Could not restore collections.json after a failed package "
                                    "update: %1").arg(ignored));
        return false;
    }
    return true;
}

bool renameCollection(Settings& settings, const QString& from, const QString& to, int& affected,
                      QString& error) {
    error.clear();
    affected = 0;
    const QString oldName = from.trimmed();
    const QString newName = to.trimmed();
    if (oldName.isEmpty())
        return fail(error, QStringLiteral("--rename-collection needs an existing name"));
    if (!validateCollectionName(newName, error))
        return false;
    if (newName == oldName)
        return true;
    QHash<QString, QStringList> collections;
    if (!readCollections(settings, collections, error))
        return false;
    if (!collections.contains(oldName))
        return fail(error, QStringLiteral("Collection '%1' does not exist").arg(oldName));
    if (collections.contains(newName))
        return fail(error, QStringLiteral("Collection '%1' already exists").arg(newName));
    const QStringList members = collections.value(oldName);
    SnapshotSet snapshots;
    if (!snapshots.add(collectionsPath(settings), error))
        return false;
    QVector<PackageRecord> records;
    if (!loadPackageRecords(settings, records, error))
        return false;
    for (const QString& id : members) {
        for (const PackageRecord& record : records) {
            if (record.id == id && record.packagePresent && record.issue.isEmpty()) {
                if (!snapshots.add(QDir(record.packagePath).filePath(QLatin1String(kPkgJsonName)),
                                   error))
                    return false;
            }
        }
    }
    collections.remove(oldName);
    collections.insert(newName, members);
    if (!writeCollections(settings, collections, error))
        return false;
    for (const PackageRecord& record : records) {
        if (!members.contains(record.id) || !record.packagePresent || !record.issue.isEmpty())
            continue;
        if (record.collection.compare(oldName, Qt::CaseInsensitive) != 0)
            continue;
        PackageRecord updated = record;
        updated.collection = newName;
        if (!writeRecordFile(updated, error)) {
            QString ignored;
            if (!snapshots.restore(ignored))
                logError(QStringLiteral("Rollback failed: %1").arg(ignored));
            return false;
        }
    }
    affected = int(members.size());
    return true;
}

bool removeCollection(Settings& settings, const QString& name, int& affected, QString& error) {
    error.clear();
    affected = 0;
    const QString target = name.trimmed();
    if (target.isEmpty())
        return fail(error, QStringLiteral("--remove-collection needs a name"));
    QHash<QString, QStringList> collections;
    if (!readCollections(settings, collections, error))
        return false;
    if (!collections.contains(target))
        return fail(error, QStringLiteral("Collection '%1' does not exist").arg(target));
    const QStringList members = collections.value(target);
    SnapshotSet snapshots;
    if (!snapshots.add(collectionsPath(settings), error))
        return false;
    QVector<PackageRecord> records;
    if (!loadPackageRecords(settings, records, error))
        return false;
    for (const PackageRecord& record : records) {
        if (members.contains(record.id) && record.packagePresent && record.issue.isEmpty()) {
            if (!snapshots.add(QDir(record.packagePath).filePath(QLatin1String(kPkgJsonName)),
                               error))
                return false;
        }
    }
    collections.remove(target);
    if (!writeCollections(settings, collections, error))
        return false;
    for (const PackageRecord& record : records) {
        if (!members.contains(record.id) || !record.packagePresent || !record.issue.isEmpty())
            continue;
        if (record.collection.compare(target, Qt::CaseInsensitive) != 0)
            continue;
        PackageRecord updated = record;
        updated.collection.clear();
        if (!writeRecordFile(updated, error)) {
            QString ignored;
            if (!snapshots.restore(ignored))
                logError(QStringLiteral("Rollback failed: %1").arg(ignored));
            return false;
        }
    }
    affected = int(members.size());
    return true;
}

bool applyBulk(Settings& settings, const BulkRequest& request, QVector<BulkResult>& results,
               QString& error) {
    error.clear();
    results.clear();
    if (request.ids.isEmpty())
        return fail(error, QStringLiteral("A bulk request needs at least one id"));
    if (!request.setCollection && !request.setEmulator && request.addTags.isEmpty()
        && request.removeTags.isEmpty() && !request.remove)
        return fail(error, QStringLiteral("A bulk request needs at least one operation"));
    if (!ensureDataLayout(settings, error))
        return false;

    QStringList ids;
    QSet<QString> seen;
    for (const QString& raw : request.ids) {
        const QString id = raw.trimmed();
        if (!isSafePackageId(id))
            return fail(error, QStringLiteral("'%1' is not a safe package id").arg(raw));
        if (seen.contains(id))
            return fail(error, QStringLiteral("Bulk id '%1' was supplied more than once").arg(id));
        seen.insert(id);
        ids.append(id);
    }
    QString tagError;
    const QStringList addTags = normalizeTags(request.addTags, tagError);
    if (!tagError.isEmpty())
        return fail(error, tagError);
    const QStringList removeTags = normalizeTags(request.removeTags, tagError);
    if (!tagError.isEmpty())
        return fail(error, tagError);
    const QString collection = request.collection.trimmed();
    if (request.setCollection && !collection.isEmpty()
        && !validateCollectionName(collection, error))
        return false;

    QVector<EmulatorProfile> profiles;
    QString emulatorExecutable;
    QString emulatorWorkingDirectory;
    if (request.setEmulator) {
        profiles = loadProfiles(settings, error);
        if (!error.isEmpty())
            return false;
        const EmulatorProfile* profile = findProfile(profiles, request.emulatorId);
        if (!profile)
            return fail(error, QStringLiteral("Unknown emulator id '%1'").arg(request.emulatorId));
        emulatorExecutable = profile->executable.trimmed();
        const QFileInfo executableInfo(emulatorExecutable);
        if (emulatorExecutable.isEmpty() || !executableInfo.isFile())
            return fail(error, QStringLiteral("Emulator '%1' has no usable executable; configure "
                                              "it in emulators.json first").arg(profile->id));
        if (!QDir::isAbsolutePath(emulatorExecutable))
            return fail(error, QStringLiteral("Emulator '%1' executable is not absolute")
                                  .arg(profile->id));
        emulatorExecutable = QDir::cleanPath(executableInfo.absoluteFilePath());
        emulatorWorkingDirectory = profile->workingDirectory.trimmed();
        if (emulatorWorkingDirectory.isEmpty())
            emulatorWorkingDirectory = executableInfo.absolutePath();
        emulatorWorkingDirectory = QDir::cleanPath(emulatorWorkingDirectory);
        if (!QFileInfo(emulatorWorkingDirectory).isDir())
            return fail(error, QStringLiteral("Emulator '%1' working directory does not exist: %2")
                                  .arg(profile->id, emulatorWorkingDirectory));
    }
    const EmulatorProfile* profile = request.setEmulator
        ? findProfile(profiles, request.emulatorId)
        : nullptr;

    QVector<PackageRecord> records;
    if (!loadPackageRecords(settings, records, error))
        return false;
    QHash<QString, PackageRecord> recordById;
    for (const PackageRecord& record : records) {
        if (record.packagePresent && record.issue.isEmpty())
            recordById.insert(record.id, record);
    }
    QVector<ContentApp> apps;
    if (!ContentRegistry::load(registryPath(settings), apps, error))
        return false;
    QHash<QString, ContentApp> appById;
    for (const ContentApp& app : apps)
        appById.insert(app.id, app);
    QHash<QString, QStringList> collections;
    if (!readCollections(settings, collections, error))
        return false;

    bool invalid = false;
    for (const QString& id : ids) {
        BulkResult result;
        result.id = id;
        const PackageRecord* record = recordById.constFind(id) == recordById.cend()
            ? nullptr
            : &recordById[id];
        const bool registered = appById.contains(id);
        if (request.remove) {
            if (!record && !registered) {
                result.error = QStringLiteral("No package or registration with this id");
                invalid = true;
            } else {
                result.ok = true;
            }
        } else if (!record) {
            result.error = QStringLiteral("No readable package with this id");
            invalid = true;
        } else {
            result.ok = true;
        }
        results.append(result);
    }
    if (invalid) {
        for (BulkResult& result : results) {
            if (result.ok) {
                result.ok = false;
                result.error = QStringLiteral("Not applied: the bulk request failed validation");
            }
        }
        return fail(error, QStringLiteral("Bulk validation failed; nothing was changed"));
    }

    SnapshotSet snapshots;
    if (!snapshots.add(registryPath(settings), error)
        || !snapshots.add(collectionsPath(settings), error))
        return false;
    for (const QString& id : ids) {
        if (request.remove)
            continue;
        const PackageRecord& record = recordById[id];
        if (!snapshots.add(QDir(record.packagePath).filePath(QLatin1String(kPkgJsonName)), error))
            return false;
    }

    const QString root = packageRoot(settings);
    QHash<QString, QString> stagedDirectories;
    const auto rollback = [&](const QString& message) -> bool {
        QString rollbackError;
        for (auto it = stagedDirectories.begin(); it != stagedDirectories.end(); ++it) {
            if (QFileInfo::exists(it.value())) {
                const QString finalDirectory = QDir(root).filePath(it.key());
                if (QFileInfo::exists(finalDirectory))
                    continue;
                if (!QDir().rename(it.value(), finalDirectory))
                    logError(QStringLiteral("Could not restore package directory '%1'")
                                 .arg(finalDirectory));
            }
        }
        if (!snapshots.restore(rollbackError))
            logError(QStringLiteral("Bulk rollback could not restore every file: %1")
                         .arg(rollbackError));
        for (BulkResult& result : results) {
            result.ok = false;
            result.error = QStringLiteral("Bulk request failed and was rolled back: %1")
                               .arg(message);
        }
        return fail(error, message + QStringLiteral(" All changes were rolled back."));
    };

    bool collectionsChanged = false;
    QHash<QString, QStringList> updatedCollections = collections;

    for (const QString& id : ids) {
        if (request.remove)
            continue;
        PackageRecord record = recordById.value(id);
        QStringList actions;
        bool changed = false;
        if (!addTags.isEmpty() || !removeTags.isEmpty()) {
            QStringList tags = record.tags;
            for (const QString& tag : addTags) {
                const bool present = std::any_of(tags.cbegin(), tags.cend(),
                                                 [&tag](const QString& existing) {
                                                     return existing.compare(tag,
                                                                             Qt::CaseInsensitive)
                                                         == 0;
                                                 });
                if (!present)
                    tags.append(tag);
            }
            for (const QString& tag : removeTags) {
                tags.erase(std::remove_if(tags.begin(), tags.end(),
                                          [&tag](const QString& existing) {
                                              return existing.compare(tag,
                                                                      Qt::CaseInsensitive) == 0;
                                          }),
                           tags.end());
            }
            if (tags != record.tags) {
                record.tags = tags;
                changed = true;
                actions.append(QStringLiteral("tags=[%1]").arg(tags.join(QLatin1Char(','))));
            }
        }
        if (request.setCollection) {
            for (auto it = updatedCollections.begin(); it != updatedCollections.end();) {
                if (it.value().removeAll(id) > 0) {
                    changed = true;
                    collectionsChanged = true;
                    actions.append(QStringLiteral("collection-=%1").arg(it.key()));
                }
                if (it.value().isEmpty())
                    it = updatedCollections.erase(it);
                else
                    ++it;
            }
            if (!collection.isEmpty()) {
                QStringList& members = updatedCollections[collection];
                if (!members.contains(id)) {
                    members.append(id);
                    collectionsChanged = true;
                }
                actions.append(QStringLiteral("collection=%1").arg(collection));
            }
            record.collection = collection;
            changed = true;
        }
        if (request.setEmulator) {
            const QString core = defaultCoreFor(profile->id, record.platform);
            const QStringList keyValues = {
                QStringLiteral("rom=%1").arg(record.romPath),
                QStringLiteral("title=%1").arg(record.title),
                QStringLiteral("id=%1").arg(record.id),
                QStringLiteral("dir=%1").arg(QFileInfo(record.romPath).absolutePath()),
                QStringLiteral("core=%1").arg(core),
            };
            QString expandError;
            const QStringList arguments = expandArguments(*profile, keyValues, expandError);
            if (!expandError.isEmpty())
                return rollback(QStringLiteral("Could not expand the arguments for '%1': %2")
                                    .arg(id, expandError));
            record.emulatorId = profile->id;
            record.arguments = arguments;
            changed = true;
            actions.append(QStringLiteral("emulator=%1").arg(profile->id));
        }
        BulkResult& result = results[ids.indexOf(id)];
        if (!changed) {
            result.action = QStringLiteral("unchanged");
            continue;
        }
        if (!writeRecordFile(record, error))
            return rollback(error);
        if (request.setEmulator) {
            ContentApp app = appById.value(id);
            app.executable = emulatorExecutable;
            app.workingDirectory = emulatorWorkingDirectory;
            app.arguments = record.arguments;
            QString upsertError;
            if (!ContentRegistry::upsert(registryPath(settings), app, upsertError))
                return rollback(QStringLiteral("Could not update the registration for '%1': %2")
                                    .arg(id, upsertError));
        }
        result.action = actions.join(QStringLiteral("; "));
    }

    for (const QString& id : ids) {
        if (!request.remove)
            continue;
        const PackageRecord* record = recordById.constFind(id) == recordById.cend()
            ? nullptr
            : &recordById[id];
        if (record && !record->packagePath.isEmpty()) {
            const QString finalDirectory = record->packagePath;
            const QString staging = newSiblingName(root, QStringLiteral(".bulk-"));
            if (!QDir().rename(finalDirectory, staging))
                return rollback(QStringLiteral("Could not stage the package directory for '%1'")
                                    .arg(id));
            stagedDirectories.insert(id, staging);
        }
        if (appById.contains(id)) {
            QString removeError;
            if (!ContentRegistry::remove(registryPath(settings), id, removeError))
                return rollback(QStringLiteral("Could not remove the registration for '%1': %2")
                                    .arg(id, removeError));
        }
        for (auto it = updatedCollections.begin(); it != updatedCollections.end();) {
            if (it.value().removeAll(id) > 0)
                collectionsChanged = true;
            if (it.value().isEmpty())
                it = updatedCollections.erase(it);
            else
                ++it;
        }
    }

    if (collectionsChanged && !writeCollections(settings, updatedCollections, error))
        return rollback(error);

    for (auto it = stagedDirectories.begin(); it != stagedDirectories.end(); ++it) {
        QString removeError;
        if (!removeDirectoryInside(root, it.value(), removeError))
            logWarning(QStringLiteral("The package was removed but its staged directory could "
                                      "not be deleted: %1").arg(removeError));
    }
    for (BulkResult& result : results) {
        if (result.action.isEmpty())
            result.action = request.remove ? QStringLiteral("removed") : QStringLiteral("applied");
    }
    return true;
}

bool verifyPackageHashes(Settings& settings, QVector<HashVerification>& results, QString& error) {
    error.clear();
    results.clear();
    QVector<PackageRecord> records;
    if (!loadPackageRecords(settings, records, error))
        return false;
    for (const PackageRecord& record : records) {
        if (!record.packagePresent)
            continue;
        HashVerification verification;
        verification.id = record.id;
        verification.romPath = record.romPath;
        verification.storedLabel = record.hashLabel;
        verification.storedHash = record.hashValue;
        verification.romSize = record.romSize;
        if (!record.issue.isEmpty()) {
            verification.state = QStringLiteral("unreadable");
            verification.error = record.issue;
            results.append(verification);
            continue;
        }
        if (record.hashValue.isEmpty() || record.hashLabel.isEmpty()) {
            verification.state = QStringLiteral("missing-hash");
            verification.error = QStringLiteral("pkg.json has no recorded content hash; use "
                                                "--relink --force to record it");
            results.append(verification);
            continue;
        }
        if (!QFileInfo::exists(record.romPath)) {
            verification.state = QStringLiteral("missing-rom");
            verification.error = QStringLiteral("ROM does not exist: %1").arg(record.romPath);
            results.append(verification);
            continue;
        }
        RomFingerprint fingerprint;
        QString hashError;
        if (!computeRomFingerprint(record.romPath, fingerprint, hashError)) {
            verification.state = QStringLiteral("unreadable");
            verification.error = hashError;
            results.append(verification);
            continue;
        }
        verification.currentHash = fingerprint.hashValue;
        if (fingerprint.hashLabel != record.hashLabel
            || fingerprint.hashValue.compare(record.hashValue, Qt::CaseInsensitive) != 0) {
            verification.state = QStringLiteral("mismatch");
            verification.error = QStringLiteral("Content hash does not match the recorded %1; "
                                                "use --relink %2 <path> (--force) to update it")
                                     .arg(record.hashLabel, record.id);
        } else {
            verification.state = QStringLiteral("ok");
        }
        results.append(verification);
    }
    return true;
}

bool relinkPackage(Settings& settings, const QString& appId, const QString& newPath, bool force,
                   QJsonObject& summary, QString& error) {
    error.clear();
    summary = QJsonObject();
    if (!isSafePackageId(appId))
        return fail(error, QStringLiteral("'%1' is not a safe package id").arg(appId));
    PackageRecord record;
    if (!findPackageRecord(settings, appId, record, error, true))
        return false;
    const QFileInfo newInfo(newPath);
    if (!newInfo.exists())
        return fail(error, QStringLiteral("The new ROM path does not exist: %1").arg(newPath));
    const QString canonicalPath = QDir::cleanPath(
        newInfo.canonicalFilePath().isEmpty() ? newInfo.absoluteFilePath()
                                              : newInfo.canonicalFilePath());
    const bool wasFolder = record.romKind.compare(QStringLiteral("folder"), Qt::CaseInsensitive)
        == 0;
    if (wasFolder && !newInfo.isDir())
        return fail(error, QStringLiteral("Package '%1' points at a ROM folder; the new path "
                                          "must also be a folder").arg(appId));
    if (!wasFolder && !newInfo.isFile())
        return fail(error, QStringLiteral("Package '%1' points at a ROM file; the new path must "
                                          "also be a file").arg(appId));
    QVector<PackageRecord> records;
    if (!loadPackageRecords(settings, records, error))
        return false;
    for (const PackageRecord& other : records) {
        if (other.id == appId || !other.packagePresent)
            continue;
        if (samePath(other.romPath, canonicalPath))
            return fail(error, QStringLiteral("The new ROM path is already imported as '%1'")
                                  .arg(other.id));
    }
    RomFingerprint fingerprint;
    if (!computeRomFingerprint(canonicalPath, fingerprint, error))
        return false;
    if (!force && (fingerprint.hashLabel != record.hashLabel
                   || fingerprint.hashValue.compare(record.hashValue, Qt::CaseInsensitive) != 0)) {
        return fail(error, QStringLiteral("The content hash of '%1' (%2 %3) does not match the "
                                          "recorded hash of package '%4' (%5 %6). Use --force to "
                                          "relink anyway.")
                              .arg(canonicalPath, fingerprint.hashLabel, fingerprint.hashValue,
                                   appId, record.hashLabel, record.hashValue));
    }
    QVector<EmulatorProfile> profiles = loadProfiles(settings, error);
    if (!error.isEmpty())
        return false;
    const EmulatorProfile* profile = findProfile(profiles, record.emulatorId);
    if (!profile)
        return fail(error, QStringLiteral("Emulator '%1' is not defined; cannot re-expand the "
                                          "argv").arg(record.emulatorId));
    if (profile->executable.trimmed().isEmpty()
        || !QFileInfo(profile->executable).isFile())
        return fail(error, QStringLiteral("Emulator '%1' has no usable executable; the "
                                          "registration cannot be updated")
                              .arg(profile->id));
    QStringList override;
    QVector<PlatformMapping> mappings;
    if (!loadPlatformMap(settings, mappings, error))
        return false;
    if (const PlatformMapping* mapping = findPlatformMapping(mappings, record.platform)) {
        if (mapping->emulatorId.compare(record.emulatorId, Qt::CaseInsensitive) == 0)
            override = mapping->arguments;
    }
    const QStringList keyValues = {
        QStringLiteral("rom=%1").arg(canonicalPath),
        QStringLiteral("title=%1").arg(record.title),
        QStringLiteral("id=%1").arg(record.id),
        QStringLiteral("dir=%1").arg(QFileInfo(canonicalPath).absolutePath()),
        QStringLiteral("core=%1").arg(defaultCoreFor(profile->id, record.platform)),
    };
    QStringList arguments = override.isEmpty()
        ? expandArguments(*profile, keyValues, error)
        : expandArgumentList(override, keyValues, error);
    if (!error.isEmpty())
        return false;

    const QString oldPath = record.romPath;
    const QString oldHash = record.hashValue;
    record.romPath = canonicalPath;
    record.romKind = newInfo.isDir() ? QStringLiteral("folder")
                                     : newInfo.suffix().toLower();
    record.arguments = arguments;
    record.romSize = fingerprint.size;
    record.romMtime = fingerprint.mtime;
    record.hashLabel = fingerprint.hashLabel;
    record.hashValue = fingerprint.hashValue;

    FileSnapshot packageSnapshot;
    if (!snapshotFile(QDir(record.packagePath).filePath(QLatin1String(kPkgJsonName)),
                      packageSnapshot, error))
        return false;
    QVector<ContentApp> apps;
    if (!ContentRegistry::load(registryPath(settings), apps, error))
        return false;
    const auto existing = std::find_if(apps.cbegin(), apps.cend(),
                                       [&appId](const ContentApp& app) {
                                           return app.id == appId;
                                       });
    if (existing == apps.cend())
        return fail(error, QStringLiteral("Package '%1' has no apps.json registration")
                              .arg(appId));
    ContentApp app = *existing;
    if (!writeRecordFile(record, error))
        return false;
    app.executable = profile->executable.trimmed();
    app.workingDirectory = profile->workingDirectory.trimmed();
    if (app.workingDirectory.isEmpty())
        app.workingDirectory = QFileInfo(app.executable).absolutePath();
    app.arguments = arguments;
    QString upsertError;
    if (!ContentRegistry::upsert(registryPath(settings), app, upsertError)) {
        QString ignored;
        if (!restoreFileSnapshot(packageSnapshot, ignored))
            logError(QStringLiteral("Could not restore pkg.json after a failed relink: %1")
                         .arg(ignored));
        return fail(error, QStringLiteral("Could not update the registration: %1").arg(upsertError));
    }
    summary.insert(QStringLiteral("appId"), appId);
    summary.insert(QStringLiteral("oldPath"), oldPath);
    summary.insert(QStringLiteral("newPath"), canonicalPath);
    summary.insert(QStringLiteral("oldHash"), oldHash);
    summary.insert(QStringLiteral("newHash"), fingerprint.hashValue);
    summary.insert(QStringLiteral("hashLabel"), fingerprint.hashLabel);
    summary.insert(QStringLiteral("forced"), force);
    summary.insert(QStringLiteral("arguments"), stringArray(arguments));
    return true;
}

bool packageArgv(Settings& settings, const QString& appId, QString& executable,
                 QString& workingDirectory, QStringList& arguments, QString& error) {
    error.clear();
    executable.clear();
    workingDirectory.clear();
    arguments.clear();
    if (!isSafePackageId(appId))
        return fail(error, QStringLiteral("'%1' is not a safe package id").arg(appId));
    QVector<ContentApp> apps;
    if (!ContentRegistry::load(registryPath(settings), apps, error))
        return false;
    const auto found = std::find_if(apps.cbegin(), apps.cend(), [&appId](const ContentApp& app) {
        return app.id == appId;
    });
    if (found == apps.cend())
        return fail(error, QStringLiteral("No apps.json registration with id '%1'").arg(appId));
    executable = found->executable;
    workingDirectory = found->workingDirectory;
    arguments = found->arguments;
    if (executable.trimmed().isEmpty())
        return fail(error, QStringLiteral("Registration '%1' has no executable").arg(appId));
    return true;
}

bool launchPackage(Settings& settings, const QString& appId, int timeoutSeconds,
                   LaunchOutcome& outcome, QString& error) {
    error.clear();
    outcome = LaunchOutcome();
    outcome.appId = appId;
    outcome.timeoutSeconds = std::max(1, timeoutSeconds);
    QString argvError;
    if (!packageArgv(settings, appId, outcome.executable, outcome.workingDirectory,
                     outcome.arguments, argvError)) {
        outcome.exitStatus = QStringLiteral("spawn-error");
        outcome.spawnError = argvError;
        return fail(error, argvError);
    }
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    if (!outcome.workingDirectory.isEmpty())
        process.setWorkingDirectory(outcome.workingDirectory);
    process.start(outcome.executable, outcome.arguments);
    if (!process.waitForStarted(5000)) {
        outcome.exitStatus = QStringLiteral("spawn-error");
        outcome.spawnError = QStringLiteral("%1: %2")
                                 .arg(outcome.executable, process.errorString());
        process.kill();
        process.waitForFinished(1000);
        return fail(error, QStringLiteral("Could not start '%1': %2")
                              .arg(outcome.executable, process.errorString()));
    }
    outcome.started = true;
    outcome.pid = static_cast<qint64>(process.processId());
    if (!process.waitForFinished(outcome.timeoutSeconds * 1000)) {
        outcome.timedOut = true;
        process.terminate();
        outcome.terminated = true;
        if (!process.waitForFinished(2000)) {
            process.kill();
            outcome.killed = true;
            process.waitForFinished(2000);
        }
        outcome.finished = process.state() == QProcess::NotRunning;
        outcome.exitStatus = QStringLiteral("timeout");
        const QByteArray out = process.readAllStandardOutput();
        const QByteArray err = process.readAllStandardError();
        outcome.stdoutText = QString::fromUtf8(out.left(4096));
        outcome.stderrText = QString::fromUtf8(err.left(4096));
        return fail(error, QStringLiteral("'%1' did not exit within %2 second(s); it was %3")
                              .arg(outcome.executable)
                              .arg(outcome.timeoutSeconds)
                              .arg(outcome.killed ? QStringLiteral("terminated and killed")
                                                  : QStringLiteral("terminated")));
    }
    outcome.finished = true;
    outcome.exitStatus = process.exitStatus() == QProcess::NormalExit
        ? QStringLiteral("normal")
        : QStringLiteral("crash");
    outcome.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    outcome.stdoutText = QString::fromUtf8(process.readAllStandardOutput().left(4096));
    outcome.stderrText = QString::fromUtf8(process.readAllStandardError().left(4096));
    return true;
}

namespace {

constexpr const char* kBackupSchema = "xmb-manager-backup/1";
constexpr qint64 kMaxBackupArtBytes = 64 * 1024;
constexpr qint64 kMaxBackupArtTotal = 1536 * 1024;

QJsonObject mappingToJson(const PlatformMapping& mapping) {
    QJsonObject object;
    object.insert(QStringLiteral("platform"), mapping.platform);
    object.insert(QStringLiteral("emulatorId"), mapping.emulatorId);
    if (!mapping.arguments.isEmpty())
        object.insert(QStringLiteral("arguments"), stringArray(mapping.arguments));
    return object;
}

QJsonObject platformMapToJson(const QVector<PlatformMapping>& mappings) {
    QVector<PlatformMapping> sorted = mappings;
    std::sort(sorted.begin(), sorted.end(),
              [](const PlatformMapping& a, const PlatformMapping& b) {
                  return a.platform.compare(b.platform, Qt::CaseInsensitive) < 0;
              });
    QJsonArray array;
    for (const PlatformMapping& mapping : sorted)
        array.append(mappingToJson(mapping));
    QJsonObject root;
    root.insert(QStringLiteral("schema"), platformMapSchema());
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("mappings"), array);
    return root;
}

bool loadMappingsObject(const QJsonObject& object, QVector<PlatformMapping>& mappings,
                        QString& error) {
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return fail(error, QStringLiteral("Could not create a temporary directory"));
    Settings temporarySettings = defaultSettings();
    temporarySettings.dataDirectory = temporary.path();
    if (!writeJsonFileAtomic(QDir(temporary.path()).filePath(QStringLiteral("platform-map.json")),
                             object, error))
        return false;
    return loadPlatformMap(temporarySettings, mappings, error);
}

bool readBundleString(const QJsonObject& object, const QString& key, bool required, QString& out,
                      const QString& where, QString& error) {
    const auto value = object.constFind(key);
    if (value == object.constEnd()) {
        if (required)
            return fail(error, QStringLiteral("%1 is missing '%2'").arg(where, key));
        out.clear();
        return true;
    }
    if (!value->isString())
        return fail(error, QStringLiteral("%1 field '%2' must be a string").arg(where, key));
    out = value->toString();
    if (out.contains(QChar::Null))
        return fail(error, QStringLiteral("%1 field '%2' must not contain NUL").arg(where, key));
    return true;
}

bool parseBundleApp(const QJsonObject& object, qsizetype index, ContentApp& app, QString& error) {
    const QString where = QStringLiteral("apps[%1]").arg(index);
    static const QStringList known = {
        QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("kind"),
        QStringLiteral("category"), QStringLiteral("executable"),
        QStringLiteral("workingDirectory"), QStringLiteral("url"), QStringLiteral("artwork"),
        QStringLiteral("arguments")
    };
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!known.contains(it.key()))
            return fail(error, QStringLiteral("%1 has unknown field '%2'").arg(where, it.key()));
    }
    if (!readBundleString(object, QStringLiteral("id"), true, app.id, where, error)
        || !readBundleString(object, QStringLiteral("title"), true, app.title, where, error)
        || !readBundleString(object, QStringLiteral("kind"), true, app.kind, where, error)
        || !readBundleString(object, QStringLiteral("executable"), true, app.executable, where,
                             error)
        || !readBundleString(object, QStringLiteral("workingDirectory"), false,
                             app.workingDirectory, where, error)
        || !readBundleString(object, QStringLiteral("category"), false, app.category, where, error)
        || !readBundleString(object, QStringLiteral("artwork"), false, app.artwork, where, error))
        return false;
    app.id = app.id.trimmed();
    if (!isSafePackageId(app.id))
        return fail(error, QStringLiteral("%1 id '%2' is not a safe package id")
                              .arg(where, app.id));
    if (app.title.trimmed().isEmpty())
        return fail(error, QStringLiteral("%1 title must not be empty").arg(where));
    if (app.kind != QStringLiteral("game"))
        return fail(error, QStringLiteral("%1 kind must be 'game' for a manager backup").arg(where));
    app.category = QStringLiteral("game");
    app.url.clear();
    if (app.executable.trimmed().isEmpty())
        return fail(error, QStringLiteral("%1 executable must not be empty").arg(where));
    const auto arguments = object.constFind(QStringLiteral("arguments"));
    if (arguments != object.constEnd()) {
        if (!arguments->isArray())
            return fail(error, QStringLiteral("%1 arguments must be an array").arg(where));
        for (const QJsonValue& value : arguments->toArray()) {
            if (!value.isString())
                return fail(error, QStringLiteral("%1 arguments must be strings").arg(where));
            const QString argument = value.toString();
            if (argument.contains(QChar::Null))
                return fail(error, QStringLiteral("%1 arguments must not contain NUL").arg(where));
            app.arguments.append(argument);
        }
    }
    return true;
}

bool parseBundlePackage(const QJsonObject& object, qsizetype index, PackageRecord& record,
                        QString& error) {
    const QString id = object.value(QStringLiteral("id")).toString().trimmed();
    if (!isSafePackageId(id))
        return fail(error, QStringLiteral("packages[%1] has an unsafe id").arg(index));
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return fail(error, QStringLiteral("Could not create a temporary directory"));
    const QString directory = QDir(temporary.path()).filePath(id);
    if (!QDir().mkpath(directory))
        return fail(error, QStringLiteral("Could not create a temporary package directory"));
    if (!writeJsonFileAtomic(QDir(directory).filePath(QLatin1String(kPkgJsonName)), object,
                             error))
        return false;
    PackageRecord parsed;
    if (!readPackageRecord(directory, parsed, error)) {
        error = QStringLiteral("packages[%1] is invalid: %2").arg(index).arg(error);
        return false;
    }
    record = parsed;
    return true;
}

bool parseBundleArt(const QJsonObject& object, qsizetype index, QString& id, QByteArray& bytes,
                    QString& error) {
    const QString where = QStringLiteral("art[%1]").arg(index);
    static const QStringList known = {QStringLiteral("id"), QStringLiteral("base64")};
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!known.contains(it.key()))
            return fail(error, QStringLiteral("%1 has unknown field '%2'").arg(where, it.key()));
    }
    if (!readBundleString(object, QStringLiteral("id"), true, id, where, error))
        return false;
    if (!isSafePackageId(id))
        return fail(error, QStringLiteral("%1 id is not a safe package id").arg(where));
    QString encoded;
    if (!readBundleString(object, QStringLiteral("base64"), true, encoded, where, error))
        return false;
    bytes = QByteArray::fromBase64(encoded.toLatin1());
    if (bytes.isEmpty())
        return fail(error, QStringLiteral("%1 has empty art data").arg(where));
    if (bytes.size() > kMaxBackupArtBytes)
        return fail(error, QStringLiteral("%1 exceeds the %2 byte art limit")
                              .arg(where).arg(kMaxBackupArtBytes));
    QImage image;
    if (!image.loadFromData(bytes, "PNG"))
        return fail(error, QStringLiteral("%1 is not a readable PNG").arg(where));
    return true;
}

} // namespace

bool writeBackup(Settings& settings, const QString& file, const BackupOptions& options,
                 QJsonObject& summary, QString& error) {
    error.clear();
    summary = QJsonObject();
    QVector<ContentApp> apps;
    if (!ContentRegistry::load(registryPath(settings), apps, error))
        return false;
    QVector<PackageRecord> records = listPackages(settings, error);
    if (!error.isEmpty())
        return false;
    QHash<QString, PackageRecord> recordById;
    for (const PackageRecord& record : records) {
        if (record.packagePresent && record.registryPresent && record.issue.isEmpty())
            recordById.insert(record.id, record);
    }
    QJsonArray appArray;
    QJsonArray packageArray;
    QJsonArray tagArray;
    QJsonArray artArray;
    qint64 artBytesTotal = 0;
    int artOmitted = 0;
    int appCount = 0;
    for (const ContentApp& app : apps) {
        if (app.kind != QStringLiteral("game"))
            continue;
        const auto found = recordById.constFind(app.id);
        if (found == recordById.cend())
            continue;
        const PackageRecord& record = found.value();
        ++appCount;
        QJsonObject appObject;
        appObject.insert(QStringLiteral("id"), app.id);
        appObject.insert(QStringLiteral("title"), app.title);
        appObject.insert(QStringLiteral("kind"), app.kind);
        appObject.insert(QStringLiteral("category"), app.category);
        appObject.insert(QStringLiteral("executable"), app.executable);
        appObject.insert(QStringLiteral("workingDirectory"), app.workingDirectory);
        appObject.insert(QStringLiteral("artwork"), app.artwork);
        appObject.insert(QStringLiteral("arguments"), stringArray(app.arguments));
        appArray.append(appObject);
        packageArray.append(recordToJsonObject(record));
        tagArray.append(QJsonObject{{QStringLiteral("id"), record.id},
                                    {QStringLiteral("tags"), stringArray(record.tags)}});
        if (options.withPackagedArt && !record.art.isEmpty()) {
            const QFileInfo artInfo(record.art);
            if (artInfo.isFile() && artInfo.size() > 0 && artInfo.size() <= kMaxBackupArtBytes
                && artBytesTotal + artInfo.size() <= kMaxBackupArtTotal) {
                QByteArray bytes;
                QFile artFile(record.art);
                if (artFile.open(QIODevice::ReadOnly)) {
                    bytes = artFile.read(kMaxBackupArtBytes + 1);
                    if (!bytes.isEmpty() && bytes.size() <= kMaxBackupArtBytes) {
                        artArray.append(QJsonObject{
                            {QStringLiteral("id"), record.id},
                            {QStringLiteral("base64"),
                             QString::fromLatin1(bytes.toBase64())}});
                        artBytesTotal += bytes.size();
                    } else {
                        ++artOmitted;
                    }
                } else {
                    ++artOmitted;
                }
            } else {
                ++artOmitted;
            }
        }
    }
    QJsonArray collectionArray;
    QHash<QString, QStringList> collections;
    if (!readCollections(settings, collections, error))
        return false;
    QStringList collectionNames = collections.keys();
    collectionNames.sort();
    for (const QString& name : collectionNames) {
        collectionArray.append(QJsonObject{{QStringLiteral("name"), name},
                                           {QStringLiteral("ids"),
                                            stringArray(collections.value(name))}});
    }
    QVector<PlatformMapping> mappings;
    if (!loadPlatformMap(settings, mappings, error))
        return false;
    const QVector<EmulatorProfile> profiles = loadProfiles(settings, error);
    if (!error.isEmpty())
        return false;
    QJsonArray overlayArray;
    for (const EmulatorProfile& profile : profiles) {
        if (profile.builtIn && profile.executable.isEmpty()
            && profile.workingDirectory.isEmpty())
            continue;
        overlayArray.append(emulatorProfileToJson(profile));
    }
    QJsonObject root;
    root.insert(QStringLiteral("schema"), QLatin1String(kBackupSchema));
    root.insert(QStringLiteral("createdAt"), timestampUtc());
    root.insert(QStringLiteral("sourceDataDirectory"), settings.dataDirectory);
    root.insert(QStringLiteral("apps"), appArray);
    root.insert(QStringLiteral("packages"), packageArray);
    root.insert(QStringLiteral("tags"), tagArray);
    root.insert(QStringLiteral("collections"), collectionArray);
    root.insert(QStringLiteral("platformMap"), platformMapToJson(mappings));
    root.insert(QStringLiteral("emulatorOverlay"), overlayArray);
    root.insert(QStringLiteral("art"), artArray);
    QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (bytes.size() > kMaxJsonBytes && !artArray.isEmpty()) {
        root.insert(QStringLiteral("art"), QJsonArray());
        root.insert(QStringLiteral("artOmitted"), QStringLiteral("bundle would exceed 4 MiB"));
        bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
        artOmitted += int(artArray.size());
        artArray = QJsonArray();
    }
    if (bytes.size() > kMaxJsonBytes)
        return fail(error, QStringLiteral("The backup bundle exceeds the %1 byte limit")
                              .arg(kMaxJsonBytes));
    if (!writeFileAtomic(file, bytes, error))
        return false;
    summary.insert(QStringLiteral("schema"), QLatin1String(kBackupSchema));
    summary.insert(QStringLiteral("file"), file);
    summary.insert(QStringLiteral("appCount"), appCount);
    summary.insert(QStringLiteral("packageCount"), packageArray.size());
    summary.insert(QStringLiteral("collectionCount"), collectionArray.size());
    summary.insert(QStringLiteral("mappingCount"), mappings.size());
    summary.insert(QStringLiteral("emulatorOverlayCount"), overlayArray.size());
    summary.insert(QStringLiteral("artIncluded"), artArray.size());
    summary.insert(QStringLiteral("artOmitted"), artOmitted);
    summary.insert(QStringLiteral("bytes"), bytes.size());
    return true;
}

bool parsePathRemap(const QString& text, PathRemap& remap, QString& error) {
    const int equals = text.indexOf(QLatin1Char('='));
    if (equals <= 0 || equals == text.size() - 1)
        return fail(error, QStringLiteral("--remap needs <from>=<to>, got '%1'").arg(text));
    remap.from = QDir::fromNativeSeparators(text.left(equals).trimmed());
    remap.to = QDir::fromNativeSeparators(text.mid(equals + 1).trimmed());
    if (remap.from.isEmpty() || remap.to.isEmpty())
        return fail(error, QStringLiteral("--remap needs non-empty paths in '%1'").arg(text));
    return true;
}

bool restoreBackup(Settings& settings, const QString& file, const QVector<PathRemap>& remaps,
                   bool replace, QJsonArray& actions, QJsonArray& conflicts, QString& error) {
    error.clear();
    actions = QJsonArray();
    conflicts = QJsonArray();
    QJsonObject root;
    if (!readJsonFile(file, root, error))
        return false;
    static const QStringList known = {
        QStringLiteral("schema"), QStringLiteral("createdAt"),
        QStringLiteral("sourceDataDirectory"), QStringLiteral("apps"),
        QStringLiteral("packages"), QStringLiteral("tags"), QStringLiteral("collections"),
        QStringLiteral("platformMap"), QStringLiteral("emulatorOverlay"),
        QStringLiteral("art"), QStringLiteral("artOmitted")
    };
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (!known.contains(it.key()))
            return fail(error, QStringLiteral("Backup has unknown field '%1'").arg(it.key()));
    }
    if (root.value(QStringLiteral("schema")).toString() != QLatin1String(kBackupSchema))
        return fail(error, QStringLiteral("Backup schema must be '%1'")
                              .arg(QLatin1String(kBackupSchema)));
    const auto appsValue = root.constFind(QStringLiteral("apps"));
    const auto packagesValue = root.constFind(QStringLiteral("packages"));
    if (appsValue == root.constEnd() || !appsValue->isArray())
        return fail(error, QStringLiteral("Backup field 'apps' must be an array"));
    if (packagesValue == root.constEnd() || !packagesValue->isArray())
        return fail(error, QStringLiteral("Backup field 'packages' must be an array"));
    if (!root.value(QStringLiteral("platformMap")).isObject())
        return fail(error, QStringLiteral("Backup field 'platformMap' must be an object"));

    QHash<QString, ContentApp> appById;
    const QJsonArray appArray = appsValue->toArray();
    for (qsizetype index = 0; index < appArray.size(); ++index) {
        if (!appArray.at(index).isObject())
            return fail(error, QStringLiteral("apps[%1] must be an object").arg(index));
        ContentApp app;
        if (!parseBundleApp(appArray.at(index).toObject(), index, app, error))
            return false;
        if (appById.contains(app.id))
            return fail(error, QStringLiteral("Duplicate app id '%1' in the backup").arg(app.id));
        appById.insert(app.id, app);
    }
    QVector<PackageRecord> bundlePackages;
    const QJsonArray packageArray = packagesValue->toArray();
    QSet<QString> packageIds;
    for (qsizetype index = 0; index < packageArray.size(); ++index) {
        if (!packageArray.at(index).isObject())
            return fail(error, QStringLiteral("packages[%1] must be an object").arg(index));
        PackageRecord record;
        if (!parseBundlePackage(packageArray.at(index).toObject(), index, record, error))
            return false;
        if (packageIds.contains(record.id))
            return fail(error, QStringLiteral("Duplicate package id '%1' in the backup")
                                  .arg(record.id));
        if (!appById.contains(record.id))
            return fail(error, QStringLiteral("Backup package '%1' has no registration")
                                  .arg(record.id));
        packageIds.insert(record.id);
        bundlePackages.append(record);
    }
    for (auto it = appById.begin(); it != appById.end(); ++it) {
        if (!packageIds.contains(it.key()))
            return fail(error, QStringLiteral("Backup registration '%1' has no package record")
                                  .arg(it.key()));
    }
    QHash<QString, QStringList> bundleCollections;
    const auto collectionsValue = root.constFind(QStringLiteral("collections"));
    if (collectionsValue != root.constEnd()) {
        if (!collectionsValue->isArray())
            return fail(error, QStringLiteral("Backup field 'collections' must be an array"));
        const QJsonArray array = collectionsValue->toArray();
        for (qsizetype index = 0; index < array.size(); ++index) {
            if (!array.at(index).isObject())
                return fail(error, QStringLiteral("collections[%1] must be an object").arg(index));
            const QJsonObject item = array.at(index).toObject();
            for (auto it = item.begin(); it != item.end(); ++it) {
                if (it.key() != QStringLiteral("name") && it.key() != QStringLiteral("ids"))
                    return fail(error, QStringLiteral("collections[%1] has unknown field '%2'")
                                          .arg(index).arg(it.key()));
            }
            QString name;
            if (!readBundleString(item, QStringLiteral("name"), true, name,
                                  QStringLiteral("collections[%1]").arg(index), error))
                return false;
            name = name.trimmed();
            if (!validateCollectionName(name, error))
                return false;
            if (bundleCollections.contains(name))
                return fail(error, QStringLiteral("Duplicate collection '%1' in the backup")
                                      .arg(name));
            const auto idsValue = item.constFind(QStringLiteral("ids"));
            if (idsValue == item.constEnd() || !idsValue->isArray())
                return fail(error, QStringLiteral("collections[%1].ids must be an array")
                                      .arg(index));
            for (const QJsonValue& value : idsValue->toArray()) {
                if (!value.isString())
                    return fail(error, QStringLiteral("collections[%1].ids must be strings")
                                          .arg(index));
                const QString id = value.toString();
                if (!isSafePackageId(id))
                    return fail(error, QStringLiteral("collections[%1] has an unsafe id")
                                          .arg(index));
                if (!bundleCollections[name].contains(id))
                    bundleCollections[name].append(id);
            }
        }
    }
    QHash<QString, QByteArray> bundleArt;
    const auto artValue = root.constFind(QStringLiteral("art"));
    if (artValue != root.constEnd()) {
        if (!artValue->isArray())
            return fail(error, QStringLiteral("Backup field 'art' must be an array"));
        const QJsonArray array = artValue->toArray();
        for (qsizetype index = 0; index < array.size(); ++index) {
            if (!array.at(index).isObject())
                return fail(error, QStringLiteral("art[%1] must be an object").arg(index));
            QString id;
            QByteArray bytes;
            if (!parseBundleArt(array.at(index).toObject(), index, id, bytes, error))
                return false;
            if (!packageIds.contains(id))
                return fail(error, QStringLiteral("art[%1] refers to unknown package '%2'")
                                      .arg(index).arg(id));
            bundleArt.insert(id, bytes);
        }
    }

    const QString rootDirectory = packageRoot(settings);
    const bool targetMapExists = QFileInfo::exists(platformMapPath(settings));

    QVector<ContentApp> existingApps;
    if (!ContentRegistry::load(registryPath(settings), existingApps, error))
        return false;
    QSet<QString> registeredIds;
    for (const ContentApp& app : existingApps)
        registeredIds.insert(app.id);
    for (const PackageRecord& record : bundlePackages) {
        const bool packageExists = QFileInfo::exists(packageDirectoryFor(settings, record.id));
        if (packageExists || registeredIds.contains(record.id)) {
            QJsonObject conflict;
            conflict.insert(QStringLiteral("id"), record.id);
            conflict.insert(QStringLiteral("packageExists"), packageExists);
            conflict.insert(QStringLiteral("registrationExists"),
                            registeredIds.contains(record.id));
            conflicts.append(conflict);
        }
    }
    if (!conflicts.isEmpty() && !replace) {
        return fail(error, QStringLiteral("%1 package(s) already exist in '%2'; use --replace to "
                                          "overwrite them")
                              .arg(conflicts.size()).arg(settings.dataDirectory));
    }
    if (!ensureDataLayout(settings, error))
        return false;

    SnapshotSet snapshots;
    if (!snapshots.add(registryPath(settings), error)
        || !snapshots.add(collectionsPath(settings), error)
        || !snapshots.add(platformMapPath(settings), error)
        || !snapshots.add(emulatorsPath(settings), error))
        return false;

    int restoredPackages = 0;
    int replacedPackages = 0;
    int embeddedArt = 0;
    QStringList missingRoms;
    QVector<QPair<QString, QString>> applied; // final directory, previous directory backup

    const auto rollback = [&](const QString& message) -> bool {
        QString rollbackError;
        for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
            const QString finalDirectory = it->first;
            const QString backup = it->second;
            if (QFileInfo::exists(finalDirectory)) {
                QString ignored;
                if (!removeDirectoryInside(rootDirectory, finalDirectory, ignored))
                    logError(QStringLiteral("Could not remove '%1' while rolling back")
                                 .arg(finalDirectory));
            }
            if (!backup.isEmpty() && QFileInfo::exists(backup)
                && !QDir().rename(backup, finalDirectory))
                logError(QStringLiteral("Could not restore the previous package '%1'")
                             .arg(finalDirectory));
        }
        if (!snapshots.restore(rollbackError))
            logError(QStringLiteral("Restore rollback could not restore every file: %1")
                         .arg(rollbackError));
        return fail(error, message + QStringLiteral(" The restore was rolled back."));
    };

    // Emulator overlay first so the restored registrations resolve.
    QVector<EmulatorProfile> profiles;
    const auto overlayValue = root.constFind(QStringLiteral("emulatorOverlay"));
    if (overlayValue != root.constEnd()) {
        if (!overlayValue->isArray())
            return fail(error, QStringLiteral("Backup field 'emulatorOverlay' must be an array"));
        const QJsonArray overlayArray = overlayValue->toArray();
        if (!overlayArray.isEmpty()) {
            profiles = loadProfiles(settings, error);
            if (!error.isEmpty())
                return false;
            int merged = 0;
            for (qsizetype index = 0; index < overlayArray.size(); ++index) {
                if (!overlayArray.at(index).isObject())
                    return fail(error, QStringLiteral("emulatorOverlay[%1] must be an object")
                                          .arg(index));
                EmulatorProfile profile;
                if (!emulatorProfileFromJson(overlayArray.at(index).toObject(), index, profile,
                                             error))
                    return false;
                EmulatorProfile* existing = findProfile(profiles, profile.id);
                if (existing) {
                    const bool builtIn = existing->builtIn;
                    *existing = profile;
                    existing->builtIn = builtIn;
                } else {
                    profiles.append(profile);
                }
                ++merged;
            }
            QVector<EmulatorProfile> overlayProfiles;
            for (const EmulatorProfile& profile : profiles) {
                if (!profile.builtIn || !profile.executable.isEmpty()
                    || !profile.workingDirectory.isEmpty())
                    overlayProfiles.append(profile);
            }
            if (!saveProfiles(settings, overlayProfiles, error))
                return false;
            actions.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("emulators")},
                                       {QStringLiteral("action"), QStringLiteral("merged")},
                                       {QStringLiteral("count"), merged}});
        }
    }

    QVector<PlatformMapping> bundleMappings;
    if (!loadMappingsObject(root.value(QStringLiteral("platformMap")).toObject(), bundleMappings,
                            error))
        return false;
    if (!targetMapExists || replace) {
        if (!savePlatformMap(settings, bundleMappings, error))
            return false;
        actions.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("platform-map")},
                                   {QStringLiteral("action"),
                                    targetMapExists ? QStringLiteral("replaced")
                                                    : QStringLiteral("written")},
                                   {QStringLiteral("count"), bundleMappings.size()}});
    } else {
        QVector<PlatformMapping> targetMappings;
        if (!loadPlatformMap(settings, targetMappings, error))
            return false;
        int added = 0;
        for (const PlatformMapping& mapping : bundleMappings) {
            PlatformMapping* existing = nullptr;
            for (PlatformMapping& candidate : targetMappings) {
                if (candidate.platform.compare(mapping.platform, Qt::CaseInsensitive) == 0) {
                    existing = &candidate;
                    break;
                }
            }
            if (existing) {
                *existing = mapping;
            } else {
                targetMappings.append(mapping);
                ++added;
            }
        }
        if (!savePlatformMap(settings, targetMappings, error))
            return false;
        actions.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("platform-map")},
                                   {QStringLiteral("action"), QStringLiteral("merged")},
                                   {QStringLiteral("count"), bundleMappings.size()},
                                   {QStringLiteral("added"), added}});
    }

    for (const PackageRecord& bundleRecord : bundlePackages) {
        PackageRecord record = bundleRecord;
        record.romPath = remappedPath(record.romPath, remaps);
        for (QString& argument : record.arguments)
            argument = remappedText(argument, remaps);
        record.art = QDir(packageDirectoryFor(settings, record.id))
                         .filePath(QLatin1String(kIconName));
        if (QFileInfo::exists(record.romPath)) {
            RomFingerprint fingerprint;
            QString hashError;
            if (!computeRomFingerprint(record.romPath, fingerprint, hashError))
                return rollback(QStringLiteral("Could not fingerprint the ROM for '%1': %2")
                                    .arg(record.id, hashError));
            record.romSize = fingerprint.size;
            record.romMtime = fingerprint.mtime;
            record.hashLabel = fingerprint.hashLabel;
            record.hashValue = fingerprint.hashValue;
        } else {
            missingRoms.append(record.id);
        }
        const QString finalDirectory = QDir(rootDirectory).filePath(record.id);
        const QString staging = newSiblingName(rootDirectory, QStringLiteral(".restore-"));
        if (!QDir().mkpath(staging))
            return rollback(QStringLiteral("Could not create a staging directory for '%1'")
                                .arg(record.id));
        QString sourceArt;
        const QByteArray artBytes = bundleArt.value(record.id);
        if (!artBytes.isEmpty()) {
            sourceArt = QDir(staging).filePath(QStringLiteral("art-source.png"));
            if (!writeFileAtomic(sourceArt, artBytes, error)) {
                QString ignored;
                removeDirectoryInside(rootDirectory, staging, ignored);
                return rollback(error);
            }
            ++embeddedArt;
        }
        if (!writeSfoAndIcon(staging, record.title, record.platform, sourceArt, error)) {
            QString ignored;
            removeDirectoryInside(rootDirectory, staging, ignored);
            return rollback(error);
        }
        if (!sourceArt.isEmpty())
            QFile::remove(sourceArt);
        record.packagePath = staging;
        if (!writeRecordFile(record, error)) {
            QString ignored;
            removeDirectoryInside(rootDirectory, staging, ignored);
            return rollback(error);
        }
        QString previousDirectory;
        if (QFileInfo::exists(finalDirectory)) {
            previousDirectory = newSiblingName(rootDirectory, QStringLiteral(".restore-old-"));
            if (!QDir().rename(finalDirectory, previousDirectory)) {
                QString ignored;
                removeDirectoryInside(rootDirectory, staging, ignored);
                return rollback(QStringLiteral("Could not move the existing package aside: %1")
                                    .arg(finalDirectory));
            }
            ++replacedPackages;
        } else {
            ++restoredPackages;
        }
        if (!QDir().rename(staging, finalDirectory)) {
            if (!previousDirectory.isEmpty())
                QDir().rename(previousDirectory, finalDirectory);
            QString ignored;
            removeDirectoryInside(rootDirectory, staging, ignored);
            return rollback(QStringLiteral("Could not commit the package directory: %1")
                                .arg(finalDirectory));
        }
        applied.append({finalDirectory, previousDirectory});
        ContentApp app = appById.value(record.id);
        app.executable = remappedPath(app.executable, remaps);
        app.workingDirectory = remappedPath(app.workingDirectory, remaps);
        app.artwork = QDir(finalDirectory).filePath(QLatin1String(kIconName));
        for (QString& argument : app.arguments)
            argument = remappedText(argument, remaps);
        QString upsertError;
        if (!ContentRegistry::upsert(registryPath(settings), app, upsertError))
            return rollback(QStringLiteral("Could not restore the registration for '%1': %2")
                                .arg(record.id, upsertError));
        actions.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("package")},
                                   {QStringLiteral("id"), record.id},
                                   {QStringLiteral("action"),
                                    previousDirectory.isEmpty() ? QStringLiteral("created")
                                                                : QStringLiteral("replaced")},
                                   {QStringLiteral("romPath"), record.romPath}});
        actions.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("registration")},
                                   {QStringLiteral("id"), record.id},
                                   {QStringLiteral("action"), QStringLiteral("restored")}});
    }

    QHash<QString, QStringList> targetCollections;
    if (!readCollections(settings, targetCollections, error))
        return rollback(error);
    QSet<QString> restoredIds;
    for (const PackageRecord& record : bundlePackages)
        restoredIds.insert(record.id);
    int mergedCollections = 0;
    for (auto it = bundleCollections.begin(); it != bundleCollections.end(); ++it) {
        QStringList& members = targetCollections[it.key()];
        int added = 0;
        for (const QString& id : it.value()) {
            if (!restoredIds.contains(id))
                continue;
            if (!members.contains(id)) {
                members.append(id);
                ++added;
            }
        }
        if (members.isEmpty())
            targetCollections.remove(it.key());
        else
            ++mergedCollections;
        actions.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("collection")},
                                   {QStringLiteral("name"), it.key()},
                                   {QStringLiteral("action"), QStringLiteral("merged")},
                                   {QStringLiteral("ids"), added}});
    }
    if (mergedCollections > 0 && !writeCollections(settings, targetCollections, error))
        return rollback(error);

    for (auto it = applied.begin(); it != applied.end(); ++it) {
        if (it->second.isEmpty())
            continue;
        QString removeError;
        if (!removeDirectoryInside(rootDirectory, it->second, removeError))
            logWarning(QStringLiteral("The previous package backup could not be deleted: %1")
                           .arg(removeError));
    }
    actions.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("summary")},
                               {QStringLiteral("created"), restoredPackages},
                               {QStringLiteral("replaced"), replacedPackages},
                               {QStringLiteral("art"), embeddedArt},
                               {QStringLiteral("missingRoms"), stringArray(missingRoms)}});
    return true;
}

} // namespace xmb::manager

