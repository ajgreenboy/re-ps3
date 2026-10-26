#include "content_registry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace xmb {
namespace {

constexpr qint64 MaxRegistryBytes = 1024 * 1024;
constexpr qsizetype MaxAppCount = 1000;

bool fail(QString& error, const QString& message) {
    error = message;
    return false;
}

bool hasOnlyKnownKeys(const QJsonObject& object, QString& error, const QString& where) {
    static const QStringList known = {
        QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("kind"),
        QStringLiteral("category"), QStringLiteral("executable"),
        QStringLiteral("workingDirectory"), QStringLiteral("url"),
        QStringLiteral("artwork"), QStringLiteral("arguments")
    };
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!known.contains(it.key()))
            return fail(error, QStringLiteral("%1 has unknown field '%2'").arg(where, it.key()));
    }
    return true;
}

bool readString(const QJsonObject& object, const QString& key, QString& output,
                bool required, const QString& where, QString& error) {
    const auto it = object.constFind(key);
    if (it == object.constEnd()) {
        if (required)
            return fail(error, QStringLiteral("%1 is missing '%2'").arg(where, key));
        output.clear();
        return true;
    }
    if (!it->isString())
        return fail(error, QStringLiteral("%1 field '%2' must be a string").arg(where, key));
    output = it->toString();
    if (output.contains(QChar::Null))
        return fail(error, QStringLiteral("%1 field '%2' must not contain a NUL character").arg(where, key));
    return true;
}

void normalizePath(QString& path, const QString& baseDirectory) {
    if (path.isEmpty())
        return;
    QFileInfo info(path);
    if (!info.isAbsolute())
        info.setFile(QDir(baseDirectory).absoluteFilePath(path));
    info.setFile(QDir::cleanPath(info.absoluteFilePath()));
    path = info.absoluteFilePath();
}

bool validateExistingPath(const QString& path, bool directory, const QString& field,
                          QString& error) {
    if (path.isEmpty())
        return true;
    const QFileInfo info(path);
    if (!info.exists() || (directory ? !info.isDir() : !info.isFile()))
        return fail(error, QStringLiteral("%1 path does not name an existing %2: %3")
                    .arg(field, directory ? QStringLiteral("directory") : QStringLiteral("file"), path));
    return true;
}

bool validateImportFiles(const QVector<ContentApp>& apps, QString& error) {
    for (qsizetype i = 0; i < apps.size(); ++i) {
        const ContentApp& app = apps.at(i);
        const QString where = QStringLiteral("apps[%1]").arg(i);
        if (!validateExistingPath(app.executable, false, where + QStringLiteral(" executable"), error)
            || !validateExistingPath(app.workingDirectory, true, where + QStringLiteral(" workingDirectory"), error)
            || !validateExistingPath(app.artwork, false, where + QStringLiteral(" artwork"), error))
            return false;
    }
    return true;
}

