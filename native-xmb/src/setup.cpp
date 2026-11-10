#include "setup.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>

namespace xmb::setup {
QString statePath(const QString& dataDirectory) { return QDir(dataDirectory).filePath(QStringLiteral("setup.json")); }
bool completed(const QString& dataDirectory) {
    QFile file(statePath(dataDirectory));
    if(!file.open(QIODevice::ReadOnly))return false;
    const auto doc=QJsonDocument::fromJson(file.readAll());
    return doc.isObject() && doc.object().value(QStringLiteral("completed")).toBool(false);
}
bool markCompleted(const QString& dataDirectory,bool skipped,QString* error) {
    QDir().mkpath(dataDirectory);
    QSaveFile file(statePath(dataDirectory));
    if(!file.open(QIODevice::WriteOnly)){if(error)*error=file.errorString();return false;}
    const QJsonObject object{{"version",1},{"completed",true},{"skipped",skipped},
        {"when",QDateTime::currentDateTime().toString(Qt::ISODate)}};
    file.write(QJsonDocument(object).toJson());
    if(!file.commit()){if(error)*error=file.errorString();return false;}
    return true;
}
QString configPath() {
    if(qEnvironmentVariableIsSet("XMB_CONFIG"))return qEnvironmentVariable("XMB_CONFIG");   // tests and scratch runs
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("xmb-config.json"));
}
QJsonObject loadConfig(const QString& path) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))return {};
    const auto doc=QJsonDocument::fromJson(file.readAll());
    return doc.isObject()?doc.object():QJsonObject{};
}
bool saveConfig(const QString& path,const QJsonObject& config,QString* error) {
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)){if(error)*error=file.errorString();return false;}
    file.write(QJsonDocument(config).toJson());
    if(!file.commit()){if(error)*error=file.errorString();return false;}
    return true;
}
QStringList requiredFirmwareFiles() {
    return {QStringLiteral("vsh/resource/explore_plugin_full.rco"),QStringLiteral("vsh/resource/system_plugin.rco"),
            QStringLiteral("vsh/resource/sysconf_plugin.rco"),QStringLiteral("vsh/resource/explore/xmb/category_game.xml"),
            QStringLiteral("data/font/SCE-PS3-RD-R-LATIN.TTF")};
}
FirmwareCheck checkFirmware(const QString& root) {
    FirmwareCheck result;
    result.exists=!root.isEmpty() && QFileInfo(root).isDir();
    for(const QString& relative:requiredFirmwareFiles())
        if(!result.exists || !QFileInfo::exists(QDir(root).filePath(relative)))result.missing<<relative;
    result.ok=result.exists && result.missing.isEmpty();
    return result;
}
QStringList findFirmwareCandidates() {
    QStringList probes;
    const QDir exeDir(QCoreApplication::applicationDirPath());
    for(const char* relative:{"dev_flash","../dev_flash","../rpcs3/bin/dev_flash","../../rpcs3/bin/dev_flash","../rpcs3/dev_flash"})
        probes<<exeDir.absoluteFilePath(QLatin1String(relative));
    const QString appData=qEnvironmentVariable("APPDATA");
    if(!appData.isEmpty())probes<<QDir(appData).filePath(QStringLiteral("RPCS3/dev_flash"));
    for(const QFileInfo& drive:QDir::drives())
        for(const char* relative:{"dev_flash","rpcs3/dev_flash","rpcs3/bin/dev_flash","PS3/dev_flash","RPCS3/dev_flash"})
            probes<<QDir(drive.absoluteFilePath()).filePath(QLatin1String(relative));
    QStringList found;
    for(const QString& probe:probes) {
        const QString clean=QDir::cleanPath(QFileInfo(probe).absoluteFilePath());
        if(!found.contains(clean,Qt::CaseInsensitive) && checkFirmware(clean).ok)found<<clean;
    }
    return found;
}
QString defaultLibraryFolder(const QString& kind) {
    const auto location=kind==QLatin1String("photo")?QStandardPaths::PicturesLocation
        :kind==QLatin1String("music")?QStandardPaths::MusicLocation:QStandardPaths::MoviesLocation;
    const QString path=QStandardPaths::writableLocation(location);
    return path.isEmpty()||!QFileInfo(path).isDir()?QString():QDir::cleanPath(path);
}
QStringList browseRoots() {
    QStringList roots;
    for(const QFileInfo& drive:QDir::drives())roots<<QDir::cleanPath(drive.absoluteFilePath())+QLatin1Char('/');
    return roots;
}
QStringList browseFolders(const QString& path) {
    if(path.isEmpty())return browseRoots();
    const QDir dir(path);
    QStringList result;
    for(const QString& name:dir.entryList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::IgnoreCase|QDir::Name))
        result<<dir.filePath(name);
    return result;
}
QString parentOf(const QString& path) {
    const QString clean=QDir::cleanPath(path);
    if(clean.size()<=3 && clean.size()>=2 && clean[1]==QLatin1Char(':'))return {};
    QDir dir(clean);
    if(!dir.cdUp())return {};
    const QString up=dir.absolutePath();
    return up==clean?QString():up;
}
QString buttonNames(quint16 buttons) {
    struct Bit { quint16 mask; const char* name; };
    static const Bit bits[]={{0x1000,"Cross"},{0x2000,"Circle"},{0x4000,"Square"},{0x8000,"Triangle"},
        {0x0001,"D-pad Up"},{0x0002,"D-pad Down"},{0x0004,"D-pad Left"},{0x0008,"D-pad Right"},
        {0x0100,"L1"},{0x0200,"R1"},{0x0040,"L3"},{0x0080,"R3"},{0x0010,"Start"},{0x0020,"Select"},{0x0400,"PS"}};
    QStringList names;
    for(const Bit& bit:bits)if(buttons&bit.mask)names<<QString::fromLatin1(bit.name);
    return names.join(QStringLiteral(", "));
}
QString axisText(float lx,float ly,float rx,float ry,float l2,float r2) {
    QStringList parts;
    const auto stick=[&](const char* name,float x,float y){
        if(std::fabs(x)<.5f && std::fabs(y)<.5f)return;
        QStringList direction;
        if(y<=-.5f)direction<<QStringLiteral("up");else if(y>=.5f)direction<<QStringLiteral("down");
        if(x<=-.5f)direction<<QStringLiteral("left");else if(x>=.5f)direction<<QStringLiteral("right");
        parts<<QStringLiteral("%1 %2").arg(QLatin1String(name),direction.join(QLatin1Char(' ')));
    };
    stick("Left stick",lx,ly);stick("Right stick",rx,ry);
    if(l2>=.5f)parts<<QStringLiteral("L2");
    if(r2>=.5f)parts<<QStringLiteral("R2");
    return parts.join(QStringLiteral(", "));
}
}
