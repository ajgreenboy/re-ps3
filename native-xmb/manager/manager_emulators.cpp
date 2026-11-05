#include "manager_emulators.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSettings>

#ifdef Q_OS_WIN
#include <QStandardPaths>
#endif

#include <algorithm>

namespace xmb::manager {
namespace {

struct BuiltInSpec {
    const char* id;
    const char* name;
    const char* platforms;      // comma-separated
    const char* argumentTemplate;
};

const BuiltInSpec kBuiltIns[] = {
    {"rpcs3", "RPCS3", "PS3", "{rom}"},
    {"retroarch", "RetroArch",
     "PS1,PS2,PSP,GameCube/Wii,SNES,NES,N64,Genesis,GBA,Game Boy,Game Boy Color,"
     "Saturn,Dreamcast,Arcade,NDS",
     "-L {core} {rom}"},
    {"pcsx2", "PCSX2", "PS2", "{rom}"},
    {"dolphin", "Dolphin", "GameCube/Wii", "-e {rom}"},
    {"duckstation", "DuckStation", "PS1", "{rom}"},
    {"ppsspp", "PPSSPP", "PSP", "{rom}"},
    {"cemu", "Cemu", "Wii U", "-g {rom}"},
    {"ryujinx", "Ryujinx", "Switch", "{rom}"},
    {"xenia", "Xenia", "Xbox 360", "{rom}"},
    {"mame", "MAME", "Arcade", "{rom}"},
    {"mednafen", "Mednafen", "PS1,Saturn,NES,SNES,Genesis,Game Boy,Game Boy Color", "{rom}"},
    {"melonds", "melonDS", "NDS", "{rom}"},
    {"desmume", "DeSmuME", "NDS", "{rom}"},
};

struct DetectionSpec {
    QString id;
    QStringList registryNeedles;
    QStringList fileNames;
    QStringList folderNames;
};

const QVector<DetectionSpec>& detectionSpecs() {
    static const QVector<DetectionSpec> specs = {
        {QStringLiteral("rpcs3"), {QStringLiteral("rpcs3")},
         {QStringLiteral("rpcs3.exe")}, {QStringLiteral("RPCS3")}},
        {QStringLiteral("retroarch"), {QStringLiteral("retroarch")},
         {QStringLiteral("retroarch.exe")},
         {QStringLiteral("RetroArch"), QStringLiteral("RetroArch-Win64")}},
        {QStringLiteral("pcsx2"), {QStringLiteral("pcsx2")},
         {QStringLiteral("pcsx2-qt.exe"), QStringLiteral("pcsx2-qtx64.exe"),
          QStringLiteral("pcsx2.exe")},
         {QStringLiteral("PCSX2")}},
        {QStringLiteral("dolphin"), {QStringLiteral("dolphin")},
         {QStringLiteral("Dolphin.exe"), QStringLiteral("dolphin-emu.exe")},
         {QStringLiteral("Dolphin"), QStringLiteral("Dolphin-x64")}},
        {QStringLiteral("duckstation"), {QStringLiteral("duckstation")},
         {QStringLiteral("duckstation-qt-x64-ReleaseLTCG.exe"),
          QStringLiteral("duckstation-qt-x64-Release.exe"),
          QStringLiteral("duckstation-qt.exe"), QStringLiteral("duckstation.exe")},
         {QStringLiteral("DuckStation")}},
        {QStringLiteral("ppsspp"), {QStringLiteral("ppsspp")},
         {QStringLiteral("PPSSPPWindows64.exe"), QStringLiteral("PPSSPPWindows.exe")},
         {QStringLiteral("PPSSPP")}},
        {QStringLiteral("cemu"), {QStringLiteral("cemu")},
         {QStringLiteral("Cemu.exe"), QStringLiteral("Cemu_release.exe")},
         {QStringLiteral("Cemu")}},
        {QStringLiteral("ryujinx"), {QStringLiteral("ryujinx")},
         {QStringLiteral("Ryujinx.exe"), QStringLiteral("Ryujinx.Ava.exe")},
         {QStringLiteral("Ryujinx"), QStringLiteral("Ryujinx Ava")}},
        {QStringLiteral("xenia"), {QStringLiteral("xenia")},
         {QStringLiteral("xenia_canary.exe"), QStringLiteral("xenia.exe")},
         {QStringLiteral("Xenia"), QStringLiteral("Xenia Canary")}},
        {QStringLiteral("mame"), {QStringLiteral("mame")},
         {QStringLiteral("mame.exe")}, {QStringLiteral("MAME"), QStringLiteral("mame")}},
        {QStringLiteral("mednafen"), {QStringLiteral("mednafen")},
         {QStringLiteral("mednafen.exe")},
         {QStringLiteral("mednafen"), QStringLiteral("Mednafen")}},
        {QStringLiteral("melonds"), {QStringLiteral("melonds")},
         {QStringLiteral("melonDS.exe")},
         {QStringLiteral("melonDS"), QStringLiteral("melonDS 0.9")}},
        {QStringLiteral("desmume"), {QStringLiteral("desmume")},
         {QStringLiteral("DeSmuME.exe"), QStringLiteral("DeSmuME_x64.exe"),
          QStringLiteral("desmume.exe")},
         {QStringLiteral("DeSmuME"), QStringLiteral("DeSmuME 0.9")}},
    };
    return specs;
}

QStringList splitPlatforms(const QString& value) {
    QStringList result;
    for (const QString& part : value.split(QLatin1Char(','))) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty() && !result.contains(trimmed))
            result.append(trimmed);
    }
    return result;
}