bool validateAppForWrite(ContentApp& app, const QString& baseDirectory, QString& error) {
    for(const auto& value : {app.id,app.title,app.kind,app.category,app.executable,app.workingDirectory,app.url,app.artwork})
        if(value.contains(QChar::Null))return fail(error, QStringLiteral("App fields must not contain NUL characters"));
    app.id = app.id.trimmed();
    app.title = app.title.trimmed();
    app.kind = app.kind.trimmed();
    app.category = app.category.trimmed();
    if (app.id.isEmpty())
        return fail(error, QStringLiteral("App id must not be empty"));
    for (const QChar ch : app.id) {
        const ushort c = ch.unicode();
        const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        if (!allowed)
            return fail(error, QStringLiteral("App id may contain only ASCII letters, digits, _, . and -"));
    }
    if (app.title.isEmpty())
        return fail(error, QStringLiteral("App title must not be empty"));
    if (app.kind != QStringLiteral("game") && app.kind != QStringLiteral("web"))
        return fail(error, QStringLiteral("App kind must be 'game' or 'web'"));
    static const QStringList categories = {
        QStringLiteral("game"), QStringLiteral("tv"), QStringLiteral("music"),
        QStringLiteral("video"), QStringLiteral("network")
    };
    if (!categories.contains(app.category))
        return fail(error, QStringLiteral("Category must be game, tv, music, video or network"));

    normalizePath(app.executable, baseDirectory);
    normalizePath(app.workingDirectory, baseDirectory);
    normalizePath(app.artwork, baseDirectory);
    if (app.kind == QStringLiteral("game")) {
        if (app.executable.isEmpty())
            return fail(error, QStringLiteral("A game requires an executable path"));
        QFileInfo executable(app.executable);
        if (!executable.isAbsolute())
            return fail(error, QStringLiteral("Game executable must resolve to an absolute path"));
#ifdef Q_OS_WIN
        if (executable.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0)
            return fail(error, QStringLiteral("Game executable must have a .exe extension"));
#endif
    } else {
        const QUrl parsed(app.url, QUrl::StrictMode);
        if (app.url.isEmpty() || !parsed.isValid()
            || (parsed.scheme() != QStringLiteral("http") && parsed.scheme() != QStringLiteral("https"))
            || parsed.host().isEmpty() || !parsed.userInfo().isEmpty()
            || parsed.authority().contains(QLatin1Char('@')))
            return fail(error, QStringLiteral("Web URL must be http(s), include a host, and contain no credentials"));
        app.executable.clear();
        app.arguments.clear();
    }
    for (const QString& argument : app.arguments) {
        if (argument.contains(QChar::Null))
            return fail(error, QStringLiteral("Arguments must not contain NUL characters"));
    }
    if (!validateExistingPath(app.executable, false, QStringLiteral("Executable"), error)
        || !validateExistingPath(app.workingDirectory, true, QStringLiteral("Working directory"), error)
        || !validateExistingPath(app.artwork, false, QStringLiteral("Artwork"), error))
        return false;
    return true;
}

bool parseApp(const QJsonValue& value, qsizetype index, const QString& baseDirectory,
              ContentApp& app, QString& error) {
    const QString where = QStringLiteral("apps[%1]").arg(index);
    if (!value.isObject())
        return fail(error, QStringLiteral("%1 must be an object").arg(where));
    const QJsonObject object = value.toObject();
    if (!hasOnlyKnownKeys(object, error, where))
        return false;

    if (!readString(object, QStringLiteral("id"), app.id, true, where, error)
        || !readString(object, QStringLiteral("title"), app.title, true, where, error)
        || !readString(object, QStringLiteral("kind"), app.kind, true, where, error)
        || !readString(object, QStringLiteral("category"), app.category, false, where, error)
        || !readString(object, QStringLiteral("executable"), app.executable, false, where, error)
        || !readString(object, QStringLiteral("workingDirectory"), app.workingDirectory, false, where, error)
        || !readString(object, QStringLiteral("url"), app.url, false, where, error)
        || !readString(object, QStringLiteral("artwork"), app.artwork, false, where, error))
        return false;

    if (app.id.isEmpty())
        return fail(error, QStringLiteral("%1 id must not be empty").arg(where));
    for (const QChar ch : app.id) {
        const ushort c = ch.unicode();
        const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        if (!allowed)
            return fail(error, QStringLiteral("%1 id may contain only ASCII letters, digits, _, . and -").arg(where));
    }
    if (app.title.trimmed().isEmpty())
        return fail(error, QStringLiteral("%1 title must not be empty").arg(where));
    if (app.kind != QStringLiteral("game") && app.kind != QStringLiteral("web"))
        return fail(error, QStringLiteral("%1 kind must be 'game' or 'web'").arg(where));

    if (!object.contains(QStringLiteral("category")))
        app.category = app.kind == QStringLiteral("game") ? QStringLiteral("game") : QStringLiteral("tv");
    static const QStringList categories = {
        QStringLiteral("game"), QStringLiteral("tv"), QStringLiteral("music"),
        QStringLiteral("video"), QStringLiteral("network")
    };
    if (!categories.contains(app.category))
        return fail(error, QStringLiteral("%1 category must be game, tv, music, video or network").arg(where));

    const auto arguments = object.constFind(QStringLiteral("arguments"));
    if (arguments != object.constEnd()) {
        if (!arguments->isArray())
            return fail(error, QStringLiteral("%1 field 'arguments' must be an array of strings").arg(where));
        const QJsonArray array = arguments->toArray();
        app.arguments.reserve(array.size());
        for (qsizetype argIndex = 0; argIndex < array.size(); ++argIndex) {
            if (!array.at(argIndex).isString())
                return fail(error, QStringLiteral("%1 arguments[%2] must be a string").arg(where).arg(argIndex));
            const QString argument = array.at(argIndex).toString();
            if (argument.contains(QChar::Null))
                return fail(error, QStringLiteral("%1 arguments[%2] must not contain a NUL character").arg(where).arg(argIndex));
            app.arguments.append(argument);
        }
    }

    normalizePath(app.executable, baseDirectory);
    if (app.kind == QStringLiteral("game")) {
        if (app.executable.isEmpty())
            return fail(error, QStringLiteral("%1 game requires an executable path").arg(where));
        QFileInfo executable(app.executable);
        if (!executable.isAbsolute())
            return fail(error, QStringLiteral("%1 game executable could not be resolved to an absolute path").arg(where));
#ifdef Q_OS_WIN
        if (executable.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0)
            return fail(error, QStringLiteral("%1 game executable must have a .exe extension").arg(where));
#endif
    } else {
        const QUrl parsed(app.url, QUrl::StrictMode);
        if (app.url.isEmpty() || !parsed.isValid()
            || (parsed.scheme() != QStringLiteral("http") && parsed.scheme() != QStringLiteral("https"))
            || parsed.host().isEmpty() || !parsed.userInfo().isEmpty()
            || parsed.authority().contains(QLatin1Char('@')))
            return fail(error, QStringLiteral("%1 web URL must be http(s), include a host, and contain no credentials").arg(where));
    }

    normalizePath(app.workingDirectory, baseDirectory);
    normalizePath(app.artwork, baseDirectory);
    return true;
}

