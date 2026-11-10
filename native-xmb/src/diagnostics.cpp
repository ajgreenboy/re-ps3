#include "diagnostics.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QtLogging>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <mutex>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <crtdbg.h>
#include <stdlib.h>
#endif

namespace xmb::diagnostics {
namespace {
std::atomic<bool> g_safe{false};
constexpr int kKeepCrashes=5;
constexpr qint64 kLogMaxBytes=5*1024*1024;
constexpr int kLogKeep=3;

#ifdef Q_OS_WIN
wchar_t g_crashDir[MAX_PATH*2]={};
char g_identity[320]="version unknown";
std::atomic<bool> g_writing{false};
std::atomic<bool> g_fatalCaptured{false};
CrashExtra g_extra=nullptr;

// Deletes every crash-* pair but the newest `keep` (names embed a sortable local timestamp).
void pruneCrashFiles(int keep) {
    if(!g_crashDir[0])return;
    const std::wstring pattern=std::wstring(g_crashDir)+L"\\crash-*.txt";
    WIN32_FIND_DATAW found;
    HANDLE find=FindFirstFileW(pattern.c_str(),&found);
    if(find==INVALID_HANDLE_VALUE)return;
    QStringList stems;
    do { stems<<QString::fromWCharArray(found.cFileName).chopped(4); } while(FindNextFileW(find,&found));
    FindClose(find);
    stems.sort();
    for(int i=0;i<int(stems.size())-keep;++i)
        for(const char* extension:{".txt",".dmp"}) {
            const std::wstring path=std::wstring(g_crashDir)+L"\\"+(stems[i]+QLatin1String(extension)).toStdWString();
            DeleteFileW(path.c_str());
        }
}

void writeAll(HANDLE file,const char* text) {
    DWORD written=0;
    WriteFile(file,text,DWORD(std::strlen(text)),&written,nullptr);
}

void writeCrashFiles(EXCEPTION_POINTERS* info,const char* fatalMessage) {
    if(!g_crashDir[0] || g_writing.exchange(true))return;
    CreateDirectoryW(g_crashDir,nullptr);
    SYSTEMTIME now;GetLocalTime(&now);
    wchar_t stem[MAX_PATH*2+64];
    _snwprintf_s(stem,_TRUNCATE,L"%s\\crash-%04d%02d%02d-%02d%02d%02d-%lu",g_crashDir,now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetCurrentProcessId());
    wchar_t dumpPath[MAX_PATH*2+80],textPath[MAX_PATH*2+80];
    _snwprintf_s(dumpPath,_TRUNCATE,L"%s.dmp",stem);
    _snwprintf_s(textPath,_TRUNCATE,L"%s.txt",stem);
    bool dumped=false;
    HANDLE dump=CreateFileW(dumpPath,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(dump!=INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(),info,FALSE};
        dumped=MiniDumpWriteDump(GetCurrentProcess(),GetCurrentProcessId(),dump,
            MINIDUMP_TYPE(MiniDumpWithThreadInfo|MiniDumpWithUnloadedModules),info?&exception:nullptr,nullptr,nullptr)!=FALSE;
        CloseHandle(dump);
    }
    char code[48],module[600]="n/a",address[40]="n/a";
    if(info) {
        std::snprintf(code,sizeof code,"0x%08lx",info->ExceptionRecord->ExceptionCode);
        std::snprintf(address,sizeof address,"%p",info->ExceptionRecord->ExceptionAddress);
        HMODULE owner=nullptr;wchar_t name[MAX_PATH]={};
        if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(info->ExceptionRecord->ExceptionAddress),&owner) && GetModuleFileNameW(owner,name,MAX_PATH)) {
            const wchar_t* base=std::wcsrchr(name,L'\\');
            std::snprintf(module,sizeof module,"%s+0x%llx",QString::fromWCharArray(base?base+1:name).toUtf8().constData(),
                static_cast<unsigned long long>(reinterpret_cast<const char*>(info->ExceptionRecord->ExceptionAddress)-reinterpret_cast<const char*>(owner)));
        }
    } else std::snprintf(code,sizeof code,"qFatal");
    HANDLE text=CreateFileW(textPath,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(text!=INVALID_HANDLE_VALUE) {
        char line[1100];
        std::snprintf(line,sizeof line,"time=%04d-%02d-%02dT%02d:%02d:%02d\n",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond);writeAll(text,line);
        std::snprintf(line,sizeof line,"build=%s\nsafe_mode=%d\npid=%lu\nthread=%lu\n",g_identity,g_safe.load()?1:0,GetCurrentProcessId(),GetCurrentThreadId());writeAll(text,line);
        std::snprintf(line,sizeof line,"code=%s\naddress=%s\nmodule=%s\n",code,address,module);writeAll(text,line);
        if(fatalMessage){std::snprintf(line,sizeof line,"message=%.512s\n",fatalMessage);writeAll(text,line);}
        std::snprintf(line,sizeof line,"dump=%s\n",dumped?"written":"failed");writeAll(text,line);
        CloseHandle(text);
    }
    pruneCrashFiles(kKeepCrashes);
    g_writing=false;
}

LONG WINAPI crashFilter(EXCEPTION_POINTERS* info) {
    writeCrashFiles(info,nullptr);
    if(g_extra)g_extra(info);
    // Ending here instead of handing the fault to Windows Error Reporting: its dialog would sit on the TV for ever and
    // the watchdog would never see the exit. The exception code becomes the process exit code.
    return EXCEPTION_EXECUTE_HANDLER;
}
void onAbort(int) {
    if(!g_fatalCaptured.exchange(true))writeCrashFiles(nullptr,"abort() called");
    std::_Exit(3);
}
#endif

std::mutex g_logMutex;
QString g_dataDirectory;
}  // namespace

