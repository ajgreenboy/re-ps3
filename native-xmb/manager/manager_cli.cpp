#include "manager_cli.h"

#include "manager_emulators.h"
#include "manager_package.h"
#include "manager_scanner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>

#include <algorithm>

namespace xmb::manager {
namespace {

const char* kSchema = "xmb-manager/1";
const char* kReportSchema = "xmb-manager-report/2";

const QStringList& verbNames() {
    static const QStringList names = {
        QStringLiteral("--list-emulators"), QStringLiteral("--detect-emulators"),
        QStringLiteral("--scan"), QStringLiteral("--import"), QStringLiteral("--import-pkg"),
        QStringLiteral("--list"), QStringLiteral("--remove"), QStringLiteral("--validate"),
        QStringLiteral("--export-report"), QStringLiteral("--map-platform"),
        QStringLiteral("--list-mappings"), QStringLiteral("--tag"), QStringLiteral("--untag"),
        QStringLiteral("--tags"), QStringLiteral("--set-collection"),
        QStringLiteral("--collections"), QStringLiteral("--rename-collection"),
        QStringLiteral("--remove-collection"), QStringLiteral("--bulk"),
        QStringLiteral("--relink"), QStringLiteral("--verify-hashes"),
        QStringLiteral("--launch-check"), QStringLiteral("--print-argv"),
        QStringLiteral("--backup"), QStringLiteral("--restore"),
    };
    return names;
}

struct CliArguments {
    QString command;
    QStringList positional;
    QString dataDirectory;
    QString emulator;
    QString collection;
    QString importPkg;
    QString removeId;
    QString reportFile;
    QString tagId;
    QStringList tags;
    QString untagId;
    QStringList untags;
    QString tagFilter;
    bool tagFilterSet = false;
    bool listTags = false;
    bool listCollections = false;
    bool listMappings = false;
    QString bulkFile;
    QString relinkId;
    QString relinkPath;
    QString launchCheckId;
    QString printArgvId;
    int launchTimeoutSeconds = 8;
    bool launchTimeoutSet = false;
    bool force = false;
    bool verifyHashes = false;
    QString mapPlatform;
    QString mapEmulator;
    QString setCollectionId;
    QString setCollectionName;
    bool setCollectionSet = false;
    QString renameCollectionOld;
    QString renameCollectionNew;
    QString removeCollectionName;
    QString backupFile;
    bool withPackagedArt = false;
    QString restoreFile;
    QStringList remapRules;
    bool replace = false;
    bool json = false;
    bool ui = false;
    bool help = false;
    bool version = false;
};

bool isValueOption(const QString& name, QString*& target, CliArguments& out) {
    if (name == QStringLiteral("--data-dir")) {
        target = &out.dataDirectory;
        return true;
    }
    if (name == QStringLiteral("--emulator")) {
        target = &out.emulator;
        return true;
    }
    if (name == QStringLiteral("--collection")) {
        target = &out.collection;
        return true;
    }
    if (name == QStringLiteral("--import-pkg")) {
        target = &out.importPkg;
        return true;
    }
    if (name == QStringLiteral("--remove")) {
        target = &out.removeId;
        return true;
    }
    if (name == QStringLiteral("--export-report")) {
        target = &out.reportFile;
        return true;
    }
    if (name == QStringLiteral("--bulk")) {
        target = &out.bulkFile;
        return true;
    }
    if (name == QStringLiteral("--launch-check")) {
        target = &out.launchCheckId;
        return true;
    }
    if (name == QStringLiteral("--print-argv")) {
        target = &out.printArgvId;
        return true;
    }
    if (name == QStringLiteral("--backup")) {
        target = &out.backupFile;
        return true;
    }
    if (name == QStringLiteral("--restore")) {
        target = &out.restoreFile;
        return true;
    }
    if (name == QStringLiteral("--remove-collection")) {
        target = &out.removeCollectionName;
        return true;
    }
    return false;
}

QString verbForOption(const QString& name) {
    if (name == QStringLiteral("--list-emulators"))
        return QStringLiteral("list-emulators");
    if (name == QStringLiteral("--detect-emulators"))
        return QStringLiteral("detect-emulators");
    if (name == QStringLiteral("--scan"))
        return QStringLiteral("scan");
    if (name == QStringLiteral("--import"))
        return QStringLiteral("import");
    if (name == QStringLiteral("--import-pkg"))
        return QStringLiteral("import-pkg");
    if (name == QStringLiteral("--list"))
        return QStringLiteral("list");
    if (name == QStringLiteral("--remove"))
        return QStringLiteral("remove");
    if (name == QStringLiteral("--validate"))
        return QStringLiteral("validate");
    if (name == QStringLiteral("--export-report"))
        return QStringLiteral("export-report");
    if (name == QStringLiteral("--map-platform"))
        return QStringLiteral("map-platform");
    if (name == QStringLiteral("--list-mappings"))
        return QStringLiteral("list-mappings");
    if (name == QStringLiteral("--tag"))
        return QStringLiteral("tag");
    if (name == QStringLiteral("--untag"))
        return QStringLiteral("untag");
    if (name == QStringLiteral("--tags"))
        return QStringLiteral("tags");
    if (name == QStringLiteral("--set-collection"))
        return QStringLiteral("set-collection");
    if (name == QStringLiteral("--collections"))
        return QStringLiteral("collections");
    if (name == QStringLiteral("--rename-collection"))
        return QStringLiteral("rename-collection");
    if (name == QStringLiteral("--remove-collection"))
        return QStringLiteral("remove-collection");
    if (name == QStringLiteral("--bulk"))
        return QStringLiteral("bulk");
    if (name == QStringLiteral("--relink"))
        return QStringLiteral("relink");
    if (name == QStringLiteral("--verify-hashes"))
        return QStringLiteral("verify-hashes");
    if (name == QStringLiteral("--launch-check"))
        return QStringLiteral("launch-check");
    if (name == QStringLiteral("--print-argv"))
        return QStringLiteral("print-argv");
    if (name == QStringLiteral("--backup"))
        return QStringLiteral("backup");
    if (name == QStringLiteral("--restore"))
        return QStringLiteral("restore");
    return QString();
}

bool parseArguments(const QStringList& arguments, CliArguments& out, QString& error) {
    QSet<QString> seen;
    int index = 1;
    bool optionsEnded = false;
    const auto takeValue = [&](QString& target, const QString& name) -> bool {
        if (index + 1 >= arguments.size()) {
            error = QStringLiteral("Option '%1' needs a value").arg(name);
            return false;
        }
        const QString candidate = arguments.at(index + 1);
        if (candidate.startsWith(QStringLiteral("--"))) {
            error = QStringLiteral("Option '%1' needs a value, but got option '%2'")
                        .arg(name, candidate);
            return false;
        }
        target = candidate;
        ++index;
        return true;
    };
    const auto setCommand = [&](const QString& verb) -> bool {
        if (!out.command.isEmpty() && out.command != verb) {
            error = QStringLiteral("Only one command may be given ('%1' and '%2')")
                        .arg(out.command, verb);
            return false;
        }
        out.command = verb;
        return true;
    };
    const auto rejectValue = [&](const QString& name) -> bool {
        if (!arguments.at(index).contains(QLatin1Char('=')))
            return true;
        error = QStringLiteral("Option '%1' does not take a value").arg(name);
        return false;
    };
    while (index < arguments.size()) {
        const QString argument = arguments.at(index);
        if (optionsEnded) {
            out.positional.append(argument);
            ++index;
            continue;
        }
        if (argument == QStringLiteral("--")) {
            optionsEnded = true;
            ++index;
            continue;
        }
        if (!argument.startsWith(QLatin1Char('-'))) {
            out.positional.append(argument);
            ++index;
            continue;
        }
        if (argument == QStringLiteral("-h") || argument == QStringLiteral("--help")) {
            out.help = true;
            ++index;
            continue;
        }
        if (argument == QStringLiteral("--version")) {
            out.version = true;
            ++index;
            continue;
        }
        if (!argument.startsWith(QStringLiteral("--"))) {
            error = QStringLiteral("Unknown option '%1'").arg(argument);
            return false;
        }
        QString name = argument;
        QString value;
        bool hasValue = false;
        const int equals = argument.indexOf(QLatin1Char('='));
        if (equals > 0) {
            name = argument.left(equals);
            value = argument.mid(equals + 1);
            hasValue = true;
        }
        if (name != QStringLiteral("--remap") && seen.contains(name)) {
            error = QStringLiteral("Option '%1' was supplied more than once").arg(name);
            return false;
        }
        seen.insert(name);

        if (name == QStringLiteral("--replace")) {
            if (!rejectValue(name))
                return false;
            out.replace = true;
            ++index;
            continue;
        }
        if (name == QStringLiteral("--json")) {
            if (!rejectValue(name))
                return false;
            out.json = true;
            ++index;
            continue;
        }
        if (name == QStringLiteral("--ui")) {
            if (!rejectValue(name))
                return false;
            out.ui = true;
            ++index;
            continue;
        }
        if (name == QStringLiteral("--force")) {
            if (!rejectValue(name))
                return false;
            out.force = true;
            ++index;
            continue;
        }
        if (name == QStringLiteral("--with-packaged-art")) {
            if (!rejectValue(name))
                return false;
            out.withPackagedArt = true;
            ++index;
            continue;
        }
        if (name == QStringLiteral("--tags")) {
            if (!rejectValue(name))
                return false;
            out.listTags = true;
            if (!setCommand(QStringLiteral("tags")))
                return false;
            ++index;
            continue;
        }
        if (name == QStringLiteral("--collections")) {
            if (!rejectValue(name))
                return false;
            out.listCollections = true;
            if (!setCommand(QStringLiteral("collections")))
                return false;
            ++index;
            continue;
        }
        if (name == QStringLiteral("--list-mappings")) {
            if (!rejectValue(name))
                return false;
            out.listMappings = true;
            if (!setCommand(QStringLiteral("list-mappings")))
                return false;
            ++index;
            continue;
        }
        if (name == QStringLiteral("--verify-hashes")) {
            if (!rejectValue(name))
                return false;
            out.verifyHashes = true;
            if (!setCommand(QStringLiteral("verify-hashes")))
                return false;
            ++index;
            continue;
        }
        if (name == QStringLiteral("--tag") || name == QStringLiteral("--untag")) {
            if (hasValue) {
                error = QStringLiteral("Option '%1' does not take an '=' value").arg(name);
                return false;
            }
            if (name == QStringLiteral("--tag") && out.command == QStringLiteral("list")) {
                if (!takeValue(out.tagFilter, name))
                    return false;
                out.tagFilterSet = true;
                ++index;
                continue;
            }
            const bool untag = name == QStringLiteral("--untag");
            QString id;
            if (!takeValue(id, name))
                return false;
            QStringList tags;
            while (index + 1 < arguments.size()
                   && !arguments.at(index + 1).startsWith(QStringLiteral("--"))) {
                tags.append(arguments.at(index + 1));
                ++index;
            }
            if (tags.isEmpty()) {
                error = QStringLiteral("--%1 needs <appId> <tag...>")
                            .arg(untag ? QStringLiteral("untag") : QStringLiteral("tag"));
                return false;
            }
            if (untag) {
                out.untagId = id;
                out.untags = tags;
            } else {
                out.tagId = id;
                out.tags = tags;
            }
            if (!setCommand(untag ? QStringLiteral("untag") : QStringLiteral("tag")))
                return false;
            ++index;
            continue;
        }
        if (name == QStringLiteral("--map-platform")
            || name == QStringLiteral("--relink")
            || name == QStringLiteral("--set-collection")
            || name == QStringLiteral("--rename-collection")) {
            if (hasValue) {
                error = QStringLiteral("Option '%1' needs two separate values").arg(name);
                return false;
            }
            QString first;
            QString second;
            if (!takeValue(first, name) || !takeValue(second, name))
                return false;
            if (name == QStringLiteral("--map-platform")) {
                out.mapPlatform = first;
                out.mapEmulator = second;
                if (!setCommand(QStringLiteral("map-platform")))
                    return false;
            } else if (name == QStringLiteral("--relink")) {
                out.relinkId = first;
                out.relinkPath = second;
                if (!setCommand(QStringLiteral("relink")))
                    return false;
            } else if (name == QStringLiteral("--set-collection")) {
                out.setCollectionId = first;
                out.setCollectionName = second;
                out.setCollectionSet = true;
                if (!setCommand(QStringLiteral("set-collection")))
                    return false;
            } else {
                out.renameCollectionOld = first;
                out.renameCollectionNew = second;
                if (!setCommand(QStringLiteral("rename-collection")))
                    return false;
            }
            ++index;
            continue;
        }
        if (name == QStringLiteral("--launch-timeout")) {
            QString text;
            if (!hasValue) {
                if (!takeValue(text, name))
                    return false;
            } else {
                text = value;
            }
            bool ok = false;
            const int seconds = text.trimmed().toInt(&ok);
            if (!ok || seconds < 1 || seconds > 3600) {
                error = QStringLiteral("--launch-timeout needs a whole number of seconds "
                                       "between 1 and 3600");
                return false;
            }
            out.launchTimeoutSeconds = seconds;
            out.launchTimeoutSet = true;
            ++index;
            continue;
        }
        if (name == QStringLiteral("--remap")) {
            if (!hasValue) {
                if (!takeValue(value, name))
                    return false;
            }
            out.remapRules.append(value);
            ++index;
            continue;
        }
        QString* target = nullptr;
        if (!isValueOption(name, target, out)) {
            if (verbNames().contains(name)) {
                if (hasValue) {
                    error = QStringLiteral("Option '%1' does not take a value").arg(name);
                    return false;
                }
                if (!setCommand(verbForOption(name)))
                    return false;
                ++index;
                continue;
            }
            error = QStringLiteral("Unknown option '%1'").arg(name);
            return false;
        }
        if (!hasValue && !takeValue(value, name))
            return false;
        *target = value;
        const QString verb = verbForOption(name);
        if (!verb.isEmpty() && !setCommand(verb))
            return false;
        ++index;
    }
    const auto is = [&out](const char* verb) {
        return out.command == QLatin1String(verb);
    };
    if (is("import-pkg") && out.importPkg.isEmpty()) {
        error = QStringLiteral("--import-pkg needs a .pkg file");
        return false;
    }
    if (is("remove") && out.removeId.isEmpty()) {
        error = QStringLiteral("--remove needs an app id");
        return false;
    }
    if (is("export-report") && out.reportFile.isEmpty()) {
        error = QStringLiteral("--export-report needs an output file");
        return false;
    }
    if (is("bulk") && out.bulkFile.isEmpty()) {
        error = QStringLiteral("--bulk needs a JSON file");
        return false;
    }
    if (is("launch-check") && out.launchCheckId.isEmpty()) {
        error = QStringLiteral("--launch-check needs an app id");
        return false;
    }
    if (is("print-argv") && out.printArgvId.isEmpty()) {
        error = QStringLiteral("--print-argv needs an app id");
        return false;
    }
    if (is("backup") && out.backupFile.isEmpty()) {
        error = QStringLiteral("--backup needs an output file");
        return false;
    }
    if (is("restore") && out.restoreFile.isEmpty()) {
        error = QStringLiteral("--restore needs a backup file");
        return false;
    }
    if (is("map-platform") && (out.mapPlatform.isEmpty() || out.mapEmulator.isEmpty())) {
        error = QStringLiteral("--map-platform needs <platform> <emulatorId>");
        return false;
    }
    if (is("tag") && (out.tagId.isEmpty() || out.tags.isEmpty())) {
        error = QStringLiteral("--tag needs <appId> <tag...>");
        return false;
    }
    if (is("untag") && (out.untagId.isEmpty() || out.untags.isEmpty())) {
        error = QStringLiteral("--untag needs <appId> <tag...>");
        return false;
    }
    if (is("set-collection") && !out.setCollectionSet) {
        error = QStringLiteral("--set-collection needs <appId> <name> (use \"\" to clear)");
        return false;
    }
    if (is("rename-collection")
        && (out.renameCollectionOld.isEmpty() || out.renameCollectionNew.isEmpty())) {
        error = QStringLiteral("--rename-collection needs <old> <new>");
        return false;
    }
    if (is("remove-collection") && out.removeCollectionName.isEmpty()) {
        error = QStringLiteral("--remove-collection needs a name");
        return false;
    }
    if (is("relink") && (out.relinkId.isEmpty() || out.relinkPath.isEmpty())) {
        error = QStringLiteral("--relink needs <appId> <newPath>");
        return false;
    }
    if (is("scan") || is("import")) {
        if (out.positional.isEmpty())
            error = QStringLiteral("--%1 needs at least one path").arg(out.command);
    } else if (!out.positional.isEmpty()) {
        error = QStringLiteral("Unexpected argument '%1'").arg(out.positional.first());
    }
    if (!out.emulator.isEmpty() && !is("import"))
        error = QStringLiteral("--emulator only applies to --import");
    if (!out.collection.isEmpty() && !is("import"))
        error = QStringLiteral("--collection only applies to --import");
    if (out.replace && !is("import") && !is("restore"))
        error = QStringLiteral("--replace only applies to --import and --restore");
    if (out.force && !is("relink"))
        error = QStringLiteral("--force only applies to --relink");
    if (out.launchTimeoutSet && !is("launch-check"))
        error = QStringLiteral("--launch-timeout only applies to --launch-check");
    if (out.withPackagedArt && !is("backup"))
        error = QStringLiteral("--with-packaged-art only applies to --backup");
    if (!out.tags.isEmpty() && !is("tag"))
        error = QStringLiteral("--tag only applies to --list (as a filter) or --tag <appId> "
                               "<tag...>");
    if (!out.untags.isEmpty() && !is("untag"))
        error = QStringLiteral("--untag only applies to --untag <appId> <tag...>");
    if (out.tagFilterSet && !is("list"))
        error = QStringLiteral("--tag as a filter only applies to --list");
    if (!out.remapRules.isEmpty() && !is("restore"))
        error = QStringLiteral("--remap only applies to --restore");
    if (is("import") && !out.importPkg.isEmpty())
        error = QStringLiteral("--import and --import-pkg cannot be combined");
    return error.isEmpty();
}

void emitJson(const QJsonObject& object) {
    QTextStream stream(stdout);
    stream << QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
    stream.flush();
}

QJsonObject baseObject(const QString& command) {
    QJsonObject object;
    object.insert(QStringLiteral("schema"), QLatin1String(kSchema));
    object.insert(QStringLiteral("command"), command);
    return object;
}

int failure(const QString& command, bool json, const QString& error, int code = 1) {
    logError(error);
    if (json) {
        QJsonObject object = baseObject(command);
        object.insert(QStringLiteral("ok"), false);
        object.insert(QStringLiteral("error"), error);
        emitJson(object);
    }
    return code;
}

int usageFailure(bool json, const QString& error) {
    logError(error);
    QTextStream stream(stderr);
    stream << cliUsage() << Qt::endl;
    if (json) {
        QJsonObject object = baseObject(QStringLiteral("usage"));
        object.insert(QStringLiteral("ok"), false);
        object.insert(QStringLiteral("error"), error);
        emitJson(object);
    }
    return 2;
}

QJsonArray stringArray(const QStringList& values) {
    QJsonArray array;
    for (const QString& value : values)
        array.append(value);
    return array;
}

QJsonObject emulatorJson(const EmulatorProfile& profile) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), profile.id);
    object.insert(QStringLiteral("name"), profile.name);
    object.insert(QStringLiteral("executable"), profile.executable);
    object.insert(QStringLiteral("workingDirectory"), profile.workingDirectory);
    object.insert(QStringLiteral("platforms"), stringArray(profile.platforms));
    object.insert(QStringLiteral("argumentTemplate"), profile.argumentTemplate);
    QJsonObject env;
    QStringList keys = profile.env.keys();
    keys.sort();
    for (const QString& key : keys)
        env.insert(key, profile.env.value(key));
    object.insert(QStringLiteral("env"), env);
    object.insert(QStringLiteral("builtIn"), profile.builtIn);
    return object;
}