bool readRegistry(const QString& path, QVector<ContentApp>& apps, QString& error,
                  bool missingIsEmpty) {
    QFile file(path);
    if (!file.exists()) {
        if (missingIsEmpty) {
            apps.clear();
            return true;
        }
        return fail(error, QStringLiteral("Registry file does not exist: %1").arg(path));
    }
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Could not open registry '%1': %2").arg(path, file.errorString()));
    if (file.size() > MaxRegistryBytes)
        return fail(error, QStringLiteral("Registry exceeds the 1 MiB limit: %1").arg(path));
    const QByteArray bytes = file.read(MaxRegistryBytes + 1);
    if (bytes.size() > MaxRegistryBytes)
        return fail(error, QStringLiteral("Registry exceeds the 1 MiB limit: %1").arg(path));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("Invalid registry JSON in '%1': %2")
                    .arg(path, parseError.errorString()));
    const QJsonObject root = document.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.key() != QStringLiteral("version") && it.key() != QStringLiteral("apps"))
            return fail(error, QStringLiteral("Registry has unknown field '%1'").arg(it.key()));
    }
    const auto version = root.constFind(QStringLiteral("version"));
    if (version == root.constEnd() || !version->isDouble() || version->toDouble() != 1.0)
        return fail(error, QStringLiteral("Registry version must be the number 1"));
    const auto appsValue = root.constFind(QStringLiteral("apps"));
    if (appsValue == root.constEnd() || !appsValue->isArray())
        return fail(error, QStringLiteral("Registry field 'apps' must be an array"));
    const QJsonArray array = appsValue->toArray();
    if (array.size() > MaxAppCount)
        return fail(error, QStringLiteral("Registry exceeds the 1000-entry limit"));

    QVector<ContentApp> parsedApps;
    parsedApps.reserve(array.size());
    QStringList ids;
    const QString baseDirectory = QFileInfo(path).absolutePath();
    for (qsizetype i = 0; i < array.size(); ++i) {
        ContentApp app;
        if (!parseApp(array.at(i), i, baseDirectory, app, error))
            return false;
        if (ids.contains(app.id))
            return fail(error, QStringLiteral("Duplicate app id '%1' in registry").arg(app.id));
        ids.append(app.id);
        parsedApps.append(std::move(app));
    }
    apps = std::move(parsedApps);
    return true;
}

