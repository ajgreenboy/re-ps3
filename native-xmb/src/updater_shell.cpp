// Settings > System Settings > System Update (PHASES-HTPC D7): check GitHub releases for a newer version, then
// download and install it on confirmation. The check and the download both run on a worker thread (updater.h's
// synchronous calls). The install stages the package next to the app and reports the path; the final folder swap
// and the watchdog restart are the remaining step (the package folder must not be swapped while it is running).
#include "shell.h"
#include "updater.h"
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QtConcurrent>

namespace xmb {
namespace {
// Worker: download the release, verify its hash, and stage it next to the app. Returns {stagedPath, error}.
QPair<QString,QString> downloadAndStage(const Release& release,const QString& installDir) {
    QString error;
    const QString zip=xmb::downloadUpdate(release,&error);
    if(zip.isEmpty())return {QString(),QObject::tr("Download failed: %1").arg(error)};
    QString staged;
    if(!xmb::applyZip(zip,installDir,&staged,&error))return {QString(),QObject::tr("Install failed: %1").arg(error)};
    return {staged,QString()};
}
}  // namespace

void Shell::activateSystemUpdate(const Entry& entry) {
    Q_UNUSED(entry);
    const QString current=QCoreApplication::applicationVersion();
    auto* watcher=new QFutureWatcher<Release>(this);
    connect(watcher,&QFutureWatcher<Release>::finished,this,[this,watcher,current]{
        const Release release=watcher->result();
        watcher->deleteLater();
        if(!release.error.isEmpty()) {
            showMessage(tr("System Update"),tr("The update check failed:\n%1").arg(release.error),MessageKind::Page);
        } else if(release.tag.isEmpty()) {
            showMessage(tr("System Update"),tr("No release is available from the update server."),MessageKind::Page);
        } else if(!xmb::newerVersion(current,release.tag,nullptr)) {
            showMessage(tr("System Update"),tr("This system software is up to date (%1).").arg(current),MessageKind::Page);
        } else {
            // A newer release exists. Download and install only after an explicit confirmation; nothing is automatic.
            const QString installDir=QCoreApplication::applicationDirPath();
            showConfirm(tr("System Update"),
                tr("Version %1 is available (you have %2).%3\n\nDownload and install it?").arg(release.tag,current,
                    release.name.isEmpty()?QString():QStringLiteral("\n\n")+release.name),
                [this,release,installDir]{
                    auto* dl=new QFutureWatcher<QPair<QString,QString>>(this);
                    connect(dl,&QFutureWatcher<QPair<QString,QString>>::finished,this,[this,dl,release]{
                        const auto result=dl->result();
                        dl->deleteLater();
                        if(!result.second.isEmpty()) {
                            showMessage(tr("System Update"),result.second,MessageKind::Page);
                        } else {
                            // The package is staged next to the app; applying it (folder swap) and restarting through
                            // the watchdog is the remaining step, which cannot run while the app is executing.
                            showMessage(tr("System Update"),
                                tr("Version %1 was downloaded and staged at:\n%2\n\nRestart the XMB through the watchdog to apply it.")
                                    .arg(release.tag,result.first),MessageKind::Page);
                        }
                    });
                    dl->setFuture(QtConcurrent::run([release,installDir]{return downloadAndStage(release,installDir);}));
                });
        }
    });
    watcher->setFuture(QtConcurrent::run([]{return xmb::latestRelease();}));
    emit changed();
}
}  // namespace xmb
