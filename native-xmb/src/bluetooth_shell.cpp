// Accessory Settings > Bluetooth Devices (PHASES-HTPC D10): list and pair Bluetooth controllers and headsets.
// The list runs on a worker thread (bluetooth_client.h's WinRT calls block). Pairing asks the user to put the
// device in pairing mode and is gated behind a confirmation and a dry-run override.
#include "shell.h"
#include "bluetooth_client.h"
#include <QtConcurrent>
#include <QFutureWatcher>

namespace xmb {
void Shell::activateBluetooth(const Entry& entry) {
    switch(entry.action) {
    case Action::BluetoothApp: {
        auto* watcher=new QFutureWatcher<QPair<QVector<BluetoothDevice>,QString>>(this);
        connect(watcher,&QFutureWatcher<QPair<QVector<BluetoothDevice>,QString>>::finished,this,[this,watcher]{
            const auto result=watcher->result();
            watcher->deleteLater();
            if(!result.second.isEmpty()) {
                showMessage(tr("Bluetooth Devices"),result.second,MessageKind::Page);
            } else if(result.first.isEmpty()) {
                showMessage(tr("Bluetooth Devices"),tr("No Bluetooth devices were found."),MessageKind::Page);
            } else {
                if(pages_.size()>=32)return;
                Page page;page.title=tr("Bluetooth Devices");
                for(const auto& dev:result.first) {
                    Entry row;
                    row.id=QStringLiteral("bt:")+dev.address;row.title=dev.name;
                    row.action=Action::BluetoothPair;row.value=dev.address;
                    row.detail=dev.paired?tr("Paired"):tr("Not paired - select to pair");
                    page.entries.push_back(std::move(row));
                }
                pages_.push_back(std::move(page));
            }
            emit changed();
        });
        watcher->setFuture(QtConcurrent::run([]{
            QString error;const auto devs=xmb::listBluetooth(&error);
            return qMakePair(devs,error);
        }));
        emit changed();
        break;
    }
    case Action::BluetoothPair: {
        // Pairing a controller needs it in pairing mode, so say so and ask before touching the system. The dry-run
        // guard (bluetooth_client.cpp) means a dev build never actually pairs.
        showConfirm(tr("Pair %1?").arg(entry.title),
            tr("Put the controller or headset in pairing mode, then confirm."),[this,entry]{
                auto* watcher=new QFutureWatcher<QString>(this);
                connect(watcher,&QFutureWatcher<QString>::finished,this,[this,watcher,entry]{
                    const QString error=watcher->result();
                    watcher->deleteLater();
                    if(error.isEmpty())notify(tr("Bluetooth"),tr("Pairing %1 was accepted.").arg(entry.title));
                    else showMessage(tr("Bluetooth"),tr("Pairing %1 failed:\n%2").arg(entry.title,error),MessageKind::Page);
                    emit changed();
                });
                watcher->setFuture(QtConcurrent::run([entry]{QString error;xmb::pairBluetooth(entry.value,&error);return error;}));
            });
        break;
    }
    default:break;
    }
}
}  // namespace xmb