bool fail(QString& error, const QString& message) {
    error = message;
    return false;
}

bool validId(const QString& id) {
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

bool hasControlCharacters(const QString& text) {
    for (const QChar ch : text) {
        if (ch.unicode() < 0x20 || ch.unicode() == 0x7f)
            return true;
    }
    return false;
}

bool validateProfile(const EmulatorProfile& profile, QString& error) {
    const QString where = QStringLiteral("emulator '%1'").arg(profile.id);
    if (!validId(profile.id))
        return fail(error, QStringLiteral("Emulator id '%1' may contain only ASCII letters, "
                                          "digits, '_', '.' and '-' (1..64 characters)")
                              .arg(profile.id));
    if (profile.name.trimmed().isEmpty())
        return fail(error, QStringLiteral("%1 has an empty name").arg(where));
    if (hasControlCharacters(profile.name) || hasControlCharacters(profile.argumentTemplate))
        return fail(error, QStringLiteral("%1 contains control characters").arg(where));
    if (profile.argumentTemplate.size() > 4096)
        return fail(error, QStringLiteral("%1 argument template is longer than 4096 characters")
                              .arg(where));
    for (const QString& platform : profile.platforms) {
        if (platform.trimmed().isEmpty())
            return fail(error, QStringLiteral("%1 has an empty platform name").arg(where));
        if (hasControlCharacters(platform))
            return fail(error, QStringLiteral("%1 has a platform name with control characters")
                                  .arg(where));
    }
    for (auto it = profile.env.begin(); it != profile.env.end(); ++it) {
        if (it.key().trimmed().isEmpty() || hasControlCharacters(it.key())
            || it.value().contains(QChar::Null))
            return fail(error, QStringLiteral("%1 has an invalid environment entry").arg(where));
    }
    QString executable = profile.executable.trimmed();
    if (!executable.isEmpty()) {
        executable = QDir::cleanPath(QDir::fromNativeSeparators(executable));
        if (!QDir::isAbsolutePath(executable))
            return fail(error, QStringLiteral("%1 executable must be absolute").arg(where));
#ifdef Q_OS_WIN
        if (QFileInfo(executable).suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0)
            return fail(error, QStringLiteral("%1 executable must end in .exe").arg(where));
#endif
    }
    const QString workingDirectory = profile.workingDirectory.trimmed();
    if (!workingDirectory.isEmpty() && !QDir::isAbsolutePath(
            QDir::cleanPath(QDir::fromNativeSeparators(workingDirectory))))
        return fail(error, QStringLiteral("%1 working directory must be absolute").arg(where));
    return true;
}

QJsonObject serializeProfile(const EmulatorProfile& profile) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), profile.id);
    object.insert(QStringLiteral("name"), profile.name);
    if (!profile.executable.isEmpty())
        object.insert(QStringLiteral("executable"), profile.executable);
    if (!profile.workingDirectory.isEmpty())
        object.insert(QStringLiteral("workingDirectory"), profile.workingDirectory);
    QJsonArray platforms;
    for (const QString& platform : profile.platforms)
        platforms.append(platform);
    object.insert(QStringLiteral("platforms"), platforms);
    object.insert(QStringLiteral("argumentTemplate"), profile.argumentTemplate);
    QJsonObject env;
    QStringList envKeys = profile.env.keys();
    envKeys.sort();
    for (const QString& key : envKeys)
        env.insert(key, profile.env.value(key));
    object.insert(QStringLiteral("env"), env);
    object.insert(QStringLiteral("builtIn"), profile.builtIn);
    return object;
}