QJsonObject mappingJson(const PlatformMapping& mapping) {
    QJsonObject object;
    object.insert(QStringLiteral("platform"), mapping.platform);
    object.insert(QStringLiteral("emulatorId"), mapping.emulatorId);
    object.insert(QStringLiteral("arguments"), stringArray(mapping.arguments));
    return object;
}

QJsonObject appJson(const ContentApp& app) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), app.id);
    object.insert(QStringLiteral("title"), app.title);
    object.insert(QStringLiteral("kind"), app.kind);
    object.insert(QStringLiteral("category"), app.category);
    object.insert(QStringLiteral("executable"), app.executable);
    object.insert(QStringLiteral("workingDirectory"), app.workingDirectory);
    object.insert(QStringLiteral("url"), app.url);
    object.insert(QStringLiteral("artwork"), app.artwork);
    object.insert(QStringLiteral("arguments"), stringArray(app.arguments));
    return object;
}

QJsonObject gameJson(const DetectedGame& game) {
    QJsonObject object;
    object.insert(QStringLiteral("title"), game.title);
    object.insert(QStringLiteral("platform"), game.platform);
    object.insert(QStringLiteral("romPath"), game.romPath);
    object.insert(QStringLiteral("romKind"), game.romKind);
    object.insert(QStringLiteral("size"), double(game.size));
    object.insert(QStringLiteral("artPath"), game.artPath);
    object.insert(QStringLiteral("sourceNote"), game.sourceNote);
    return object;
}

QString packageState(const PackageRecord& record) {
    if (!record.issue.isEmpty())
        return QStringLiteral("invalid");
    if (record.missingPackage)
        return QStringLiteral("missing-package");
    if (record.orphaned)
        return QStringLiteral("orphaned-package");
    if (!record.registryPresent)
        return QStringLiteral("package-only");
    return QStringLiteral("ok");
}

