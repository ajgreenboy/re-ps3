#include "desktop_mode.h"
#include "ps_watcher.h"
#include "shell.h"
#include "view.h"
#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QLocalSocket>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWindow>
#include <memory>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

namespace xmb {
// ---- Single instance ---------------------------------------------------------------------------------
const char* instanceCommandName(InstanceCommand command) {
    switch(command) {
    case InstanceCommand::Show:return "show";
    case InstanceCommand::Desktop:return "desktop";
    case InstanceCommand::Quit:return "quit";
    }
    return "show";
}

bool singleInstanceEnabled() {
    return qEnvironmentVariable("XMB_SINGLE_INSTANCE")!=QLatin1String("0");
}

QString instanceServerName(const QString& dataDir) {
    QString user=qEnvironmentVariable("USERNAME");
    if(user.isEmpty())user=qEnvironmentVariable("USER");
    user.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")),QStringLiteral("_"));
    // The data directory is part of the identity: two XMBs on one data directory would fight over its stores, while a
    // scratch directory (a test, a second profile) is a separate instance by design.
    const QString canonical=QDir::cleanPath(QFileInfo(dataDir).absoluteFilePath()).toLower();
    const QString hash=QString::fromLatin1(QCryptographicHash::hash(canonical.toUtf8(),QCryptographicHash::Sha1).toHex().left(10));
    return QStringLiteral("re-ps3-native-xmb-%1-%2").arg(user,hash);
}

int sendInstanceCommand(const QString& serverName,InstanceCommand command,int timeoutMs) {
#ifdef Q_OS_WIN
    // This process was just started by the user, so it may hand the foreground right to the running XMB; without it
    // Windows would keep a returning XMB behind whatever the user has open.
    AllowSetForegroundWindow(ASFW_ANY);
#endif
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if(!socket.waitForConnected(500))return -1;
    socket.write(QByteArray(instanceCommandName(command))+"\n");
    socket.flush();
    socket.waitForBytesWritten(timeoutMs);
    QByteArray reply;
    QElapsedTimer waited;waited.start();
    while(!reply.contains('\n') && waited.elapsed()<timeoutMs) {
        if(socket.waitForReadyRead(200))reply+=socket.readAll();
        else if(socket.state()!=QLocalSocket::ConnectedState){reply+=socket.readAll();break;}
    }
    return reply.startsWith("ok")?0:1;
}

InstanceServer::InstanceServer(QObject* parent):QObject(parent) {
    connect(&server_,&QLocalServer::newConnection,this,&InstanceServer::onConnection);
}

bool InstanceServer::listen(const QString& name,QString* error) {
    server_.setSocketOptions(QLocalServer::UserAccessOption);   // the owning user only
    if(server_.listen(name))return true;
    if(error)*error=server_.errorString();
    return false;
}

void InstanceServer::setHandler(Handler handler) {
    handler_=std::move(handler);
    const auto queued=std::exchange(pending_,{});
    for(const auto command:queued)dispatch(command);
}

void InstanceServer::dispatch(InstanceCommand command) {
    if(handler_)handler_(command);
    else {pending_.push_back(command);qInfo().noquote()<<QString("instance: %1 queued until the XMB has started").arg(instanceCommandName(command));}
}

void InstanceServer::onConnection() {
    while(QLocalSocket* socket=server_.nextPendingConnection()) {
        auto buffer=std::make_shared<QByteArray>();
        connect(socket,&QLocalSocket::readyRead,this,[this,socket,buffer]{
            *buffer+=socket->readAll();
            const int end=buffer->indexOf('\n');
            if(end<0 && buffer->size()<64)return;
            const QByteArray word=buffer->left(end<0?buffer->size():end).trimmed();
            buffer->clear();
            disconnect(socket,&QLocalSocket::readyRead,this,nullptr);
            InstanceCommand command=InstanceCommand::Show;
            bool known=true;
            if(word=="show")command=InstanceCommand::Show;
            else if(word=="desktop")command=InstanceCommand::Desktop;
            else if(word=="quit")command=InstanceCommand::Quit;
            else known=false;
            socket->write(known?"ok\n":"bad\n");
            socket->flush();
            socket->disconnectFromServer();
            if(known){qInfo().noquote()<<QString("instance: command %1 received from a second launch").arg(QString::fromLatin1(word));dispatch(command);}
            else qWarning().noquote()<<QString("instance: ignored unknown command \"%1\"").arg(QString::fromLatin1(word.left(32)));
        });
        connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
    }
}

// ---- Desktop Mode ------------------------------------------------------------------------------------
namespace {
constexpr int kHotkeyId=0x5842;      // "XB": thread-local, so no clash with another process or window
constexpr int kHiddenPollMs=100;     // PsWatcher while hidden (20 ms when shown)
constexpr int kNormalPollMs=20;
constexpr int kHiddenControllerMs=250;

#ifdef Q_OS_WIN
QString foregroundName() {
    const HWND window=GetForegroundWindow();
    if(!window)return QStringLiteral("none");
    wchar_t title[96]{};GetWindowTextW(window,title,95);
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    return QString("pid=%1 title=\"%2\"").arg(pid).arg(QString::fromWCharArray(title));
}
#else
QString foregroundName(){return QStringLiteral("n/a");}
#endif

// The tray icon is drawn here, from nothing: no firmware asset, no file. A rounded dark tile with the XMB's cross-bar.
QIcon trayIcon() {
    QPixmap pixmap(64,64);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x14,0x1c,0x30));
    p.drawRoundedRect(QRectF(2,2,60,60),12,12);
    p.setBrush(QColor(0xe8,0xee,0xff));
    p.drawRoundedRect(QRectF(10,28,44,8),4,4);
    p.drawRoundedRect(QRectF(20,12,8,40),4,4);
    p.setBrush(QColor(0x5a,0x8c,0xff));
    p.drawEllipse(QPointF(44,22),6,6);
    p.end();
    return QIcon(pixmap);
}
}

