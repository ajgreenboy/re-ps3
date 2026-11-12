#include "web_adopt.h"
#include <QDir>
#include <QFileInfo>
#ifdef Q_OS_WIN
#include <QHash>
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#endif

namespace xmb::webAdopt {
QString normalizeProfilePath(const QString& path) {
    const QString value=path.trimmed();
    if(value.isEmpty())return {};
    const QFileInfo info(value);
    QString clean=info.exists()?info.canonicalFilePath():QString();
    if(clean.isEmpty())clean=QDir::cleanPath(info.absoluteFilePath());
    clean=QDir::fromNativeSeparators(clean);
    while(clean.size()>3 && clean.endsWith(QLatin1Char('/')))clean.chop(1);
    return clean.toLower();
}
QString profileFromArguments(const QStringList& arguments) {
    static const QString key=QStringLiteral("--user-data-dir=");
    for(const QString& argument:arguments)
        if(argument.startsWith(key,Qt::CaseInsensitive))return argument.mid(key.size());
    return {};
}
bool isBrowserRoot(const QStringList& arguments) {
    for(const QString& argument:arguments)if(argument.startsWith(QLatin1String("--type=")))return false;
    return true;
}

#ifdef Q_OS_WIN
namespace {
using NtQueryFn=LONG(NTAPI*)(HANDLE,ULONG,PVOID,ULONG,PULONG);
constexpr ULONG kProcessCommandLineInformation=60;   // Windows 8.1+, needs only PROCESS_QUERY_LIMITED_INFORMATION
struct UnicodeString{USHORT length;USHORT maximumLength;PWSTR buffer;};

bool readCommandLine(DWORD pid,QStringList* out) {
    static const NtQueryFn query=reinterpret_cast<NtQueryFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationProcess")));
    if(!query)return false;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process)return false;
    bool ok=false;
    ULONG needed=0;
    query(process,kProcessCommandLineInformation,nullptr,0,&needed);
    if(needed>=sizeof(UnicodeString) && needed<(1u<<20)) {
        QByteArray buffer(int(needed)+64,'\0');
        ULONG got=0;
        const LONG status=query(process,kProcessCommandLineInformation,buffer.data(),ULONG(buffer.size()),&got);
        if(status>=0) {
            const auto* text=reinterpret_cast<const UnicodeString*>(buffer.constData());
            if(text->buffer && text->length>0) {
                const QString line=QString::fromWCharArray(text->buffer,text->length/2);
                int argc=0;
                LPWSTR* argv=CommandLineToArgvW(reinterpret_cast<LPCWSTR>(line.utf16()),&argc);
                if(argv) {
                    QStringList list;
                    for(int i=0;i<argc;++i)list<<QString::fromWCharArray(argv[i]);
                    LocalFree(argv);
                    *out=list;ok=true;
                }
            }
        }
    }
    CloseHandle(process);
    return ok;
}
QHash<quint32,quint32> parentMap() {
    QHash<quint32,quint32> parents;
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snap==INVALID_HANDLE_VALUE)return parents;
    PROCESSENTRY32W e{};e.dwSize=sizeof(e);
    for(BOOL more=Process32FirstW(snap,&e);more;more=Process32NextW(snap,&e))parents.insert(e.th32ProcessID,e.th32ParentProcessID);
    CloseHandle(snap);
    return parents;
}
bool inTree(const QHash<quint32,quint32>& parents,quint32 pid,quint32 root) {
    for(int depth=0;pid && depth<32;++depth){if(pid==root)return true;pid=parents.value(pid,0);}
    return false;
}
struct WindowSearch{QHash<quint32,quint32> parents;quint32 root;HWND newest;int count;};
BOOL CALLBACK collect(HWND h,LPARAM lp) {
    auto* s=reinterpret_cast<WindowSearch*>(lp);
    DWORD pid=0;GetWindowThreadProcessId(h,&pid);
    if(!IsWindowVisible(h) || GetWindow(h,GW_OWNER) || (GetWindowLongW(h,GWL_EXSTYLE)&WS_EX_TOOLWINDOW) || !inTree(s->parents,pid,s->root))return TRUE;
    ++s->count;
    if(!s->newest)s->newest=h;   // EnumWindows walks top to bottom in Z order: the first hit is the frontmost
    return TRUE;
}
}

