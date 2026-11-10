#include "updater.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QUrl>
#include <QDialog>
#include <QFutureWatcher>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#ifndef XMB_APP_VERSION
#define XMB_APP_VERSION "unknown"
#endif

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

struct InstallResult { QString error; bool dryRun = false; };

int showUpdater(const QString& directory, const QString& current) {
    QDialog dialog;
    dialog.setWindowTitle(QStringLiteral("XMB Updater"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QStringLiteral("Current version: %1").arg(current), &dialog));
    auto* latest = new QLabel(QStringLiteral("Latest release: "), &dialog);
    auto* status = new QLabel(&dialog);
    status->setWordWrap(true);
    auto* progress = new QProgressBar(&dialog);
    progress->setRange(0, 100);
    progress->hide();
    auto* check = new QPushButton(QStringLiteral("Check for Updates"), &dialog);
    auto* install = new QPushButton(QStringLiteral("Download and Install"), &dialog);
    auto* close = new QPushButton(QStringLiteral("Close"), &dialog);
    install->setEnabled(false);
    layout->addWidget(latest);
    layout->addWidget(status);
    layout->addWidget(progress);
    layout->addWidget(check);
    layout->addWidget(install);
    layout->addWidget(close);
    dialog.resize(400, 200);
    xmb::Release release;
    QFutureWatcher<xmb::Release> checkWatcher;
    QFutureWatcher<InstallResult> installWatcher;
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(check, &QPushButton::clicked, &dialog, [&] {
        check->setEnabled(false);
        install->setEnabled(false);
        latest->setText(QStringLiteral("Latest release: "));
        status->setText(QStringLiteral("Checking for updates..."));
        checkWatcher.setFuture(QtConcurrent::run([] { return xmb::latestRelease(); }));
    });
    QObject::connect(&checkWatcher, &QFutureWatcher<xmb::Release>::finished, &dialog, [&] {
        release = checkWatcher.result();
        latest->setText(QStringLiteral("Latest release: %1").arg(release.tag));
        QString why;
        const bool newer = release.error.isEmpty() && xmb::newerVersion(current, release.tag, &why);
        const QString error = release.error.isEmpty() ? why : release.error;
        status->setText(!error.isEmpty() ? error : newer
            ? QStringLiteral("Version %1 available").arg(release.tag) : QStringLiteral("Up to date"));
        install->setEnabled(newer && error.isEmpty());
        check->setEnabled(true);
    });
    QObject::connect(install, &QPushButton::clicked, &dialog, [&] {
        check->setEnabled(false);
        install->setEnabled(false);
        progress->setRange(0, 0);
        progress->show();
        status->setText(QStringLiteral("Downloading update..."));
        const auto selectedRelease = release;
        installWatcher.setFuture(QtConcurrent::run([selectedRelease, directory, &dialog, progress] {
            InstallResult result;
            const QString zip = xmb::downloadUpdate(selectedRelease, &result.error, [&dialog, progress](qint64 received, qint64 total) {
                QMetaObject::invokeMethod(&dialog, [progress, received, total] {
                    if(total > 0) {
                        progress->setRange(0, 100);
                        progress->setValue(int(qBound(0.0, double(received) * 100.0 / double(total), 100.0)));
                    }
                }, Qt::QueuedConnection);
            });
            if(zip.isEmpty()) return result;
            QString staged;
            if(xmb::applyZip(zip, directory, &staged, &result.error)) {
                result.dryRun = qEnvironmentVariable("XMB_UPDATER_DRY_RUN") == QStringLiteral("1");
                swapFolders(directory, staged, &result.error);
            }
            QFile::remove(zip);
            return result;
        }));
    });
    QObject::connect(&installWatcher, &QFutureWatcher<InstallResult>::finished, &dialog, [&] {
        const auto result = installWatcher.result();
        progress->hide();
        status->setText(!result.error.isEmpty() ? result.error : result.dryRun
            ? QStringLiteral("Dry run: would swap the staged update; no installation changed.")
            : QStringLiteral("Installed. Restart to use the new version."));
        check->setEnabled(true);
        install->setEnabled(!result.error.isEmpty());
    });
    dialog.exec();
    checkWatcher.waitForFinished();
    installWatcher.waitForFinished();
    return 0;
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
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
    parser.addOption({QStringLiteral("gui"), QStringLiteral("Show the graphical updater")});
    parser.addOption({QStringLiteral("install-dir"), QStringLiteral("Installation directory for graphical updater"), QStringLiteral("directory")});
    parser.addPositionalArgument(QStringLiteral("install-dir"), QStringLiteral("Installation directory for --install"), QStringLiteral("[install-dir]"));
    if(!parser.parse(app.arguments())) { err << parser.errorText() << Qt::endl; return 2; }
    if(parser.isSet(QStringLiteral("help"))) parser.showHelp();
    const bool check = parser.isSet(QStringLiteral("check")), update = parser.isSet(QStringLiteral("update")), install = parser.isSet(QStringLiteral("install"));
    if(!check && !update && !install && !parser.isSet(QStringLiteral("current")) && !parser.isSet(QStringLiteral("version")) && parser.positionalArguments().isEmpty()) {
        const QString directory = parser.isSet(QStringLiteral("install-dir")) ? parser.value(QStringLiteral("install-dir")) : QCoreApplication::applicationDirPath();
        return showUpdater(directory, app.applicationVersion());
    }
    if(int(check) + int(update) + int(install) != 1 ||
       parser.isSet(QStringLiteral("gui")) || parser.isSet(QStringLiteral("install-dir")) ||
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
