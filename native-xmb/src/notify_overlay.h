#pragma once

#include <QWidget>

namespace xmb {
class Assets;

class NotifyOverlay : public QWidget {
public:
    NotifyOverlay(Assets& assets,const QString& fontFamily);
    void showNotification(const QString& title,const QString& detail,const QString& icon);
    void hideNotification();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Assets& assets_;
    QString fontFamily_,title_,detail_,icon_;
};
}
