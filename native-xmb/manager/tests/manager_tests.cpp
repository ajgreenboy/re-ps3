// XMB Manager test runner.
//
// Builds a scratch data directory under C:\dev\out\manager-dev (or the path
// given as argv[1]), generates synthetic fixtures with Qt (PNG writer and the
// SFO writer), runs every CLI verb against the built xmb-manager.exe, and
// writes a full transcript. Exit code 0 only when every check passes.

#include "manager_core.h"
#include "manager_emulators.h"
#include "manager_package.h"
#include "manager_scanner.h"
#include "content_registry.h"

#include <QCoreApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLinearGradient>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

namespace {

int gFailures = 0;
QStringList gTranscript;

void record(const QString& line) {
    gTranscript.append(line);
    QTextStream stream(stdout);
    stream << line << Qt::endl;
}

void check(bool condition, const QString& what) {
    if (condition) {
        record(QStringLiteral("PASS  %1").arg(what));
    } else {
        ++gFailures;
        record(QStringLiteral("FAIL  %1").arg(what));
    }
}

void writeFile(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(bytes);
}

QByteArray readFileBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

QJsonObject readJsonObjectFile(const QString& path, bool& ok) {
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(readFileBytes(path), &error);
    ok = error.error == QJsonParseError::NoError && document.isObject();
    return document.object();
}

void writeJsonObjectFile(const QString& path, const QJsonObject& object) {
    writeFile(path, QJsonDocument(object).toJson(QJsonDocument::Indented));
}

bool copyTree(const QString& from, const QString& to) {
    const QFileInfo source(from);
    if (source.isDir()) {
        if (!QDir().mkpath(to))
            return false;
        const QFileInfoList entries = QDir(from).entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& entry : entries) {
            const QString target = QDir(to).filePath(entry.fileName());
            if (entry.isDir()) {
                if (!copyTree(entry.absoluteFilePath(), target))
                    return false;
            } else if (!QFile::copy(entry.absoluteFilePath(), target)) {
                return false;
            }
        }
        return true;
    }
    QDir().mkpath(QFileInfo(to).absolutePath());
    return source.isFile() && QFile::copy(from, to);
}

QImage makeArtwork(int width, int height, const QString& title) {
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(QColor(20, 28, 42));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QLinearGradient gradient(0, 0, width, height);
    gradient.setColorAt(0.0, QColor(46, 92, 148));
    gradient.setColorAt(1.0, QColor(12, 17, 26));
    painter.fillRect(image.rect(), gradient);
    painter.setPen(QColor(240, 246, 255));
    QFont font(QStringLiteral("Segoe UI"), std::max(12, height / 8), QFont::Bold);
    painter.setFont(font);
    painter.drawText(image.rect().adjusted(20, 20, -20, -20),
                     Qt::AlignLeft | Qt::AlignBottom | Qt::TextWordWrap, title);
    painter.end();
    return image;
}

bool savePng(const QImage& image, const QString& path) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    return image.save(path, "PNG");
}

struct CommandResult {
    int exitCode = -1;
    QString stdoutText;
    QString stderrText;
    QString command;
};

CommandResult runManager(const QString& managerExe, const QStringList& arguments) {
    CommandResult result;
    result.command = QStringLiteral("xmb-manager %1").arg(arguments.join(QLatin1Char(' ')));
    record(QString());
    record(QStringLiteral("> %1").arg(result.command));
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(managerExe, arguments);
    if (!process.waitForStarted(15000)) {
        result.stderrText = QStringLiteral("could not start xmb-manager: %1")
                                .arg(process.errorString());
        record(result.stderrText);
        return result;
    }
    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished(5000);
        result.stderrText = QStringLiteral("xmb-manager timed out");
        record(result.stderrText);
        return result;
    }
    result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    result.stdoutText = QString::fromUtf8(process.readAllStandardOutput());
    result.stderrText = QString::fromUtf8(process.readAllStandardError());
    if (!result.stdoutText.isEmpty())
        record(result.stdoutText.trimmed());
    if (!result.stderrText.isEmpty())
        record(QStringLiteral("[stderr] %1").arg(result.stderrText.trimmed()));
    record(QStringLiteral("[exit %1]").arg(result.exitCode));
    return result;
}

QJsonObject parseJson(const CommandResult& result, const QString& what, bool& ok) {
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(result.stdoutText.trimmed().toUtf8(),
                                                           &error);
    ok = error.error == QJsonParseError::NoError && document.isObject();
    if (!ok)
        record(QStringLiteral("FAIL  %1 is not one JSON object: %2")
                   .arg(what, error.errorString()));
    return document.object();
}

QByteArray buildSfo(const QHash<QString, QString>& values) {
    QStringList keys = values.keys();
    std::sort(keys.begin(), keys.end());
    QByteArray keyTable;
    QVector<quint16> keyOffsets;
    for (const QString& key : keys) {
        keyOffsets.append(static_cast<quint16>(keyTable.size()));
        keyTable.append(key.toUtf8());
        keyTable.append('\0');
    }
    while (keyTable.size() % 4 != 0)
        keyTable.append('\0');
    struct Entry {
        quint32 offset;
        quint32 length;
        quint32 maxLength;
    };
    QVector<Entry> entries;
    QByteArray dataTable;
    for (const QString& key : keys) {
        const QByteArray payload = values.value(key).toUtf8() + '\0';
        while (dataTable.size() % 4 != 0)
            dataTable.append('\0');
        Entry entry;
        entry.offset = static_cast<quint32>(dataTable.size());
        entry.length = static_cast<quint32>(payload.size());
        dataTable.append(payload);
        while (dataTable.size() % 4 != 0)
            dataTable.append('\0');
        entry.maxLength = static_cast<quint32>(dataTable.size() - entry.offset);
        entries.append(entry);
    }
    const quint32 keyTableStart = static_cast<quint32>(20 + 16 * keys.size());
    const quint32 dataStart = static_cast<quint32>(keyTableStart + keyTable.size());
    QByteArray out(static_cast<qsizetype>(dataStart) + dataTable.size(), '\0');
    const auto put32 = [&out](qsizetype offset, quint32 value) {
        out[offset] = static_cast<char>(value & 0xff);
        out[offset + 1] = static_cast<char>((value >> 8) & 0xff);
        out[offset + 2] = static_cast<char>((value >> 16) & 0xff);
        out[offset + 3] = static_cast<char>((value >> 24) & 0xff);
    };
    const auto put16 = [&out](qsizetype offset, quint16 value) {
        out[offset] = static_cast<char>(value & 0xff);
        out[offset + 1] = static_cast<char>((value >> 8) & 0xff);
    };
    put32(0, 0x46535000U);
    put32(4, 0x00000101U);
    put32(8, keyTableStart);
    put32(12, dataStart);
    put32(16, static_cast<quint32>(keys.size()));
    for (qsizetype index = 0; index < keys.size(); ++index) {
        const qsizetype offset = 20 + index * 16;
        put16(offset, keyOffsets.at(index));
        put16(offset + 2, 0x0204);
        put32(offset + 4, entries.at(index).length);
        put32(offset + 8, entries.at(index).maxLength);
        put32(offset + 12, entries.at(index).offset);
    }
    out.replace(static_cast<qsizetype>(keyTableStart), keyTable.size(), keyTable);
    out.replace(static_cast<qsizetype>(dataStart), dataTable.size(), dataTable);
    return out;
}

QByteArray makeSyntheticPkg(const QByteArray& payload, quint16 revision, const QString& contentId) {
    const qint64 headerSize = 0x280;
    QByteArray file(static_cast<qsizetype>(headerSize) + payload.size(), '\0');
    const auto put16 = [&file](qsizetype offset, quint16 value) {
        file[offset] = static_cast<char>((value >> 8) & 0xff);
        file[offset + 1] = static_cast<char>(value & 0xff);
    };
    const auto put32 = [&file](qsizetype offset, quint32 value) {
        file[offset] = static_cast<char>((value >> 24) & 0xff);
        file[offset + 1] = static_cast<char>((value >> 16) & 0xff);
        file[offset + 2] = static_cast<char>((value >> 8) & 0xff);
        file[offset + 3] = static_cast<char>(value & 0xff);
    };
    const auto put64 = [&put32, &file](qsizetype offset, quint64 value) {
        put32(offset, static_cast<quint32>(value >> 32));
        put32(offset + 4, static_cast<quint32>(value & 0xffffffffULL));
    };
    put32(0, 0x7f504b47U);
    put16(0x04, revision);
    put16(0x06, 1);
    put32(0x08, 0xc0);
    put32(0x0c, 1);
    put32(0x10, 0x40);
    put32(0x14, 1);
    put64(0x18, static_cast<quint64>(file.size()));
    put64(0x20, static_cast<quint64>(headerSize));
    put64(0x28, static_cast<quint64>(payload.size()));
    const QByteArray id = contentId.toLatin1().left(0x24);
    file.replace(0x30, id.size(), id);
    for (int index = 0; index < 16; ++index)
        file[0x60 + index] = static_cast<char>(index + 1);
    for (int index = 0; index < 16; ++index)
        file[0x70 + index] = static_cast<char>(0x80 + index);
    for (int index = 0; index < 0x40; ++index)
        file[0x80 + index] = static_cast<char>(0xa0 + index);
    file.replace(headerSize, payload.size(), payload);
    return file;
}

