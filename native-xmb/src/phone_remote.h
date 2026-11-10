#pragma once

#include <QObject>
#include <QTcpServer>
#include <QUrl>

namespace xmb {
class PhoneRemote : public QObject {
    Q_OBJECT
public:
    explicit PhoneRemote(QObject* parent=nullptr);
    bool setEnabled(bool enabled);
    bool isEnabled() const { return server_.isListening(); }
    QString pairingCode() const { return code_; }
    QUrl url() const;
    QString errorString() const { return server_.errorString(); }
signals:
    void keyRequested(int key);
    void psRequested();
private:
    QTcpServer server_;
    QString code_;
};
}
