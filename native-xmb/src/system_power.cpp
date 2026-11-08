#include "system_power.h"
#include <QByteArray>
#include <QDebug>
#include <atomic>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <powrprof.h>
#include <reason.h>
#endif

namespace xmb {
namespace {

// -1 = not decided yet (read the environment on first use), 0 = off, 1 = on.
std::atomic<int> dryRunState{-1};

void setError(QString* error,const QString& text) {
    if(error)*error=text;
}

#ifdef Q_OS_WIN
QString win32Message(DWORD code) {
    wchar_t* buffer=nullptr;
    const DWORD length=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr,code,0,reinterpret_cast<LPWSTR>(&buffer),0,nullptr);
    QString text=length&&buffer?QString::fromWCharArray(buffer,int(length)).trimmed():QStringLiteral("unknown error");
    if(buffer)LocalFree(buffer);
    return QStringLiteral("%1 (Win32 error %2)").arg(text).arg(code);
}

QString failure(const char* call,DWORD code) {
    return QStringLiteral("%1 failed: %2").arg(QLatin1String(call),win32Message(code));
}

bool capabilities(SYSTEM_POWER_CAPABILITIES& caps) {
    ZeroMemory(&caps,sizeof caps);
    return GetPwrCapabilities(&caps)!=FALSE;
}
#endif

// Logs and reports a dry-run request. Returns true when the caller must stop (dry run active).
bool dryRunned(const char* action,QString* error) {
    if(!powerDryRun())return false;
    qInfo().noquote()<<QStringLiteral("power: %1 (dry run)").arg(QLatin1String(action));
    setError(error,QString());
    return true;
}

} // namespace

bool powerDryRun() {
    int state=dryRunState.load();
    if(state<0) {
        const QByteArray value=qgetenv("XMB_POWER_DRY_RUN").trimmed();
        state=(!value.isEmpty() && value!="0")?1:0;
        int expected=-1;
        dryRunState.compare_exchange_strong(expected,state);
        state=dryRunState.load();
    }
    return state==1;
}

void setPowerDryRun(bool on) {
    dryRunState.store(on?1:0);
    qInfo().noquote()<<QStringLiteral("power: dry run %1").arg(on?"on":"off");
}

bool canSleep() {
#ifdef Q_OS_WIN
    SYSTEM_POWER_CAPABILITIES caps;
    if(!capabilities(caps))return false;
    return caps.SystemS1||caps.SystemS2||caps.SystemS3;
#else
    return false;
#endif
}

bool canHibernate() {
#ifdef Q_OS_WIN
    SYSTEM_POWER_CAPABILITIES caps;
    if(!capabilities(caps))return false;
    return caps.SystemS4&&caps.HiberFilePresent;
#else
    return false;
#endif
}

QString powerCapabilitiesText() {
#ifdef Q_OS_WIN
    SYSTEM_POWER_CAPABILITIES caps;
    if(!capabilities(caps))return failure("GetPwrCapabilities",GetLastError());
    auto yn=[](BOOLEAN b){return b?QStringLiteral("yes"):QStringLiteral("no");};
    return QStringLiteral("S1 %1, S2 %2, S3 %3, S4 %4 (hiberfile %5), S5 %6, modern standby (AoAc) %7, "
                          "hybrid sleep (fast system wake) %8, display dimming %9")
        .arg(yn(caps.SystemS1),yn(caps.SystemS2),yn(caps.SystemS3),yn(caps.SystemS4),yn(caps.HiberFilePresent),
             yn(caps.SystemS5),yn(caps.AoAc),yn(caps.FastSystemS4),yn(caps.VideoDimPresent));
#else
    return QStringLiteral("not supported on this platform");
#endif
}

bool enableShutdownPrivilege(QString* error) {
#ifdef Q_OS_WIN
    HANDLE token=nullptr;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&token)) {
        setError(error,failure("OpenProcessToken",GetLastError()));
        return false;
    }
    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount=1;
    privileges.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
    if(!LookupPrivilegeValueW(nullptr,SE_SHUTDOWN_NAME,&privileges.Privileges[0].Luid)) {
        const DWORD code=GetLastError();
        CloseHandle(token);
        setError(error,failure("LookupPrivilegeValue(SeShutdownPrivilege)",code));
        return false;
    }
    // AdjustTokenPrivileges returns TRUE even when the privilege is not held; only GetLastError tells.
    const BOOL adjusted=AdjustTokenPrivileges(token,FALSE,&privileges,0,nullptr,nullptr);
    const DWORD code=GetLastError();
    CloseHandle(token);
    if(!adjusted) {
        setError(error,failure("AdjustTokenPrivileges",code));
        return false;
    }
    if(code!=ERROR_SUCCESS) {
        setError(error,code==ERROR_NOT_ALL_ASSIGNED
            ?QStringLiteral("SeShutdownPrivilege is not held by this account (ERROR_NOT_ALL_ASSIGNED); a policy forbids shutting down")
            :failure("AdjustTokenPrivileges",code));
        return false;
    }
    setError(error,QString());
    return true;
#else
    setError(error,QStringLiteral("not supported on this platform"));
    return false;
#endif
}

