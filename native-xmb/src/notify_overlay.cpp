#include "notify_overlay.h"
#include "assets.h"
#include <QGuiApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QScreen>
#include <algorithm>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

namespace xmb {
NotifyOverlay::NotifyOverlay(Assets& assets,const QString& fontFamily):
    QWidget(nullptr,Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::Tool|Qt::WindowDoesNotAcceptFocus),
    assets_(assets),fontFamily_(fontFamily) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void NotifyOverlay::showNotification(const QString& title,const QString& detail,const QString& icon) {
    if(isVisible() && title_==title && detail_==detail && icon_==icon)return;
    title_=title;detail_=detail;icon_=icon;
    QScreen* screen=QGuiApplication::primaryScreen();
#ifdef Q_OS_WIN
    const HWND foreground=GetForegroundWindow();
    RECT bounds{};
    if(foreground && GetWindowRect(foreground,&bounds)) {
        const QPoint center((bounds.left+bounds.right)/2,(bounds.top+bounds.bottom)/2);
        if(QScreen* found=QGuiApplication::screenAt(center))screen=found;
    }
#endif
    if(!screen)return;
    const QRect area=screen->geometry();
    // Scale the 1920x1080 XMB coordinates to the target screen, including its 24 px right margin.
    const qreal sx=area.width()/1920.0,sy=area.height()/1080.0;
    const int width=qRound(400*sx),height=qRound((detail.isEmpty()?58:84)*sy);
    setGeometry(area.right()+1-qRound(24*sx)-width,area.top()+qRound(145*sy),width,height);
#ifdef Q_OS_WIN
    const HWND hwnd=reinterpret_cast<HWND>(winId());
    const LONG_PTR styles=GetWindowLongPtr(hwnd,GWL_EXSTYLE);
    SetWindowLongPtr(hwnd,GWL_EXSTYLE,styles|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT|WS_EX_LAYERED);
    SetWindowPos(hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_FRAMECHANGED);
#endif
    show();
    update();
}

void NotifyOverlay::hideNotification() {
    hide();title_.clear();detail_.clear();icon_.clear();
}

void NotifyOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const qreal height=detail_.isEmpty()?58.:84.;
    p.scale(width()/400.0,this->height()/height);
    const QRectF plaque(0,0,400,height);
    const QImage background=assets_.bestImage(QStringLiteral("system_plugin"),QStringLiteral("tex_notification"));
    if(!background.isNull()) {
        const qreal i=std::min<qreal>(16.,std::min(background.width(),background.height())/2.);
        const qreal sx[]={0.,i,background.width()-i,qreal(background.width())};
        const qreal sy[]={0.,i,background.height()-i,qreal(background.height())};
        const qreal dx[]={0.,i,400.-i,400.},dy[]={0.,i,height-i,height};
        p.save();p.setOpacity(.94);
        for(int row=0;row<3;++row)for(int col=0;col<3;++col)
            p.drawImage(QRectF(dx[col],dy[row],dx[col+1]-dx[col],dy[row+1]-dy[row]),background,
                QRectF(sx[col],sy[row],sx[col+1]-sx[col],sy[row+1]-sy[row]));
        p.restore();
    } else {
        p.setPen(Qt::NoPen);p.setBrush(QColor(20,20,20,220));p.drawRoundedRect(plaque,12,12);
    }
    const QImage image=assets_.bestImage(QStringLiteral("system_plugin"),icon_);
    if(!image.isNull()) {
        const qreal side=height-12.;
        p.drawImage(QRectF(13.,(height-side)/2.,side,side),image);
    }
    const qreal left=image.isNull()?16.:13.+height-12.+14.;
    const qreal textWidth=400.-16.-left;
    QFont titleFont(fontFamily_);titleFont.setPixelSize(22);
    const QString title=QFontMetrics(titleFont).elidedText(title_,Qt::ElideRight,int(textWidth));
    const auto label=[&](const QRectF& rect,const QString& text,int size,const QColor& colour) {
        QFont font(fontFamily_);font.setPixelSize(size);font.setStyleStrategy(QFont::PreferAntialias);
        font.setHintingPreference(QFont::PreferNoHinting);p.setFont(font);
        p.setPen(QColor(0,0,0,38));p.drawText(rect.translated(1,1),Qt::AlignLeft|Qt::AlignVCenter,text);
        p.setPen(colour);p.drawText(rect,Qt::AlignLeft|Qt::AlignVCenter,text);
    };
    if(!detail_.isEmpty()) {
        label(QRectF(left,10.,textWidth,28.),title,22,QColor(245,245,245));
        label(QRectF(left,36.,textWidth,24.),detail_,18,QColor(206,206,206));
    } else label(QRectF(left,8.,textWidth,height-16.),title,22,QColor(245,245,245));
}
}
