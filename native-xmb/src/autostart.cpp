#include "autostart.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QXmlStreamReader>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <mutex>
#include <thread>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace xmb::autostart {
namespace {
constexpr auto kDefaultTask="re-ps3-native-kiosk";
std::atomic<int> gState{-1};           // -1 not read yet, 0 off, 1 on
std::atomic<bool> gReading{false};

QString appDir(){return QCoreApplication::applicationDirPath();}
QString scriptPath(const char* name){return QDir::toNativeSeparators(QDir(appDir()).filePath(QString::fromLatin1(name)));}
QString powershell(){
    return QDir::toNativeSeparators(qEnvironmentVariable("SystemRoot",QStringLiteral("C:/Windows"))+
        QStringLiteral("/System32/WindowsPowerShell/v1.0/powershell.exe"));
}
void hideConsole(QProcess& process){
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW;});
#else
    Q_UNUSED(process)
#endif
}
// Runs a program with an argument list (no shell). Returns the exit code, or -1 when it did not finish.
int run(const QString& program,const QStringList& arguments,QString* output,int timeoutMs){
    QProcess process;
    hideConsole(process);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program,arguments);
    if(!process.waitForStarted(5000)){if(output)*output=process.errorString()+QStringLiteral(" [")+program+QStringLiteral("]");return -1;}
    if(!process.waitForFinished(timeoutMs)){process.kill();process.waitForFinished(2000);if(output)*output=QStringLiteral("timed out");return -1;}
    if(output)*output=QString::fromLocal8Bit(process.readAll()).trimmed();
    return process.exitCode();
}
// The task XML the scheduler holds: on only when an Exec action runs a watchdog.ps1 that is the one beside us.
bool readTask(){
    QString xml;
    const QString schtasks=QDir::toNativeSeparators(qEnvironmentVariable("SystemRoot",QStringLiteral("C:/Windows"))+QStringLiteral("/System32/schtasks.exe"));
    if(run(schtasks,{QStringLiteral("/Query"),QStringLiteral("/TN"),taskName(),QStringLiteral("/XML")},&xml,8000)!=0)return false;
    const int open=xml.indexOf(QLatin1Char('<'));
    QXmlStreamReader reader(open>=0?xml.mid(open):xml);
    const QString want=scriptPath("watchdog.ps1");
    bool inExec=false;
    while(!reader.atEnd()){
        reader.readNext();
        if(reader.isStartElement()){
            if(reader.name()==QLatin1String("Exec"))inExec=true;
            else if(inExec && reader.name()==QLatin1String("Arguments")){
                if(reader.readElementText().contains(want,Qt::CaseInsensitive))return true;
                inExec=false;
            }
        } else if(reader.isEndElement() && reader.name()==QLatin1String("Exec"))inExec=false;
    }
    return false;
}
}

QString taskName(){
    const QString overridden=qEnvironmentVariable("XMB_AUTOSTART_TASK").trimmed();
    return overridden.isEmpty()?QString::fromLatin1(kDefaultTask):overridden;
}
bool available(QString* reason){
    for(const char* name:{"watchdog.ps1","install-autostart.ps1"}) {
        if(!QFileInfo::exists(scriptPath(name))) {
            if(reason)*reason=QStringLiteral("Only available in the packaged build: %1 was not found next to the program.").arg(QString::fromLatin1(name));
            return false;
        }
    }
    return true;
}
void prefetch(){
    if(!available() || gReading.exchange(true))return;
    std::thread([]{
        gState=readTask()?1:0;
        gReading=false;
    }).detach();
}
bool enabled(){
    if(!available())return false;
    if(gState.load()<0)prefetch();
    return gState.load()==1;
}
bool setEnabled(bool on,QString* error){
    QString reason;
    if(!available(&reason)){if(error)*error=reason;return false;}
    QStringList arguments{QStringLiteral("-NoProfile"),QStringLiteral("-ExecutionPolicy"),QStringLiteral("Bypass"),QStringLiteral("-File"),
        scriptPath("install-autostart.ps1"),QStringLiteral("-PackageDirectory"),QDir::toNativeSeparators(appDir()),
        QStringLiteral("-TaskName"),taskName()};
    if(on) {
        // Keep the display this run was started on, so the logon task lands on the same screen.
        const auto args=QCoreApplication::arguments();
        const int at=int(args.indexOf(QStringLiteral("--display")));
        bool ok=false;
        if(at>=0 && at+1<args.size()){const int index=args[at+1].toInt(&ok);if(ok && index>=0)arguments<<QStringLiteral("-Display")<<QString::number(index);}
    } else arguments<<QStringLiteral("-Remove");
    QString output;
    const int code=run(powershell(),arguments,&output,60000);
    qInfo().noquote()<<QStringLiteral("autostart: %1 task \"%2\" -> exit %3 %4").arg(on?"create":"remove",taskName()).arg(code).arg(output.left(300));
    // The read-back is the proof: the scheduler either holds the task for this watchdog or it does not.
    const bool now=readTask();
    gState=now?1:0;
    if(code!=0 || now!=on) {
        if(error)*error=code!=0?QStringLiteral("the installer script failed (exit %1): %2").arg(code).arg(output.left(200))
                                :QStringLiteral("the scheduler still reports the task as %1").arg(now?"present":"absent");
        return false;
    }
    return true;
}

void setEnabledAsync(bool on,std::function<void(bool,QString)> done) {
    // The write blocks ~1 s (PowerShell start-up + the script + the schtasks read-back), so it runs on a worker
    // and only the result comes back to the GUI thread (BEHAVIOUR-GAPS-SPEC section 7 / HTPC-LEFTOVERS D3).
    (void)QtConcurrent::run([on,done=std::move(done)] {
        QString error;
        const bool ok=setEnabled(on,&error);   // same write + read-back as the blocking form; updates gState
        // Deliver on the GUI thread so a caller can touch widgets; with no event loop (never here) fall back
        // to the worker thread rather than dropping the result.
        if(QCoreApplication::instance())
            QMetaObject::invokeMethod(QCoreApplication::instance(),
                [done=std::move(done),ok,error] { if(done) done(ok,error); }, Qt::QueuedConnection);
        else if(done) done(ok,error);
    });
}
}