QJsonObject packageJson(const PackageRecord& record) {
    QJsonObject object;
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
    object.insert(QStringLiteral("packagePath"), record.packagePath);
    object.insert(QStringLiteral("packagePresent"), record.packagePresent);
    object.insert(QStringLiteral("registryPresent"), record.registryPresent);
    object.insert(QStringLiteral("orphaned"), record.orphaned);
    object.insert(QStringLiteral("missingPackage"), record.missingPackage);
    object.insert(QStringLiteral("state"), packageState(record));
    if (!record.issue.isEmpty())
        object.insert(QStringLiteral("issue"), record.issue);
    return object;
}

QString sizeText(qint64 bytes) {
    if (bytes <= 0)
        return QStringLiteral("0 B");
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KiB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MiB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

bool loadSettingsOrReport(const CliArguments& arguments, Settings& settings, QString& error) {
    if (!loadSettings(settings, error))
        return false;
    if (!arguments.dataDirectory.isEmpty()
        && !setDataDirectory(settings, arguments.dataDirectory, error, true))
        return false;
    return true;
}

int commandListEmulators(const CliArguments& arguments, Settings& settings) {
    QString error;
    const QVector<EmulatorProfile> profiles = loadProfiles(settings, error);
    if (!error.isEmpty())
        return failure(QStringLiteral("list-emulators"), arguments.json, error);
    QJsonArray array;
    for (const EmulatorProfile& profile : profiles)
        array.append(emulatorJson(profile));
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("list-emulators"));
        object.insert(QStringLiteral("ok"), true);
        object.insert(QStringLiteral("count"), array.size());
        object.insert(QStringLiteral("emulators"), array);
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << QStringLiteral("Emulator profiles (%1):").arg(profiles.size()) << Qt::endl;
        for (const EmulatorProfile& profile : profiles) {
            out << QStringLiteral("%1 | %2 | %3 | %4")
                       .arg(profile.id, profile.name,
                            profile.executable.isEmpty() ? QStringLiteral("(not configured)")
                                                         : profile.executable,
                            profile.platforms.join(QStringLiteral(", ")))
                << Qt::endl;
        }
        out.flush();
    }
    return 0;
}

int commandDetectEmulators(const CliArguments& arguments, Settings& settings) {
    QString error;
    if (!ensureDataLayout(settings, error))
        return failure(QStringLiteral("detect-emulators"), arguments.json, error);
    QVector<EmulatorProfile> profiles = loadProfiles(settings, error);
    if (!error.isEmpty())
        return failure(QStringLiteral("detect-emulators"), arguments.json, error);
    QStringList notes;
    const QVector<EmulatorProfile> detected = detectInstalled(&notes);
    QJsonArray detectedJson;
    int merged = 0;
    for (const EmulatorProfile& profile : detected) {
        if (profile.executable.isEmpty())
            continue;
        ++merged;
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), profile.id);
        entry.insert(QStringLiteral("name"), profile.name);
        entry.insert(QStringLiteral("executable"), profile.executable);
        entry.insert(QStringLiteral("workingDirectory"), profile.workingDirectory);
        for (const QString& note : notes) {
            if (note.startsWith(profile.id + QStringLiteral(": "))) {
                entry.insert(QStringLiteral("evidence"), note);
                break;
            }
        }
        detectedJson.append(entry);
        EmulatorProfile* target = findProfile(profiles, profile.id);
        if (target) {
            target->executable = profile.executable;
            target->workingDirectory = profile.workingDirectory;
        } else {
            profiles.append(profile);
        }
    }
    bool saved = false;
    if (merged > 0) {
        if (!saveProfiles(settings, profiles, error))
            return failure(QStringLiteral("detect-emulators"), arguments.json, error);
        saved = true;
    }
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("detect-emulators"));
        object.insert(QStringLiteral("ok"), true);
        object.insert(QStringLiteral("detected"), detectedJson);
        object.insert(QStringLiteral("notes"), stringArray(notes));
        object.insert(QStringLiteral("saved"), saved);
        emitJson(object);
    } else {
        QTextStream out(stdout);
        for (const QString& note : notes)
            out << note << Qt::endl;
        out << QStringLiteral("Detected %1 emulator%2%3")
                   .arg(merged)
                   .arg(merged == 1 ? QString() : QStringLiteral("s"))
                   .arg(saved ? QStringLiteral("; saved to emulators.json") : QString())
            << Qt::endl;
        out.flush();
    }
    return 0;
}

int commandScan(const CliArguments& arguments) {
    QStringList warnings;
    const QVector<DetectedGame> games = scanPaths(arguments.positional, &warnings);
    for (const QString& warning : warnings)
        logWarning(warning);
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("scan"));
        object.insert(QStringLiteral("ok"), true);
        object.insert(QStringLiteral("roots"), stringArray(arguments.positional));
        QJsonArray array;
        for (const DetectedGame& game : games)
            array.append(gameJson(game));
        object.insert(QStringLiteral("count"), array.size());
        object.insert(QStringLiteral("games"), array);
        object.insert(QStringLiteral("warnings"), stringArray(warnings));
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << QStringLiteral("Scan found %1 game%2:")
                   .arg(games.size())
                   .arg(games.size() == 1 ? QString() : QStringLiteral("s"))
            << Qt::endl;
        for (const DetectedGame& game : games) {
            out << QStringLiteral("[%1] %2").arg(game.platform, game.title) << Qt::endl;
            out << QStringLiteral("    %1").arg(game.romPath) << Qt::endl;
            out << QStringLiteral("    %1, %2%3")
                       .arg(game.romKind, sizeText(game.size),
                            game.artPath.isEmpty()
                                ? QString()
                                : QStringLiteral(", art %1").arg(game.artPath))
                << Qt::endl;
            if (!game.sourceNote.isEmpty())
                out << QStringLiteral("    note: %1").arg(game.sourceNote) << Qt::endl;
        }
        out.flush();
    }
    return 0;
}

int commandImport(const CliArguments& arguments, Settings& settings) {
    QString error;
    if (!ensureDataLayout(settings, error))
        return failure(QStringLiteral("import"), arguments.json, error);
    const QVector<EmulatorProfile> profiles = loadProfiles(settings, error);
    if (!error.isEmpty())
        return failure(QStringLiteral("import"), arguments.json, error);
    const EmulatorProfile* fixedProfile = nullptr;
    if (!arguments.emulator.isEmpty()) {
        fixedProfile = findProfile(profiles, arguments.emulator);
        if (!fixedProfile)
            return failure(QStringLiteral("import"), arguments.json,
                           QStringLiteral("Unknown emulator id '%1'. Known ids: %2")
                               .arg(arguments.emulator,
                                    [&profiles]() {
                                        QStringList ids;
                                        for (const EmulatorProfile& item : profiles)
                                            ids.append(item.id);
                                        return ids.join(QStringLiteral(", "));
                                    }()));
    }
    QStringList warnings;
    const QVector<DetectedGame> games = scanPaths(arguments.positional, &warnings);
    for (const QString& warning : warnings)
        logWarning(warning);
    if (games.isEmpty())
        return failure(QStringLiteral("import"), arguments.json,
                       QStringLiteral("No supported games were found under the given paths"));
    QJsonArray imported;
    QJsonArray failed;
    int failures = 0;
    for (const DetectedGame& game : games) {
        const EmulatorProfile* profile = fixedProfile;
        QStringList argumentOverride;
        bool fromMap = false;
        if (!profile) {
            QString emulatorId;
            QString reason;
            if (!resolvePlatformTarget(settings, game.platform, emulatorId, argumentOverride,
                                       fromMap, reason)) {
                ++failures;
                failed.append(QJsonObject{{QStringLiteral("romPath"), game.romPath},
                                          {QStringLiteral("platform"), game.platform},
                                          {QStringLiteral("error"), reason}});
                logError(QStringLiteral("%1: %2").arg(game.title, reason));
                continue;
            }
            if (emulatorId.isEmpty()) {
                const QString message =
                    QStringLiteral("No emulator mapping for platform '%1': %2. Add one with "
                                   "--map-platform \"%1\" <emulatorId>.")
                        .arg(game.platform, reason);
                ++failures;
                failed.append(QJsonObject{{QStringLiteral("romPath"), game.romPath},
                                          {QStringLiteral("platform"), game.platform},
                                          {QStringLiteral("error"), message}});
                logError(QStringLiteral("%1: %2").arg(game.title, message));
                continue;
            }
            profile = findProfile(profiles, emulatorId);
            if (!profile) {
                const QString message =
                    QStringLiteral("platform-map.json maps '%1' to unknown emulator '%2'; add "
                                   "that profile or fix the mapping")
                        .arg(game.platform, emulatorId);
                ++failures;
                failed.append(QJsonObject{{QStringLiteral("romPath"), game.romPath},
                                          {QStringLiteral("platform"), game.platform},
                                          {QStringLiteral("error"), message}});
                logError(QStringLiteral("%1: %2").arg(game.title, message));
                continue;
            }
        }
        PackageInput input;
        input.game = game;
        input.emulatorId = profile->id;
        input.collection = arguments.collection;
        input.replace = arguments.replace;
        input.argumentsOverride = argumentOverride;
        input.emulatorFromMapping = fromMap;
        ContentApp app;
        QString packageError;
        if (!createPackage(settings, input, *profile, &app, packageError)) {
            ++failures;
            failed.append(QJsonObject{{QStringLiteral("romPath"), game.romPath},
                                      {QStringLiteral("platform"), game.platform},
                                      {QStringLiteral("error"), packageError}});
            logError(QStringLiteral("%1: %2").arg(game.title, packageError));
            continue;
        }
        const QString packagePath = packageDirectoryFor(settings, app.id);
        imported.append(QJsonObject{{QStringLiteral("id"), app.id},
                                    {QStringLiteral("title"), app.title},
                                    {QStringLiteral("platform"), game.platform},
                                    {QStringLiteral("emulatorId"), profile->id},
                                    {QStringLiteral("mappingUsed"), fromMap},
                                    {QStringLiteral("packagePath"), packagePath},
                                    {QStringLiteral("app"), appJson(app)}});
        logInfo(QStringLiteral("Imported '%1' as %2 (emulator %3%4)")
                    .arg(app.title, app.id, profile->id,
                         fromMap ? QStringLiteral(", platform map") : QString()));
    }
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("import"));
        object.insert(QStringLiteral("ok"), failures == 0);
        object.insert(QStringLiteral("emulator"),
                      fixedProfile ? fixedProfile->id : QString());
        object.insert(QStringLiteral("emulatorSource"),
                      fixedProfile ? QStringLiteral("option") : QStringLiteral("platform-map"));
        object.insert(QStringLiteral("collection"), arguments.collection);
        object.insert(QStringLiteral("replace"), arguments.replace);
        object.insert(QStringLiteral("imported"), imported);
        object.insert(QStringLiteral("failed"), failed);
        object.insert(QStringLiteral("warnings"), stringArray(warnings));
        if (failures > 0)
            object.insert(QStringLiteral("error"),
                          QStringLiteral("%1 of %2 imports failed")
                              .arg(failures).arg(games.size()));
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << QStringLiteral("Imported %1 of %2 game%3")
                   .arg(imported.size())
                   .arg(games.size())
                   .arg(games.size() == 1 ? QString() : QStringLiteral("s"))
            << Qt::endl;
        for (const QJsonValue& value : imported) {
            const QJsonObject entry = value.toObject();
            out << QStringLiteral("    %1 | %2 | %3 | %4")
                       .arg(entry.value(QStringLiteral("id")).toString(),
                            entry.value(QStringLiteral("title")).toString(),
                            entry.value(QStringLiteral("emulatorId")).toString(),
                            entry.value(QStringLiteral("packagePath")).toString())
                << Qt::endl;
        }
        out.flush();
    }
    return failures == 0 ? 0 : 1;
}