bool DesktopMode::parseHotkey(const QString& text,unsigned* modifiers,unsigned* virtualKey) {
#ifdef Q_OS_WIN
    const QStringList parts=text.split(QLatin1Char('+'));
    if(parts.size()<2)return false;
    unsigned mods=0;
    for(int i=0;i<parts.size()-1;++i) {
        const QString& part=parts[i];
        if(part==QLatin1String("Ctrl"))mods|=MOD_CONTROL;
        else if(part==QLatin1String("Alt"))mods|=MOD_ALT;
        else if(part==QLatin1String("Shift"))mods|=MOD_SHIFT;
        else if(part==QLatin1String("Win"))mods|=MOD_WIN;
        else return false;
    }
    if(!mods)return false;
    const QString& key=parts.last();
    unsigned vk=0;
    static const QHash<QString,unsigned> named{{"Home",VK_HOME},{"End",VK_END},{"Insert",VK_INSERT},{"Delete",VK_DELETE},{"PageUp",VK_PRIOR},{"PageDown",VK_NEXT}};
    if(named.contains(key))vk=named.value(key);
    else if(key.size()==1 && ((key[0]>=QLatin1Char('A') && key[0]<=QLatin1Char('Z')) || (key[0]>=QLatin1Char('0') && key[0]<=QLatin1Char('9'))))vk=unsigned(key[0].unicode());
    else if(key.startsWith(QLatin1Char('F'))) {
        bool ok=false;const int n=key.mid(1).toInt(&ok);
        if(ok && n>=1 && n<=24)vk=VK_F1+unsigned(n-1);
    }
    if(!vk)return false;
    if(modifiers)*modifiers=mods;
    if(virtualKey)*virtualKey=vk;
    return true;
#else
    Q_UNUSED(text);Q_UNUSED(modifiers);Q_UNUSED(virtualKey);
    return false;
#endif
}