bool removeScratch(const QString& root) {
    const QString cleaned = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const QFileInfo info(cleaned);
    bool allowed = info.fileName().endsWith(QStringLiteral("manager-dev"), Qt::CaseInsensitive);
    if (!allowed && info.fileName().compare(QStringLiteral("data"), Qt::CaseInsensitive) == 0) {
        // Accept <out>/manager-org/data style scratch roots.
        allowed = QFileInfo(info.absolutePath()).fileName().startsWith(QStringLiteral("manager-"),
                                                                       Qt::CaseInsensitive);
    }
    if (!allowed)
        return false;
    if (QFileInfo::exists(cleaned))
        return QDir(cleaned).removeRecursively();
    return true;
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_AUTO_SCREEN_SCALE_FACTOR", "0");
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("xmb-manager-tests"));

    const QString root = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                  : QStringLiteral("C:/dev/out/manager-dev");
    const QString managerExe =
        argc > 2 ? QString::fromLocal8Bit(argv[2])
                 : QDir(QCoreApplication::applicationDirPath())
                       .filePath(QStringLiteral("xmb-manager.exe"));
    record(QStringLiteral("xmb-manager tests"));
    record(QStringLiteral("scratch: %1").arg(QDir::cleanPath(root)));
    record(QStringLiteral("manager: %1").arg(managerExe));
    if (!QFileInfo(managerExe).isFile()) {
        record(QStringLiteral("FATAL: manager executable not found"));
        return 2;
    }
    if (!removeScratch(root)) {
        record(QStringLiteral("FATAL: refusing to remove %1 (must end in manager-dev)").arg(root));
        return 2;
    }

    const QString roms = QDir(root).filePath(QStringLiteral("roms"));
    const QString data = QDir(root).filePath(QStringLiteral("data"));
    const QString known = QDir(root).filePath(QStringLiteral("known"));
    const QString pkgFolder = QDir(roms).filePath(
        QStringLiteral("PS3 Folders/Test PS3 Folder"));
    const QString sfoPath = QDir(pkgFolder).filePath(QStringLiteral("PS3_GAME/PARAM.SFO"));
    const QString iconPath = QDir(pkgFolder).filePath(QStringLiteral("PS3_GAME/ICON0.PNG"));
    const QString readablePkgPath = QDir(root).filePath(QStringLiteral("fixtures/readable.pkg"));
    const QString retailPkgPath = QDir(root).filePath(QStringLiteral("fixtures/retail.pkg"));

    QDir().mkpath(roms);
    QDir().mkpath(data);
    QDir().mkpath(QDir(pkgFolder).filePath(QStringLiteral("PS3_GAME/USRDIR")));
    QDir().mkpath(QDir(root).filePath(QStringLiteral("fixtures")));

    // Synthetic ROMs: empty files with real extensions.
    const QStringList romFiles = {
        QStringLiteral("PS3/Test Game (USA) [Rev 1].iso"),
        QStringLiteral("PSP/Test PSP Game.cso"),
        QStringLiteral("SNES/Chrono Test (Europe).sfc"),
        QStringLiteral("NDS/Test NDS Game.nds"),
        QStringLiteral("Switch/Test Switch Game.nsp"),
        QStringLiteral("GameCube/Test Cube.rvz"),
        QStringLiteral("Arcade/Test Arcade.zip"),
        QStringLiteral("Disc/Unknown Disc.iso"),
        QStringLiteral("PS2/Test Game With Spaces.iso"),
    };
    for (const QString& relative : romFiles)
        writeFile(QDir(roms).filePath(relative), QByteArray());
    // Non-empty ROMs so content hashes and relink can be exercised.
    writeFile(QDir(roms).filePath(QStringLiteral("SNES/Chrono Test (Europe).sfc")),
              QByteArray("SNES-CHRONO-TEST-CONTENT-001"));
    writeFile(QDir(roms).filePath(QStringLiteral("NDS/Test NDS Game.nds")),
              QByteArray("NDS-TEST-CONTENT-001"));
    const QStringList extraRomFiles = {
        QStringLiteral("SNES/Second Test.sfc"),
        QStringLiteral("PS2/Mapped Game.iso"),
        QStringLiteral("3DS/Test 3DS Game.3ds"),
        QStringLiteral("Probe/Launch Probe.sfc"),
        QStringLiteral("Probe/Missing Probe.sfc"),
        QStringLiteral("Probe/Slow Probe.sfc"),
        QStringLiteral("Probe/Bulk Remove.sfc"),
    };
    for (const QString& relative : extraRomFiles) {
        writeFile(QDir(roms).filePath(relative),
                  QStringLiteral("ROM-CONTENT-%1").arg(relative).toUtf8());
    }
    writeFile(QDir(pkgFolder).filePath(QStringLiteral("PS3_GAME/USRDIR/EBOOT.BIN")),
              QByteArray());

    QImage art = makeArtwork(640, 360, QStringLiteral("Chrono Test"));
    check(savePng(art, QDir(roms).filePath(QStringLiteral("SNES/cover.png"))),
          QStringLiteral("Qt wrote a sidecar PNG"));
    check(savePng(makeArtwork(512, 288, QStringLiteral("Test PS3 Folder")), iconPath),
          QStringLiteral("Qt wrote the PS3 folder ICON0.PNG"));
    QHash<QString, QString> sfoValues;
    sfoValues.insert(QStringLiteral("TITLE"), QStringLiteral("Test PS3 Folder"));
    sfoValues.insert(QStringLiteral("CATEGORY"), QStringLiteral("HG"));
    sfoValues.insert(QStringLiteral("APP_VER"), QStringLiteral("01.00"));
    sfoValues.insert(QStringLiteral("ICON0"), QStringLiteral("ICON0.PNG"));
    QByteArray sfoBytes;
    QString sfoError;
    check(xmb::manager::writeParamSfo(sfoValues, sfoBytes, sfoError),
          QStringLiteral("PARAM.SFO writer accepted the fixture"));
    writeFile(sfoPath, sfoBytes);

    // SFO: compare the writer against an independent byte layout, then parse it.
    QHash<QString, QString> expectedValues;
    expectedValues.insert(QStringLiteral("TITLE"), QStringLiteral("Test Game"));
    expectedValues.insert(QStringLiteral("CATEGORY"), QStringLiteral("HG"));
    QByteArray produced;
    check(xmb::manager::writeParamSfo(expectedValues, produced, sfoError),
          QStringLiteral("writeParamSfo produced bytes"));
    check(produced == buildSfo(expectedValues),
          QStringLiteral("writeParamSfo matches the byte-exact SFO v1.1 layout"));
    QHash<QString, QString> parsedSfo;
    check(xmb::manager::readParamSfo(produced, parsedSfo, sfoError)
              && parsedSfo == expectedValues,
          QStringLiteral("readParamSfo round trips the writer"));
    QByteArray corrupt = produced;
    corrupt[1] = 'X';
    check(!xmb::manager::readParamSfo(corrupt, parsedSfo, sfoError),
          QStringLiteral("readParamSfo rejects a bad magic"));

    // Argument splitting: values with spaces stay one argv element.
    xmb::manager::EmulatorProfile argumentProfile;
    argumentProfile.id = QStringLiteral("retroarch");
    argumentProfile.argumentTemplate = QStringLiteral("--rom={rom} --title \"{title}\" {core}");
    QString argumentError;
    const QString spacedRom = QStringLiteral("C:/Games/Test Game/disc file.iso");
    const QStringList argumentList =
        xmb::manager::expandArguments(argumentProfile,
                                      {QStringLiteral("rom=%1").arg(spacedRom),
                                       QStringLiteral("title=Test Game"),
                                       QStringLiteral("core=mednafen_psx_libretro")},
                                      argumentError);
    check(argumentError.isEmpty() && argumentList.size() == 4
              && argumentList.at(0) == QStringLiteral("--rom=") + spacedRom
              && argumentList.at(1) == QStringLiteral("--title")
              && argumentList.at(2) == QStringLiteral("Test Game")
              && argumentList.at(3) == QStringLiteral("mednafen_psx_libretro"),
          QStringLiteral("expandArguments keeps a spaced {rom} value in one argv element"));
    xmb::manager::EmulatorProfile bareProfile;
    bareProfile.argumentTemplate = QStringLiteral("{rom}");
    const QStringList bareList = xmb::manager::expandArguments(
        bareProfile, {QStringLiteral("rom=%1").arg(spacedRom)}, argumentError);
    check(argumentError.isEmpty() && bareList.size() == 1 && bareList.first() == spacedRom,
          QStringLiteral("a bare {rom} token is one argv element even with spaces"));
    xmb::manager::EmulatorProfile quoteProfile;
    quoteProfile.argumentTemplate = QStringLiteral("\"{rom}\"");
    const QStringList quoteList = xmb::manager::expandArguments(
        quoteProfile, {QStringLiteral("rom=%1").arg(spacedRom)}, argumentError);
    check(argumentError.isEmpty() && quoteList.size() == 1 && quoteList.first() == spacedRom,
          QStringLiteral("quotes group a placeholder into one argv element"));
    xmb::manager::EmulatorProfile badProfile;
    badProfile.argumentTemplate = QStringLiteral("\"{rom}");
    check(xmb::manager::expandArguments(badProfile, {QStringLiteral("rom=x")}, argumentError)
                  .isEmpty()
              && !argumentError.isEmpty(),
          QStringLiteral("expandArguments rejects an unterminated quote"));
    xmb::manager::EmulatorProfile unknownProfile;
    unknownProfile.argumentTemplate = QStringLiteral("{nope}");
    check(xmb::manager::expandArguments(unknownProfile, {QStringLiteral("rom=x")}, argumentError)
                  .isEmpty()
              && !argumentError.isEmpty(),
          QStringLiteral("expandArguments rejects an unknown placeholder"));

    // Platform and title rules.
    check(xmb::manager::platformForPath(QDir(roms).filePath(
              QStringLiteral("SNES/Chrono Test (Europe).sfc"))) == QStringLiteral("SNES"),
          QStringLiteral("platformForPath maps .sfc to SNES"));
    check(xmb::manager::platformForPath(QDir(roms).filePath(
              QStringLiteral("PSP/Test PSP Game.cso"))) == QStringLiteral("PSP"),
          QStringLiteral("platformForPath maps .cso to PSP"));
    check(xmb::manager::platformForPath(QDir(roms).filePath(
              QStringLiteral("Disc/Unknown Disc.iso"))) == QStringLiteral("Disc image"),
          QStringLiteral("platformForPath falls back to Disc image"));
    check(xmb::manager::platformForPath(pkgFolder) == QStringLiteral("PS3"),
          QStringLiteral("platformForPath detects a PS3 package folder"));
    check(xmb::manager::titleFromFileName(QStringLiteral("Sonic the Hedgehog 2 (USA) [Rev 1].md"))
              == QStringLiteral("Sonic the Hedgehog 2"),
          QStringLiteral("title cleaning strips region and revision tags"));

    // Deterministic fake installs so detection is exercised without touching the host.
    const QString programs = QDir(known).filePath(QStringLiteral("Programs"));
    QDir().mkpath(QDir(programs).filePath(QStringLiteral("RPCS3")));
    QDir().mkpath(QDir(programs).filePath(QStringLiteral("RetroArch")));
    QDir().mkpath(QDir(programs).filePath(QStringLiteral("PCSX2")));
    QDir().mkpath(QDir(programs).filePath(QStringLiteral("DuckStation")));
    writeFile(QDir(programs).filePath(QStringLiteral("RPCS3/rpcs3.exe")), QByteArray());
    writeFile(QDir(programs).filePath(QStringLiteral("RetroArch/retroarch.exe")), QByteArray());
    writeFile(QDir(programs).filePath(QStringLiteral("PCSX2/pcsx2-qt.exe")), QByteArray());
    writeFile(QDir(programs).filePath(
                  QStringLiteral("DuckStation/duckstation-qt-x64-ReleaseLTCG.exe")),
              QByteArray());
    QDir().mkpath(QDir(known).filePath(QStringLiteral("PF")));
    QDir().mkpath(QDir(known).filePath(QStringLiteral("PF86")));
    QDir().mkpath(QDir(known).filePath(QStringLiteral("AppData")));
    QDir().mkpath(QDir(known).filePath(QStringLiteral("User")));
    qputenv("LOCALAPPDATA", known.toUtf8());
    qputenv("APPDATA", QDir(known).filePath(QStringLiteral("AppData")).toUtf8());
    qputenv("ProgramFiles", QDir(known).filePath(QStringLiteral("PF")).toUtf8());
    qputenv("ProgramFiles(x86)", QDir(known).filePath(QStringLiteral("PF86")).toUtf8());
    qputenv("ProgramW6432", QDir(known).filePath(QStringLiteral("PF")).toUtf8());
    qputenv("USERPROFILE", QDir(known).filePath(QStringLiteral("User")).toUtf8());

    QStringList detectionNotes;
    const QVector<xmb::manager::EmulatorProfile> detected =
        xmb::manager::detectInstalled(&detectionNotes);
    bool detectedRpcs3 = false;
    bool allExist = true;
    for (const xmb::manager::EmulatorProfile& profile : detected) {
        if (profile.executable.isEmpty())
            continue;
        if (!QFileInfo(profile.executable).isFile())
            allExist = false;
        if (profile.id == QStringLiteral("rpcs3"))
            detectedRpcs3 = true;
    }
    check(detectedRpcs3, QStringLiteral("detectInstalled found the scratch RPCS3 install"));
    check(allExist, QStringLiteral("every detected executable exists (no invented installs)"));
    check(!detectionNotes.isEmpty(), QStringLiteral("detection recorded evidence notes"));

    // Readable and retail package fixtures.
    QHash<QString, QString> pkgSfo;
    pkgSfo.insert(QStringLiteral("TITLE"), QStringLiteral("Readable Test Package"));
    pkgSfo.insert(QStringLiteral("CATEGORY"), QStringLiteral("HG"));
    pkgSfo.insert(QStringLiteral("APP_VER"), QStringLiteral("01.00"));
    QByteArray pkgSfoBytes;
    check(xmb::manager::writeParamSfo(pkgSfo, pkgSfoBytes, sfoError),
          QStringLiteral("wrote the readable package PARAM.SFO"));
    QByteArray pkgPng;
    {
        QImage image = makeArtwork(512, 288, QStringLiteral("Readable"));
        QBuffer buffer(&pkgPng);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
    }
    QByteArray readablePayload = pkgSfoBytes + pkgPng;
    writeFile(readablePkgPath,
              makeSyntheticPkg(readablePayload, 0x0000,
                               QStringLiteral("TEST00000-TEST00000_00-TESTPACKAGE0000")));
    QByteArray randomPayload(4096, '\0');
    for (int index = 0; index < randomPayload.size(); ++index)
        randomPayload[index] = static_cast<char>((index * 37 + 11) & 0xff);
    writeFile(retailPkgPath,
              makeSyntheticPkg(randomPayload, 0x8000,
                               QStringLiteral("UP0001-NPUB00000_00-RETAILTEST0000000")));

    const QStringList dataArgs = {QStringLiteral("--data-dir"), data};
    const QString snesRom = QDir(roms).filePath(QStringLiteral("SNES/Chrono Test (Europe).sfc"));
    const QString ps2Rom =
        QDir(roms).filePath(QStringLiteral("PS2/Test Game With Spaces.iso"));
    const QString ps3Folder = pkgFolder;
    const QString snesId = xmb::manager::packageIdFor(snesRom, QStringLiteral("SNES"));
    const QString ps2Id = xmb::manager::packageIdFor(ps2Rom, QStringLiteral("PS2"));
    const QString secondSnesRom = QDir(roms).filePath(QStringLiteral("SNES/Second Test.sfc"));
    const QString mappedPs2Rom = QDir(roms).filePath(QStringLiteral("PS2/Mapped Game.iso"));
    const QString threeDsRom = QDir(roms).filePath(QStringLiteral("3DS/Test 3DS Game.3ds"));
    const QString launchProbeRom = QDir(roms).filePath(QStringLiteral("Probe/Launch Probe.sfc"));
    const QString missingProbeRom = QDir(roms).filePath(QStringLiteral("Probe/Missing Probe.sfc"));
    const QString slowProbeRom = QDir(roms).filePath(QStringLiteral("Probe/Slow Probe.sfc"));
    const QString bulkRemoveRom = QDir(roms).filePath(QStringLiteral("Probe/Bulk Remove.sfc"));
    const QString secondSnesId =
        xmb::manager::packageIdFor(secondSnesRom, QStringLiteral("SNES"));
    const QString mappedPs2Id =
        xmb::manager::packageIdFor(mappedPs2Rom, QStringLiteral("PS2"));
    const QString threeDsId = xmb::manager::packageIdFor(threeDsRom, QStringLiteral("3DS"));
    const QString launchProbeId =
        xmb::manager::packageIdFor(launchProbeRom, QStringLiteral("SNES"));
    const QString missingProbeId =
        xmb::manager::packageIdFor(missingProbeRom, QStringLiteral("SNES"));
    const QString slowProbeId =
        xmb::manager::packageIdFor(slowProbeRom, QStringLiteral("SNES"));
    const QString bulkRemoveId =
        xmb::manager::packageIdFor(bulkRemoveRom, QStringLiteral("SNES"));

    CommandResult result = runManager(managerExe, dataArgs
        + QStringList{QStringLiteral("--detect-emulators"), QStringLiteral("--json")});
    bool ok = false;
    QJsonObject json = parseJson(result, QStringLiteral("--detect-emulators"), ok);
    check(result.exitCode == 0 && ok && json.value(QStringLiteral("detected")).isArray()
              && !json.value(QStringLiteral("detected")).toArray().isEmpty(),
          QStringLiteral("--detect-emulators succeeds and reports detections"));

    result = runManager(managerExe, dataArgs
        + QStringList{QStringLiteral("--list-emulators"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--list-emulators"), ok);
    bool rpcs3Configured = false;
    if (ok) {
        for (const QJsonValue& value : json.value(QStringLiteral("emulators")).toArray()) {
            const QJsonObject entry = value.toObject();
            if (entry.value(QStringLiteral("id")).toString() == QStringLiteral("rpcs3")
                && !entry.value(QStringLiteral("executable")).toString().isEmpty())
                rpcs3Configured = true;
        }
    }
    check(result.exitCode == 0 && rpcs3Configured,
          QStringLiteral("--list-emulators shows the detected RPCS3 path"));

    // Harmless launch fixtures: cmd.exe /c exit 0 never opens a game window.
    // cmd-missing points at a scratch copy that is deleted after its package is
    // imported, so --launch-check can prove the spawn-error path.
    const QString windowsDirectory = QStringLiteral("C:/Windows/System32");
    const QString cmdExe = QDir(windowsDirectory).filePath(QStringLiteral("cmd.exe"));
    const QString missingExe = QDir(root).filePath(QStringLiteral("fixtures/cmd-missing.exe"));
    check(QFileInfo(cmdExe).isFile(),
          QStringLiteral("the harmless cmd.exe launch fixture exists"));
    QDir().mkpath(QFileInfo(missingExe).absolutePath());
    check(QFile::copy(cmdExe, missingExe) || QFileInfo(missingExe).isFile(),
          QStringLiteral("copied cmd.exe for the missing-executable fixture"));
    xmb::manager::Settings testSettings;
    testSettings.dataDirectory = data;
    bool emulatorsOk = false;
    QJsonObject emulatorsRoot =
        readJsonObjectFile(xmb::manager::emulatorsPath(testSettings), emulatorsOk);
    QJsonArray emulatorArray = emulatorsRoot.value(QStringLiteral("emulators")).toArray();
    const auto appendProfile = [&emulatorArray](const QString& id, const QString& name,
                                                const QString& executable,
                                                const QString& workingDirectory,
                                                const QString& argumentTemplate) {
        emulatorArray.append(QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("executable"), executable},
            {QStringLiteral("workingDirectory"), workingDirectory},
            {QStringLiteral("platforms"), QJsonArray()},
            {QStringLiteral("argumentTemplate"), argumentTemplate},
            {QStringLiteral("env"), QJsonObject()},
            {QStringLiteral("builtIn"), false}});
    };
    appendProfile(QStringLiteral("cmd-probe"), QStringLiteral("cmd probe"), cmdExe,
                  windowsDirectory, QStringLiteral("/c exit 0"));
    appendProfile(QStringLiteral("cmd-missing"), QStringLiteral("cmd missing"),
                  QDir::cleanPath(missingExe), QDir(root).filePath(QStringLiteral("fixtures")),
                  QStringLiteral("/c exit 0"));
    appendProfile(QStringLiteral("cmd-slow"), QStringLiteral("cmd slow"), cmdExe,
                  windowsDirectory,
                  QStringLiteral("/c for /l %i in (1,1,1000000000) do @rem"));
    emulatorsRoot.insert(QStringLiteral("emulators"), emulatorArray);
    writeJsonObjectFile(xmb::manager::emulatorsPath(testSettings), emulatorsRoot);
    check(emulatorsOk && emulatorArray.size() >= 16,
          QStringLiteral("the data directory has the temporary cmd.exe profiles"));

    result = runManager(managerExe, dataArgs
        + QStringList{QStringLiteral("--scan"), roms, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--scan"), ok);
    QStringList foundTitles;
    QStringList foundPlatforms;
    QString snesArt;
    QString ps3FolderArt;
    if (ok) {
        for (const QJsonValue& value : json.value(QStringLiteral("games")).toArray()) {
            const QJsonObject game = value.toObject();
            foundTitles.append(game.value(QStringLiteral("title")).toString());
            foundPlatforms.append(game.value(QStringLiteral("platform")).toString());
            if (game.value(QStringLiteral("platform")).toString() == QStringLiteral("SNES")
                && snesArt.isEmpty())
                snesArt = game.value(QStringLiteral("artPath")).toString();
            if (game.value(QStringLiteral("platform")).toString() == QStringLiteral("PS3")
                && game.value(QStringLiteral("romKind")).toString() == QStringLiteral("folder"))
                ps3FolderArt = game.value(QStringLiteral("artPath")).toString();
        }
    }
    check(result.exitCode == 0 && foundTitles.contains(QStringLiteral("Chrono Test"))
              && foundPlatforms.contains(QStringLiteral("SNES")),
          QStringLiteral("--scan found the SNES game with its cleaned title"));
    check(foundTitles.contains(QStringLiteral("Test PSP Game"))
              && foundPlatforms.contains(QStringLiteral("PSP")),
          QStringLiteral("--scan found the PSP .cso"));
    check(foundTitles.contains(QStringLiteral("Test NDS Game"))
              && foundPlatforms.contains(QStringLiteral("NDS")),
          QStringLiteral("--scan found the NDS ROM"));
    check(foundTitles.contains(QStringLiteral("Test Switch Game"))
              && foundPlatforms.contains(QStringLiteral("Switch")),
          QStringLiteral("--scan found the Switch ROM"));
    check(foundTitles.contains(QStringLiteral("Test Cube"))
              && foundPlatforms.contains(QStringLiteral("GameCube/Wii")),
          QStringLiteral("--scan found the GameCube/Wii image"));
    check(foundTitles.contains(QStringLiteral("Test Arcade"))
              && foundPlatforms.contains(QStringLiteral("Arcade")),
          QStringLiteral("--scan found the arcade archive"));
    check(foundPlatforms.contains(QStringLiteral("Disc image")),
          QStringLiteral("--scan classifies an unknown .iso as Disc image"));
    check(foundTitles.contains(QStringLiteral("Test Game"))
              && foundPlatforms.contains(QStringLiteral("PS3")),
          QStringLiteral("--scan found the PS3 .iso via its folder hint"));
    check(foundTitles.contains(QStringLiteral("Test PS3 Folder"))
              && foundPlatforms.contains(QStringLiteral("PS3")),
          QStringLiteral("--scan found the PS3 package folder using its PARAM.SFO title"));
    check(!snesArt.isEmpty() && QFileInfo(snesArt).isFile(),
          QStringLiteral("--scan discovered the SNES sidecar art"));
    check(!ps3FolderArt.isEmpty() && QFileInfo(ps3FolderArt).isFile(),
          QStringLiteral("--scan discovered the PS3 folder ICON0"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--import"), snesRom, QStringLiteral("--emulator"),
        QStringLiteral("retroarch"), QStringLiteral("--collection"),
        QStringLiteral("SNES Classics"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--import (SNES)"), ok);
    check(result.exitCode == 0 && ok
              && json.value(QStringLiteral("imported")).toArray().size() == 1,
          QStringLiteral("--import created the SNES package"));
    const QString snesPackage = xmb::manager::packageDirectoryFor(
        [&]() {
            xmb::manager::Settings settings;
            QDir().mkpath(data);
            settings.dataDirectory = data;
            return settings;
        }(),
        snesId);
    check(QFileInfo(QDir(snesPackage).filePath(QStringLiteral("ICON0.PNG"))).isFile()
              && QFileInfo(QDir(snesPackage).filePath(QStringLiteral("PARAM.SFO"))).isFile()
              && QFileInfo(QDir(snesPackage).filePath(QStringLiteral("pkg.json"))).isFile(),
          QStringLiteral("the SNES package has ICON0.PNG, PARAM.SFO and pkg.json"));
    const QImage packageIcon(QDir(snesPackage).filePath(QStringLiteral("ICON0.PNG")));
    check(!packageIcon.isNull() && packageIcon.width() <= 512 && packageIcon.height() <= 288,
          QStringLiteral("ICON0.PNG is a PNG no larger than 512x288"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--import"), snesRom, QStringLiteral("--emulator"),
        QStringLiteral("retroarch"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--import duplicate"), ok);
    bool duplicateRefused = false;
    if (ok) {
        const QJsonArray failed = json.value(QStringLiteral("failed")).toArray();
        duplicateRefused = failed.size() == 1
            && failed.first().toObject().value(QStringLiteral("error")).toString()
                   .contains(QStringLiteral("already imported"));
    }
    check(result.exitCode == 1 && duplicateRefused,
          QStringLiteral("a duplicate import is refused with a clear error"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--import"), snesRom, QStringLiteral("--emulator"),
        QStringLiteral("retroarch"), QStringLiteral("--collection"),
        QStringLiteral("SNES Classics"), QStringLiteral("--replace"),
        QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--import --replace"), ok);
    check(result.exitCode == 0 && ok
              && json.value(QStringLiteral("imported")).toArray().size() == 1,
          QStringLiteral("--replace updates the existing package"));
    {
        xmb::manager::Settings collectionSettings;
        collectionSettings.dataDirectory = data;
        QHash<QString, QStringList> collections;
        QString collectionError;
        check(xmb::manager::readCollections(collectionSettings, collections, collectionError)
                  && collections.value(QStringLiteral("SNES Classics")).contains(snesId),
              QStringLiteral("--collection persisted the SNES package membership"));
    }

    // Tags: trim + dedupe + list + filter + untag + validation.
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--tag"), snesId, QStringLiteral("Classic"),
        QStringLiteral("  Co-op  "), QStringLiteral("Classic"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--tag"), ok);
    QStringList snesTags;
    if (ok) {
        for (const QJsonValue& value : json.value(QStringLiteral("tags")).toArray())
            snesTags.append(value.toString());
    }
    check(result.exitCode == 0 && snesTags == QStringList({QStringLiteral("Classic"),
                                                           QStringLiteral("Co-op")}),
          QStringLiteral("--tag trims, de-duplicates and normalises tags"));

    result = runManager(managerExe, dataArgs
        + QStringList{QStringLiteral("--tags"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--tags"), ok);
    bool sawClassic = false;
    bool sawCoop = false;
    if (ok) {
        for (const QJsonValue& value : json.value(QStringLiteral("tags")).toArray()) {
            const QJsonObject entry = value.toObject();
            if (entry.value(QStringLiteral("tag")).toString() == QStringLiteral("Classic")
                && entry.value(QStringLiteral("count")).toInt() == 1)
                sawClassic = true;
            if (entry.value(QStringLiteral("tag")).toString() == QStringLiteral("Co-op")
                && entry.value(QStringLiteral("count")).toInt() == 1)
                sawCoop = true;
        }
    }
    check(result.exitCode == 0 && sawClassic && sawCoop,
          QStringLiteral("--tags lists every tag with its package count"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--list"), QStringLiteral("--tag"), QStringLiteral("co-op"),
        QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--list --tag"), ok);
    check(result.exitCode == 0 && ok
              && json.value(QStringLiteral("packageCount")).toInt() == 1
              && json.value(QStringLiteral("packages")).toArray().first().toObject()
                     .value(QStringLiteral("id")).toString() == snesId,
          QStringLiteral("--list --tag filters packages by tag"));
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--list"), QStringLiteral("--tag"), QStringLiteral("absent"),
        QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--list --tag absent"), ok);
    check(result.exitCode == 0 && json.value(QStringLiteral("packageCount")).toInt() == 0,
          QStringLiteral("--list --tag reports zero matches"));

    const QString tooLongTag(40, QLatin1Char('x'));
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--tag"), snesId, tooLongTag, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--tag too long"), ok);
    check(result.exitCode == 1 && ok
              && json.value(QStringLiteral("error")).toString().contains(QStringLiteral("32")),
          QStringLiteral("--tag rejects a tag longer than the 32-character limit"));

    // Tags survive --replace.
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--import"), snesRom, QStringLiteral("--emulator"),
        QStringLiteral("retroarch"), QStringLiteral("--collection"),
        QStringLiteral("SNES Classics"), QStringLiteral("--replace"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--import --replace keeps tags"), ok);
    bool tagsPreserved = false;
    QString snesCollectionAfterReplace;
    {
        bool packageOk = false;
        const QJsonObject package = readJsonObjectFile(
            QDir(xmb::manager::packageDirectoryFor(testSettings, snesId))
                .filePath(QStringLiteral("pkg.json")),
            packageOk);
        QStringList tags;
        for (const QJsonValue& value : package.value(QStringLiteral("tags")).toArray())
            tags.append(value.toString());
        snesCollectionAfterReplace = package.value(QStringLiteral("collection")).toString();
        tagsPreserved = packageOk
            && tags == QStringList({QStringLiteral("Classic"), QStringLiteral("Co-op")});
    }
    check(result.exitCode == 0 && tagsPreserved
              && snesCollectionAfterReplace == QStringLiteral("SNES Classics"),
          QStringLiteral("tags and the collection are preserved across --replace"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--untag"), snesId, QStringLiteral("Co-op"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--untag"), ok);
    QStringList afterUntag;
    if (ok) {
        for (const QJsonValue& value : json.value(QStringLiteral("tags")).toArray())
            afterUntag.append(value.toString());
    }
    check(result.exitCode == 0 && afterUntag == QStringList({QStringLiteral("Classic")}),
          QStringLiteral("--untag removes one tag"));
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--tag"), snesId, QStringLiteral("Co-op"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--tag re-add"), ok);
    check(result.exitCode == 0 && json.value(QStringLiteral("tags")).toArray().size() == 2,
          QStringLiteral("--tag re-adds the tag"));

    // Collections: counts, set/clear, rename and remove update package membership.
    result = runManager(managerExe, dataArgs
        + QStringList{QStringLiteral("--collections"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--collections"), ok);
    check(result.exitCode == 0 && ok
              && json.value(QStringLiteral("count")).toInt() == 1
              && json.value(QStringLiteral("collections")).toArray().first().toObject()
                     .value(QStringLiteral("name")).toString()
                     == QStringLiteral("SNES Classics")
              && json.value(QStringLiteral("collections")).toArray().first().toObject()
                     .value(QStringLiteral("count")).toInt() == 1,
          QStringLiteral("--collections lists names with counts"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--set-collection"), snesId, QStringLiteral("SNES Vault"),
        QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--set-collection"), ok);
    {
        QHash<QString, QStringList> collections;
        QString collectionError;
        readCollections(testSettings, collections, collectionError);
        bool packageOk = false;
        const QJsonObject package = readJsonObjectFile(
            QDir(xmb::manager::packageDirectoryFor(testSettings, snesId))
                .filePath(QStringLiteral("pkg.json")),
            packageOk);
        check(result.exitCode == 0
                  && collections.value(QStringLiteral("SNES Vault")).contains(snesId)
                  && !collections.contains(QStringLiteral("SNES Classics"))
                  && package.value(QStringLiteral("collection")).toString()
                      == QStringLiteral("SNES Vault"),
              QStringLiteral("--set-collection moves the package membership"));
    }

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--rename-collection"), QStringLiteral("SNES Vault"),
        QStringLiteral("SNES Legends"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--rename-collection"), ok);
    {
        QHash<QString, QStringList> collections;
        QString collectionError;
        readCollections(testSettings, collections, collectionError);
        bool packageOk = false;
        const QJsonObject package = readJsonObjectFile(
            QDir(xmb::manager::packageDirectoryFor(testSettings, snesId))
                .filePath(QStringLiteral("pkg.json")),
            packageOk);
        check(result.exitCode == 0 && ok
                  && json.value(QStringLiteral("affected")).toInt() == 1
                  && collections.contains(QStringLiteral("SNES Legends"))
                  && !collections.contains(QStringLiteral("SNES Vault"))
                  && package.value(QStringLiteral("collection")).toString()
                      == QStringLiteral("SNES Legends"),
              QStringLiteral("--rename-collection renames the collection and its members"));
    }

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--remove-collection"), QStringLiteral("SNES Legends"),
        QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--remove-collection"), ok);
    {
        QHash<QString, QStringList> collections;
        QString collectionError;
        readCollections(testSettings, collections, collectionError);
        bool packageOk = false;
        const QJsonObject package = readJsonObjectFile(
            QDir(xmb::manager::packageDirectoryFor(testSettings, snesId))
                .filePath(QStringLiteral("pkg.json")),
            packageOk);
        check(result.exitCode == 0 && collections.isEmpty()
                  && package.value(QStringLiteral("collection")).toString().isEmpty(),
              QStringLiteral("--remove-collection removes it and clears the package field"));
    }

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--set-collection"), snesId, QStringLiteral("SNES Classics"),
        QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--set-collection restore"), ok);
    check(result.exitCode == 0 && ok, QStringLiteral("--set-collection restores the membership"));
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--set-collection"), snesId, QString(), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--set-collection clear"), ok);
    {
        QHash<QString, QStringList> collections;
        QString collectionError;
        readCollections(testSettings, collections, collectionError);
        check(result.exitCode == 0 && json.value(QStringLiteral("cleared")).toBool()
                  && !collections.contains(QStringLiteral("SNES Classics")),
              QStringLiteral("--set-collection \"\" clears the membership"));
    }
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--set-collection"), snesId, QStringLiteral("SNES Classics"),
        QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--set-collection restore 2"), ok);
    check(result.exitCode == 0 && ok,
          QStringLiteral("the SNES collection is restored for the remaining checks"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--import"), ps2Rom, QStringLiteral("--emulator"),
        QStringLiteral("pcsx2"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--import (PS2)"), ok);
    check(result.exitCode == 0 && ok
              && json.value(QStringLiteral("imported")).toArray().size() == 1,
          QStringLiteral("--import handles a ROM path containing spaces"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--import"), ps3Folder, QStringLiteral("--emulator"),
        QStringLiteral("rpcs3"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--import (PS3 folder)"), ok);
    check(result.exitCode == 0 && ok
              && json.value(QStringLiteral("imported")).toArray().size() == 1,
          QStringLiteral("--import created a package for the PS3 folder"));

    // Platform-map resolution: default mapping, gap refusal, mapping, override.
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--import"), mappedPs2Rom, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--import via platform map"), ok);
    QString mappedEmulator;
    if (ok) {
        const QJsonArray imported = json.value(QStringLiteral("imported")).toArray();
        if (!imported.isEmpty())
            mappedEmulator = imported.first().toObject()
                                 .value(QStringLiteral("emulatorId")).toString();
    }
    check(result.exitCode == 0 && mappedEmulator == QStringLiteral("pcsx2")
              && json.value(QStringLiteral("emulatorSource")).toString()
                  == QStringLiteral("platform-map"),
          QStringLiteral("--import without --emulator resolves PS2 to pcsx2 from the map"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--import"), threeDsRom, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--import mapping gap"), ok);
    bool gapRefused = false;
    if (ok) {
        const QJsonArray failed = json.value(QStringLiteral("failed")).toArray();
        gapRefused = failed.size() == 1
            && failed.first().toObject().value(QStringLiteral("error")).toString()
                   .contains(QStringLiteral("No emulator mapping"))
            && failed.first().toObject().value(QStringLiteral("error")).toString()
                   .contains(QStringLiteral("3DS"));
    }
    check(result.exitCode == 1 && gapRefused,
          QStringLiteral("a platform with no mapping is refused with a clear reason"));
    check(!QFileInfo::exists(xmb::manager::packageDirectoryFor(testSettings, threeDsId)),
          QStringLiteral("the refused mapping left no package directory"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--map-platform"), QStringLiteral("3DS"), QStringLiteral("retroarch"),
        QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--map-platform"), ok);
    check(result.exitCode == 0 && ok
              && json.value(QStringLiteral("platform")).toString() == QStringLiteral("3DS")
              && json.value(QStringLiteral("emulatorId")).toString()
                  == QStringLiteral("retroarch"),
          QStringLiteral("--map-platform writes a mapping"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--import"), threeDsRom, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--import mapped 3DS"), ok);
    QString threeDsEmulator;
    bool threeDsFromMap = false;
    if (ok) {
        const QJsonArray imported = json.value(QStringLiteral("imported")).toArray();
        if (!imported.isEmpty()) {
            threeDsEmulator = imported.first().toObject()
                                  .value(QStringLiteral("emulatorId")).toString();
            threeDsFromMap = imported.first().toObject()
                                 .value(QStringLiteral("mappingUsed")).toBool();
        }
    }
    check(result.exitCode == 0 && threeDsEmulator == QStringLiteral("retroarch")
              && threeDsFromMap,
          QStringLiteral("the new mapping resolves a platform no profile lists"));

    // Add a per-platform argument override for SNES and import through the map.
    bool mapFileOk = false;
    QJsonObject mapRoot = readJsonObjectFile(xmb::manager::platformMapPath(testSettings),
                                             mapFileOk);
    QJsonArray mapArray = mapRoot.value(QStringLiteral("mappings")).toArray();
    for (qsizetype index = 0; index < mapArray.size(); ++index) {
        QJsonObject entry = mapArray.at(index).toObject();
        if (entry.value(QStringLiteral("platform")).toString() == QStringLiteral("SNES")) {
            entry.insert(QStringLiteral("arguments"),
                         QJsonArray{QStringLiteral("-L"), QStringLiteral("snes9x_libretro"),
                                    QStringLiteral("{rom}")});
            mapArray.replace(index, entry);
        }
    }
    mapRoot.insert(QStringLiteral("mappings"), mapArray);
    writeJsonObjectFile(xmb::manager::platformMapPath(testSettings), mapRoot);
    check(mapFileOk
              && mapRoot.value(QStringLiteral("schema")).toString()
                  == QStringLiteral("xmb-platform-map/1"),
          QStringLiteral("platform-map.json uses schema xmb-platform-map/1"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--import"), secondSnesRom, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--import with argument override"), ok);
    bool overrideApplied = false;
    if (ok) {
        const QJsonArray imported = json.value(QStringLiteral("imported")).toArray();
        if (!imported.isEmpty()) {
            const QJsonArray arguments = imported.first().toObject()
                                             .value(QStringLiteral("app")).toObject()
                                             .value(QStringLiteral("arguments")).toArray();
            overrideApplied = arguments.size() == 3
                && arguments.at(0).toString() == QStringLiteral("-L")
                && arguments.at(1).toString() == QStringLiteral("snes9x_libretro")
                && arguments.at(2).toString() == QDir::cleanPath(secondSnesRom);
        }
    }
    check(result.exitCode == 0 && overrideApplied,
          QStringLiteral("the per-platform argument override replaces the profile template"));

    result = runManager(managerExe, dataArgs
        + QStringList{QStringLiteral("--list-mappings"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--list-mappings"), ok);
    QString snesMapped;
    QString threeDsMapped;
    bool discUnmapped = false;
    bool snesOverride = false;
    if (ok) {
        for (const QJsonValue& value : json.value(QStringLiteral("mappings")).toArray()) {
            const QJsonObject entry = value.toObject();
            const QString platform = entry.value(QStringLiteral("platform")).toString();
            if (platform == QStringLiteral("SNES")) {
                snesMapped = entry.value(QStringLiteral("emulatorId")).toString();
                snesOverride = entry.value(QStringLiteral("arguments")).toArray().size() == 3;
            }
            if (platform == QStringLiteral("3DS"))
                threeDsMapped = entry.value(QStringLiteral("emulatorId")).toString();
            if (platform == QStringLiteral("Disc image")
                && !entry.value(QStringLiteral("mapped")).toBool())
                discUnmapped = true;
        }
    }
    check(result.exitCode == 0 && snesMapped == QStringLiteral("retroarch")
              && threeDsMapped == QStringLiteral("retroarch") && discUnmapped && snesOverride,
          QStringLiteral("--list-mappings reports every platform, gap and override"));
    record(QStringLiteral("mapping resolution table:"));
    if (ok) {
        for (const QJsonValue& value : json.value(QStringLiteral("mappings")).toArray()) {
            const QJsonObject entry = value.toObject();
            record(QStringLiteral("  %1 -> %2 (%3%4)")
                       .arg(entry.value(QStringLiteral("platform")).toString(),
                            entry.value(QStringLiteral("emulatorId")).toString().isEmpty()
                                ? QStringLiteral("(unmapped)")
                                : entry.value(QStringLiteral("emulatorId")).toString(),
                            entry.value(QStringLiteral("source")).toString(),
                            entry.value(QStringLiteral("configured")).toBool()
                                ? QStringLiteral(", configured")
                                : QStringLiteral("")));
        }
    }

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--import-pkg"), readablePkgPath, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--import-pkg readable"), ok);
    check(result.exitCode == 0 && ok
              && json.value(QStringLiteral("imported")).toBool(),
          QStringLiteral("--import-pkg imports a package with readable metadata"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--import-pkg"), retailPkgPath, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--import-pkg retail"), ok);
    check(result.exitCode == 1
              && json.value(QStringLiteral("error")).toString()
                     .contains(QStringLiteral("Encrypted retail")),
          QStringLiteral("--import-pkg refuses an encrypted retail package honestly"));
    const QString retailId = xmb::manager::packageIdFor(retailPkgPath, QStringLiteral("PS3"));
    xmb::manager::Settings settings;
    settings.dataDirectory = data;
    check(!QFileInfo::exists(xmb::manager::packageDirectoryFor(settings, retailId)),
          QStringLiteral("the refused retail package left no package directory"));

    result = runManager(managerExe, dataArgs
        + QStringList{QStringLiteral("--validate"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--validate"), ok);
    check(result.exitCode == 0 && json.value(QStringLiteral("consistent")).toBool(),
          QStringLiteral("--validate reports a consistent registry and package set"));

    result = runManager(managerExe, dataArgs
        + QStringList{QStringLiteral("--list"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--list"), ok);
    check(result.exitCode == 0 && ok
              && json.value(QStringLiteral("orphanedPackages")).toInt() == 0
              && json.value(QStringLiteral("missingPackages")).toInt() == 0,
          QStringLiteral("--list reports no orphaned or missing packages"));

    // apps.json loaded through the real ContentRegistry, printed as evidence.
    QVector<xmb::ContentApp> apps;
    QString registryError;
    check(xmb::ContentRegistry::load(xmb::manager::registryPath(settings), apps, registryError),
          QStringLiteral("ContentRegistry::load accepts the generated apps.json"));
    record(QStringLiteral("apps.json (%1 entries):").arg(apps.size()));
    for (const xmb::ContentApp& entry : apps) {
        record(QStringLiteral("  id=%1 title=%2 kind=%3 category=%4 executable=%5")
                   .arg(entry.id, entry.title, entry.kind, entry.category, entry.executable));
        record(QStringLiteral("    workingDirectory=%1 artwork=%2")
                   .arg(entry.workingDirectory, entry.artwork));
        record(QStringLiteral("    arguments=%1")
                   .arg(QStringLiteral("[%1]").arg(entry.arguments.join(QStringLiteral("] [")))));
    }
    const auto ps2Entry = std::find_if(apps.cbegin(), apps.cend(),
                                       [&ps2Id](const xmb::ContentApp& entry) {
                                           return entry.id == ps2Id;
                                       });
    check(ps2Entry != apps.cend() && ps2Entry->arguments.size() == 1
              && ps2Entry->arguments.first() == QDir::cleanPath(ps2Rom),
          QStringLiteral("the spaced PS2 ROM stayed one apps.json argument"));
    check(ps2Entry != apps.cend() && QFileInfo(ps2Entry->executable).isFile()
              && QFileInfo(ps2Entry->workingDirectory).isDir()
              && QFileInfo(ps2Entry->artwork).isFile(),
          QStringLiteral("the PS2 registration points at real executable/cwd/artwork files"));

    // Bulk operations: validation is atomic, then three packages are updated.
    const QString bulkFile = QDir(root).filePath(QStringLiteral("bulk.json"));
    writeJsonObjectFile(
        bulkFile,
        QJsonObject{{QStringLiteral("addTags"), QJsonArray{QStringLiteral("ghosttag")}},
                    {QStringLiteral("ids"),
                     QJsonArray{snesId, ps2Id, QStringLiteral("pkg-ghost-000000000000")}}});
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--bulk"), bulkFile, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--bulk invalid"), ok);
    bool bulkRefused = false;
    bool bulkUnchanged = false;
    if (ok) {
        bulkRefused = json.value(QStringLiteral("rolledBack")).toBool()
            && json.value(QStringLiteral("failed")).toInt() >= 1;
        bool packageOk = false;
        const QJsonObject package = readJsonObjectFile(
            QDir(xmb::manager::packageDirectoryFor(testSettings, snesId))
                .filePath(QStringLiteral("pkg.json")),
            packageOk);
        QStringList tags;
        for (const QJsonValue& value : package.value(QStringLiteral("tags")).toArray())
            tags.append(value.toString());
        bulkUnchanged = packageOk && !tags.contains(QStringLiteral("ghosttag"));
    }
    check(result.exitCode == 1 && bulkRefused && bulkUnchanged,
          QStringLiteral("--bulk validates every id first and changes nothing on failure"));

    writeJsonObjectFile(
        bulkFile,
        QJsonObject{{QStringLiteral("setCollection"), QStringLiteral("Bulk Set")},
                    {QStringLiteral("addTags"),
                     QJsonArray{QStringLiteral("bulk"), QStringLiteral("shared")}},
                    {QStringLiteral("ids"), QJsonArray{snesId, ps2Id, threeDsId}}});
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--bulk"), bulkFile, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--bulk apply"), ok);
    bool bulkApplied = false;
    if (ok) {
        bulkApplied = json.value(QStringLiteral("applied")).toInt() == 3
            && json.value(QStringLiteral("failed")).toInt() == 0
            && json.value(QStringLiteral("results")).toArray().size() == 3;
    }
    check(result.exitCode == 0 && bulkApplied,
          QStringLiteral("--bulk applies to three packages with per-id results"));
    {
        bool allTagged = true;
        bool allCollected = true;
        for (const QString& id : {snesId, ps2Id, threeDsId}) {
            bool packageOk = false;
            const QJsonObject package = readJsonObjectFile(
                QDir(xmb::manager::packageDirectoryFor(testSettings, id))
                    .filePath(QStringLiteral("pkg.json")),
                packageOk);
            QStringList tags;
            for (const QJsonValue& value : package.value(QStringLiteral("tags")).toArray())
                tags.append(value.toString());
            allTagged = allTagged && packageOk && tags.contains(QStringLiteral("bulk"))
                && tags.contains(QStringLiteral("shared"));
            allCollected = allCollected
                && package.value(QStringLiteral("collection")).toString()
                    == QStringLiteral("Bulk Set");
        }
        QHash<QString, QStringList> collections;
        QString collectionError;
        readCollections(testSettings, collections, collectionError);
        check(allTagged && allCollected
                  && collections.value(QStringLiteral("Bulk Set")).size() == 3,
              QStringLiteral("--bulk wrote tags, collections and memberships for all three"));
    }

    writeJsonObjectFile(bulkFile,
                        QJsonObject{{QStringLiteral("setEmulator"), QStringLiteral("pcsx2")},
                                    {QStringLiteral("removeTags"),
                                     QJsonArray{QStringLiteral("shared")}},
                                    {QStringLiteral("ids"), QJsonArray{snesId, ps2Id, threeDsId}}});
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--bulk"), bulkFile, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--bulk emulator"), ok);
    bool emulatorApplied = false;
    if (ok) {
        emulatorApplied = json.value(QStringLiteral("applied")).toInt() == 3;
        QVector<xmb::ContentApp> bulkApps;
        QString bulkRegistryError;
        xmb::ContentRegistry::load(xmb::manager::registryPath(testSettings), bulkApps,
                                   bulkRegistryError);
        for (const QString& id : {snesId, ps2Id, threeDsId}) {
            bool packageOk = false;
            const QJsonObject package = readJsonObjectFile(
                QDir(xmb::manager::packageDirectoryFor(testSettings, id))
                    .filePath(QStringLiteral("pkg.json")),
                packageOk);
            QStringList tags;
            for (const QJsonValue& value : package.value(QStringLiteral("tags")).toArray())
                tags.append(value.toString());
            const bool registered = std::any_of(
                bulkApps.cbegin(), bulkApps.cend(), [&id](const xmb::ContentApp& app) {
                    return app.id == id
                        && app.executable.endsWith(QStringLiteral("pcsx2-qt.exe"),
                                                   Qt::CaseInsensitive);
                });
            emulatorApplied = emulatorApplied && packageOk && registered
                && package.value(QStringLiteral("emulatorId")).toString()
                    == QStringLiteral("pcsx2")
                && !tags.contains(QStringLiteral("shared"));
        }
    }
    check(result.exitCode == 0 && emulatorApplied,
          QStringLiteral("--bulk setEmulator rewrites pkg.json and apps.json; removeTags applies"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--import"), bulkRemoveRom, QStringLiteral("--emulator"),
        QStringLiteral("retroarch"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--import bulk remove"), ok);
    check(result.exitCode == 0 && ok, QStringLiteral("imported a package for the bulk remove"));
    writeJsonObjectFile(bulkFile,
                        QJsonObject{{QStringLiteral("remove"), true},
                                    {QStringLiteral("ids"), QJsonArray{bulkRemoveId}}});
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--bulk"), bulkFile, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--bulk remove"), ok);
    check(result.exitCode == 0 && ok && json.value(QStringLiteral("applied")).toInt() == 1
              && !QFileInfo::exists(xmb::manager::packageDirectoryFor(testSettings, bulkRemoveId)),
          QStringLiteral("--bulk remove deletes the package and its registration"));

    // ROM identity: relink accepts an identical copy, refuses a different one,
    // and --force accepts it. Tags and the collection survive the relink.
    const QString relinkDirectory = QDir(roms).filePath(QStringLiteral("Relink"));
    const QString identicalCopy = QDir(relinkDirectory).filePath(
        QStringLiteral("Chrono Copy.sfc"));
    const QString differentCopy = QDir(relinkDirectory).filePath(
        QStringLiteral("Chrono Different.sfc"));
    writeFile(identicalCopy, readFileBytes(snesRom));
    writeFile(differentCopy, QByteArray("SNES-CHRONO-DIFFERENT-CONTENT-999"));
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--relink"), snesId, identicalCopy, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--relink identical copy"), ok);
    bool relinkAccepted = false;
    QString relinkedRom;
    QStringList relinkedTags;
    QString relinkedCollection;
    if (ok) {
        relinkAccepted = QDir::cleanPath(json.value(QStringLiteral("newPath")).toString())
                == QDir::cleanPath(identicalCopy)
            && json.value(QStringLiteral("oldHash")).toString()
                == json.value(QStringLiteral("newHash")).toString();
        bool packageOk = false;
        const QJsonObject package = readJsonObjectFile(
            QDir(xmb::manager::packageDirectoryFor(testSettings, snesId))
                .filePath(QStringLiteral("pkg.json")),
            packageOk);
        relinkedRom = package.value(QStringLiteral("romPath")).toString();
        relinkedCollection = package.value(QStringLiteral("collection")).toString();
        for (const QJsonValue& value : package.value(QStringLiteral("tags")).toArray())
            relinkedTags.append(value.toString());
    }
    check(result.exitCode == 0 && relinkAccepted
              && QDir::cleanPath(relinkedRom) == QDir::cleanPath(identicalCopy)
              && relinkedTags
                  == QStringList({QStringLiteral("Classic"), QStringLiteral("Co-op"),
                                  QStringLiteral("bulk")})
              && relinkedCollection == QStringLiteral("Bulk Set"),
          QStringLiteral("--relink accepts an identical copy and keeps tags/collection"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--relink"), snesId, differentCopy, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--relink mismatch"), ok);
    check(result.exitCode == 1 && ok
              && json.value(QStringLiteral("error")).toString()
                     .contains(QStringLiteral("does not match")),
          QStringLiteral("--relink refuses a different content hash"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--relink"), snesId, differentCopy, QStringLiteral("--force"),
        QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--relink --force"), ok);
    bool forcedAccepted = false;
    if (ok) {
        forcedAccepted = json.value(QStringLiteral("forced")).toBool();
        bool packageOk = false;
        const QJsonObject package = readJsonObjectFile(
            QDir(xmb::manager::packageDirectoryFor(testSettings, snesId))
                .filePath(QStringLiteral("pkg.json")),
            packageOk);
        forcedAccepted = forcedAccepted && packageOk
            && QDir::cleanPath(package.value(QStringLiteral("romPath")).toString())
                == QDir::cleanPath(differentCopy)
            && package.value(QStringLiteral("romHash")).toString()
                == json.value(QStringLiteral("newHash")).toString();
    }
    check(result.exitCode == 0 && forcedAccepted,
          QStringLiteral("--relink --force accepts a different hash and records the new one"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--import"), QDir(roms).filePath(QStringLiteral("NDS/Test NDS Game.nds")),
        QStringLiteral("--emulator"), QStringLiteral("retroarch"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--import NDS"), ok);
    const QString ndsId = xmb::manager::packageIdFor(
        QDir(roms).filePath(QStringLiteral("NDS/Test NDS Game.nds")), QStringLiteral("NDS"));
    check(result.exitCode == 0 && ok, QStringLiteral("imported the NDS package for hash checks"));

    result = runManager(managerExe, dataArgs
        + QStringList{QStringLiteral("--verify-hashes"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--verify-hashes clean"), ok);
    check(result.exitCode == 0 && ok
              && json.value(QStringLiteral("failed")).toInt() == 0
              && json.value(QStringLiteral("passed")).toInt() >= 5,
          QStringLiteral("--verify-hashes is clean for every recorded package"));

    const QString ndsRom = QDir(roms).filePath(QStringLiteral("NDS/Test NDS Game.nds"));
    const QByteArray ndsOriginal = readFileBytes(ndsRom);
    writeFile(ndsRom, ndsOriginal + QByteArray("CORRUPTED"));
    result = runManager(managerExe, dataArgs
        + QStringList{QStringLiteral("--verify-hashes"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--verify-hashes corrupted"), ok);
    bool mismatchReported = false;
    if (ok) {
        for (const QJsonValue& value : json.value(QStringLiteral("packages")).toArray()) {
            const QJsonObject entry = value.toObject();
            if (entry.value(QStringLiteral("id")).toString() == ndsId
                && entry.value(QStringLiteral("state")).toString() == QStringLiteral("mismatch"))
                mismatchReported = true;
        }
    }
    check(result.exitCode == 1 && json.value(QStringLiteral("failed")).toInt() == 1
              && mismatchReported,
          QStringLiteral("--verify-hashes reports one corrupted file"));
    writeFile(ndsRom, ndsOriginal);
    result = runManager(managerExe, dataArgs
        + QStringList{QStringLiteral("--verify-hashes"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--verify-hashes restored"), ok);
    check(result.exitCode == 0 && json.value(QStringLiteral("failed")).toInt() == 0,
          QStringLiteral("--verify-hashes is clean again after restoring the ROM"));

    // Launch checking through harmless cmd.exe profiles.
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--import"), launchProbeRom, QStringLiteral("--emulator"),
        QStringLiteral("cmd-probe"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--import launch probe"), ok);
    check(result.exitCode == 0 && ok,
          QStringLiteral("imported the cmd.exe launch probe package"));
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--import"), slowProbeRom, QStringLiteral("--emulator"),
        QStringLiteral("cmd-slow"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--import slow probe"), ok);
    check(result.exitCode == 0 && ok, QStringLiteral("imported the timeout probe package"));
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--import"), missingProbeRom, QStringLiteral("--emulator"),
        QStringLiteral("cmd-missing"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--import missing probe"), ok);
    check(result.exitCode == 0 && ok, QStringLiteral("imported the missing-executable probe"));
    check(QFile::remove(missingExe),
          QStringLiteral("deleted the copied cmd.exe to force a spawn error"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--print-argv"), launchProbeId, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--print-argv"), ok);
    bool argvExact = false;
    QString probeExecutable;
    if (ok) {
        const QJsonArray argumentArray = json.value(QStringLiteral("arguments")).toArray();
        probeExecutable = json.value(QStringLiteral("executable")).toString();
        argvExact = argumentArray.size() == 3
            && argumentArray.at(0).toString() == QStringLiteral("/c")
            && argumentArray.at(1).toString() == QStringLiteral("exit")
            && argumentArray.at(2).toString() == QStringLiteral("0")
            && QDir::cleanPath(probeExecutable) == QDir::cleanPath(cmdExe);
    }
    check(result.exitCode == 0 && argvExact,
          QStringLiteral("--print-argv prints the exact expanded argv as JSON without launching"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--launch-check"), launchProbeId, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--launch-check cmd"), ok);
    bool launchOk = false;
    if (ok) {
        const QJsonArray argumentArray = json.value(QStringLiteral("arguments")).toArray();
        launchOk = json.value(QStringLiteral("started")).toBool()
            && json.value(QStringLiteral("finished")).toBool()
            && json.value(QStringLiteral("pid")).toDouble() > 0
            && json.value(QStringLiteral("exitCode")).toInt() == 0
            && json.value(QStringLiteral("exitStatus")).toString() == QStringLiteral("normal")
            && argumentArray.size() == 3
            && argumentArray.at(0).toString() == QStringLiteral("/c")
            && argumentArray.at(1).toString() == QStringLiteral("exit")
            && argumentArray.at(2).toString() == QStringLiteral("0");
    }
    check(result.exitCode == 0 && launchOk,
          QStringLiteral("--launch-check on the cmd.exe profile exits 0 with the exact argv"));
    record(QStringLiteral("launch-check evidence: executable=%1 argv=%2 pid=%3 exit=%4")
               .arg(json.value(QStringLiteral("executable")).toString(),
                    QStringLiteral("[%1]").arg(
                        [&json]() {
                            QStringList parts;
                            for (const QJsonValue& value : json.value(QStringLiteral("arguments"))
                                                              .toArray())
                                parts.append(value.toString());
                            return parts.join(QStringLiteral(", "));
                        }()),
                    QString::number(json.value(QStringLiteral("pid")).toDouble(), 'f', 0),
                    QString::number(json.value(QStringLiteral("exitCode")).toInt())));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--launch-check"), missingProbeId, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--launch-check missing exe"), ok);
    check(result.exitCode == 1 && ok
              && !json.value(QStringLiteral("started")).toBool()
              && !json.value(QStringLiteral("spawnError")).toString().isEmpty(),
          QStringLiteral("--launch-check reports a spawn error for a missing executable"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--launch-check"), slowProbeId, QStringLiteral("--launch-timeout"),
        QStringLiteral("1"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--launch-check timeout"), ok);
    check(result.exitCode == 1 && ok
              && json.value(QStringLiteral("timedOut")).toBool()
              && (json.value(QStringLiteral("terminated")).toBool()
                  || json.value(QStringLiteral("killed")).toBool()),
          QStringLiteral("--launch-check terminates a timed-out child"));
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--remove"), missingProbeId, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--remove missing probe"), ok);
    check(result.exitCode == 0 && ok,
          QStringLiteral("removed the missing-executable probe package"));

    // Backup and restore into a fresh data directory with a ROM path remap.
    const QString backupFile = QDir(root).filePath(QStringLiteral("backup.json"));
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--backup"), backupFile, QStringLiteral("--with-packaged-art"),
        QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--backup"), ok);
    bool backupOk = false;
    int backedUpPackages = 0;
    if (ok) {
        backedUpPackages = json.value(QStringLiteral("packageCount")).toInt();
        backupOk = json.value(QStringLiteral("appCount")).toInt() == backedUpPackages
            && backedUpPackages >= 6
            && json.value(QStringLiteral("artIncluded")).toInt() > 0;
    }
    check(result.exitCode == 0 && backupOk,
          QStringLiteral("--backup writes a bundle with apps, packages and embedded art"));

    bool bundleOk = false;
    const QJsonObject bundle = readJsonObjectFile(backupFile, bundleOk);
    bool bundleHasSnesTags = false;
    bool bundleHasBulkCollection = false;
    bool bundleHasMappings = false;
    bool bundleHasCmdProfile = false;
    if (bundleOk) {
        for (const QJsonValue& value : bundle.value(QStringLiteral("tags")).toArray()) {
            const QJsonObject entry = value.toObject();
            if (entry.value(QStringLiteral("id")).toString() == snesId) {
                const QJsonArray tags = entry.value(QStringLiteral("tags")).toArray();
                bundleHasSnesTags = tags.size() == 3
                    && tags.at(0).toString() == QStringLiteral("Classic")
                    && tags.at(1).toString() == QStringLiteral("Co-op")
                    && tags.at(2).toString() == QStringLiteral("bulk");
            }
        }
        for (const QJsonValue& value : bundle.value(QStringLiteral("collections")).toArray()) {
            const QJsonObject entry = value.toObject();
            if (entry.value(QStringLiteral("name")).toString() == QStringLiteral("Bulk Set"))
                bundleHasBulkCollection = true;
        }
        const QJsonArray mappings =
            bundle.value(QStringLiteral("platformMap")).toObject()
                .value(QStringLiteral("mappings")).toArray();
        bool sawSnesOverride = false;
        bool sawThreeDs = false;
        for (const QJsonValue& value : mappings) {
            const QJsonObject entry = value.toObject();
            if (entry.value(QStringLiteral("platform")).toString() == QStringLiteral("SNES")
                && entry.value(QStringLiteral("arguments")).toArray().size() == 3)
                sawSnesOverride = true;
            if (entry.value(QStringLiteral("platform")).toString() == QStringLiteral("3DS"))
                sawThreeDs = true;
        }
        bundleHasMappings = sawSnesOverride && sawThreeDs;
        for (const QJsonValue& value : bundle.value(QStringLiteral("emulatorOverlay")).toArray()) {
            if (value.toObject().value(QStringLiteral("id")).toString()
                == QStringLiteral("cmd-probe"))
                bundleHasCmdProfile = true;
        }
    }
    check(bundleOk
              && bundle.value(QStringLiteral("schema")).toString()
                  == QStringLiteral("xmb-manager-backup/1")
              && bundleHasSnesTags && bundleHasBulkCollection && bundleHasMappings
              && bundleHasCmdProfile,
          QStringLiteral("the backup bundle carries schema, tags, collections, mappings and "
                         "the emulator overlay"));

    const QString migratedRoms = QDir(root).filePath(QStringLiteral("migrated-roms"));
    QDir(migratedRoms).removeRecursively();
    check(copyTree(roms, migratedRoms), QStringLiteral("copied the ROM tree to the restore target"));
    const QString restoredData = QDir(root).filePath(QStringLiteral("restored/data"));
    QDir(QDir(root).filePath(QStringLiteral("restored"))).removeRecursively();
    const QStringList restoreArgs = {QStringLiteral("--data-dir"), restoredData};
    result = runManager(managerExe, restoreArgs + QStringList{
        QStringLiteral("--restore"), backupFile, QStringLiteral("--remap"),
        QStringLiteral("%1=%2").arg(roms, migratedRoms), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--restore"), ok);
    int restoredPackageActions = 0;
    if (ok) {
        for (const QJsonValue& value : json.value(QStringLiteral("actions")).toArray()) {
            const QJsonObject action = value.toObject();
            if (action.value(QStringLiteral("kind")).toString() == QStringLiteral("package"))
                ++restoredPackageActions;
        }
    }
    check(result.exitCode == 0 && ok && restoredPackageActions == backedUpPackages,
          QStringLiteral("--restore recreates every package with path remapping"));

    xmb::manager::Settings restoredSettings;
    restoredSettings.dataDirectory = restoredData;
    bool restoredPackageOk = false;
    const QJsonObject restoredSnesPackage = readJsonObjectFile(
        QDir(xmb::manager::packageDirectoryFor(restoredSettings, snesId))
            .filePath(QStringLiteral("pkg.json")),
        restoredPackageOk);
    QStringList restoredTags;
    for (const QJsonValue& value :
         restoredSnesPackage.value(QStringLiteral("tags")).toArray())
        restoredTags.append(value.toString());
    QHash<QString, QStringList> restoredCollections;
    QString restoredCollectionError;
    readCollections(restoredSettings, restoredCollections, restoredCollectionError);
    bool restoredMapOk = false;
    const QJsonObject restoredMap =
        readJsonObjectFile(xmb::manager::platformMapPath(restoredSettings), restoredMapOk);
    bool restoredSnesOverride = false;
    bool restoredThreeDs = false;
    for (const QJsonValue& value : restoredMap.value(QStringLiteral("mappings")).toArray()) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("platform")).toString() == QStringLiteral("SNES")
            && entry.value(QStringLiteral("arguments")).toArray().size() == 3)
            restoredSnesOverride = true;
        if (entry.value(QStringLiteral("platform")).toString() == QStringLiteral("3DS")
            && entry.value(QStringLiteral("emulatorId")).toString()
                == QStringLiteral("retroarch"))
            restoredThreeDs = true;
    }
    check(restoredPackageOk
              && QDir::cleanPath(restoredSnesPackage.value(QStringLiteral("romPath")).toString())
                     .startsWith(QDir::cleanPath(migratedRoms) + QLatin1Char('/'),
                                 Qt::CaseInsensitive)
              && restoredTags
                  == QStringList({QStringLiteral("Classic"), QStringLiteral("Co-op"),
                                  QStringLiteral("bulk")})
              && restoredCollections.value(QStringLiteral("Bulk Set")).contains(snesId)
              && restoredCollections.value(QStringLiteral("Bulk Set")).size() >= 2,
          QStringLiteral("restore reproduces ROM paths (remapped), tags and collection membership"));
    check(restoredMapOk && restoredSnesOverride && restoredThreeDs,
          QStringLiteral("restore reproduces the platform map including the SNES override"));

    result = runManager(managerExe, restoreArgs + QStringList{
        QStringLiteral("--validate"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--validate restored"), ok);
    check(result.exitCode == 0 && json.value(QStringLiteral("consistent")).toBool(),
          QStringLiteral("the restored library validates cleanly"));

    result = runManager(managerExe, restoreArgs + QStringList{
        QStringLiteral("--launch-check"), launchProbeId, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--launch-check restored"), ok);
    check(result.exitCode == 0 && json.value(QStringLiteral("exitCode")).toInt() == 0,
          QStringLiteral("the restored cmd.exe package still launches with exit 0"));

    result = runManager(managerExe, restoreArgs + QStringList{
        QStringLiteral("--restore"), backupFile, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--restore conflict"), ok);
    check(result.exitCode == 1 && ok
              && !json.value(QStringLiteral("conflicts")).toArray().isEmpty(),
          QStringLiteral("--restore refuses conflicts without --replace"));
    result = runManager(managerExe, restoreArgs + QStringList{
        QStringLiteral("--restore"), backupFile, QStringLiteral("--replace"),
        QStringLiteral("--remap"), QStringLiteral("%1=%2").arg(roms, migratedRoms),
        QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--restore --replace"), ok);
    check(result.exitCode == 0 && ok,
          QStringLiteral("--restore --replace overwrites the restored library"));

    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--remove"), ps2Id, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--remove"), ok);
    check(result.exitCode == 0 && ok && json.value(QStringLiteral("removed")).toBool(),
          QStringLiteral("--remove reports success"));
    check(!QFileInfo::exists(xmb::manager::packageDirectoryFor(settings, ps2Id)),
          QStringLiteral("--remove deleted the package directory"));
    QVector<xmb::ContentApp> afterRemove;
    check(xmb::ContentRegistry::load(xmb::manager::registryPath(settings), afterRemove,
                                     registryError),
          QStringLiteral("apps.json stays valid after --remove"));
    const bool ps2StillRegistered =
        std::any_of(afterRemove.cbegin(), afterRemove.cend(), [&ps2Id](const xmb::ContentApp& app) {
            return app.id == ps2Id;
        });
    check(!ps2StillRegistered, QStringLiteral("--remove deleted the apps.json entry"));

    // --validate integrity injections: platform map, tags, collection membership
    // and package hashes each produce a named issue, then the state is restored.
    const QByteArray platformMapBytes = readFileBytes(xmb::manager::platformMapPath(testSettings));
    writeJsonObjectFile(
        xmb::manager::platformMapPath(testSettings),
        QJsonObject{{QStringLiteral("schema"), QStringLiteral("xmb-platform-map/1")},
                    {QStringLiteral("version"), 1},
                    {QStringLiteral("mappings"),
                     QJsonArray{QJsonObject{{QStringLiteral("platform"), QStringLiteral("SNES")},
                                            {QStringLiteral("emulatorId"),
                                             QStringLiteral("does-not-exist")}}}}});
    result = runManager(managerExe, dataArgs
        + QStringList{QStringLiteral("--validate"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--validate bad mapping"), ok);
    bool unknownMappingIssue = false;
    if (ok) {
        for (const QJsonValue& value : json.value(QStringLiteral("issues")).toArray()) {
            if (value.toObject().value(QStringLiteral("kind")).toString()
                == QStringLiteral("unknown-mapped-emulator"))
                unknownMappingIssue = true;
        }
    }
    check(result.exitCode == 1 && unknownMappingIssue,
          QStringLiteral("--validate reports an unknown mapped emulator"));
    writeFile(xmb::manager::platformMapPath(testSettings), platformMapBytes);

    const QByteArray collectionsBytes = readFileBytes(xmb::manager::collectionsPath(testSettings));
    bool collectionsOk = false;
    QJsonObject collectionsRoot =
        readJsonObjectFile(xmb::manager::collectionsPath(testSettings), collectionsOk);
    QJsonArray collectionArray = collectionsRoot.value(QStringLiteral("collections")).toArray();
    if (collectionArray.isEmpty()) {
        collectionArray.append(QJsonObject{
            {QStringLiteral("name"), QStringLiteral("Bulk Set")},
            {QStringLiteral("ids"), QJsonArray{QStringLiteral("pkg-ghost-000000000000")}}});
    } else {
        QJsonObject first = collectionArray.at(0).toObject();
        QJsonArray ids = first.value(QStringLiteral("ids")).toArray();
        ids.append(QStringLiteral("pkg-ghost-000000000000"));
        first.insert(QStringLiteral("ids"), ids);
        collectionArray.replace(0, first);
    }
    collectionsRoot.insert(QStringLiteral("collections"), collectionArray);
    writeJsonObjectFile(xmb::manager::collectionsPath(testSettings), collectionsRoot);
    result = runManager(managerExe, dataArgs
        + QStringList{QStringLiteral("--validate"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--validate ghost collection"), ok);
    bool ghostIssue = false;
    if (ok) {
        for (const QJsonValue& value : json.value(QStringLiteral("issues")).toArray()) {
            if (value.toObject().value(QStringLiteral("kind")).toString()
                == QStringLiteral("collection-unknown-id"))
                ghostIssue = true;
        }
    }
    check(result.exitCode == 1 && ghostIssue,
          QStringLiteral("--validate reports a collection id with no package"));
    writeFile(xmb::manager::collectionsPath(testSettings), collectionsBytes);

    const QString snesPackageJson = QDir(xmb::manager::packageDirectoryFor(testSettings, snesId))
                                        .filePath(QStringLiteral("pkg.json"));
    const QByteArray snesPackageBytes = readFileBytes(snesPackageJson);
    bool snesPackageOk = false;
    QJsonObject snesPackageObject = readJsonObjectFile(snesPackageJson, snesPackageOk);
    snesPackageObject.insert(QStringLiteral("tags"),
                             QJsonArray{QStringLiteral("   ")});
    writeJsonObjectFile(snesPackageJson, snesPackageObject);
    result = runManager(managerExe, dataArgs
        + QStringList{QStringLiteral("--validate"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--validate bad tag"), ok);
    bool tagIssue = false;
    if (ok) {
        for (const QJsonValue& value : json.value(QStringLiteral("issues")).toArray()) {
            const QJsonObject issue = value.toObject();
            if (issue.value(QStringLiteral("kind")).toString() == QStringLiteral("invalid-package")
                && issue.value(QStringLiteral("message")).toString().contains(
                    QStringLiteral("tag")))
                tagIssue = true;
        }
    }
    check(result.exitCode == 1 && tagIssue,
          QStringLiteral("--validate reports an invalid tag"));
    writeFile(snesPackageJson, snesPackageBytes);

    const QByteArray threeDsOriginal = readFileBytes(threeDsRom);
    writeFile(threeDsRom, threeDsOriginal + QByteArray("CORRUPTED"));
    result = runManager(managerExe, dataArgs
        + QStringList{QStringLiteral("--validate"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--validate bad hash"), ok);
    bool hashIssue = false;
    if (ok) {
        for (const QJsonValue& value : json.value(QStringLiteral("issues")).toArray()) {
            const QJsonObject issue = value.toObject();
            if (issue.value(QStringLiteral("kind")).toString() == QStringLiteral("package-hash")
                && issue.value(QStringLiteral("id")).toString() == threeDsId)
                hashIssue = true;
        }
    }
    check(result.exitCode == 1 && hashIssue,
          QStringLiteral("--validate reports a package whose ROM hash changed"));
    writeFile(threeDsRom, threeDsOriginal);

    result = runManager(managerExe, dataArgs
        + QStringList{QStringLiteral("--validate"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--validate restored"), ok);
    check(result.exitCode == 0 && json.value(QStringLiteral("consistent")).toBool(),
          QStringLiteral("--validate is consistent after the injected faults are removed"));

    result = runManager(managerExe, dataArgs
        + QStringList{QStringLiteral("--validate"), QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--validate after remove"), ok);
    check(result.exitCode == 0 && json.value(QStringLiteral("consistent")).toBool(),
          QStringLiteral("--validate stays consistent after removal"));

    const QString reportPath = QDir(root).filePath(QStringLiteral("report.json"));
    result = runManager(managerExe, dataArgs + QStringList{
        QStringLiteral("--export-report"), reportPath, QStringLiteral("--json")});
    json = parseJson(result, QStringLiteral("--export-report"), ok);
    QJsonObject report;
    QFile reportFile(reportPath);
    bool reportOk = reportFile.open(QIODevice::ReadOnly);
    if (reportOk) {
        QJsonParseError reportError{};
        const QJsonDocument document = QJsonDocument::fromJson(reportFile.readAll(), &reportError);
        report = document.object();
        reportOk = reportError.error == QJsonParseError::NoError
            && report.value(QStringLiteral("schema")).toString()
                   == QStringLiteral("xmb-manager-report/2");
    }
    bool reportHashClean = !report.value(QStringLiteral("hashState")).toArray().isEmpty();
    for (const QJsonValue& value : report.value(QStringLiteral("hashState")).toArray()) {
        if (value.toObject().value(QStringLiteral("state")).toString() != QStringLiteral("ok"))
            reportHashClean = false;
    }
    check(result.exitCode == 0 && ok && reportOk
              && report.value(QStringLiteral("packageCount")).toInt() >= 2
              && !report.value(QStringLiteral("mappings")).toArray().isEmpty()
              && !report.value(QStringLiteral("tags")).toArray().isEmpty()
              && !report.value(QStringLiteral("collections")).toArray().isEmpty()
              && reportHashClean,
          QStringLiteral("--export-report wrote a valid xmb-manager-report/2 report"));

    check(!QFileInfo::exists(QDir(root).filePath(QStringLiteral("stray"))),
          QStringLiteral("scratch layout has no stray marker"));

    const QString transcriptPath = QDir(root).filePath(QStringLiteral("cli-transcript.txt"));
    writeFile(transcriptPath, gTranscript.join(QLatin1Char('\n')).toUtf8() + '\n');
    QTextStream stream(stdout);
    stream << Qt::endl
           << QStringLiteral("Transcript: %1").arg(transcriptPath) << Qt::endl
           << QStringLiteral("%1 failure%2").arg(gFailures)
                  .arg(gFailures == 1 ? QString() : QStringLiteral("s"))
           << Qt::endl;
    stream.flush();
    return gFailures == 0 ? 0 : 1;
}