int commandImportPkg(const CliArguments& arguments, Settings& settings) {
    QString error;
    QStringList notes;
    const bool imported = importRealPkg(settings, arguments.importPkg, error, &notes);
    for (const QString& note : notes)
        logInfo(note);
    if (!imported) {
        if (arguments.json) {
            QJsonObject object = baseObject(QStringLiteral("import-pkg"));
            object.insert(QStringLiteral("ok"), false);
            object.insert(QStringLiteral("file"), arguments.importPkg);
            object.insert(QStringLiteral("imported"), false);
            object.insert(QStringLiteral("notes"), stringArray(notes));
            object.insert(QStringLiteral("error"), error);
            emitJson(object);
        }
        logError(error);
        return 1;
    }
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("import-pkg"));
        object.insert(QStringLiteral("ok"), true);
        object.insert(QStringLiteral("file"), arguments.importPkg);
        object.insert(QStringLiteral("imported"), true);
        object.insert(QStringLiteral("notes"), stringArray(notes));
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << QStringLiteral("Imported a virtual package from %1").arg(arguments.importPkg)
            << Qt::endl;
        out.flush();
    }
    return 0;
}

int commandList(const CliArguments& arguments, Settings& settings) {
    QString error;
    QVector<ContentApp> apps;
    if (!ContentRegistry::load(registryPath(settings), apps, error))
        return failure(QStringLiteral("list"), arguments.json, error);
    QVector<PackageRecord> packages = listPackages(settings, error);
    if (!error.isEmpty())
        return failure(QStringLiteral("list"), arguments.json, error);
    QString tagFilter;
    QSet<QString> filteredIds;
    if (arguments.tagFilterSet) {
        QString tagError;
        const QStringList normalized = normalizeTags({arguments.tagFilter}, tagError);
        if (!tagError.isEmpty())
            return failure(QStringLiteral("list"), arguments.json, tagError);
        tagFilter = normalized.isEmpty() ? QString() : normalized.first();
        if (tagFilter.isEmpty())
            return failure(QStringLiteral("list"), arguments.json,
                           QStringLiteral("--tag needs a non-empty tag to filter by"));
        const QString wanted = tagFilter.toCaseFolded();
        QVector<PackageRecord> filteredPackages;
        for (const PackageRecord& record : packages) {
            const bool matches = std::any_of(record.tags.cbegin(), record.tags.cend(),
                                             [&wanted](const QString& tag) {
                                                 return tag.toCaseFolded() == wanted;
                                             });
            if (matches) {
                filteredIds.insert(record.id);
                filteredPackages.append(record);
            }
        }
        packages = filteredPackages;
        QVector<ContentApp> filteredApps;
        for (const ContentApp& app : apps) {
            if (filteredIds.contains(app.id))
                filteredApps.append(app);
        }
        apps = filteredApps;
    }
    QJsonArray appArray;
    for (const ContentApp& app : apps)
        appArray.append(appJson(app));
    QJsonArray packageArray;
    int orphaned = 0;
    int missing = 0;
    for (const PackageRecord& record : packages) {
        packageArray.append(packageJson(record));
        if (record.orphaned)
            ++orphaned;
        if (record.missingPackage)
            ++missing;
    }
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("list"));
        object.insert(QStringLiteral("ok"), true);
        object.insert(QStringLiteral("apps"), appArray);
        object.insert(QStringLiteral("appCount"), appArray.size());
        object.insert(QStringLiteral("packages"), packageArray);
        object.insert(QStringLiteral("packageCount"), packageArray.size());
        object.insert(QStringLiteral("orphanedPackages"), orphaned);
        object.insert(QStringLiteral("missingPackages"), missing);
        if (arguments.tagFilterSet)
            object.insert(QStringLiteral("tag"), tagFilter);
        emitJson(object);
    } else {
        QTextStream out(stdout);
        if (arguments.tagFilterSet)
            out << QStringLiteral("Filtered by tag '%1'").arg(tagFilter) << Qt::endl;
        out << QStringLiteral("Registrations (%1):").arg(apps.size()) << Qt::endl;
        for (const ContentApp& app : apps) {
            out << QStringLiteral("%1 | %2 | %3 | %4")
                       .arg(app.id, app.title, app.kind,
                            app.executable.isEmpty() ? app.url : app.executable)
                << Qt::endl;
        }
        out << QStringLiteral("Packages (%1, orphaned %2, missing %3):")
                   .arg(packages.size())
                   .arg(orphaned)
                   .arg(missing)
            << Qt::endl;
        for (const PackageRecord& record : packages) {
            out << QStringLiteral("%1 | %2 | %3 | %4")
                       .arg(packageState(record), record.id, record.title,
                            record.romPath.isEmpty() ? QStringLiteral("(no ROM path)")
                                                     : record.romPath)
                << Qt::endl;
        }
        out.flush();
    }
    return 0;
}

int commandRemove(const CliArguments& arguments, Settings& settings) {
    QString error;
    if (!removePackage(settings, arguments.removeId, error))
        return failure(QStringLiteral("remove"), arguments.json, error);
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("remove"));
        object.insert(QStringLiteral("ok"), true);
        object.insert(QStringLiteral("appId"), arguments.removeId);
        object.insert(QStringLiteral("removed"), true);
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << QStringLiteral("Removed package and registration '%1'").arg(arguments.removeId)
            << Qt::endl;
        out.flush();
    }
    return 0;
}

int commandValidate(const CliArguments& arguments, Settings& settings) {
    QJsonArray issues;
    const auto addIssue = [&issues](const QString& kind, const QString& id,
                                    const QString& message) {
        issues.append(QJsonObject{{QStringLiteral("kind"), kind},
                                  {QStringLiteral("id"), id},
                                  {QStringLiteral("message"), message}});
    };
    QVector<ContentApp> apps;
    QString error;
    const bool registryLoaded = ContentRegistry::load(registryPath(settings), apps, error);
    if (!registryLoaded)
        addIssue(QStringLiteral("registry"), QString(), error);
    for (const ContentApp& app : apps) {
        if (!app.executable.isEmpty() && !QFileInfo(app.executable).isFile())
            addIssue(QStringLiteral("missing-executable"), app.id,
                     QStringLiteral("Executable does not exist: %1").arg(app.executable));
        if (!app.workingDirectory.isEmpty() && !QFileInfo(app.workingDirectory).isDir())
            addIssue(QStringLiteral("missing-working-directory"), app.id,
                     QStringLiteral("Working directory does not exist: %1")
                         .arg(app.workingDirectory));
        if (!app.artwork.isEmpty() && !QFileInfo(app.artwork).isFile())
            addIssue(QStringLiteral("missing-artwork"), app.id,
                     QStringLiteral("Artwork does not exist: %1").arg(app.artwork));
    }
    const QVector<EmulatorProfile> profiles = loadProfiles(settings, error);
    const bool profilesLoaded = error.isEmpty();
    if (!profilesLoaded)
        addIssue(QStringLiteral("emulators"), QString(), error);
    QVector<PackageRecord> packages;
    if (registryLoaded) {
        packages = listPackages(settings, error);
        if (!error.isEmpty()) {
            addIssue(QStringLiteral("packages"), QString(), error);
            packages.clear();
        }
    }
    for (const PackageRecord& record : packages) {
        if (!record.issue.isEmpty())
            addIssue(QStringLiteral("invalid-package"), record.id, record.issue);
        if (record.orphaned)
            addIssue(QStringLiteral("orphaned-package"), record.id,
                     QStringLiteral("Package exists but apps.json has no entry"));
        if (record.missingPackage)
            addIssue(QStringLiteral("missing-package"), record.id,
                     QStringLiteral("apps.json entry has no package directory"));
        if (!record.romPath.isEmpty() && !QFileInfo(record.romPath).exists())
            addIssue(QStringLiteral("missing-rom"), record.id,
                     QStringLiteral("ROM does not exist: %1").arg(record.romPath));
        if (!record.art.isEmpty() && !QFileInfo(record.art).isFile())
            addIssue(QStringLiteral("missing-artwork"), record.id,
                     QStringLiteral("Package icon does not exist: %1").arg(record.art));
        if (record.packagePresent) {
            const QDir directory(record.packagePath);
            if (!QFileInfo(directory.filePath(QStringLiteral("ICON0.PNG"))).isFile())
                addIssue(QStringLiteral("missing-icon"), record.id,
                         QStringLiteral("Package has no ICON0.PNG"));
            if (!QFileInfo(directory.filePath(QStringLiteral("PARAM.SFO"))).isFile())
                addIssue(QStringLiteral("missing-param-sfo"), record.id,
                         QStringLiteral("Package has no PARAM.SFO"));
        }
        if (!record.emulatorId.isEmpty() && profilesLoaded) {
            const EmulatorProfile* profile = findProfile(profiles, record.emulatorId);
            if (!profile) {
                addIssue(QStringLiteral("unknown-emulator"), record.id,
                         QStringLiteral("Emulator '%1' is not defined").arg(record.emulatorId));
            } else if (profile->executable.isEmpty()
                       || !QFileInfo(profile->executable).isFile()) {
                addIssue(QStringLiteral("unconfigured-emulator"), record.id,
                         QStringLiteral("Emulator '%1' has no usable executable")
                             .arg(record.emulatorId));
            }
        }
        if (record.packagePresent) {
            QString tagError;
            normalizeTags(record.tags, tagError);
            if (!tagError.isEmpty())
                addIssue(QStringLiteral("invalid-tag"), record.id, tagError);
        }
    }
    QVector<PlatformMapping> mappings;
    QString mappingError;
    if (!loadPlatformMap(settings, mappings, mappingError)) {
        addIssue(QStringLiteral("platform-map"), QString(), mappingError);
    } else if (profilesLoaded) {
        for (const PlatformMapping& mapping : mappings) {
            if (!findProfile(profiles, mapping.emulatorId)) {
                addIssue(QStringLiteral("unknown-mapped-emulator"), mapping.platform,
                         QStringLiteral("platform-map.json maps '%1' to unknown emulator '%2'")
                             .arg(mapping.platform, mapping.emulatorId));
            }
            if (!mapping.arguments.isEmpty()) {
                QString expandError;
                expandArgumentList(mapping.arguments,
                                   {QStringLiteral("rom=ROM"), QStringLiteral("title=Title"),
                                    QStringLiteral("id=id"), QStringLiteral("dir=DIR"),
                                    QStringLiteral("core=core")},
                                   expandError);
                if (!expandError.isEmpty())
                    addIssue(QStringLiteral("invalid-mapping-arguments"), mapping.platform,
                             expandError);
            }
        }
    }
    QHash<QString, QStringList> collections;
    QString collectionError;
    QSet<QString> packageIds;
    for (const PackageRecord& record : packages) {
        if (record.packagePresent && record.issue.isEmpty())
            packageIds.insert(record.id);
    }
    if (!readCollections(settings, collections, collectionError)) {
        addIssue(QStringLiteral("collections"), QString(), collectionError);
    } else {
        for (auto it = collections.cbegin(); it != collections.cend(); ++it) {
            for (const QString& id : it.value()) {
                if (!packageIds.contains(id)) {
                    addIssue(QStringLiteral("collection-unknown-id"), id,
                             QStringLiteral("Collection '%1' references an id with no readable "
                                            "package").arg(it.key()));
                }
            }
        }
        for (const PackageRecord& record : packages) {
            if (record.issue.isEmpty() && record.packagePresent && !record.collection.isEmpty()) {
                const auto members = collections.constFind(record.collection);
                if (members == collections.cend() || !members->contains(record.id)) {
                    addIssue(QStringLiteral("collection-mismatch"), record.id,
                             QStringLiteral("pkg.json says collection '%1' but collections.json "
                                            "does not list it").arg(record.collection));
                }
            }
            if (!record.packagePresent || !record.issue.isEmpty())
                continue;
            for (auto it = collections.cbegin(); it != collections.cend(); ++it) {
                if (it.key().compare(record.collection, Qt::CaseInsensitive) != 0
                    && it.value().contains(record.id)) {
                    addIssue(QStringLiteral("collection-mismatch"), record.id,
                             QStringLiteral("collections.json lists it under '%1' but pkg.json "
                                            "says '%2'")
                                 .arg(it.key(), record.collection));
                }
            }
        }
    }
    QVector<HashVerification> hashChecks;
    QString hashError;
    if (!verifyPackageHashes(settings, hashChecks, hashError)) {
        addIssue(QStringLiteral("package-hash"), QString(), hashError);
    } else {
        for (const HashVerification& check : hashChecks) {
            if (check.state == QStringLiteral("mismatch"))
                addIssue(QStringLiteral("package-hash"), check.id, check.error);
            else if (check.state == QStringLiteral("missing-hash"))
                addIssue(QStringLiteral("missing-hash"), check.id, check.error);
            else if (check.state == QStringLiteral("unreadable"))
                addIssue(QStringLiteral("package-hash"), check.id, check.error);
        }
    }
    const bool consistent = issues.isEmpty();
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("validate"));
        object.insert(QStringLiteral("ok"), consistent);
        object.insert(QStringLiteral("consistent"), consistent);
        object.insert(QStringLiteral("issues"), issues);
        object.insert(QStringLiteral("apps"), apps.size());
        object.insert(QStringLiteral("packages"), packages.size());
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << QStringLiteral("Registry and packages are %1 (%2 issue%3).")
                   .arg(consistent ? QStringLiteral("consistent") : QStringLiteral("inconsistent"),
                        QString::number(issues.size()),
                        issues.size() == 1 ? QString() : QStringLiteral("s"))
            << Qt::endl;
        for (const QJsonValue& value : issues) {
            const QJsonObject issue = value.toObject();
            out << QStringLiteral("    %1 | %2 | %3")
                       .arg(issue.value(QStringLiteral("kind")).toString(),
                            issue.value(QStringLiteral("id")).toString(),
                            issue.value(QStringLiteral("message")).toString())
                << Qt::endl;
        }
        out.flush();
    }
    return consistent ? 0 : 1;
}