bool requestSleep(bool hibernate,QString* error) {
    const char* action=hibernate?"hibernate":"sleep";
    if(hibernate?!canHibernate():!canSleep()) {
        setError(error,hibernate
            ?QStringLiteral("hibernate is not available on this PC (S4 unsupported or hibernation turned off)")
            :QStringLiteral("sleep is not available on this PC (no S1-S3 state; a Modern Standby PC cannot be put to sleep this way)"));
        qWarning().noquote()<<QStringLiteral("power: %1 refused: %2").arg(QLatin1String(action),error?*error:QString());
        return false;
    }
    if(dryRunned(action,error))return true;
#ifdef Q_OS_WIN
    QString privilegeError;
    if(!enableShutdownPrivilege(&privilegeError)) {
        setError(error,privilegeError);
        qWarning().noquote()<<QStringLiteral("power: %1 failed: %2").arg(QLatin1String(action),privilegeError);
        return false;
    }
    qInfo().noquote()<<QStringLiteral("power: %1 requested").arg(QLatin1String(action));
    // Blocks until resume on success.
    if(!SetSuspendState(hibernate?TRUE:FALSE,FALSE,FALSE)) {
        const QString text=failure("SetSuspendState",GetLastError());
        setError(error,text);
        qWarning().noquote()<<QStringLiteral("power: %1 failed: %2").arg(QLatin1String(action),text);
        return false;
    }
    qInfo().noquote()<<QStringLiteral("power: resumed from %1").arg(QLatin1String(action));
    setError(error,QString());
    return true;
#else
    setError(error,QStringLiteral("not supported on this platform"));
    return false;
#endif
}

namespace {
bool initiate(bool restart,QString* error) {
    const char* action=restart?"restart":"shutdown";
    if(dryRunned(action,error))return true;
#ifdef Q_OS_WIN
    QString privilegeError;
    if(!enableShutdownPrivilege(&privilegeError)) {
        setError(error,privilegeError);
        qWarning().noquote()<<QStringLiteral("power: %1 failed: %2").arg(QLatin1String(action),privilegeError);
        return false;
    }
    qInfo().noquote()<<QStringLiteral("power: %1 requested").arg(QLatin1String(action));
    // No SHUTDOWN_FORCE_OTHERS / SHUTDOWN_FORCE_SELF: applications may save or veto.
    const DWORD flags=restart?SHUTDOWN_RESTART:SHUTDOWN_POWEROFF;
    const DWORD code=InitiateShutdownW(nullptr,nullptr,0,flags,SHTDN_REASON_MAJOR_OTHER|SHTDN_REASON_FLAG_PLANNED);
    if(code!=ERROR_SUCCESS) {
        // InitiateShutdown returns the error code itself rather than setting GetLastError.
        const QString text=failure("InitiateShutdown",code);
        setError(error,text);
        qWarning().noquote()<<QStringLiteral("power: %1 failed: %2").arg(QLatin1String(action),text);
        return false;
    }
    setError(error,QString());
    return true;
#else
    setError(error,QStringLiteral("not supported on this platform"));
    return false;
#endif
}
} // namespace

bool requestRestart(QString* error) { return initiate(true,error); }
bool requestShutdown(QString* error) { return initiate(false,error); }

bool turnDisplayOff(QString* error,quintptr window) {
    if(dryRunned(window?"display off (own window)":"display off (broadcast)",error))return true;
#ifdef Q_OS_WIN
    HWND target=window?reinterpret_cast<HWND>(window):HWND_BROADCAST;
    if(window && !IsWindow(target)) {
        setError(error,QStringLiteral("display off: the given window handle is not a window"));
        return false;
    }
    qInfo().noquote()<<QStringLiteral("power: display off requested (%1)").arg(window?"own window":"broadcast");
    DWORD_PTR reply=0;
    SetLastError(ERROR_SUCCESS);
    const LRESULT delivered=SendMessageTimeoutW(target,WM_SYSCOMMAND,SC_MONITORPOWER,2,SMTO_ABORTIFHUNG|SMTO_BLOCK,1000,&reply);
    if(!delivered) {
        const DWORD code=GetLastError();
        const QString text=code==ERROR_TIMEOUT||code==ERROR_SUCCESS
            ?QStringLiteral("SendMessageTimeout(SC_MONITORPOWER) timed out; the request may be ignored")
            :failure("SendMessageTimeout(SC_MONITORPOWER)",code);
        setError(error,text);
        qWarning().noquote()<<"power:"<<text;
        return false;
    }
    setError(error,QString());
    return true;
#else
    Q_UNUSED(window);
    setError(error,QStringLiteral("not supported on this platform"));
    return false;
#endif
}

bool lockWorkstation(QString* error) {
    if(dryRunned("lock",error))return true;
#ifdef Q_OS_WIN
    qInfo()<<"power: lock requested";
    if(!LockWorkStation()) {
        const QString text=failure("LockWorkStation",GetLastError());
        setError(error,text);
        qWarning().noquote()<<"power:"<<text;
        return false;
    }
    setError(error,QString());
    return true;
#else
    setError(error,QStringLiteral("not supported on this platform"));
    return false;
#endif
}

} // namespace xmb
