#include "archive_extract.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

namespace xmb {
bool extractArchive(const QString& archivePath, const QString& destDir, QString* error) {
    const QString ext=QFileInfo(archivePath).suffix().toLower();
    QString program; QStringList args;
    if(ext==QStringLiteral("zip") || ext==QStringLiteral("tar") || ext==QStringLiteral("gz") ||
       ext==QStringLiteral("tgz") || ext==QStringLiteral("bz2")) {
        program=QStringLiteral("tar"); args<<QStringLiteral("-xf")<<archivePath<<QStringLiteral("-C")<<destDir;
    } else {
        program=QStringLiteral("7z"); args<<QStringLiteral("x")<<archivePath<<QStringLiteral("-o")+destDir<<QStringLiteral("-y");
    }
    QProcess process;
    process.start(program,args);
    if(!process.waitForStarted(5000)) {
        if(error)*error=QStringLiteral("Extractor not found: %1 (install 7-Zip for .rar/.7z)").arg(program); return false;
    }
    if(!process.waitForFinished(30*60*1000)) {
        process.kill(); if(error)*error=QStringLiteral("Extraction timed out"); return false;
    }
    if(process.exitCode()!=0) {
        if(error)*error=QStringLiteral("Extraction failed: %1").arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed());
        return false;
    }
    return true;
}
QString findGameExecutable(const QString& folder, QString* error) {
    QList<QDir> queue{QDir(folder)};
    while(!queue.isEmpty()) {
        QDir dir=queue.takeFirst();
        const auto entries=dir.entryInfoList(QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name);
        for(const auto& entry:entries) {
            if(entry.isDir()) { queue.append(QDir(entry.absoluteFilePath())); continue; }
            if(entry.suffix().compare(QStringLiteral("exe"),Qt::CaseInsensitive)!=0)continue;
            const QString base=entry.completeBaseName().toLower();
            if(base.contains(QStringLiteral("unins")) || base.contains(QStringLiteral("setup")) ||
               base.contains(QStringLiteral("install")))continue;
            return entry.absoluteFilePath();
        }
    }
    if(error)*error=QStringLiteral("No executable found in the extracted folder");
    return {};
}
} // namespace xmb