bool safeMode(){return g_safe.load();}
void setSafeMode(bool on){g_safe=on;}

void installCrashCapture(CrashExtra extra) {
#ifdef Q_OS_WIN
    g_extra=extra;
    SetUnhandledExceptionFilter(crashFilter);
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    std::signal(SIGABRT,onAbort);
#else
    Q_UNUSED(extra)
#endif
}

QString crashDirectory(const QString& dataDirectory){return QDir(dataDirectory).filePath(QStringLiteral("crashes"));}

void setCrashDirectory(const QString& dataDirectory) {
    g_dataDirectory=dataDirectory;
#ifdef Q_OS_WIN
    const QString directory=QDir::toNativeSeparators(QDir(dataDirectory).absoluteFilePath(QStringLiteral("crashes")));
    if(directory.size()>=int(sizeof g_crashDir/sizeof g_crashDir[0])-1)return;
    QDir().mkpath(directory);
    // The build identity: application version plus the source fingerprint build.ps1 wrote beside the exe.
    QString fingerprint=QStringLiteral("unknown");
    QFile info(QCoreApplication::applicationDirPath()+QStringLiteral("/build-info.json"));
    if(info.size()<65536 && info.open(QIODevice::ReadOnly))
        fingerprint=QJsonDocument::fromJson(info.readAll()).object().value(QStringLiteral("sourceFingerprint")).toString(fingerprint).left(16);
    std::snprintf(g_identity,sizeof g_identity,"%s fingerprint %s",QCoreApplication::applicationVersion().toUtf8().constData(),fingerprint.toUtf8().constData());
    directory.toWCharArray(g_crashDir);g_crashDir[directory.size()]=L'\0';
    pruneCrashFiles(kKeepCrashes);
#else
    Q_UNUSED(dataDirectory)
#endif
}

void captureFatal(const char* message) {
#ifdef Q_OS_WIN
    if(!g_fatalCaptured.exchange(true))writeCrashFiles(nullptr,message);
#else
    Q_UNUSED(message)
#endif
}

void armTestCrash() {
    const QString spec=qEnvironmentVariable("XMB_TEST_CRASH");
    if(spec.isEmpty())return;
    int delay=-1;bool fatal=false;
    for(const QString& term:spec.split(',',Qt::SkipEmptyParts)) {
        const QString key=term.section(':',0,0).trimmed(),value=term.section(':',1).trimmed();
        if(key==QLatin1String("kind")){fatal=(value==QLatin1String("qfatal"));continue;}
        const bool applies=key==QLatin1String("after-ms") || (key==QLatin1String("safe-after-ms") && safeMode()) ||
                           (key==QLatin1String("normal-after-ms") && !safeMode());
        bool ok=false;const int ms=value.toInt(&ok);
        if(applies && ok && ms>=0)delay=ms;
    }
    if(delay<0)return;
    qWarning().noquote()<<QString("diagnostics: XMB_TEST_CRASH armed: %1 in %2 ms (%3 run)")
        .arg(fatal?QStringLiteral("qFatal"):QStringLiteral("access violation")).arg(delay).arg(safeMode()?QStringLiteral("safe"):QStringLiteral("normal"));
    QTimer::singleShot(delay,qApp,[fatal]{
        if(fatal)qFatal("XMB_TEST_CRASH: deliberate fatal message");
        *static_cast<volatile int*>(nullptr)=1;
    });
}

