#include "media_keys.h"
#include <QDebug>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

namespace xmb {
const char* remoteKeyName(RemoteKey key) {
    switch(key) {
    case RemoteKey::PlayPause:return "play/pause";
    case RemoteKey::Play:return "play";
    case RemoteKey::Pause:return "pause";
    case RemoteKey::Stop:return "stop";
    case RemoteKey::Next:return "next";
    case RemoteKey::Previous:return "previous";
    case RemoteKey::FastForward:return "fast-forward";
    case RemoteKey::Rewind:return "rewind";
    case RemoteKey::Back:return "back";
    case RemoteKey::Home:return "home";
    case RemoteKey::Info:return "info";
    case RemoteKey::ChannelUp:return "channel-up";
    case RemoteKey::ChannelDown:return "channel-down";
    case RemoteKey::None:break;
    }
    return "none";
}

RemoteKey remoteKeyForQtKey(int key,Qt::KeyboardModifiers modifiers) {
    switch(key) {
    case Qt::Key_MediaTogglePlayPause:return RemoteKey::PlayPause;
    case Qt::Key_MediaPlay:case Qt::Key_Play:return RemoteKey::Play;
    case Qt::Key_MediaPause:return RemoteKey::Pause;
    case Qt::Key_MediaStop:return RemoteKey::Stop;
    case Qt::Key_MediaNext:return RemoteKey::Next;
    case Qt::Key_MediaPrevious:return RemoteKey::Previous;
    case Qt::Key_AudioForward:return RemoteKey::FastForward;
    case Qt::Key_AudioRewind:return RemoteKey::Rewind;
    case Qt::Key_Back:return RemoteKey::Back;
    case Qt::Key_HomePage:case Qt::Key_Guide:return RemoteKey::Home;
    case Qt::Key_Info:case Qt::Key_Menu:case Qt::Key_Context1:return RemoteKey::Info;
    case Qt::Key_ChannelUp:return RemoteKey::ChannelUp;
    case Qt::Key_ChannelDown:return RemoteKey::ChannelDown;
    default:break;
    }
    // Windows Media Center keyboard shortcuts, which some MCE-style remotes and receivers send as keystrokes
    // instead of WM_APPCOMMAND. INFERRED from Media Center's published shortcut list; no such remote was tested.
    // Ctrl alone (no Alt) so Ctrl+Alt+<key> AltGr text entry never matches.
    const auto mods=modifiers&(Qt::ControlModifier|Qt::ShiftModifier|Qt::AltModifier|Qt::MetaModifier);
    const bool ctrl=mods==Qt::ControlModifier,ctrlShift=mods==(Qt::ControlModifier|Qt::ShiftModifier);
    if(ctrl) {
        switch(key) {
        case Qt::Key_P:return RemoteKey::PlayPause;   // Pause/Play
        case Qt::Key_F:return RemoteKey::Next;        // Skip forward
        case Qt::Key_B:return RemoteKey::Previous;    // Skip back
        case Qt::Key_D:return RemoteKey::Info;        // More info / Details
        case Qt::Key_G:return RemoteKey::Home;        // Guide
        default:break;
        }
    }
    if(ctrlShift) {
        switch(key) {
        case Qt::Key_P:return RemoteKey::Play;
        case Qt::Key_S:return RemoteKey::Stop;
        case Qt::Key_F:return RemoteKey::FastForward;
        case Qt::Key_B:return RemoteKey::Rewind;
        default:break;
        }
    }
    return RemoteKey::None;
}

#ifdef Q_OS_WIN
RemoteKey remoteKeyForAppCommand(int command) {
    switch(command) {
    case APPCOMMAND_MEDIA_PLAY_PAUSE:return RemoteKey::PlayPause;
    case APPCOMMAND_MEDIA_PLAY:return RemoteKey::Play;
    case APPCOMMAND_MEDIA_PAUSE:return RemoteKey::Pause;
    case APPCOMMAND_MEDIA_STOP:return RemoteKey::Stop;
    case APPCOMMAND_MEDIA_NEXTTRACK:return RemoteKey::Next;
    case APPCOMMAND_MEDIA_PREVIOUSTRACK:return RemoteKey::Previous;
    case APPCOMMAND_MEDIA_FAST_FORWARD:return RemoteKey::FastForward;
    case APPCOMMAND_MEDIA_REWIND:return RemoteKey::Rewind;
    case APPCOMMAND_MEDIA_CHANNEL_UP:return RemoteKey::ChannelUp;
    case APPCOMMAND_MEDIA_CHANNEL_DOWN:return RemoteKey::ChannelDown;
    case APPCOMMAND_BROWSER_BACKWARD:return RemoteKey::Back;
    case APPCOMMAND_BROWSER_HOME:return RemoteKey::Home;
    default:return RemoteKey::None;
    }
}
RemoteKey remoteKeyForVirtualKey(unsigned virtualKey) {
    switch(virtualKey) {
    case VK_MEDIA_PLAY_PAUSE:return RemoteKey::PlayPause;
    case VK_PLAY:return RemoteKey::Play;
    case VK_MEDIA_STOP:return RemoteKey::Stop;
    case VK_MEDIA_NEXT_TRACK:return RemoteKey::Next;
    case VK_MEDIA_PREV_TRACK:return RemoteKey::Previous;
    case VK_BROWSER_BACK:return RemoteKey::Back;
    case VK_BROWSER_HOME:return RemoteKey::Home;
    default:return RemoteKey::None;
    }
}
#endif

MediaKeyFilter::MediaKeyFilter(Handler handler):handler_(std::move(handler)) { clock_.start(); }

bool MediaKeyFilter::nativeEventFilter(const QByteArray& type,void* message,qintptr* result) {
#ifdef Q_OS_WIN
    if(type!="windows_generic_MSG" && type!="windows_dispatcher_MSG")return false;
    auto* msg=static_cast<MSG*>(message);
    RemoteKey key=RemoteKey::None;
    QString source;
    bool isAppCommand=false;
    if(msg->message==WM_APPCOMMAND) {
        const int command=GET_APPCOMMAND_LPARAM(msg->lParam);
        key=remoteKeyForAppCommand(command);
        if(key==RemoteKey::None)return false;
        const int device=GET_DEVICE_LPARAM(msg->lParam);
        // The mouse's X buttons also arrive as BROWSER_BACKWARD/FORWARD; a mouse Back is still Back.
        source=QString("appcommand %1 (%2)").arg(command)
            .arg(device==FAPPCOMMAND_MOUSE?"mouse":device==FAPPCOMMAND_KEY?"key":"oem");
        isAppCommand=true;
    } else if(msg->message==WM_KEYDOWN || msg->message==WM_SYSKEYDOWN) {
        key=remoteKeyForVirtualKey(unsigned(msg->wParam));
        if(key==RemoteKey::None)return false;
        // Auto-repeat (bit 30: the key was already down) is one held press, not a stream of presses: a held
        // Next must not skip a whole album.
        if(msg->lParam&(LPARAM(1)<<30)) {
            if(result)*result=0;
            return swallowedKeyUp_==unsigned(msg->wParam);
        }
        source=QString("vk 0x%1").arg(unsigned(msg->wParam),2,16,QChar('0'));
    } else if(msg->message==WM_KEYUP || msg->message==WM_SYSKEYUP) {
        if(swallowedKeyUp_ && swallowedKeyUp_==unsigned(msg->wParam)) {
            swallowedKeyUp_=0;
            if(result)*result=0;
            return true;
        }
        return false;
    } else return false;
    const qint64 now=clock_.elapsed();
    if(key==lastKey_ && now-lastAt_<100) {
        if(qEnvironmentVariableIsSet("XMB_INPUT_TRACE"))
            qInfo().noquote()<<QString("remote: trace %1 %2 duplicate of a press %3 ms ago (swallowed)")
                .arg(remoteKeyName(key),source).arg(now-lastAt_);
        if(!isAppCommand)swallowedKeyUp_=unsigned(msg->wParam);
        if(result)*result=isAppCommand?TRUE:0;
        return true;
    }
    if(!handler_(key,source))return false;   // declined: Windows keeps its own handling of the message
    lastKey_=key;lastAt_=now;
    if(!isAppCommand)swallowedKeyUp_=unsigned(msg->wParam);
    // WM_APPCOMMAND: "If an application processes this message, it should return TRUE."
    if(result)*result=isAppCommand?TRUE:0;
    return true;
#else
    Q_UNUSED(type);Q_UNUSED(message);Q_UNUSED(result);
    return false;
#endif
}
}
