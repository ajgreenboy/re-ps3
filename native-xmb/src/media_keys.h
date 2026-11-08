#pragma once
#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>
#include <QString>
#include <Qt>
#include <functional>

namespace xmb {
// Remote and media-key input (PHASES H10, audit/INPUT.md). A couch remote reaches a Windows PC in one of three
// forms, and all three land on the same small set of XMB actions:
//   - WM_APPCOMMAND (an MCE/IR receiver, a Bluetooth HID remote, a keyboard's media row via DefWindowProc);
//   - WM_KEYDOWN with VK_MEDIA_* / VK_BROWSER_* (most keyboards and Flirc-style receivers);
//   - a Qt key event (Qt::Key_Media*, Key_Back, Key_HomePage, Key_Info, Key_Menu ...) or the Windows Media Center
//     keyboard shortcuts some remotes send instead (Ctrl+P, Ctrl+D, Ctrl+G ...).
// Volume keys are NOT handled here: View's VolumeKeyFilter owns them (audit/VOLUME.md path, unchanged).
enum class RemoteKey {
    None,
    PlayPause, Play, Pause, Stop, Next, Previous, FastForward, Rewind,  // transport: only while media is open
    Back,         // = Circle
    Home,         // = PS button hold: the system menu (a second press closes it)
    Info,         // = Triangle (MCE "More info", the keyboard Menu key)
    ChannelUp,    // = PageUp: ten rows up
    ChannelDown,  // = PageDown: ten rows down
};
const char* remoteKeyName(RemoteKey key);
// Qt key events: the Qt::Key_* a remote or media keyboard produces, plus the Media Center keyboard shortcuts.
RemoteKey remoteKeyForQtKey(int key,Qt::KeyboardModifiers modifiers);
#ifdef Q_OS_WIN
RemoteKey remoteKeyForAppCommand(int command);       // GET_APPCOMMAND_LPARAM value
RemoteKey remoteKeyForVirtualKey(unsigned virtualKey); // VK_MEDIA_*, VK_BROWSER_BACK/HOME, VK_PLAY
#endif

// The native half: WM_APPCOMMAND and the WM_KEYDOWN VK_* form are taken before Qt or DefWindowProc sees them,
// so one press is one action even when a keyboard delivers both forms (the same rule VolumeKeyFilter uses: the
// second form of the same action within 100 ms is the same press). When the callback declines (no media is open,
// another surface owns input) the message is left alone and Windows routes it on as it would without this app.
class MediaKeyFilter final:public QAbstractNativeEventFilter {
public:
    using Handler=std::function<bool(RemoteKey,const QString& source)>;
    explicit MediaKeyFilter(Handler handler);
    bool nativeEventFilter(const QByteArray& type,void* message,qintptr* result) override;
private:
    Handler handler_;
    QElapsedTimer clock_;
    RemoteKey lastKey_=RemoteKey::None;
    qint64 lastAt_=-1000;
    unsigned swallowedKeyUp_=0;   // the VK whose WM_KEYDOWN was taken: its WM_KEYUP is taken too
};
}
