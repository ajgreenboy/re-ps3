#pragma once
#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QVector>
#include "desktop_pad.h"
#include <functional>
class QMainWindow;
class QMenu;
class QScreen;
class QSystemTrayIcon;
class QAction;
class QTimer;

// Single instance (PHASES-HTPC D2) and Desktop Mode (D1); design and measurements in audit/DESKTOP-MODE.md.
namespace xmb {
class Shell;
class View;

// ---- Single instance ---------------------------------------------------------------------------------
// One XMB per user per data directory. A second launch hands its command to the running one over a local socket
// (a named pipe on Windows, reachable by the owning user only) and exits. Only real interactive/kiosk runs take part:
// main.cpp never calls any of this for report modes or --script runs, and XMB_SINGLE_INSTANCE=0 turns it off.
enum class InstanceCommand { Show, Desktop, Quit };
const char* instanceCommandName(InstanceCommand command);
bool singleInstanceEnabled();                         // false when XMB_SINGLE_INSTANCE=0
QString instanceServerName(const QString& dataDir);   // per user, per data directory
// 0 = delivered and acknowledged, 1 = connected but not acknowledged within the timeout, -1 = no instance is listening.
int sendInstanceCommand(const QString& serverName,InstanceCommand command,int timeoutMs);

class InstanceServer final : public QObject {
    Q_OBJECT
public:
    using Handler=std::function<void(InstanceCommand command)>;
    explicit InstanceServer(QObject* parent=nullptr);
    bool listen(const QString& name,QString* error);
    // Commands that arrive before the handler exists (the XMB is still starting) are kept and delivered here, in order.
    void setHandler(Handler handler);
private:
    void onConnection();
    void dispatch(InstanceCommand command);
    QLocalServer server_;
    Handler handler_;
    QVector<InstanceCommand> pending_;
};

// ---- Desktop Mode ------------------------------------------------------------------------------------
// "Switch to Desktop" (INFERRED: the firmware has no such row). The kiosk window is withdrawn, the frame loop and the
// 16 ms controller timer stop, and the process stays alive so returning is instant. Ways back: the Guide/PS button on any
// pad (the watcher keeps polling at a low rate), the tray icon, a global hotkey (registered only while hidden), or a second
// launch. Music keeps playing when desktop/keepMusic is on (the default); video always pauses.
class DesktopMode final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    DesktopMode(QMainWindow* window,View* view,Shell* shell,bool kiosk,QScreen* kioskScreen,QObject* parent=nullptr);
    ~DesktopMode() override;
    bool active() const { return active_; }
    void enter(const QString& source);
    void leave(const QString& source);
    // A second launch with no Desktop Mode to leave: bring a visible XMB to the front.
    void showForeground(const QString& source);
    bool nativeEventFilter(const QByteArray& type,void* message,qintptr* result) override;
    // "Ctrl+Alt+Home" -> Win32 modifier flags and virtual key; false when the text is not in the grammar the preference accepts.
    static bool parseHotkey(const QString& text,unsigned* modifiers,unsigned* virtualKey);
signals:
    void exitRequested(const QString& source);
    void returnedToXmb(const QString& source);   // D9: the window is back (HDMI-CEC TV wake hooks here)
private:
    void ensureTray();
    bool registerHotkey();
    void unregisterHotkey();
    void probeForeground(qint64 returnedAt);
    void updateTrayTip();
    QString selection() const;
    QMainWindow* window_;
    View* view_;
    Shell* shell_;
    bool kiosk_;
    QPointer<QScreen> kioskScreen_;
    QPointer<QScreen> screen_;
    QSystemTrayIcon* tray_=nullptr;
    QMenu* menu_=nullptr;
    QAction* padAction_=nullptr;
    desktoppad::DesktopPadDriver* pad_=nullptr;
    QTimer* idleReport_=nullptr;
    QElapsedTimer clock_;
    bool active_=false;
    bool hotkeyRegistered_=false;
    bool wasFullScreen_=false,wasMaximized_=false;
    QRect normalGeometry_;
    QString selectionAtEnter_;
    qint64 enteredAt_=0,framesAtEnter_=0,reportAt_=0,reportFrames_=0;
    int generation_=0;
};
}