bool parseProfile(const QJsonObject& object, qsizetype index, EmulatorProfile& profile,
                  QString& error) {
    const QString where = QStringLiteral("emulators[%1]").arg(index);
    static const QStringList known = {
        QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("executable"),
        QStringLiteral("workingDirectory"), QStringLiteral("platforms"),
        QStringLiteral("argumentTemplate"), QStringLiteral("env"), QStringLiteral("builtIn")
    };
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!known.contains(it.key()))
            return fail(error, QStringLiteral("%1 has unknown field '%2'").arg(where, it.key()));
    }
    const auto stringField = [&](const QString& key, bool required, QString& out) {
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
    };
    if (!stringField(QStringLiteral("id"), true, profile.id)
        || !stringField(QStringLiteral("name"), true, profile.name)
        || !stringField(QStringLiteral("executable"), false, profile.executable)
        || !stringField(QStringLiteral("workingDirectory"), false, profile.workingDirectory)
        || !stringField(QStringLiteral("argumentTemplate"), false, profile.argumentTemplate))
        return false;
    const auto platformsValue = object.constFind(QStringLiteral("platforms"));
    if (platformsValue == object.constEnd() || !platformsValue->isArray())
        return fail(error, QStringLiteral("%1 field 'platforms' must be an array").arg(where));
    profile.platforms.clear();
    for (const QJsonValue& value : platformsValue->toArray()) {
        if (!value.isString())
            return fail(error, QStringLiteral("%1 platforms must contain strings").arg(where));
        const QString platform = value.toString().trimmed();
        if (platform.isEmpty())
            return fail(error, QStringLiteral("%1 has an empty platform").arg(where));
        if (!profile.platforms.contains(platform))
            profile.platforms.append(platform);
    }
    const auto envValue = object.constFind(QStringLiteral("env"));
    profile.env.clear();
    if (envValue != object.constEnd()) {
        if (!envValue->isObject())
            return fail(error, QStringLiteral("%1 field 'env' must be an object").arg(where));
        const QJsonObject env = envValue->toObject();
        for (auto it = env.begin(); it != env.end(); ++it) {
            if (!it->isString())
                return fail(error, QStringLiteral("%1 env values must be strings").arg(where));
            if (hasControlCharacters(it.key()) || it->toString().contains(QChar::Null))
                return fail(error, QStringLiteral("%1 env entry is invalid").arg(where));
            profile.env.insert(it.key(), it->toString());
        }
    }
    if (object.contains(QStringLiteral("builtIn")) && !object.value(QStringLiteral("builtIn")).isBool())
        return fail(error, QStringLiteral("%1 field 'builtIn' must be a boolean").arg(where));
    if (!validateProfile(profile, error))
        return false;
    return true;
}

bool parseKeyValues(const QStringList& keyValues, QHash<QString, QString>& values,
                    QString& error) {
    values.clear();
    for (const QString& entry : keyValues) {
        const int equals = entry.indexOf(QLatin1Char('='));
        if (equals <= 0)
            return fail(error, QStringLiteral("Argument value '%1' must have the form name=value")
                                  .arg(entry));
        const QString key = entry.left(equals).trimmed();
        const QString value = entry.mid(equals + 1);
        if (key.isEmpty() || hasControlCharacters(key))
            return fail(error, QStringLiteral("Argument value '%1' has an invalid name").arg(entry));
        if (value.contains(QChar::Null))
            return fail(error, QStringLiteral("Argument value for '%1' contains NUL").arg(key));
        if (values.contains(key))
            return fail(error, QStringLiteral("Argument value '%1' was supplied twice").arg(key));
        values.insert(key, value);
    }
    return true;
}

QStringList knownRoots() {
    QStringList roots;
#ifdef Q_OS_WIN
    const auto addEnv = [&roots](const char* name) {
        const QString value = qEnvironmentVariable(name).trimmed();
        if (!value.isEmpty())
            roots.append(QDir::fromNativeSeparators(value));
    };
    addEnv("ProgramFiles");
    addEnv("ProgramFiles(x86)");
    addEnv("ProgramW6432");
    addEnv("LOCALAPPDATA");
    addEnv("APPDATA");
    const QString localPrograms = qEnvironmentVariable("LOCALAPPDATA");
    if (!localPrograms.isEmpty())
        roots.append(QDir(localPrograms).filePath(QStringLiteral("Programs")));
    const QString profile = qEnvironmentVariable("USERPROFILE");
    if (!profile.isEmpty()) {
        roots.append(QDir(profile).filePath(QStringLiteral("Applications")));
        roots.append(QDir(profile).filePath(QStringLiteral("Emulators")));
    }
#endif
    const QFileInfoList drives = QDir::drives();
    for (const QFileInfo& drive : drives) {
        const QString root = drive.absoluteFilePath();
        for (const char* folder : {"Emulators", "Emulation", "Games", "RetroArch-Win64"}) {
            const QString candidate = QDir(root).filePath(QLatin1String(folder));
            if (QFileInfo(candidate).isDir())
                roots.append(candidate);
        }
    }
    QStringList unique;
    for (const QString& root : roots) {
        const QString cleaned = QDir::cleanPath(root);
        if (QFileInfo(cleaned).isDir() && !unique.contains(cleaned, Qt::CaseInsensitive))
            unique.append(cleaned);
    }
    return unique;
}

struct RegistryEntry {
    QString key;
    QString displayName;
    QString installLocation;
    QString displayIcon;
    QString uninstallString;
};