DesktopMode::DesktopMode(QMainWindow* window,View* view,Shell* shell,bool kiosk,QScreen* kioskScreen,QObject* parent)
    :QObject(parent),window_(window),view_(view),shell_(shell),kiosk_(kiosk),kioskScreen_(kioskScreen) {
    clock_.start();
    qApp->installNativeEventFilter(this);
    // Controller-driven desktop (desktop_pad.h): live only between enter() and leave().
    pad_=new desktoppad::DesktopPadDriver({
        [this]{return shell_->desktopControllerMouse();},
        [this]{return shell_->desktopPointerSpeed();},
        [this]{return shell_->titleRunning();},
        [this]{return window_->isVisible();}},this);
    connect(pad_,&desktoppad::DesktopPadDriver::idleReasonChanged,this,[this]{updateTrayTip();});
    // The Guide/PS button on any pad. The watcher is the one that already reads the HID/XInput Guide bit independent of
    // window focus; View's own handlers ignore a press while the window is hidden (not actionable), so this is the only
    // listener that acts. A press within 600 ms of entering is the tail of the press that chose the row.
    connect(&view_->psWatcher(),&PsWatcher::pressed,this,[this]{
        if(active_ && clock_.elapsed()-enteredAt_>600)leave(QStringLiteral("Guide button"));
    });
}

DesktopMode::~DesktopMode() {
    unregisterHotkey();
    if(tray_)tray_->hide();
    delete menu_;
}

QString DesktopMode::selection() const {
    const auto entries=shell_->entries();const int selection=shell_->selection();
    return QString("%1/%2/%3/%4").arg(shell_->categories()[shell_->category()].id).arg(shell_->depth()).arg(selection)
        .arg(selection>=0 && selection<entries.size()?entries[selection].id:QString{});
}

void DesktopMode::ensureTray() {
    if(tray_)return;
    menu_=new QMenu;
    auto* back=menu_->addAction(QObject::tr("Return to XMB"));
    padAction_=menu_->addAction(QObject::tr("Controller mouse and keyboard"));
    padAction_->setCheckable(true);
    connect(menu_,&QMenu::aboutToShow,this,[this]{padAction_->setChecked(shell_->desktopControllerMouse());});
    connect(padAction_,&QAction::triggered,this,[this](bool on){shell_->setDesktopControllerMouse(on);updateTrayTip();});
    auto* quit=menu_->addAction(QObject::tr("Exit XMB"));
    connect(back,&QAction::triggered,this,[this]{leave(QStringLiteral("tray menu"));});
    connect(quit,&QAction::triggered,this,[this]{emit exitRequested(QStringLiteral("tray menu"));});
    tray_=new QSystemTrayIcon(trayIcon(),this);
    tray_->setToolTip(QObject::tr("XMB (desktop mode)"));
    tray_->setContextMenu(menu_);
    connect(tray_,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason reason){
        if(reason==QSystemTrayIcon::Trigger || reason==QSystemTrayIcon::DoubleClick)leave(QStringLiteral("tray icon"));
    });
}

void DesktopMode::updateTrayTip() {
    if(!tray_)return;
    QString pad;
    if(!shell_->desktopControllerMouse())pad=QObject::tr("controller mouse off");
    else if(pad_->active() && !pad_->idleReason().isEmpty())pad=QObject::tr("controller mouse paused: %1").arg(pad_->idleReason());
    else pad=QObject::tr("controller mouse on");
    tray_->setToolTip(QObject::tr("XMB (desktop mode) - %1").arg(pad));
}

bool DesktopMode::registerHotkey() {
#ifdef Q_OS_WIN
    unsigned mods=0,vk=0;
    QString text=shell_->desktopHotkey();
    if(!parseHotkey(text,&mods,&vk)) {
        qWarning().noquote()<<QString("desktop: hotkey \"%1\" is not valid; using Ctrl+Alt+Home").arg(text);
        text=QStringLiteral("Ctrl+Alt+Home");parseHotkey(text,&mods,&vk);
    }
    // Thread-wide (no window): WM_HOTKEY is posted to this thread's queue and Qt hands it to nativeEventFilter. The
    // registration belongs to the process and ends with it.
    if(RegisterHotKey(nullptr,kHotkeyId,mods|MOD_NOREPEAT,vk)) {
        hotkeyRegistered_=true;
        qInfo().noquote()<<QString("desktop: hotkey %1 registered (vk=0x%2 mods=0x%3)").arg(text).arg(vk,0,16).arg(mods,0,16);
        return true;
    }
    qWarning().noquote()<<QString("desktop: hotkey %1 could not be registered (error %2); the Guide button, the tray icon and a second launch still work")
        .arg(text).arg(GetLastError());
#endif
    return false;
}

