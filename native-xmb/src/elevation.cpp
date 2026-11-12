#include "elevation.h"
#include <QCoreApplication>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

namespace xmb::elevation {
const char* name(State state) {
    switch(state) {
    case State::NotElevated:return "not-elevated";
    case State::Elevated:return "elevated";
    case State::Unknown:break;
    }
    return "unknown";
}

State processState(quint32 pid) {
#ifdef Q_OS_WIN
    if(!pid)return State::Unknown;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(pid));
    if(!process)return State::Unknown;
    State result=State::Unknown;
    HANDLE token=nullptr;
    if(OpenProcessToken(process,TOKEN_QUERY,&token)) {
        TOKEN_ELEVATION elevation{};
        DWORD size=0;
        if(GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&size))
            result=elevation.TokenIsElevated?State::Elevated:State::NotElevated;
        CloseHandle(token);
    }
    CloseHandle(process);
    return result;
#else
    Q_UNUSED(pid);
    return State::Unknown;
#endif
}

State windowState(quintptr hwnd) {
#ifdef Q_OS_WIN
    DWORD pid=0;
    if(!hwnd || !IsWindow(reinterpret_cast<HWND>(hwnd)))return State::Unknown;
    GetWindowThreadProcessId(reinterpret_cast<HWND>(hwnd),&pid);
    return processState(quint32(pid));
#else
    Q_UNUSED(hwnd);
    return State::Unknown;
#endif
}

State selfState() {
#ifdef Q_OS_WIN
    static const State cached=processState(quint32(GetCurrentProcessId()));
    return cached;
#else
    return State::Unknown;
#endif
}

bool blocksDelivery(State target,State self) { return target==State::Elevated && self!=State::Elevated; }
bool blocksDelivery(State target) { return blocksDelivery(target,selfState()); }

QString fullMessage() {
    return QCoreApplication::translate("xmb::elevation","The window in front is running as administrator, so the controller cannot drive it.");
}
QString noticeTitle() { return QCoreApplication::translate("xmb::elevation","Administrator window"); }
QString noticeDetail() { return QCoreApplication::translate("xmb::elevation","The controller cannot drive it."); }
}