QJsonObject buildReport(Settings& settings, QJsonArray& errors) {
    QJsonObject report;
    report.insert(QStringLiteral("schema"), QLatin1String(kReportSchema));
    report.insert(QStringLiteral("generatedAt"), timestampUtc());
    report.insert(QStringLiteral("dataDirectory"), settings.dataDirectory);
    report.insert(QStringLiteral("paths"),
                  QJsonObject{{QStringLiteral("registry"), registryPath(settings)},
                              {QStringLiteral("packages"), packageRoot(settings)},
                              {QStringLiteral("emulators"), emulatorsPath(settings)},
                              {QStringLiteral("collections"), collectionsPath(settings)},
                              {QStringLiteral("platformMap"), platformMapPath(settings)}});
    QString error;
    const QVector<EmulatorProfile> profiles = loadProfiles(settings, error);
    if (!error.isEmpty()) {
        errors.append(QStringLiteral("emulators: %1").arg(error));
        report.insert(QStringLiteral("emulators"), QJsonArray());
    } else {
        QJsonArray array;
        for (const EmulatorProfile& profile : profiles)
            array.append(emulatorJson(profile));
        report.insert(QStringLiteral("emulators"), array);
    }
    QVector<ContentApp> apps;
    if (!ContentRegistry::load(registryPath(settings), apps, error)) {
        errors.append(QStringLiteral("registry: %1").arg(error));
        report.insert(QStringLiteral("apps"), QJsonArray());
    } else {
        QJsonArray array;
        for (const ContentApp& app : apps)
            array.append(appJson(app));
        report.insert(QStringLiteral("apps"), array);
    }
    const QVector<PackageRecord> packages = listPackages(settings, error);
    if (!error.isEmpty())
        errors.append(QStringLiteral("packages: %1").arg(error));
    QJsonArray packageArray;
    QHash<QString, QStringList> tagMembers; // case-folded tag -> package ids
    QHash<QString, QString> tagSpelling;
    for (const PackageRecord& record : packages) {
        packageArray.append(packageJson(record));
        for (const QString& tag : record.tags) {
            const QString key = tag.toCaseFolded();
            if (!tagSpelling.contains(key))
                tagSpelling.insert(key, tag);
            tagMembers[key].append(record.id);
        }
    }
    report.insert(QStringLiteral("packages"), packageArray);
    QVector<PlatformMapping> mappings;
    QString mappingError;
    if (!loadPlatformMap(settings, mappings, mappingError)) {
        errors.append(QStringLiteral("platform-map: %1").arg(mappingError));
        report.insert(QStringLiteral("mappings"), QJsonArray());
    } else {
        QJsonArray array;
        for (const PlatformMapping& mapping : mappings)
            array.append(mappingJson(mapping));
        report.insert(QStringLiteral("mappings"), array);
    }
    QJsonArray tagArray;
    QStringList tagKeys = tagSpelling.keys();
    tagKeys.sort();
    for (const QString& key : tagKeys) {
        tagArray.append(QJsonObject{
            {QStringLiteral("tag"), tagSpelling.value(key)},
            {QStringLiteral("count"), tagMembers.value(key).size()},
            {QStringLiteral("ids"), stringArray(tagMembers.value(key))}});
    }
    report.insert(QStringLiteral("tags"), tagArray);
    QHash<QString, QStringList> collections;
    QString collectionError;
    QJsonArray collectionArray;
    if (!readCollections(settings, collections, collectionError)) {
        errors.append(QStringLiteral("collections: %1").arg(collectionError));
    } else {
        QStringList names = collections.keys();
        names.sort();
        for (const QString& name : names) {
            collectionArray.append(QJsonObject{
                {QStringLiteral("name"), name},
                {QStringLiteral("count"), collections.value(name).size()},
                {QStringLiteral("ids"), stringArray(collections.value(name))}});
        }
    }
    report.insert(QStringLiteral("collections"), collectionArray);
    QVector<HashVerification> hashChecks;
    QString hashError;
    QJsonArray hashArray;
    if (!verifyPackageHashes(settings, hashChecks, hashError)) {
        errors.append(QStringLiteral("hashes: %1").arg(hashError));
    } else {
        for (const HashVerification& check : hashChecks) {
            hashArray.append(QJsonObject{
                {QStringLiteral("id"), check.id},
                {QStringLiteral("state"), check.state},
                {QStringLiteral("romPath"), check.romPath},
                {QStringLiteral("storedLabel"), check.storedLabel},
                {QStringLiteral("storedHash"), check.storedHash},
                {QStringLiteral("currentHash"), check.currentHash},
                {QStringLiteral("error"), check.error}});
        }
    }
    report.insert(QStringLiteral("hashState"), hashArray);
    report.insert(QStringLiteral("errors"), errors);
    report.insert(QStringLiteral("appCount"), apps.size());
    report.insert(QStringLiteral("packageCount"), packages.size());
    report.insert(QStringLiteral("emulatorCount"), profiles.size());
    report.insert(QStringLiteral("mappingCount"), mappings.size());
    report.insert(QStringLiteral("tagCount"), tagArray.size());
    report.insert(QStringLiteral("collectionCount"), collectionArray.size());
    return report;
}

int commandExportReport(const CliArguments& arguments, Settings& settings) {
    QJsonArray errors;
    const QJsonObject report = buildReport(settings, errors);
    QString error;
    if (!writeJsonFileAtomic(arguments.reportFile, report, error))
        return failure(QStringLiteral("export-report"), arguments.json, error);
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("export-report"));
        object.insert(QStringLiteral("ok"), true);
        object.insert(QStringLiteral("file"), arguments.reportFile);
        object.insert(QStringLiteral("written"), true);
        object.insert(QStringLiteral("errors"), errors);
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << QStringLiteral("Wrote report to %1").arg(arguments.reportFile) << Qt::endl;
        for (const QJsonValue& value : errors)
            out << QStringLiteral("    warning: %1").arg(value.toString()) << Qt::endl;
        out.flush();
    }
    return 0;
}

int commandMapPlatform(const CliArguments& arguments, Settings& settings) {
    QString error;
    if (!ensureDataLayout(settings, error))
        return failure(QStringLiteral("map-platform"), arguments.json, error);
    QVector<EmulatorProfile> profiles = loadProfiles(settings, error);
    if (!error.isEmpty())
        return failure(QStringLiteral("map-platform"), arguments.json, error);
    const QString platform = arguments.mapPlatform.trimmed();
    if (platform.isEmpty())
        return failure(QStringLiteral("map-platform"), arguments.json,
                       QStringLiteral("The platform id must not be empty"));
    const EmulatorProfile* profile = findProfile(profiles, arguments.mapEmulator);
    if (!profile)
        return failure(QStringLiteral("map-platform"), arguments.json,
                       QStringLiteral("Unknown emulator id '%1'").arg(arguments.mapEmulator));
    QVector<PlatformMapping> mappings;
    if (!loadPlatformMap(settings, mappings, error))
        return failure(QStringLiteral("map-platform"), arguments.json, error);
    bool replaced = false;
    for (PlatformMapping& mapping : mappings) {
        if (mapping.platform.compare(platform, Qt::CaseInsensitive) == 0) {
            mapping.emulatorId = profile->id;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        PlatformMapping mapping;
        mapping.platform = platform;
        mapping.emulatorId = profile->id;
        mappings.append(mapping);
    }
    if (!savePlatformMap(settings, mappings, error))
        return failure(QStringLiteral("map-platform"), arguments.json, error);
    QJsonArray array;
    for (const PlatformMapping& mapping : mappings)
        array.append(mappingJson(mapping));
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("map-platform"));
        object.insert(QStringLiteral("ok"), true);
        object.insert(QStringLiteral("platform"), platform);
        object.insert(QStringLiteral("emulatorId"), profile->id);
        object.insert(QStringLiteral("replaced"), replaced);
        object.insert(QStringLiteral("mappings"), array);
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << QStringLiteral("Mapped platform '%1' to emulator '%2'%3")
                   .arg(platform, profile->id,
                        replaced ? QStringLiteral(" (replaced)") : QString())
            << Qt::endl;
        out.flush();
    }
    return 0;
}