void DesktopMode::unregisterHotkey() {
#ifdef Q_OS_WIN
    if(!hotkeyRegistered_)return;
    UnregisterHotKey(nullptr,kHotkeyId);
    hotkeyRegistered_=false;
    qInfo()<<"desktop: hotkey unregistered";
#endif
}

bool DesktopMode::nativeEventFilter(const QByteArray& type,void* message,qintptr*) {
#ifdef Q_OS_WIN
    if(!hotkeyRegistered_ || (type!="windows_generic_MSG" && type!="windows_dispatcher_MSG"))return false;
    const MSG* msg=static_cast<MSG*>(message);
    if(msg->message==WM_HOTKEY && msg->wParam==WPARAM(kHotkeyId)) {
        // Posted, not nested: leave() runs from the event loop, not inside the message dispatch.
        QTimer::singleShot(0,this,[this]{if(active_)leave(QStringLiteral("hotkey"));});
        return true;
    }
#else
    Q_UNUSED(type);Q_UNUSED(message);
#endif
    return false;
}

void DesktopMode::enter(const QString& source) {
    if(active_){qInfo().noquote()<<QString("desktop: enter via %1 ignored (already in desktop mode)").arg(source);return;}
    if(shell_->psMenuOpen())shell_->psMenuClose();
    wasFullScreen_=window_->isFullScreen();
    wasMaximized_=window_->isMaximized();
    normalGeometry_=window_->normalGeometry();
    screen_=window_->screen();
    selectionAtEnter_=selection();
    const QString media=shell_->pauseMediaForDesktop();
    qInfo().noquote()<<QString("desktop: enter via %1 (fullscreen=%2 kiosk=%3 screen=%4 selection=%5 media=\"%6\")")
        .arg(source).arg(wasFullScreen_?1:0).arg(kiosk_?1:0).arg(screen_?screen_->name():QStringLiteral("?"),selectionAtEnter_,media);
    active_=true;
    enteredAt_=clock_.elapsed();
    shell_->setDesktopMode(true);
    setDesktopRenderPause(true);
    view_->setSuspended(true);
    view_->psWatcher().setPollInterval(kHiddenPollMs);
    ensureTray();
    if(QSystemTrayIcon::isSystemTrayAvailable())tray_->show();
    else qWarning()<<"desktop: no system tray is available; the Guide button, the hotkey and a second launch bring the XMB back";
    registerHotkey();
    window_->hide();
    updateTrayTip();
    pad_->setActive(true);
    framesAtEnter_=reportFrames_=view_->framesSwapped();
    reportAt_=enteredAt_;
    if(!idleReport_) {
        idleReport_=new QTimer(this);
        idleReport_->setInterval(2000);
        connect(idleReport_,&QTimer::timeout,this,[this]{
            const qint64 now=clock_.elapsed(),frames=view_->framesSwapped();
            qInfo().noquote()<<QString("desktop: idle frames=%1 in last %2 ms (hidden %3 ms, controller poll %4 ms, guide poll %5 ms)")
                .arg(frames-reportFrames_).arg(now-reportAt_).arg(now-enteredAt_).arg(kHiddenControllerMs).arg(view_->psWatcher().pollInterval());
            reportAt_=now;reportFrames_=frames;
        });
    }
    idleReport_->start();
    ++generation_;
}