QJsonObject serializeApp(const ContentApp& app) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), app.id);
    object.insert(QStringLiteral("title"), app.title);
    object.insert(QStringLiteral("kind"), app.kind);
    object.insert(QStringLiteral("category"), app.category);
    if (!app.executable.isEmpty()) object.insert(QStringLiteral("executable"), app.executable);
    if (!app.workingDirectory.isEmpty()) object.insert(QStringLiteral("workingDirectory"), app.workingDirectory);
    if (!app.url.isEmpty()) object.insert(QStringLiteral("url"), app.url);
    if (!app.artwork.isEmpty()) object.insert(QStringLiteral("artwork"), app.artwork);
    QJsonArray arguments;
    for (const QString& argument : app.arguments)
        arguments.append(argument);
    object.insert(QStringLiteral("arguments"), arguments);
    return object;
}

bool writeRegistry(const QString& path, const QVector<ContentApp>& apps, QString& error) {
    QJsonArray entries;
    for (const ContentApp& app : apps)
        entries.append(serializeApp(app));
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("apps"), entries);
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (bytes.size() > MaxRegistryBytes)
        return fail(error, QStringLiteral("Merged registry exceeds the 1 MiB limit"));

    if(!QDir().mkpath(QFileInfo(path).absolutePath()))
        return fail(error, QStringLiteral("Could not create the app registry directory"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, QStringLiteral("Could not write registry '%1': %2").arg(path, file.errorString()));
    if (file.write(bytes) != bytes.size()) {
        const QString message = file.errorString();
        file.cancelWriting();
        return fail(error, QStringLiteral("Could not write registry '%1': %2").arg(path, message));
    }
    if (!file.commit())
        return fail(error, QStringLiteral("Could not atomically replace registry '%1': %2").arg(path, file.errorString()));
    return true;
}

} // namespace

bool ContentRegistry::load(const QString& path, QVector<ContentApp>& apps, QString& error) {
    error.clear();
    QVector<ContentApp> parsed;
    if (!readRegistry(path, parsed, error, true))
        return false;
    apps = std::move(parsed);
    return true;
}

bool ContentRegistry::importFile(const QString& sourcePath, const QString& targetPath, QString& error) {
    error.clear();
    QVector<ContentApp> incoming;
    if (!readRegistry(sourcePath, incoming, error, false))
        return false;
    if (!validateImportFiles(incoming, error))
        return false;

    QVector<ContentApp> merged;
    if (!readRegistry(targetPath, merged, error, true))
        return false;
    for (const ContentApp& app : incoming) {
        const auto existing = std::find_if(merged.begin(), merged.end(), [&app](const ContentApp& candidate) {
            return candidate.id == app.id;
        });
        if (existing == merged.end())
            merged.append(app);
        else
            *existing = app;
    }
    if (merged.size() > MaxAppCount)
        return fail(error, QStringLiteral("Merged registry exceeds the 1000-entry limit"));
    return writeRegistry(targetPath, merged, error);
}

bool ContentRegistry::upsert(const QString& path, ContentApp app, QString& error) {
    error.clear();
    QVector<ContentApp> current;
    if (!readRegistry(path, current, error, true))
        return false;
    if (!validateAppForWrite(app, QFileInfo(path).absolutePath(), error))
        return false;
    const auto existing = std::find_if(current.begin(), current.end(), [&app](const ContentApp& candidate) {
        return candidate.id == app.id;
    });
    if (existing == current.end())
        current.append(std::move(app));
    else
        *existing = std::move(app);
    if (current.size() > MaxAppCount)
        return fail(error, QStringLiteral("Registry exceeds the 1000-entry limit"));
    return writeRegistry(path, current, error);
}

bool ContentRegistry::remove(const QString& path, const QString& id, QString& error) {
    error.clear();
    QVector<ContentApp> current;
    if (!readRegistry(path, current, error, true))
        return false;
    const auto existing = std::find_if(current.begin(), current.end(), [&id](const ContentApp& candidate) {
        return candidate.id == id;
    });
    if (existing == current.end())
        return fail(error, QStringLiteral("App registration no longer exists: %1").arg(id));
    current.erase(existing);
    return writeRegistry(path, current, error);
}

} // namespace xmb
