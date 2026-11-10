#include "updater.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QUrl>

namespace {
QTextStream out(stdout), err(stderr);

bool copyTree(const QString& source, const QString& destination) {
    const QFileInfo info(source);
    if(info.isSymLink()) return false;
    if(info.isDir()) {
        if(!QDir().mkpath(destination)) return false;
        const auto entries = QDir(source).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        for(const auto& entry : entries)
            if(!copyTree(entry.absoluteFilePath(), QDir(destination).filePath(entry.fileName()))) return false;
        return true;
    }
    if(QFileInfo::exists(destination) && !QFile::remove(destination)) return false;
    return QFile::copy(source, destination);
}

bool swapFolders(const QString& directory, const QString& staged, QString* error) {
    const QString install = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
    const QString old = install + QStringLiteral(".old");
    if(qEnvironmentVariable("XMB_UPDATER_DRY_RUN") == QStringLiteral("1")) {
        out << "would swap " << staged << " -> " << install << Qt::endl;
        return true;
    }
    if(QFileInfo::exists(old) && (!QFileInfo(old).isDir() || !QDir(old).removeRecursively())) {
        *error = QStringLiteral("Cannot remove previous backup: %1").arg(old); return false;
    }
    if(!QDir().rename(install, old)) { *error = QStringLiteral("Cannot move installation to %1").arg(old); return false; }
    if(!QDir().rename(staged, install)) {
        const bool restored = QDir().rename(old, install);
        *error = restored ? QStringLiteral("Cannot move staged installation; original restored")
                          : QStringLiteral("Cannot move staged installation or restore original; backup: %1").arg(old);
        return false;
    }
    for(const auto& name : {QStringLiteral("xmb-config.json"), QStringLiteral("data")}) {
        const QString source = QDir(old).filePath(name);
        if(QFileInfo::exists(source) && !copyTree(source, QDir(install).filePath(name))) {
            *error = QStringLiteral("Cannot carry over %1; original remains at %2").arg(name, old); return false;
        }
    }
    out << "Installed " << install << "; previous installation kept at " << old << Qt::endl;
    return true;
}

bool installZip(const QString& zip, const QString& install) {
    QString staged, error;
    if(!xmb::applyZip(zip, install, &staged, &error) || !swapFolders(install, staged, &error)) {
        err << error << Qt::endl; return false;
    }
    return true;
}

bool verifyLocal(const QString& zip) {
    QString hash;
    QFile sidecar(zip + QStringLiteral(".sha256"));
    if(sidecar.exists()) {
        if(!sidecar.open(QIODevice::ReadOnly) || sidecar.size() > 65536) {
            err << "Cannot read SHA-256 sidecar" << Qt::endl; return false;
        }
        static const QRegularExpression re(QStringLiteral("^\\s*([0-9a-fA-F]{64})(?:\\s|$)"));
        hash = re.match(QString::fromUtf8(sidecar.readAll())).captured(1);
        if(hash.isEmpty()) { err << "Invalid SHA-256 sidecar" << Qt::endl; return false; }
    } else {
        const auto release = xmb::latestRelease();
        const auto asset = xmb::updateAsset(release);
        if(release.error.isEmpty() && asset.name.compare(QFileInfo(zip).fileName(), Qt::CaseInsensitive) == 0 &&
           xmb::updateHashSource(release, asset) == QStringLiteral("body")) {
            static const QRegularExpression re(QStringLiteral("(?:^|\\n)\\s*SHA256[: ]*([0-9a-fA-F]{64})(?=\\s|$)"), QRegularExpression::CaseInsensitiveOption);
            hash = re.match(release.body).captured(1);
        }
    }
    if(hash.isEmpty()) { out << "No applicable SHA-256 available; proceeding without verification" << Qt::endl; return true; }
    QString error;
    if(!xmb::verifyUpdateFile(zip, hash, &error)) { err << error << Qt::endl; return false; }
    out << "SHA-256 verified" << Qt::endl; return true;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("xmb-updater"));
    app.setApplicationVersion(QStringLiteral(XMB_APP_VERSION));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Standalone XMB updater"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("check"), QStringLiteral("Check for a newer release")});
    parser.addOption({QStringLiteral("update"), QStringLiteral("Update an existing installation"), QStringLiteral("install-dir")});
    parser.addOption({QStringLiteral("install"), QStringLiteral("Install a local ZIP"), QStringLiteral("zip")});
    parser.addOption({QStringLiteral("current"), QStringLiteral("Current app version for update"), QStringLiteral("version")});
    parser.addOption({QStringLiteral("version"), QStringLiteral("Current app version for check"), QStringLiteral("version")});
    parser.addPositionalArgument(QStringLiteral("install-dir"), QStringLiteral("Installation directory for --install"), QStringLiteral("[install-dir]"));
    if(!parser.parse(app.arguments())) { err << parser.errorText() << Qt::endl; return 2; }
    if(parser.isSet(QStringLiteral("help"))) parser.showHelp();
    const bool check = parser.isSet(QStringLiteral("check")), update = parser.isSet(QStringLiteral("update")), install = parser.isSet(QStringLiteral("install"));
    if(int(check) + int(update) + int(install) != 1 ||
       (install ? parser.positionalArguments().size() != 1 : !parser.positionalArguments().isEmpty()) ||
       (parser.isSet(QStringLiteral("current")) && !update) || (parser.isSet(QStringLiteral("version")) && !check)) {
        err << "Invalid arguments. Use --help for usage." << Qt::endl; return 2;
    }
    if(install) return verifyLocal(parser.value(QStringLiteral("install"))) && installZip(parser.value(QStringLiteral("install")), parser.positionalArguments().first()) ? 0 : 2;
    const QString current = parser.isSet(check ? QStringLiteral("version") : QStringLiteral("current"))
        ? parser.value(check ? QStringLiteral("version") : QStringLiteral("current")) : app.applicationVersion();
    const auto release = xmb::latestRelease();
    QString why;
    const bool newer = release.error.isEmpty() && xmb::newerVersion(current, release.tag, &why);
    const QString error = release.error.isEmpty() ? why : release.error;
    const QString url = release.tag.isEmpty() ? QStringLiteral("-") : QStringLiteral("https://github.com/ajgreenboy/XMB-Rewritten/releases/tag/") + QString::fromLatin1(QUrl::toPercentEncoding(release.tag));
    out << "current=" << current << " latest=" << (release.tag.isEmpty() ? QStringLiteral("-") : release.tag)
        << " url=" << url << " newer=" << (newer ? "yes" : "no");
    if(!error.isEmpty()) out << " error=" << error;
    out << Qt::endl;
    if(!error.isEmpty()) return 2;
    if(check) return newer ? 0 : 1;
    if(!newer) { out << "Already up to date" << Qt::endl; return 0; }
    const QString directory = parser.value(QStringLiteral("update"));
    if(qEnvironmentVariable("XMB_UPDATER_DRY_RUN") == QStringLiteral("1")) {
        out << "would swap downloaded update -> " << QFileInfo(directory).absoluteFilePath() << " (dry run; download skipped)" << Qt::endl; return 0;
    }
    QString downloadError;
    const QString zip = xmb::downloadUpdate(release, &downloadError, [](qint64 received, qint64 total) {
        out << "Downloaded " << received << "/" << total << " bytes" << Qt::endl;
    });
    if(zip.isEmpty()) { err << downloadError << Qt::endl; return 2; }
    const bool ok = installZip(zip, directory);
    QFile::remove(zip);
    return ok ? 0 : 2;
}