void DesktopMode::leave(const QString& source) {
    if(!active_)return;
    const qint64 returnedAt=clock_.elapsed();
    qInfo().noquote()<<QString("desktop: return via %1 after %2 ms hidden (frames while hidden=%3)")
        .arg(source).arg(returnedAt-enteredAt_).arg(view_->framesSwapped()-framesAtEnter_);
    active_=false;
    ++generation_;
    pad_->setActive(false);   // releases anything it holds before the window reappears
    idleReport_->stop();
    unregisterHotkey();
    if(tray_)tray_->hide();
    // Same display, same state as before: kiosk goes back to a frameless fullscreen on its screen; a normal window to
    // whatever it was (fullscreen, maximised or its own rectangle).
    QScreen* screen=screen_?screen_.data():(kioskScreen_?kioskScreen_.data():QGuiApplication::primaryScreen());
    if(kiosk_) {
        window_->setScreen(screen);
        window_->setGeometry(screen->geometry());
        window_->showFullScreen();
    } else if(wasFullScreen_) {
        window_->setScreen(screen);
        window_->showFullScreen();
    } else if(wasMaximized_)window_->showMaximized();
    else {
        window_->showNormal();
        if(normalGeometry_.isValid())window_->setGeometry(normalGeometry_);
    }
    setDesktopRenderPause(false);
    view_->setSuspended(false);
    view_->psWatcher().setPollInterval(kNormalPollMs);
    shell_->setDesktopMode(false);
    bringWindowForward(window_,view_);
    view_->update();
    qInfo().noquote()<<QString("desktop: window shown fullscreen=%1 screen=%2 geometry=%3x%4+%5+%6 selection=%7 kept=%8")
        .arg(window_->isFullScreen()?1:0).arg(window_->screen()?window_->screen()->name():QStringLiteral("?"))
        .arg(window_->width()).arg(window_->height()).arg(window_->x()).arg(window_->y()).arg(selection()).arg(selection()==selectionAtEnter_?1:0);
    probeForeground(returnedAt);
    emit returnedToXmb(source);
}

void DesktopMode::probeForeground(qint64 returnedAt) {
    // The foreground can be refused for a moment (foreground lock); keep re-asserting at ~150, ~500 and ~1500 ms, the
    // same schedule TitleSession uses after a title ends, and log how long it took.
    const int generation=generation_;
    auto* probe=new QTimer(this);probe->setInterval(20);
    auto attempts=std::make_shared<int>(0);
    connect(probe,&QTimer::timeout,this,[this,probe,generation,returnedAt,attempts]{
        const qint64 elapsed=clock_.elapsed()-returnedAt;
        if(generation!=generation_){probe->stop();probe->deleteLater();return;}
#ifdef Q_OS_WIN
        const bool front=GetForegroundWindow()==reinterpret_cast<HWND>(window_->winId());
#else
        const bool front=window_->isActiveWindow();
#endif
        if(front && QApplication::focusWidget()==view_) {
            qInfo().noquote()<<QString("desktop: XMB foreground+focus after %1 ms fullscreen=%2").arg(elapsed).arg(window_->isFullScreen()?1:0);
            probe->stop();probe->deleteLater();return;
        }
        if(elapsed>=3000) {
            qWarning().noquote()<<QString("desktop: XMB did not regain the foreground within 3000 ms (foreground=%1)").arg(foregroundName());
            probe->stop();probe->deleteLater();return;
        }
        if((elapsed>=150 && *attempts==0) || (elapsed>=500 && *attempts==1) || (elapsed>=1500 && *attempts==2)) {
            ++*attempts;
            qInfo().noquote()<<QString("desktop: foreground retry %1 after %2 ms (foreground=%3)").arg(*attempts).arg(elapsed).arg(foregroundName());
            bringWindowForward(window_,view_);
        }
    });
    probe->start();
}

void DesktopMode::showForeground(const QString& source) {
    if(active_){leave(source);return;}
    qInfo().noquote()<<QString("desktop: %1 while the XMB is showing; bringing it to the front").arg(source);
    if(window_->isMinimized())window_->showNormal();
    if(kiosk_ && !window_->isFullScreen())window_->showFullScreen();
    bringWindowForward(window_,view_);
}
}