QVector<RegistryEntry> readUninstallRegistry() {
    QVector<RegistryEntry> entries;
#ifdef Q_OS_WIN
    const QStringList roots = {
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
    };
    for (const QString& root : roots) {
        QSettings rootSettings(root, QSettings::NativeFormat);
        if (rootSettings.status() != QSettings::NoError)
            continue;
        for (const QString& group : rootSettings.childGroups()) {
            const QString key = root + QLatin1Char('\\') + group;
            QSettings item(key, QSettings::NativeFormat);
            if (item.status() != QSettings::NoError)
                continue;
            RegistryEntry entry;
            entry.key = key;
            entry.displayName = item.value(QStringLiteral("DisplayName")).toString().trimmed();
            entry.installLocation =
                item.value(QStringLiteral("InstallLocation")).toString().trimmed();
            entry.displayIcon = item.value(QStringLiteral("DisplayIcon")).toString().trimmed();
            entry.uninstallString = item.value(QStringLiteral("UninstallString")).toString().trimmed();
            if (!entry.displayName.isEmpty())
                entries.append(entry);
        }
    }
#endif
    return entries;
}

QString quotedPath(const QString& value) {
    QString text = value.trimmed();
    if (text.startsWith(QLatin1Char('"'))) {
        const int closing = text.indexOf(QLatin1Char('"'), 1);
        if (closing > 1)
            return text.mid(1, closing - 1);
        return text.mid(1);
    }
    const int comma = text.indexOf(QLatin1Char(','));
    if (comma >= 0)
        text = text.left(comma);
    return text.trimmed();
}

QString existingExecutable(const QString& directory, const QStringList& fileNames,
                           bool allowOneLevel) {
    if (directory.isEmpty())
        return QString();
    const QFileInfo dirInfo(directory);
    if (!dirInfo.isDir())
        return QString();
    for (const QString& name : fileNames) {
        const QString direct = QDir(directory).filePath(name);
        if (QFileInfo(direct).isFile())
            return QDir::cleanPath(QFileInfo(direct).absoluteFilePath());
        const QString bin = QDir(directory).filePath(QStringLiteral("bin/") + name);
        if (QFileInfo(bin).isFile())
            return QDir::cleanPath(QFileInfo(bin).absoluteFilePath());
    }
    if (!allowOneLevel)
        return QString();
    // List the DIRECTORY itself, not QFileInfo(directory).dir(): for a path without a trailing separator the
    // latter is the PARENT, which silently skipped the one-level scan (found while grading the manager:
    // RPCS3_DIR=C:\dev missed C:\dev\rpcs3\bin\rpcs3.exe while the same path with a trailing slash worked).
    const QFileInfoList children = QDir(directory).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    int visited = 0;
    for (const QFileInfo& child : children) {
        if (++visited > 400)
            break;
        for (const QString& name : fileNames) {
            const QString direct = QDir(child.absoluteFilePath()).filePath(name);
            if (QFileInfo(direct).isFile())
                return QDir::cleanPath(QFileInfo(direct).absoluteFilePath());
            const QString bin = QDir(child.absoluteFilePath()).filePath(QStringLiteral("bin/") + name);
            if (QFileInfo(bin).isFile())
                return QDir::cleanPath(QFileInfo(bin).absoluteFilePath());
        }
    }
    return QString();
}

struct DetectionResult {
    QString executable;
    QString evidence;
};

