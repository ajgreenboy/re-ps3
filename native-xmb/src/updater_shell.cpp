// Settings > System Settings > System Update (PHASES-HTPC D7): check GitHub releases for a newer version and
// report. The check runs on a worker thread (updater.h's synchronous latestRelease). Downloading and installing
// is the next step and is not wired yet; the message says so honestly.
#include "shell.h"
#include "updater.h"
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QtConcurrent>

namespace xmb {
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
            // A newer release exists; downloading/installing (and the confirm-and-restart flow) is D7's next step.
            showMessage(tr("System Update"),
                tr("Version %1 is available (you have %2).%3\n\nDownloading and installing is not wired yet.")
                    .arg(release.tag,current,release.name.isEmpty()?QString():QStringLiteral("\n\n")+release.name),
                MessageKind::Page);
        }
    });
    watcher->setFuture(QtConcurrent::run([]{return xmb::latestRelease();}));
    emit changed();
}
}  // namespace xmb