CrashRecord lastCrash(const QString& dataDirectory) {
    CrashRecord record;
    QDir directory(crashDirectory(dataDirectory));
    const QStringList files=directory.entryList({QStringLiteral("crash-*.txt")},QDir::Files,QDir::Name|QDir::Reversed);
    if(files.isEmpty())return record;
    QFile file(directory.filePath(files.first()));
    if(!file.open(QIODevice::ReadOnly))return record;
    record.found=true;record.file=file.fileName();
    for(const QByteArray& raw:file.readAll().split('\n')) {
        const QString line=QString::fromUtf8(raw).trimmed();
        const QString key=line.section('=',0,0),value=line.section('=',1);
        if(key==QLatin1String("time"))record.time=QDateTime::fromString(value,Qt::ISODate);
        else if(key==QLatin1String("code"))record.code=value;
        else if(key==QLatin1String("module"))record.module=value;
    }
    if(!record.time.isValid())record.time=QFileInfo(file).lastModified();
    return record;
}

QString logDirectory(const QString& dataDirectory) {
    const QString configured=qEnvironmentVariable("XMB_LOG");
    if(!configured.isEmpty())return QDir::toNativeSeparators(QFileInfo(configured).absolutePath());
    return QDir::toNativeSeparators(QDir(dataDirectory).absoluteFilePath(QStringLiteral("logs")));
}

bool rotateLog(const QString& path,qint64 maxBytes,int keep) {
    if(keep<1 || QFileInfo(path).size()<maxBytes)return false;
    QFile::remove(QStringLiteral("%1.%2").arg(path).arg(keep));
    for(int i=keep-1;i>=1;--i)QFile::rename(QStringLiteral("%1.%2").arg(path).arg(i),QStringLiteral("%1.%2").arg(path).arg(i+1));
    return QFile::rename(path,path+QStringLiteral(".1"));
}

void installLogHandler() {
    static const QString path=qEnvironmentVariable("XMB_LOG");
    static qint64 written=0;
    if(!path.isEmpty()){QDir().mkpath(QFileInfo(path).absolutePath());rotateLog(path,kLogMaxBytes,kLogKeep);written=QFileInfo(path).size();}
    qInstallMessageHandler([](QtMsgType type,const QMessageLogContext& context,const QString& message){
        if(type==QtFatalMsg)captureFatal(message.toUtf8().constData());
        std::lock_guard<std::mutex> lock(g_logMutex);
        if(path.isEmpty()) {
            std::fprintf(stderr,"%s\n",qFormatLogMessage(type,context,message).toLocal8Bit().constData());
            return;
        }
        if(written>=kLogMaxBytes){rotateLog(path,kLogMaxBytes,kLogKeep);written=0;}
        QFile file(path);
        if(file.open(QIODevice::Append|QIODevice::Text)){const QByteArray line=(message+QLatin1Char('\n')).toUtf8();file.write(line);written+=line.size();}
    });
}

QStringList summaryLines() {
    const QString& dataDirectory=g_dataDirectory;
    QStringList lines;
    const CrashRecord crash=lastCrash(dataDirectory);
    if(crash.found)
        lines<<QStringLiteral("Last crash: ")+crash.time.toString(QStringLiteral("yyyy/MM/dd HH:mm:ss"))+QStringLiteral(", code ")+crash.code+
            (crash.module.isEmpty() || crash.module==QLatin1String("n/a")?QString{}:QStringLiteral(" in ")+crash.module);
    else lines<<QStringLiteral("Last crash: none recorded");
    lines<<QStringLiteral("Run mode: ")+(safeMode()?QString::fromLatin1(kSafeModeNotice):QStringLiteral("normal"));
    lines<<QStringLiteral("Log folder: ")+logDirectory(dataDirectory);
    return lines;
}

QString pageText() {
    QStringList lines{QStringLiteral("Diagnostics"),QString{}};
    lines<<summaryLines();
    lines<<QStringLiteral("Crash files: ")+QDir::toNativeSeparators(crashDirectory(g_dataDirectory))+QStringLiteral(" (newest 5 kept, never uploaded)");
    return lines.join(QChar::LineFeed);
}

}  // namespace xmb::diagnostics