int commandListMappings(const CliArguments& arguments, Settings& settings) {
    QString error;
    QVector<PlatformMapping> mappings;
    if (!loadPlatformMap(settings, mappings, error))
        return failure(QStringLiteral("list-mappings"), arguments.json, error);
    const QVector<EmulatorProfile> profiles = loadProfiles(settings, error);
    if (!error.isEmpty())
        return failure(QStringLiteral("list-mappings"), arguments.json, error);
    QJsonArray array;
    int mapped = 0;
    int unmapped = 0;
    QStringList platforms = knownPlatforms();
    for (const PlatformMapping& mapping : mappings) {
        if (!platforms.contains(mapping.platform, Qt::CaseInsensitive))
            platforms.append(mapping.platform);
    }
    std::sort(platforms.begin(), platforms.end(),
              [](const QString& a, const QString& b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });
    for (const QString& platform : platforms) {
        QJsonObject entry;
        entry.insert(QStringLiteral("platform"), platform);
        const PlatformMapping* mapping = findPlatformMapping(mappings, platform);
        if (mapping) {
            entry.insert(QStringLiteral("emulatorId"), mapping->emulatorId);
            entry.insert(QStringLiteral("source"), QStringLiteral("platform-map"));
            entry.insert(QStringLiteral("arguments"), stringArray(mapping->arguments));
        } else {
            QString fallback;
            for (const EmulatorProfile& profile : profiles) {
                if (profile.platforms.contains(platform, Qt::CaseInsensitive)) {
                    fallback = profile.id;
                    break;
                }
            }
            entry.insert(QStringLiteral("emulatorId"), fallback);
            entry.insert(QStringLiteral("source"),
                         fallback.isEmpty() ? QStringLiteral("none")
                                            : QStringLiteral("profile"));
            entry.insert(QStringLiteral("arguments"), QJsonArray());
        }
        const QString emulatorId = entry.value(QStringLiteral("emulatorId")).toString();
        const EmulatorProfile* profile = emulatorId.isEmpty()
            ? nullptr
            : findProfile(profiles, emulatorId);
        const bool configured = profile && !profile->executable.isEmpty()
            && QFileInfo(profile->executable).isFile();
        entry.insert(QStringLiteral("mapped"), !emulatorId.isEmpty());
        entry.insert(QStringLiteral("configured"), configured);
        if (emulatorId.isEmpty())
            ++unmapped;
        else
            ++mapped;
        array.append(entry);
    }
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("list-mappings"));
        object.insert(QStringLiteral("ok"), true);
        object.insert(QStringLiteral("count"), array.size());
        object.insert(QStringLiteral("mapped"), mapped);
        object.insert(QStringLiteral("unmapped"), unmapped);
        object.insert(QStringLiteral("mappings"), array);
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << QStringLiteral("Platform mappings (%1 mapped, %2 unmapped):")
                   .arg(mapped)
                   .arg(unmapped)
            << Qt::endl;
        for (const QJsonValue& value : array) {
            const QJsonObject entry = value.toObject();
            out << QStringLiteral("%1 -> %2 (%3%4)")
                       .arg(entry.value(QStringLiteral("platform")).toString(),
                            entry.value(QStringLiteral("emulatorId")).toString().isEmpty()
                                ? QStringLiteral("(unmapped)")
                                : entry.value(QStringLiteral("emulatorId")).toString(),
                            entry.value(QStringLiteral("source")).toString(),
                            entry.value(QStringLiteral("configured")).toBool()
                                ? QStringLiteral(", configured")
                                : QStringLiteral(", not configured"))
                << Qt::endl;
        }
        out.flush();
    }
    return 0;
}

int commandTagOperation(const CliArguments& arguments, Settings& settings, bool remove) {
    const QString appId = remove ? arguments.untagId : arguments.tagId;
    const QStringList tags = remove ? arguments.untags : arguments.tags;
    QStringList outTags;
    QString error;
    if (!updatePackageTags(settings, appId, remove ? QStringList() : tags,
                           remove ? tags : QStringList(), outTags, error))
        return failure(remove ? QStringLiteral("untag") : QStringLiteral("tag"),
                       arguments.json, error);
    if (arguments.json) {
        QJsonObject object = baseObject(remove ? QStringLiteral("untag") : QStringLiteral("tag"));
        object.insert(QStringLiteral("ok"), true);
        object.insert(QStringLiteral("appId"), appId);
        object.insert(QStringLiteral("tags"), stringArray(outTags));
        object.insert(QStringLiteral("changed"), stringArray(tags));
        object.insert(QStringLiteral("removed"), remove);
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << QStringLiteral("%1 tags for %2: %3")
                   .arg(remove ? QStringLiteral("Removed") : QStringLiteral("Added"),
                        appId, outTags.join(QStringLiteral(", ")))
            << Qt::endl;
        out.flush();
    }
    return 0;
}

int commandTags(const CliArguments& arguments, Settings& settings) {
    QString error;
    const QVector<PackageRecord> packages = listPackages(settings, error);
    if (!error.isEmpty())
        return failure(QStringLiteral("tags"), arguments.json, error);
    QHash<QString, QStringList> members;
    QHash<QString, QString> spelling;
    for (const PackageRecord& record : packages) {
        for (const QString& tag : record.tags) {
            const QString key = tag.toCaseFolded();
            if (!spelling.contains(key))
                spelling.insert(key, tag);
            if (!members[key].contains(record.id))
                members[key].append(record.id);
        }
    }
    QStringList keys = spelling.keys();
    keys.sort();
    QJsonArray array;
    for (const QString& key : keys) {
        array.append(QJsonObject{{QStringLiteral("tag"), spelling.value(key)},
                                 {QStringLiteral("count"), members.value(key).size()},
                                 {QStringLiteral("ids"), stringArray(members.value(key))}});
    }
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("tags"));
        object.insert(QStringLiteral("ok"), true);
        object.insert(QStringLiteral("count"), array.size());
        object.insert(QStringLiteral("tags"), array);
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << QStringLiteral("Tags (%1):").arg(array.size()) << Qt::endl;
        for (const QJsonValue& value : array) {
            const QJsonObject entry = value.toObject();
            out << QStringLiteral("%1 | %2 package%3")
                       .arg(entry.value(QStringLiteral("tag")).toString())
                       .arg(entry.value(QStringLiteral("count")).toInt())
                       .arg(entry.value(QStringLiteral("count")).toInt() == 1 ? QString()
                                                                             : QStringLiteral("s"))
                << Qt::endl;
        }
        out.flush();
    }
    return 0;
}

int commandSetCollection(const CliArguments& arguments, Settings& settings) {
    QString error;
    if (!setPackageCollection(settings, arguments.setCollectionId,
                              arguments.setCollectionName, error))
        return failure(QStringLiteral("set-collection"), arguments.json, error);
    const QString name = arguments.setCollectionName.trimmed();
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("set-collection"));
        object.insert(QStringLiteral("ok"), true);
        object.insert(QStringLiteral("appId"), arguments.setCollectionId);
        object.insert(QStringLiteral("collection"), name);
        object.insert(QStringLiteral("cleared"), name.isEmpty());
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << (name.isEmpty()
                    ? QStringLiteral("Cleared the collection for %1").arg(arguments.setCollectionId)
                    : QStringLiteral("Set the collection of %1 to '%2'")
                          .arg(arguments.setCollectionId, name))
            << Qt::endl;
        out.flush();
    }
    return 0;
}

int commandCollections(const CliArguments& arguments, Settings& settings) {
    QString error;
    QHash<QString, QStringList> collections;
    if (!readCollections(settings, collections, error))
        return failure(QStringLiteral("collections"), arguments.json, error);
    QStringList names = collections.keys();
    names.sort();
    QJsonArray array;
    for (const QString& name : names) {
        array.append(QJsonObject{{QStringLiteral("name"), name},
                                 {QStringLiteral("count"),
                                  collections.value(name).size()},
                                 {QStringLiteral("ids"), stringArray(collections.value(name))}});
    }
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("collections"));
        object.insert(QStringLiteral("ok"), true);
        object.insert(QStringLiteral("count"), array.size());
        object.insert(QStringLiteral("collections"), array);
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << QStringLiteral("Collections (%1):").arg(array.size()) << Qt::endl;
        for (const QJsonValue& value : array) {
            const QJsonObject entry = value.toObject();
            out << QStringLiteral("%1 | %2 package%3")
                       .arg(entry.value(QStringLiteral("name")).toString())
                       .arg(entry.value(QStringLiteral("count")).toInt())
                       .arg(entry.value(QStringLiteral("count")).toInt() == 1 ? QString()
                                                                             : QStringLiteral("s"))
                << Qt::endl;
        }
        out.flush();
    }
    return 0;
}

int commandRenameCollection(const CliArguments& arguments, Settings& settings) {
    int affected = 0;
    QString error;
    if (!renameCollection(settings, arguments.renameCollectionOld,
                          arguments.renameCollectionNew, affected, error))
        return failure(QStringLiteral("rename-collection"), arguments.json, error);
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("rename-collection"));
        object.insert(QStringLiteral("ok"), true);
        object.insert(QStringLiteral("from"), arguments.renameCollectionOld);
        object.insert(QStringLiteral("to"), arguments.renameCollectionNew);
        object.insert(QStringLiteral("affected"), affected);
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << QStringLiteral("Renamed collection '%1' to '%2' (%3 member%4 updated)")
                   .arg(arguments.renameCollectionOld, arguments.renameCollectionNew)
                   .arg(affected)
                   .arg(affected == 1 ? QString() : QStringLiteral("s"))
            << Qt::endl;
        out.flush();
    }
    return 0;
}

int commandRemoveCollection(const CliArguments& arguments, Settings& settings) {
    int affected = 0;
    QString error;
    if (!removeCollection(settings, arguments.removeCollectionName, affected, error))
        return failure(QStringLiteral("remove-collection"), arguments.json, error);
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("remove-collection"));
        object.insert(QStringLiteral("ok"), true);
        object.insert(QStringLiteral("name"), arguments.removeCollectionName);
        object.insert(QStringLiteral("affected"), affected);
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << QStringLiteral("Removed collection '%1' (%2 member%3)")
                   .arg(arguments.removeCollectionName)
                   .arg(affected)
                   .arg(affected == 1 ? QString() : QStringLiteral("s"))
            << Qt::endl;
        out.flush();
    }
    return 0;
}