DetectionResult detectOne(const DetectionSpec& spec, const QStringList& roots,
                          const QVector<RegistryEntry>& registry) {
    DetectionResult result;
    // 1. An explicit per-emulator override wins over every heuristic: <ID>_DIR and the generic
    //    XMB_EMULATOR_ROOTS list (semicolon separated). This is how a portable install in an unusual
    //    place is found without the user hand-editing the profile, and it keeps the evidence honest.
    {
        const QString overrideId = spec.id.toUpper() + QStringLiteral("_DIR");
        const QString overrideDir = qEnvironmentVariable(overrideId.toUtf8().constData()).trimmed();
        if (!overrideDir.isEmpty()) {
            const QString found = existingExecutable(QDir::fromNativeSeparators(overrideDir),
                                                     spec.fileNames, true);
            if (!found.isEmpty()) {
                result.executable = found;
                result.evidence = QStringLiteral("environment %1").arg(overrideId);
                return result;
            }
        }
        const QStringList genericRoots = qEnvironmentVariable("XMB_EMULATOR_ROOTS")
                                             .split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (const QString& generic : genericRoots) {
            const QString base = QDir::fromNativeSeparators(generic.trimmed());
            const QString found = existingExecutable(base, spec.fileNames, true);
            if (!found.isEmpty()) {
                result.executable = found;
                result.evidence = QStringLiteral("XMB_EMULATOR_ROOTS %1").arg(base);
                return result;
            }
        }
    }
    // 2. A portable folder kept under a workspace-ish parent two levels down from a drive root: a
    //    directory whose name contains the emulator id (rpcs3, retroarch, dolphin, ...) inside a
    //    parent whose name suggests a development/emulation/tools area (C:\dev\rpcs3 is the case this
    //    machine has). The parent filter keeps the scan cheap and the evidence explicit, and the
    //    manager UI applies the same rule so the CLI and the GUI agree.
    static const QStringList portableParents = {
        QStringLiteral("dev"), QStringLiteral("emulation"), QStringLiteral("emulators"),
        QStringLiteral("emulator"), QStringLiteral("games"), QStringLiteral("retro"),
        QStringLiteral("apps"), QStringLiteral("tools"), QStringLiteral("portable"),
        QStringLiteral("programs")
    };
    for (const QFileInfo& drive : QDir::drives()) {
        const QString root = drive.absoluteFilePath();
        const QFileInfoList parents = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& parent : parents) {
            const QString parentName = parent.fileName().toLower();
            const bool promising = std::any_of(portableParents.cbegin(), portableParents.cend(),
                                               [&parentName](const QString& needle) {
                                                   return parentName.contains(needle);
                                               });
            const QStringList candidates = promising ? QStringList{parent.absoluteFilePath()} : QStringList{};
            for (const QString& candidateParent : candidates) {
                const QFileInfoList children =
                    QDir(candidateParent).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QFileInfo& child : children) {
                    if (!child.fileName().toLower().contains(spec.id))
                        continue;
                    const QString found =
                        existingExecutable(child.absoluteFilePath(), spec.fileNames, true);
                    if (!found.isEmpty()) {
                        result.executable = found;
                        result.evidence = QStringLiteral("portable folder %1")
                                              .arg(QDir::cleanPath(child.absoluteFilePath()));
                        return result;
                    }
                }
            }
        }
    }
    for (const QString& root : roots) {
        for (const QString& folder : spec.folderNames) {
            const QString directory = QDir(root).filePath(folder);
            const QString found = existingExecutable(directory, spec.fileNames, false);
            if (!found.isEmpty()) {
                result.executable = found;
                result.evidence = QStringLiteral("known location %1").arg(directory);
                return result;
            }
        }
        const QString found = existingExecutable(root, spec.fileNames, true);
        if (!found.isEmpty()) {
            result.executable = found;
            result.evidence = QStringLiteral("known location %1").arg(root);
            return result;
        }
    }
    for (const RegistryEntry& entry : registry) {
        const QString display = entry.displayName.toLower();
        bool matches = false;
        for (const QString& needle : spec.registryNeedles) {
            if (display.contains(needle)) {
                matches = true;
                break;
            }
        }
        if (!matches)
            continue;
        const QString install = QDir::fromNativeSeparators(quotedPath(entry.installLocation));
        QString found = existingExecutable(install, spec.fileNames, true);
        if (!found.isEmpty()) {
            result.executable = found;
            result.evidence = QStringLiteral("registry '%1' InstallLocation")
                                  .arg(entry.displayName);
            return result;
        }
        const QString icon = QDir::fromNativeSeparators(quotedPath(entry.displayIcon));
        if (QFileInfo(icon).isFile()
            && QFileInfo(icon).suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) == 0) {
            result.executable = QDir::cleanPath(QFileInfo(icon).absoluteFilePath());
            result.evidence = QStringLiteral("registry '%1' DisplayIcon").arg(entry.displayName);
            return result;
        }
        const QString uninstall = QDir::fromNativeSeparators(quotedPath(entry.uninstallString));
        if (!uninstall.isEmpty()) {
            found = existingExecutable(QFileInfo(uninstall).absolutePath(), spec.fileNames, false);
            if (!found.isEmpty()) {
                result.executable = found;
                result.evidence = QStringLiteral("registry '%1' UninstallString")
                                      .arg(entry.displayName);
                return result;
            }
        }
    }
    return result;
}

} // namespace

QVector<EmulatorProfile> builtInProfiles() {
    QVector<EmulatorProfile> profiles;
    profiles.reserve(int(sizeof(kBuiltIns) / sizeof(kBuiltIns[0])));
    for (const BuiltInSpec& spec : kBuiltIns) {
        EmulatorProfile profile;
        profile.id = QLatin1String(spec.id);
        profile.name = QLatin1String(spec.name);
        profile.platforms = splitPlatforms(QLatin1String(spec.platforms));
        profile.argumentTemplate = QLatin1String(spec.argumentTemplate);
        profile.builtIn = true;
        profiles.append(profile);
    }
    return profiles;
}

