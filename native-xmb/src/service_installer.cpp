#include "service_installer.h"
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>

namespace xmb {
namespace {
bool executableAt(const QString& path, const QString& executable) {
    if(path.isEmpty())return false;
    const QFileInfo info(path);
    return info.isFile() ? info.fileName().compare(executable,Qt::CaseInsensitive)==0
                         : QFileInfo(QDir(path).filePath(executable)).isFile();
}
QString registryValue(const QString& key, const QString& value) {
#ifdef Q_OS_WIN
    return QSettings(key,QSettings::NativeFormat).value(value).toString();
#else
    Q_UNUSED(key) Q_UNUSED(value)
    return {};
#endif
}
}
QVector<ServiceAvailability> serviceAvailability() {
    const QString steam=registryValue(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"),QStringLiteral("SteamPath"));
    const QString discord=registryValue(QStringLiteral("HKEY_CURRENT_USER\\Software\\Discord"),QStringLiteral("AppPath"));
    const QString spotify=registryValue(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Spotify"),QStringLiteral("InstallLocation"));
    const QString sevenZip=registryValue(QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\7-Zip"),QStringLiteral("Path"));
    const bool has7z=!QStandardPaths::findExecutable(QStringLiteral("7z.exe")).isEmpty() || executableAt(sevenZip,QStringLiteral("7z.exe"));
    return {
        {executableAt(steam,QStringLiteral("steam.exe")),QStringLiteral("Steam"),QStringLiteral("https://cdn.cloudflare.steamstatic.com/client/installer/SteamSetup.exe")},
        {!QStandardPaths::findExecutable(QStringLiteral("discord.exe")).isEmpty() || executableAt(discord,QStringLiteral("discord.exe")),QStringLiteral("Discord"),QStringLiteral("https://discord.com/api/download/stable?platform=win")},
        {!QStandardPaths::findExecutable(QStringLiteral("Spotify.exe")).isEmpty() || executableAt(spotify,QStringLiteral("Spotify.exe")),QStringLiteral("Spotify"),QStringLiteral("https://download.scdn.co/SpotifyFullSetup.exe")},
        {has7z,QStringLiteral("7-Zip"),QStringLiteral("https://www.7-zip.org/a/7z2409-x64.exe")}
    };
}
bool downloadInstaller(const QString& url, const QString& name, QString* path, QString* error,
                       const std::function<void(qint64,qint64)>& progress) {
    Q_UNUSED(name)
    if(path)path->clear();
    if(error)error->clear();
    const auto fail=[&](const QString& message){if(error)*error=message;return false;};
    const QUrl parsed(url);
    if(parsed.scheme()!=QLatin1String("https") || parsed.host().isEmpty())return fail(QStringLiteral("Invalid HTTPS URL"));
    QTemporaryFile file(QDir::tempPath()+QStringLiteral("/xmb-service-XXXXXX.exe"));
    if(!file.open())return fail(QStringLiteral("Cannot create temporary installer"));
    QNetworkAccessManager manager;
    QNetworkRequest request(parsed);
    request.setRawHeader("User-Agent","XMB-Rewritten/0.2");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply=manager.get(request);
    QEventLoop loop;QTimer timer;timer.setSingleShot(true);
    bool timedOut=false,failedWrite=false;qint64 received=0;
    QObject::connect(&timer,&QTimer::timeout,&loop,[&]{timedOut=true;reply->abort();});
    QObject::connect(reply,&QNetworkReply::readyRead,&loop,[&]{
        const QByteArray bytes=reply->readAll();received+=bytes.size();
        if(received>512LL*1024*1024 || file.write(bytes)!=bytes.size()){failedWrite=true;reply->abort();}
    });
    QObject::connect(reply,&QNetworkReply::downloadProgress,&loop,[&](qint64 a,qint64 b){if(progress)progress(a,b);});
    QObject::connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);
    timer.start(10*60*1000);loop.exec();
    const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if(timedOut)return fail(QStringLiteral("Request timed out"));
    if(failedWrite)return fail(QStringLiteral("Response too large or cannot write download"));
    if(reply->error()!=QNetworkReply::NoError || status<200 || status>=300)
        return fail(QStringLiteral("Network request failed: %1 (HTTP %2)").arg(reply->errorString()).arg(status));
    if(received==0 || !file.flush())return fail(QStringLiteral("Empty download or cannot write installer"));
    const QString result=file.fileName();file.close();file.setAutoRemove(false);
    if(path)*path=result;
    return true;
}
} // namespace xmb