QList<Holder> findHolders(const QString& profileDir,int* unreadable) {
    QList<Holder> found;
    if(unreadable)*unreadable=0;
    const QString wanted=normalizeProfilePath(profileDir);
    if(wanted.isEmpty())return found;
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snap==INVALID_HANDLE_VALUE)return found;
    PROCESSENTRY32W e{};e.dwSize=sizeof(e);
    for(BOOL more=Process32FirstW(snap,&e);more;more=Process32NextW(snap,&e)) {
        const QString name=QString::fromWCharArray(e.szExeFile);
        if(name.compare(QLatin1String("msedge.exe"),Qt::CaseInsensitive)!=0 && name.compare(QLatin1String("chrome.exe"),Qt::CaseInsensitive)!=0)continue;
        QStringList arguments;
        if(!readCommandLine(e.th32ProcessID,&arguments)){if(unreadable)++*unreadable;continue;}
        if(!isBrowserRoot(arguments))continue;
        const QString profile=profileFromArguments(arguments);
        if(profile.isEmpty() || normalizeProfilePath(profile)!=wanted)continue;
        found.push_back({quint32(e.th32ProcessID),name});
    }
    CloseHandle(snap);
    return found;
}
bool treeHasVisibleWindow(quint32 rootPid) {
    WindowSearch s{parentMap(),rootPid,nullptr,0};
    EnumWindows(collect,reinterpret_cast<LPARAM>(&s));
    return s.count>0;
}
bool bringTreeForward(quint32 rootPid) {
    WindowSearch s{parentMap(),rootPid,nullptr,0};
    EnumWindows(collect,reinterpret_cast<LPARAM>(&s));
    if(!s.newest)return false;
    if(IsIconic(s.newest))ShowWindow(s.newest,SW_RESTORE);
    BringWindowToTop(s.newest);
    return SetForegroundWindow(s.newest)!=FALSE;
}
bool foregroundInTree(quint32 rootPid){
    DWORD pid=0;
    if(HWND h=GetForegroundWindow())GetWindowThreadProcessId(h,&pid);
    return pid && inTree(parentMap(),pid,rootPid);
}
void* openWatch(quint32 pid){return OpenProcess(SYNCHRONIZE,FALSE,pid);}
bool watchExited(void* handle){return !handle || WaitForSingleObject(static_cast<HANDLE>(handle),0)!=WAIT_TIMEOUT;}
void closeWatch(void* handle){if(handle)CloseHandle(static_cast<HANDLE>(handle));}
void allowForeground(quint32 pid){AllowSetForegroundWindow(pid);}
int terminateTree(quint32 rootPid) {
    if(!rootPid)return 0;
    const auto parents=parentMap();
    int killed=0;
    for(auto it=parents.cbegin();it!=parents.cend();++it) {
        if(it.key()==rootPid || !inTree(parents,it.key(),rootPid))continue;
        if(HANDLE h=OpenProcess(PROCESS_TERMINATE,FALSE,it.key())){if(TerminateProcess(h,1))++killed;CloseHandle(h);}
    }
    if(HANDLE h=OpenProcess(PROCESS_TERMINATE,FALSE,rootPid)){if(TerminateProcess(h,1))++killed;CloseHandle(h);}
    return killed;
}
#else
QList<Holder> findHolders(const QString&,int* unreadable){if(unreadable)*unreadable=0;return {};}
bool foregroundInTree(quint32){return false;}
void* openWatch(quint32){return nullptr;}
bool watchExited(void*){return true;}
void closeWatch(void*){}
void allowForeground(quint32){}
bool treeHasVisibleWindow(quint32){return false;}
bool bringTreeForward(quint32){return false;}
int terminateTree(quint32){return 0;}
#endif
}
