// Settings > System Settings > System Update (PHASES-HTPC D7): check GitHub releases for a newer version, then
// download and install it on confirmation. The check and download run on a worker thread.
#include "shell.h"
#include "updater.h"
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QSaveFile>
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
                    connect(dl,&QFutureWatcher<QPair<QString,QString>>::finished,this,[this,dl,release,installDir]{
                        const auto result=dl->result();
                        dl->deleteLater();
                        if(!result.second.isEmpty()) {
                            showMessage(tr("System Update"),result.second,MessageKind::Page);
                        } else {
                            showConfirm(tr("System Update"),
                                tr("Version %1 was staged at:\n%2\n\nApply it now and restart through the watchdog? Files stored inside the installation, including data and configuration, will remain in the .old folder. The previous .old folder will be removed.").arg(release.tag,result.first),
                                [this,installDir,staged=result.first]{
                                    if(qEnvironmentVariable("XMB_UPDATER_DRY_RUN")==QStringLiteral("1")) {
                                        qInfo().noquote()<<"updater: would apply staged path"<<staged<<"and restart";
                                        showMessage(tr("System Update"),tr("Dry run: update was not applied."),MessageKind::Page);
                                        return;
                                    }
                                    QSaveFile marker(staged+QStringLiteral(".pending"));
                                    const QByteArray data=installDir.toUtf8();
                                    if(!marker.open(QIODevice::WriteOnly) || marker.write(data)!=data.size() || !marker.commit()) {
                                        showMessage(tr("System Update"),tr("Could not write the pending update marker: %1").arg(marker.errorString()),MessageKind::Page);
                                        return;
                                    }
                                    emit requestUpdateRestart();
                                });
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