int commandBulk(const CliArguments& arguments, Settings& settings) {
    QString error;
    QJsonObject document;
    if (!readJsonFile(arguments.bulkFile, document, error))
        return failure(QStringLiteral("bulk"), arguments.json, error);
    static const QStringList known = {
        QStringLiteral("setCollection"), QStringLiteral("setEmulator"),
        QStringLiteral("addTags"), QStringLiteral("removeTags"),
        QStringLiteral("remove"), QStringLiteral("ids")
    };
    for (auto it = document.begin(); it != document.end(); ++it) {
        if (!known.contains(it.key()))
            return failure(QStringLiteral("bulk"), arguments.json,
                           QStringLiteral("Bulk file has unknown field '%1'").arg(it.key()));
    }
    const auto idsValue = document.constFind(QStringLiteral("ids"));
    if (idsValue == document.constEnd() || !idsValue->isArray())
        return failure(QStringLiteral("bulk"), arguments.json,
                       QStringLiteral("Bulk file field 'ids' must be an array"));
    BulkRequest request;
    for (const QJsonValue& value : idsValue->toArray()) {
        if (!value.isString())
            return failure(QStringLiteral("bulk"), arguments.json,
                           QStringLiteral("Bulk 'ids' must contain strings"));
        request.ids.append(value.toString());
    }
    const auto collectionValue = document.constFind(QStringLiteral("setCollection"));
    if (collectionValue != document.constEnd()) {
        if (!collectionValue->isString())
            return failure(QStringLiteral("bulk"), arguments.json,
                           QStringLiteral("Bulk field 'setCollection' must be a string"));
        request.setCollection = true;
        request.collection = collectionValue->toString();
    }
    const auto emulatorValue = document.constFind(QStringLiteral("setEmulator"));
    if (emulatorValue != document.constEnd()) {
        if (!emulatorValue->isString() || emulatorValue->toString().trimmed().isEmpty())
            return failure(QStringLiteral("bulk"), arguments.json,
                           QStringLiteral("Bulk field 'setEmulator' must be a non-empty string"));
        request.setEmulator = true;
        request.emulatorId = emulatorValue->toString();
    }
    const auto readTags = [&](const char* key, QStringList& target) -> bool {
        const auto value = document.constFind(QLatin1String(key));
        if (value == document.constEnd())
            return true;
        if (!value->isArray())
            return false;
        for (const QJsonValue& item : value->toArray()) {
            if (!item.isString())
                return false;
            target.append(item.toString());
        }
        return true;
    };
    if (!readTags("addTags", request.addTags) || !readTags("removeTags", request.removeTags))
        return failure(QStringLiteral("bulk"), arguments.json,
                       QStringLiteral("Bulk 'addTags' and 'removeTags' must be arrays of strings"));
    const auto removeValue = document.constFind(QStringLiteral("remove"));
    if (removeValue != document.constEnd()) {
        if (!removeValue->isBool())
            return failure(QStringLiteral("bulk"), arguments.json,
                           QStringLiteral("Bulk field 'remove' must be a boolean"));
        request.remove = removeValue->toBool();
    }
    QVector<BulkResult> results;
    const bool applied = applyBulk(settings, request, results, error);
    int succeeded = 0;
    int failed = 0;
    QJsonArray resultArray;
    for (const BulkResult& result : results) {
        if (result.ok)
            ++succeeded;
        else
            ++failed;
        QJsonObject entry{{QStringLiteral("id"), result.id},
                          {QStringLiteral("ok"), result.ok},
                          {QStringLiteral("action"), result.action}};
        if (!result.error.isEmpty())
            entry.insert(QStringLiteral("error"), result.error);
        resultArray.append(entry);
    }
    const bool ok = applied && failed == 0;
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("bulk"));
        object.insert(QStringLiteral("ok"), ok);
        object.insert(QStringLiteral("applied"), succeeded);
        object.insert(QStringLiteral("failed"), failed);
        object.insert(QStringLiteral("rolledBack"), !applied);
        object.insert(QStringLiteral("results"), resultArray);
        if (!ok) {
            const QString message = error.isEmpty() ? QStringLiteral("Bulk request failed") : error;
            object.insert(QStringLiteral("error"), message);
        }
        emitJson(object);
    } else {
        QTextStream out(stdout);
        for (const BulkResult& result : results) {
            out << QStringLiteral("%1 | %2 | %3")
                       .arg(result.ok ? QStringLiteral("ok") : QStringLiteral("failed"),
                            result.id, result.action)
                << Qt::endl;
            if (!result.error.isEmpty())
                out << QStringLiteral("    %1").arg(result.error) << Qt::endl;
        }
        out << QStringLiteral("Bulk applied %1, failed %2").arg(succeeded).arg(failed)
            << Qt::endl;
        out.flush();
    }
    return ok ? 0 : 1;
}

int commandRelink(const CliArguments& arguments, Settings& settings) {
    QJsonObject summary;
    QString error;
    if (!relinkPackage(settings, arguments.relinkId, arguments.relinkPath, arguments.force,
                       summary, error)) {
        if (arguments.json) {
            QJsonObject object = baseObject(QStringLiteral("relink"));
            object.insert(QStringLiteral("ok"), false);
            object.insert(QStringLiteral("appId"), arguments.relinkId);
            object.insert(QStringLiteral("newPath"), arguments.relinkPath);
            object.insert(QStringLiteral("forced"), arguments.force);
            object.insert(QStringLiteral("error"), error);
            emitJson(object);
        }
        logError(error);
        return 1;
    }
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("relink"));
        object.insert(QStringLiteral("ok"), true);
        for (auto it = summary.begin(); it != summary.end(); ++it)
            object.insert(it.key(), it.value());
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << QStringLiteral("Relinked %1 -> %2 (%3 %4) with %5 argument%6")
                   .arg(arguments.relinkId,
                        summary.value(QStringLiteral("newPath")).toString(),
                        summary.value(QStringLiteral("hashLabel")).toString(),
                        summary.value(QStringLiteral("newHash")).toString(),
                        QString::number(summary.value(QStringLiteral("arguments"))
                                            .toArray().size()),
                        summary.value(QStringLiteral("arguments")).toArray().size() == 1
                            ? QString()
                            : QStringLiteral("s"))
            << Qt::endl;
        if (arguments.force)
            out << QStringLiteral("    (accepted with --force)") << Qt::endl;
        out.flush();
    }
    return 0;
}

int commandVerifyHashes(const CliArguments& arguments, Settings& settings) {
    QVector<HashVerification> results;
    QString error;
    if (!verifyPackageHashes(settings, results, error))
        return failure(QStringLiteral("verify-hashes"), arguments.json, error);
    QJsonArray array;
    int passed = 0;
    int failed = 0;
    for (const HashVerification& check : results) {
        if (check.state == QStringLiteral("ok"))
            ++passed;
        else
            ++failed;
        array.append(QJsonObject{{QStringLiteral("id"), check.id},
                                 {QStringLiteral("state"), check.state},
                                 {QStringLiteral("romPath"), check.romPath},
                                 {QStringLiteral("storedLabel"), check.storedLabel},
                                 {QStringLiteral("storedHash"), check.storedHash},
                                 {QStringLiteral("currentHash"), check.currentHash},
                                 {QStringLiteral("error"), check.error}});
    }
    const bool ok = failed == 0;
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("verify-hashes"));
        object.insert(QStringLiteral("ok"), ok);
        object.insert(QStringLiteral("checked"), results.size());
        object.insert(QStringLiteral("passed"), passed);
        object.insert(QStringLiteral("failed"), failed);
        object.insert(QStringLiteral("packages"), array);
        emitJson(object);
    } else {
        QTextStream out(stdout);
        for (const HashVerification& check : results) {
            out << QStringLiteral("%1 | %2 | %3")
                       .arg(check.state, check.id, check.error)
                << Qt::endl;
        }
        out << QStringLiteral("Verify-hashes: %1 checked, %2 ok, %3 failed")
                   .arg(results.size())
                   .arg(passed)
                   .arg(failed)
            << Qt::endl;
        out.flush();
    }
    return ok ? 0 : 1;
}

int commandLaunchCheck(const CliArguments& arguments, Settings& settings) {
    LaunchOutcome outcome;
    QString error;
    const bool spawned = launchPackage(settings, arguments.launchCheckId,
                                       arguments.launchTimeoutSeconds, outcome, error);
    const bool ok = spawned && !outcome.timedOut && outcome.exitCode == 0;
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("launch-check"));
        object.insert(QStringLiteral("ok"), ok);
        object.insert(QStringLiteral("appId"), outcome.appId);
        object.insert(QStringLiteral("executable"), outcome.executable);
        object.insert(QStringLiteral("workingDirectory"), outcome.workingDirectory);
        object.insert(QStringLiteral("arguments"), stringArray(outcome.arguments));
        object.insert(QStringLiteral("pid"), double(outcome.pid));
        object.insert(QStringLiteral("started"), outcome.started);
        object.insert(QStringLiteral("finished"), outcome.finished);
        object.insert(QStringLiteral("timedOut"), outcome.timedOut);
        object.insert(QStringLiteral("terminated"), outcome.terminated);
        object.insert(QStringLiteral("killed"), outcome.killed);
        object.insert(QStringLiteral("timeoutSeconds"), outcome.timeoutSeconds);
        object.insert(QStringLiteral("exitCode"), outcome.exitCode);
        object.insert(QStringLiteral("exitStatus"), outcome.exitStatus);
        object.insert(QStringLiteral("spawnError"), outcome.spawnError);
        object.insert(QStringLiteral("stdout"), outcome.stdoutText);
        object.insert(QStringLiteral("stderr"), outcome.stderrText);
        if (!ok) {
            const QString message = error.isEmpty() ? QStringLiteral("launch-check failed") : error;
            object.insert(QStringLiteral("error"), message);
        }
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << QStringLiteral("executable: %1").arg(outcome.executable) << Qt::endl;
        out << QStringLiteral("argv: %1").arg(formatArguments(outcome.arguments)) << Qt::endl;
        out << QStringLiteral("pid: %1").arg(outcome.pid) << Qt::endl;
        if (!outcome.spawnError.isEmpty())
            out << QStringLiteral("spawn error: %1").arg(outcome.spawnError) << Qt::endl;
        if (outcome.timedOut)
            out << QStringLiteral("timed out after %1s (%2)")
                       .arg(outcome.timeoutSeconds)
                       .arg(outcome.killed ? QStringLiteral("killed")
                                           : QStringLiteral("terminated"))
                << Qt::endl;
        else
            out << QStringLiteral("exit code: %1").arg(outcome.exitCode) << Qt::endl;
        if (!outcome.stdoutText.isEmpty())
            out << QStringLiteral("stdout: %1").arg(outcome.stdoutText.trimmed()) << Qt::endl;
        if (!outcome.stderrText.isEmpty())
            out << QStringLiteral("stderr: %1").arg(outcome.stderrText.trimmed()) << Qt::endl;
        out.flush();
    }
    return ok ? 0 : 1;
}