QVector<EmulatorProfile> loadProfiles(Settings& settings, QString& error) {
    error.clear();
    QVector<EmulatorProfile> profiles = builtInProfiles();
    const QString path = emulatorsPath(settings);
    if (!QFileInfo::exists(path))
        return profiles;
    const auto failWith = [&error](const QString& message) {
        error = message;
        return QVector<EmulatorProfile>();
    };
    QJsonObject root;
    if (!readJsonFile(path, root, error))
        return QVector<EmulatorProfile>();
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.key() != QStringLiteral("version") && it.key() != QStringLiteral("emulators"))
            return failWith(QStringLiteral("emulators.json has unknown field '%1'")
                                .arg(it.key()));
    }
    const auto version = root.constFind(QStringLiteral("version"));
    if (version == root.constEnd() || !version->isDouble() || version->toDouble() != 1.0)
        return failWith(QStringLiteral("emulators.json version must be the number 1"));
    const auto list = root.constFind(QStringLiteral("emulators"));
    if (list == root.constEnd() || !list->isArray())
        return failWith(QStringLiteral("emulators.json field 'emulators' must be an array"));
    const QJsonArray array = list->toArray();
    if (array.size() > 128)
        return failWith(QStringLiteral("emulators.json exceeds the 128-profile limit"));
    QSet<QString> seen;
    for (qsizetype index = 0; index < array.size(); ++index) {
        if (!array.at(index).isObject())
            return failWith(QStringLiteral("emulators[%1] must be an object").arg(index));
        EmulatorProfile profile;
        if (!parseProfile(array.at(index).toObject(), index, profile, error))
            return QVector<EmulatorProfile>();
        if (seen.contains(profile.id.toLower()))
            return failWith(QStringLiteral("Duplicate emulator id '%1'").arg(profile.id));
        seen.insert(profile.id.toLower());
        const bool builtInId = std::any_of(profiles.cbegin(), profiles.cend(),
                                           [&profile](const EmulatorProfile& candidate) {
                                               return candidate.id.compare(profile.id,
                                                                           Qt::CaseInsensitive) == 0;
                                           });
        profile.builtIn = builtInId;
        if (builtInId) {
            // A user entry replaces the built-in row by id, field for field.
            for (qsizetype i = 0; i < profiles.size(); ++i) {
                if (profiles.at(i).id.compare(profile.id, Qt::CaseInsensitive) == 0) {
                    profiles[i] = profile;
                    break;
                }
            }
        } else {
            profiles.append(profile);
        }
    }
    return profiles;
}

bool saveProfiles(Settings& settings, const QVector<EmulatorProfile>& profiles, QString& error) {
    error.clear();
    for (const EmulatorProfile& profile : profiles) {
        if (!validateProfile(profile, error))
            return false;
    }
    QVector<EmulatorProfile> sorted = profiles;
    std::sort(sorted.begin(), sorted.end(), [](const EmulatorProfile& a, const EmulatorProfile& b) {
        return a.id.toLower() < b.id.toLower();
    });
    QJsonArray array;
    for (const EmulatorProfile& profile : sorted)
        array.append(serializeProfile(profile));
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("emulators"), array);
    return writeJsonFileAtomic(emulatorsPath(settings), root, error);
}

QVector<EmulatorProfile> detectInstalled(QStringList* notes) {
    const QStringList roots = knownRoots();
    const QVector<RegistryEntry> registry = readUninstallRegistry();
    QVector<EmulatorProfile> profiles = builtInProfiles();
    for (EmulatorProfile& profile : profiles) {
        const auto spec = std::find_if(detectionSpecs().cbegin(), detectionSpecs().cend(),
                                       [&profile](const DetectionSpec& candidate) {
                                           return candidate.id == profile.id;
                                       });
        if (spec == detectionSpecs().cend()) {
            if (notes)
                notes->append(QStringLiteral("%1: no detection rule").arg(profile.id));
            continue;
        }
        const DetectionResult result = detectOne(*spec, roots, registry);
        if (result.executable.isEmpty()) {
            if (notes)
                notes->append(QStringLiteral("%1: not detected").arg(profile.id));
            continue;
        }
        profile.executable = result.executable;
        profile.workingDirectory = QFileInfo(result.executable).absolutePath();
        if (notes) {
            notes->append(QStringLiteral("%1: %2 (evidence: %3)")
                              .arg(profile.id, profile.executable, result.evidence));
        }
    }
    return profiles;
}

