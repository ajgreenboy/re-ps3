#include "cec.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace xmb::cec {

bool dryRun() { return qEnvironmentVariable("XMB_CEC_DRY_RUN")==QStringLiteral("1"); }

namespace {
QStringList searchDirs() {
    QStringList dirs{QCoreApplication::applicationDirPath()};
    for(const char* env:{"ProgramFiles(x86)","ProgramFiles","ProgramW6432"}) {
        const QString root=qEnvironmentVariable(env);
        if(root.isEmpty())continue;
        dirs<<QDir(root).filePath(QStringLiteral("Pulse-Eight/USB-CEC Adapter"))
            <<QDir(root).filePath(QStringLiteral("Pulse-Eight/USB-CEC Adapter (x64)"))
            <<QDir(root).filePath(QStringLiteral("libcec"));
    }
    return dirs;
}
}

Availability detect() {
    Availability a;
    const QString forced=qEnvironmentVariable("XMB_CEC_CLIENT");
    if(!forced.isEmpty()) {
        if(QFileInfo(forced).isFile()){a.available=true;a.client=forced;return a;}
        a.reason=QStringLiteral("XMB_CEC_CLIENT does not name a file");
        return a;
    }
    if(dryRun()){a.available=true;a.client=QStringLiteral("cec-client");return a;}
    const QStringList dirs=searchDirs();
    for(const auto& dir:dirs) {
        const QString exe=QDir(dir).filePath(QStringLiteral("cec-client.exe"));
        if(QFileInfo(exe).isFile()){a.available=true;a.client=exe;break;}
    }
    if(!a.available) {
        const QString onPath=QStandardPaths::findExecutable(QStringLiteral("cec-client"));
        if(!onPath.isEmpty()){a.available=true;a.client=onPath;}
    }
    for(const auto& dir:dirs)
        if(QFileInfo(QDir(dir).filePath(QStringLiteral("libcec.dll"))).isFile()){a.libcecFound=true;break;}
    if(!a.available)
        a.reason=a.libcecFound
            ?QStringLiteral("libcec.dll is installed but cec-client.exe was not found; reinstall the Pulse-Eight USB-CEC Adapter software")
            :QStringLiteral("needs a Pulse-Eight USB-CEC adapter and its libcec software (cec-client.exe); not found on this PC");
    return a;
}

QByteArray commandText(const QVector<Action>& actions) {
    QByteArray text;
    for(const auto action:actions) {
        switch(action) {
        case Action::TvOn:text+="on 0\n";break;
        case Action::ActiveSource:text+="as\n";break;
        case Action::Standby:text+="standby 0\n";break;
        }
    }
    return text;
}
QStringList clientArguments() { return {QStringLiteral("-s"),QStringLiteral("-d"),QStringLiteral("1")}; }

bool send(const QVector<Action>& actions,const QString& why,bool wait,int timeoutMs) {
    const auto availability=detect();
    const QByteArray text=commandText(actions);
    const QString shown=QString::fromLatin1(text).trimmed().replace(QLatin1Char('\n'),QStringLiteral(" | "));
    if(!availability.available) {
        qInfo().noquote()<<"cec: not sent ("+why+"):"<<availability.reason;
        return false;
    }
    if(dryRun()) {
        qInfo().noquote()<<QString("cec: DRY RUN (%1) would start %2 %3 with stdin \"%4\"").arg(why,availability.client,clientArguments().join(' '),shown);
        return true;
    }
    auto* process=new QProcess;
    process->setProgram(availability.client);
    process->setArguments(clientArguments());
    process->setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),process,[process,why](int code,QProcess::ExitStatus){
        qInfo().noquote()<<QString("cec: (%1) cec-client exited %2: %3").arg(why).arg(code).arg(QString::fromLocal8Bit(process->readAll()).trimmed().left(300));
        process->deleteLater();
    });
    QObject::connect(process,&QProcess::errorOccurred,process,[process,why](QProcess::ProcessError e){
        if(e!=QProcess::FailedToStart)return;
        qWarning().noquote()<<QString("cec: (%1) cec-client could not start: %2").arg(why,process->errorString());
        process->deleteLater();
    });
    process->start();
    if(!process->waitForStarted(3000)){
        qWarning().noquote()<<QString("cec: (%1) cec-client did not start: %2").arg(why,process->errorString());
        process->deleteLater();
        return false;
    }
    process->write(text);
    process->closeWriteChannel();
    qInfo().noquote()<<QString("cec: (%1) sent \"%2\" through %3").arg(why,shown,availability.client);
    if(wait) {
        const bool done=process->waitForFinished(timeoutMs);
        if(!done){qWarning().noquote()<<QString("cec: (%1) cec-client still running after %2 ms; leaving it").arg(why).arg(timeoutMs);}
        return done;
    }
    return true;
}
}