int commandPrintArgv(const CliArguments& arguments, Settings& settings) {
    QString executable;
    QString workingDirectory;
    QStringList argv;
    QString error;
    if (!packageArgv(settings, arguments.printArgvId, executable, workingDirectory, argv, error))
        return failure(QStringLiteral("print-argv"), arguments.json, error);
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("print-argv"));
        object.insert(QStringLiteral("ok"), true);
        object.insert(QStringLiteral("appId"), arguments.printArgvId);
        object.insert(QStringLiteral("executable"), executable);
        object.insert(QStringLiteral("workingDirectory"), workingDirectory);
        object.insert(QStringLiteral("arguments"), stringArray(argv));
        object.insert(QStringLiteral("argv"), stringArray(argv));
        object.insert(QStringLiteral("rendered"), formatArguments(argv));
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << formatArguments(argv) << Qt::endl;
        out.flush();
    }
    return 0;
}

int commandBackup(const CliArguments& arguments, Settings& settings) {
    BackupOptions options;
    options.withPackagedArt = arguments.withPackagedArt;
    QJsonObject summary;
    QString error;
    if (!writeBackup(settings, arguments.backupFile, options, summary, error))
        return failure(QStringLiteral("backup"), arguments.json, error);
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("backup"));
        object.insert(QStringLiteral("ok"), true);
        for (auto it = summary.begin(); it != summary.end(); ++it)
            object.insert(it.key(), it.value());
        emitJson(object);
    } else {
        QTextStream out(stdout);
        out << QStringLiteral("Wrote %1 (%2 apps, %3 packages, %4 collections, %5 mappings, %6 "
                              "art file%7)")
                   .arg(arguments.backupFile)
                   .arg(summary.value(QStringLiteral("appCount")).toInt())
                   .arg(summary.value(QStringLiteral("packageCount")).toInt())
                   .arg(summary.value(QStringLiteral("collectionCount")).toInt())
                   .arg(summary.value(QStringLiteral("mappingCount")).toInt())
                   .arg(summary.value(QStringLiteral("artIncluded")).toInt())
                   .arg(summary.value(QStringLiteral("artIncluded")).toInt() == 1 ? QString()
                                                                                 : QStringLiteral("s"))
            << Qt::endl;
        out.flush();
    }
    return 0;
}

int commandRestore(const CliArguments& arguments, Settings& settings) {
    QVector<PathRemap> remaps;
    for (const QString& text : arguments.remapRules) {
        PathRemap remap;
        QString remapError;
        if (!parsePathRemap(text, remap, remapError)) {
            if (arguments.json) {
                QJsonObject object = baseObject(QStringLiteral("restore"));
                object.insert(QStringLiteral("ok"), false);
                object.insert(QStringLiteral("error"), remapError);
                emitJson(object);
            }
            logError(remapError);
            return 2;
        }
        remaps.append(remap);
    }
    QJsonArray actions;
    QJsonArray conflicts;
    QString error;
    const bool restored = restoreBackup(settings, arguments.restoreFile, remaps,
                                        arguments.replace, actions, conflicts, error);
    if (arguments.json) {
        QJsonObject object = baseObject(QStringLiteral("restore"));
        object.insert(QStringLiteral("ok"), restored);
        object.insert(QStringLiteral("file"), arguments.restoreFile);
        object.insert(QStringLiteral("replaced"), arguments.replace);
        object.insert(QStringLiteral("remapCount"), remaps.size());
        object.insert(QStringLiteral("actions"), actions);
        object.insert(QStringLiteral("conflicts"), conflicts);
        if (!restored)
            object.insert(QStringLiteral("error"), error);
        emitJson(object);
    } else {
        QTextStream out(stdout);
        for (const QJsonValue& value : actions) {
            const QJsonObject action = value.toObject();
            out << QStringLiteral("%1 | %2 | %3")
                       .arg(action.value(QStringLiteral("kind")).toString(),
                            action.value(QStringLiteral("id")).toString(),
                            action.value(QStringLiteral("action")).toString())
                << Qt::endl;
        }
        for (const QJsonValue& value : conflicts) {
            const QJsonObject conflict = value.toObject();
            out << QStringLiteral("conflict | %1")
                       .arg(conflict.value(QStringLiteral("id")).toString())
                << Qt::endl;
        }
        if (restored)
            out << QStringLiteral("Restored %1").arg(arguments.restoreFile) << Qt::endl;
        out.flush();
    }
    if (!restored)
        logError(error);
    return restored ? 0 : 1;
}

} // namespace

bool isCliInvocation(const QStringList& arguments) {
    for (qsizetype index = 1; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        for (const QString& verb : verbNames()) {
            if (argument == verb || argument.startsWith(verb + QLatin1Char('=')))
                return true;
        }
        if (argument == QStringLiteral("--json") || argument == QStringLiteral("--help")
            || argument == QStringLiteral("-h") || argument == QStringLiteral("--version"))
            return true;
    }
    return false;
}

QString cliUsage() {
    return QStringLiteral(
        "xmb-manager - XMB Manager CLI\n"
        "\n"
        "Usage: xmb-manager <command> [options]\n"
        "\n"
        "Emulators and scanning:\n"
        "  --list-emulators             List built-in and configured emulator profiles\n"
        "  --detect-emulators           Scan install locations/registry and save detections\n"
        "  --scan <dir...>              Scan files or folders for games\n"
        "  --import <path...> [--emulator <id>] [--collection <name>] [--replace]\n"
        "                               Import games as virtual packages (emulator resolved\n"
        "                               from platform-map.json when --emulator is omitted)\n"
        "  --import-pkg <file>          Import a readable PS3 .pkg (refuses retail crypto)\n"
        "\n"
        "Library:\n"
        "  --list [--tag <tag>]         List apps.json entries and packages\n"
        "  --remove <appId>             Remove a package and its registration\n"
        "  --validate                   Check registry, packages, mappings, tags, hashes\n"
        "  --export-report <file>       Write a JSON report (xmb-manager-report/2)\n"
        "\n"
        "Platform map:\n"
        "  --map-platform <platform> <emulatorId>  Map a scanner platform to an emulator\n"
        "  --list-mappings [--json]     Show the resolved platform -> emulator table\n"
        "\n"
        "Tags and collections:\n"
        "  --tag <appId> <tag...>       Add tags to a package\n"
        "  --untag <appId> <tag...>     Remove tags from a package\n"
        "  --tags                       List every tag with its package count\n"
        "  --set-collection <appId> <name>  Set (or clear with \"\") the package collection\n"
        "  --collections                List collections with counts\n"
        "  --rename-collection <old> <new>  Rename a collection and update members\n"
        "  --remove-collection <name>   Remove a collection and update members\n"
        "\n"
        "Bulk operations:\n"
        "  --bulk <file.json>           Apply {ids, setCollection, setEmulator, addTags,\n"
        "                               removeTags, remove} to many packages transactionally\n"
        "\n"
        "ROM identity:\n"
        "  --relink <appId> <newPath> [--force]  Move a package to a new ROM path\n"
        "  --verify-hashes              Verify every recorded ROM content hash\n"
        "\n"
        "Launch:\n"
        "  --launch-check <appId> [--launch-timeout <sec>]  Launch through the registered\n"
        "                               emulator and report executable, argv, pid, exit code\n"
        "  --print-argv <appId>         Print the exact argv as JSON without launching\n"
        "\n"
        "Backup:\n"
        "  --backup <file.json> [--with-packaged-art]  Write a portable bundle\n"
        "  --restore <file.json> [--remap <from>=<to> ...] [--replace]  Restore a bundle\n"
        "\n"
        "Options:\n"
        "  --data-dir <dir>             Data directory (default: <exe dir>/data)\n"
        "  --json                       Machine-readable JSON on stdout\n"
        "  --ui                         Start the Qt UI (no command)\n"
        "  -h, --help                   Show this help\n"
        "  --version                    Show the version\n"
        "\n"
        "Exit codes: 0 success, 1 operation failed, 2 usage/configuration error.\n");
}

int runCli(const QStringList& arguments) {
    CliArguments parsed;
    QString parseError;
    if (!parseArguments(arguments, parsed, parseError))
        return usageFailure(parsed.json, parseError);
    if (parsed.help) {
        QTextStream out(stdout);
        out << cliUsage();
        out.flush();
        return 0;
    }
    if (parsed.version) {
        QTextStream out(stdout);
        out << QStringLiteral("xmb-manager 0.1.0") << Qt::endl;
        out.flush();
        return 0;
    }
    if (parsed.command.isEmpty())
        return usageFailure(parsed.json, QStringLiteral("No command was given"));
    if (parsed.ui)
        return usageFailure(parsed.json, QStringLiteral("--ui cannot be combined with a command"));
    Settings settings;
    QString settingsError;
    if (!loadSettingsOrReport(parsed, settings, settingsError))
        return usageFailure(parsed.json, settingsError);
    if (parsed.command == QStringLiteral("list-emulators"))
        return commandListEmulators(parsed, settings);
    if (parsed.command == QStringLiteral("detect-emulators"))
        return commandDetectEmulators(parsed, settings);
    if (parsed.command == QStringLiteral("scan"))
        return commandScan(parsed);
    if (parsed.command == QStringLiteral("import"))
        return commandImport(parsed, settings);
    if (parsed.command == QStringLiteral("import-pkg"))
        return commandImportPkg(parsed, settings);
    if (parsed.command == QStringLiteral("list"))
        return commandList(parsed, settings);
    if (parsed.command == QStringLiteral("remove"))
        return commandRemove(parsed, settings);
    if (parsed.command == QStringLiteral("validate"))
        return commandValidate(parsed, settings);
    if (parsed.command == QStringLiteral("export-report"))
        return commandExportReport(parsed, settings);
    if (parsed.command == QStringLiteral("map-platform"))
        return commandMapPlatform(parsed, settings);
    if (parsed.command == QStringLiteral("list-mappings"))
        return commandListMappings(parsed, settings);
    if (parsed.command == QStringLiteral("tag"))
        return commandTagOperation(parsed, settings, false);
    if (parsed.command == QStringLiteral("untag"))
        return commandTagOperation(parsed, settings, true);
    if (parsed.command == QStringLiteral("tags"))
        return commandTags(parsed, settings);
    if (parsed.command == QStringLiteral("set-collection"))
        return commandSetCollection(parsed, settings);
    if (parsed.command == QStringLiteral("collections"))
        return commandCollections(parsed, settings);
    if (parsed.command == QStringLiteral("rename-collection"))
        return commandRenameCollection(parsed, settings);
    if (parsed.command == QStringLiteral("remove-collection"))
        return commandRemoveCollection(parsed, settings);
    if (parsed.command == QStringLiteral("bulk"))
        return commandBulk(parsed, settings);
    if (parsed.command == QStringLiteral("relink"))
        return commandRelink(parsed, settings);
    if (parsed.command == QStringLiteral("verify-hashes"))
        return commandVerifyHashes(parsed, settings);
    if (parsed.command == QStringLiteral("launch-check"))
        return commandLaunchCheck(parsed, settings);
    if (parsed.command == QStringLiteral("print-argv"))
        return commandPrintArgv(parsed, settings);
    if (parsed.command == QStringLiteral("backup"))
        return commandBackup(parsed, settings);
    if (parsed.command == QStringLiteral("restore"))
        return commandRestore(parsed, settings);
    return usageFailure(parsed.json, QStringLiteral("Unknown command"));
}

} // namespace xmb::manager


