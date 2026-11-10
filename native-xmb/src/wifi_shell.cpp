// Network > Wi-Fi Networks (PHASES-HTPC D10): list the host's wireless networks. Connecting (with a passphrase
// prompt) is the next step; the list is read-only and honest about that.
#include "shell.h"
#include "wifi_client.h"
#include <QtConcurrent>
#include <QFutureWatcher>

namespace xmb {
void Shell::initWifi() {
    for(auto& column:categories_) {
        if(column.id!=QLatin1String("network"))continue;
        Entry row;
        row.id=QStringLiteral("wifi");row.title=tr("Wi-Fi Networks");
        row.action=Action::WifiApp;
        row.container=QStringLiteral("explore_category_network");
        row.texture=QStringLiteral("item_tex_plain_folder");row.source=QStringLiteral("native");
        column.entries.push_back(std::move(row));
    }
}
void Shell::activateWifi(const Entry& entry) {
    Q_UNUSED(entry);
    auto* watcher=new QFutureWatcher<QPair<QVector<WifiNetwork>,QString>>(this);
    connect(watcher,&QFutureWatcher<QPair<QVector<WifiNetwork>,QString>>::finished,this,[this,watcher]{
        const auto result=watcher->result();
        watcher->deleteLater();
        if(!result.second.isEmpty()) {
            showMessage(tr("Wi-Fi Networks"),result.second,MessageKind::Page);
        } else if(result.first.isEmpty()) {
            showMessage(tr("Wi-Fi Networks"),tr("No wireless networks were found."),MessageKind::Page);
        } else {
            if(pages_.size()>=32)return;
            Page page;page.title=tr("Wi-Fi Networks");
            for(const auto& net:result.first) {
                Entry row;
                row.id=QStringLiteral("wifi:")+net.ssid;row.title=net.ssid;
                row.action=Action::Information;
                row.detail=net.connected?tr("Connected")
                    :QStringLiteral("%1% (%2)").arg(net.signalQuality).arg(net.auth);
                page.entries.push_back(std::move(row));
            }
            pages_.push_back(std::move(page));
        }
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run([]{
        QString error;const auto nets=xmb::listNetworks(&error);
        return qMakePair(nets,error);
    }));
    emit changed();
}
}  // namespace xmb