QStringList expandArguments(const EmulatorProfile& profile, const QStringList& keyValues,
                            QString& error) {
    error.clear();
    QHash<QString, QString> values;
    if (!parseKeyValues(keyValues, values, error))
        return QStringList();
    const QString& text = profile.argumentTemplate;
    QStringList arguments;
    int index = 0;
    const auto substitute = [&](QString& output) -> bool {
        const int closing = text.indexOf(QLatin1Char('}'), index + 1);
        if (closing < 0) {
            fail(error, QStringLiteral("Argument template has an unterminated '{' at offset %1")
                            .arg(index));
            return false;
        }
        const QString name = text.mid(index + 1, closing - index - 1);
        if (name.isEmpty()) {
            fail(error, QStringLiteral("Argument template has an empty placeholder at offset %1")
                            .arg(index));
            return false;
        }
        if (!values.contains(name)) {
            fail(error, QStringLiteral("Argument template placeholder '{%1}' has no value; "
                                       "supply %1=<value>")
                            .arg(name));
            return false;
        }
        const QString value = values.value(name);
        if (value.contains(QChar::Null)) {
            fail(error, QStringLiteral("Argument value for '{%1}' contains NUL").arg(name));
            return false;
        }
        output.append(value);
        index = closing + 1;
        return true;
    };
    while (index < text.size()) {
        while (index < text.size() && text.at(index).isSpace())
            ++index;
        if (index >= text.size())
            break;
        QString argument;
        bool any = false;
        while (index < text.size() && !text.at(index).isSpace()) {
            const QChar ch = text.at(index);
            if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
                const QChar quote = ch;
                ++index;
                any = true;
                while (index < text.size() && text.at(index) != quote) {
                    if (text.at(index) == QLatin1Char('{')) {
                        if (!substitute(argument))
                            return QStringList();
                    } else if (text.at(index) == QLatin1Char('}')) {
                        fail(error, QStringLiteral("Argument template has an unmatched '}' "
                                                   "at offset %1").arg(index));
                        return QStringList();
                    } else {
                        argument.append(text.at(index));
                        ++index;
                    }
                }
                if (index >= text.size()) {
                    fail(error, QStringLiteral("Argument template has an unterminated quote"));
                    return QStringList();
                }
                ++index;
            } else if (ch == QLatin1Char('{')) {
                any = true;
                if (!substitute(argument))
                    return QStringList();
            } else if (ch == QLatin1Char('}')) {
                fail(error, QStringLiteral("Argument template has an unmatched '}' at offset %1")
                                .arg(index));
                return QStringList();
            } else {
                argument.append(ch);
                any = true;
                ++index;
            }
        }
        if (any)
            arguments.append(argument);
    }
    return arguments;
}

QString formatArguments(const QStringList& arguments) {
    QStringList rendered;
    rendered.reserve(arguments.size());
    for (const QString& argument : arguments) {
        if (argument.isEmpty() || argument.contains(QLatin1Char(' '))
            || argument.contains(QLatin1Char('\t')) || argument.contains(QLatin1Char('"')))
            rendered.append(QLatin1Char('"') + argument + QLatin1Char('"'));
        else
            rendered.append(argument);
    }
    return rendered.join(QLatin1Char(' '));
}

QStringList knownPlatforms() {
    static const QStringList scannerPlatforms = {
        QStringLiteral("PS3"), QStringLiteral("PSP"), QStringLiteral("PS2"),
        QStringLiteral("PS1"), QStringLiteral("GameCube/Wii"), QStringLiteral("Switch"),
        QStringLiteral("3DS"), QStringLiteral("NDS"), QStringLiteral("SNES"),
        QStringLiteral("NES"), QStringLiteral("N64"), QStringLiteral("Genesis"),
        QStringLiteral("GBA"), QStringLiteral("Game Boy"), QStringLiteral("Game Boy Color"),
        QStringLiteral("Saturn"), QStringLiteral("Dreamcast"), QStringLiteral("Arcade"),
        QStringLiteral("Disc image"),
    };
    QStringList platforms = scannerPlatforms;
    for (const PlatformMapping& mapping : defaultPlatformMappings()) {
        if (!platforms.contains(mapping.platform, Qt::CaseInsensitive))
            platforms.append(mapping.platform);
    }
    for (const EmulatorProfile& profile : builtInProfiles()) {
        for (const QString& platform : profile.platforms) {
            if (!platforms.contains(platform, Qt::CaseInsensitive))
                platforms.append(platform);
        }
    }
    std::sort(platforms.begin(), platforms.end(),
              [](const QString& a, const QString& b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });
    return platforms;
}

bool resolvePlatformTarget(Settings& settings, const QString& platform, QString& emulatorId,
                           QStringList& argumentOverride, bool& fromMap, QString& reason) {
    emulatorId.clear();
    argumentOverride.clear();
    fromMap = false;
    reason.clear();
    const QString wanted = platform.trimmed();
    if (wanted.isEmpty()) {
        reason = QStringLiteral("the platform is empty");
        return true;
    }
    QVector<PlatformMapping> mappings;
    QString error;
    if (!loadPlatformMap(settings, mappings, error)) {
        reason = error;
        return false;
    }
    if (const PlatformMapping* mapping = findPlatformMapping(mappings, wanted)) {
        emulatorId = mapping->emulatorId;
        argumentOverride = mapping->arguments;
        fromMap = true;
        return true;
    }
    const QVector<EmulatorProfile> profiles = loadProfiles(settings, error);
    if (!error.isEmpty()) {
        reason = error;
        return false;
    }
    for (const EmulatorProfile& profile : profiles) {
        if (profile.platforms.contains(wanted, Qt::CaseInsensitive)) {
            emulatorId = profile.id;
            return true;
        }
    }
    reason = QStringLiteral("platform '%1' is not in platform-map.json and no emulator profile "
                            "lists it").arg(wanted);
    return true;
}

QString resolveEmulatorForPlatform(Settings& settings, const QString& platform, QString* reason) {
    QString emulatorId;
    QStringList argumentOverride;
    bool fromMap = false;
    QString localReason;
    const bool loaded = resolvePlatformTarget(settings, platform, emulatorId, argumentOverride,
                                              fromMap, localReason);
    if (!loaded || emulatorId.isEmpty()) {
        if (reason)
            *reason = loaded ? localReason
                             : QStringLiteral("could not read the platform map: %1").arg(localReason);
        return QString();
    }
    if (reason)
        reason->clear();
    return emulatorId;
}

QStringList expandArgumentList(const QStringList& elements, const QStringList& keyValues,
                               QString& error) {
    error.clear();
    QHash<QString, QString> values;
    if (!parseKeyValues(keyValues, values, error))
        return QStringList();
    QStringList arguments;
    arguments.reserve(elements.size());
    for (const QString& element : elements) {
        QString expanded;
        expanded.reserve(element.size());
        int index = 0;
        while (index < element.size()) {
            const QChar ch = element.at(index);
            if (ch == QLatin1Char('{')) {
                const int closing = element.indexOf(QLatin1Char('}'), index + 1);
                if (closing < 0) {
                    fail(error, QStringLiteral("Argument override has an unterminated '{' at "
                                               "offset %1").arg(index));
                    return QStringList();
                }
                const QString name = element.mid(index + 1, closing - index - 1);
                if (name.isEmpty()) {
                    fail(error, QStringLiteral("Argument override has an empty placeholder at "
                                               "offset %1").arg(index));
                    return QStringList();
                }
                if (!values.contains(name)) {
                    fail(error, QStringLiteral("Argument override placeholder '{%1}' has no "
                                               "value; supply %1=<value>").arg(name));
                    return QStringList();
                }
                const QString value = values.value(name);
                if (value.contains(QChar::Null)) {
                    fail(error, QStringLiteral("Argument value for '{%1}' contains NUL").arg(name));
                    return QStringList();
                }
                expanded.append(value);
                index = closing + 1;
            } else if (ch == QLatin1Char('}')) {
                fail(error, QStringLiteral("Argument override has an unmatched '}' at offset %1")
                                .arg(index));
                return QStringList();
            } else {
                expanded.append(ch);
                ++index;
            }
        }
        arguments.append(expanded);
    }
    return arguments;
}

QJsonObject emulatorProfileToJson(const EmulatorProfile& profile) {
    return serializeProfile(profile);
}

bool emulatorProfileFromJson(const QJsonObject& object, qsizetype index,
                             EmulatorProfile& profile, QString& error) {
    return parseProfile(object, index, profile, error);
}

QString defaultCoreFor(const QString& emulatorId, const QString& platform) {
    if (emulatorId.compare(QStringLiteral("retroarch"), Qt::CaseInsensitive) != 0)
        return QString();
    static const QHash<QString, QString> cores = {
        {QStringLiteral("PS1"), QStringLiteral("mednafen_psx_libretro")},
        {QStringLiteral("PS2"), QStringLiteral("pcsx2_libretro")},
        {QStringLiteral("PSP"), QStringLiteral("ppsspp_libretro")},
        {QStringLiteral("GameCube/Wii"), QStringLiteral("dolphin_libretro")},
        {QStringLiteral("SNES"), QStringLiteral("snes9x_libretro")},
        {QStringLiteral("NES"), QStringLiteral("mesen_libretro")},
        {QStringLiteral("N64"), QStringLiteral("mupen64plus_next_libretro")},
        {QStringLiteral("Genesis"), QStringLiteral("genesis_plus_gx_libretro")},
        {QStringLiteral("GBA"), QStringLiteral("mgba_libretro")},
        {QStringLiteral("Game Boy"), QStringLiteral("gambatte_libretro")},
        {QStringLiteral("Game Boy Color"), QStringLiteral("gambatte_libretro")},
        {QStringLiteral("Saturn"), QStringLiteral("mednafen_saturn_libretro")},
        {QStringLiteral("Dreamcast"), QStringLiteral("flycast_libretro")},
        {QStringLiteral("Arcade"), QStringLiteral("mame_libretro")},
        {QStringLiteral("NDS"), QStringLiteral("melonds_libretro")},
    };
    return cores.value(platform);
}

const EmulatorProfile* findProfile(const QVector<EmulatorProfile>& profiles, const QString& id) {
    const auto found = std::find_if(profiles.cbegin(), profiles.cend(),
                                    [&id](const EmulatorProfile& profile) {
                                        return profile.id.compare(id, Qt::CaseInsensitive) == 0;
                                    });
    return found == profiles.cend() ? nullptr : &*found;
}

EmulatorProfile* findProfile(QVector<EmulatorProfile>& profiles, const QString& id) {
    const auto found = std::find_if(profiles.begin(), profiles.end(),
                                    [&id](const EmulatorProfile& profile) {
                                        return profile.id.compare(id, Qt::CaseInsensitive) == 0;
                                    });
    return found == profiles.end() ? nullptr : &*found;
}

} // namespace xmb::manager

