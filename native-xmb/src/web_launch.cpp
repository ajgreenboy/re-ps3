#include "web_launch.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace xmb::webLaunch {
QString normalizeWindow(const QString& value) {
    const QString v=value.trimmed().toLower();
    if(v.isEmpty())return QStringLiteral("app");
    return isValidWindow(v)?v:QString();
}
bool isValidWindow(const QString& value) {
    const QString v=value.trimmed().toLower();
    return v.isEmpty()||v==QLatin1String("app")||v==QLatin1String("kiosk")||v==QLatin1String("browser");
}
QString findChromium() {
    const QString forced=qEnvironmentVariable("XMB_WEB_BROWSER");
    if(!forced.isEmpty())return forced==QLatin1String("none")?QString():(QFileInfo(forced).isFile()?forced:QString());
    QStringList candidates;
    for(const char* variable:{"ProgramFiles(x86)","ProgramFiles"}) {
        const QString root=qEnvironmentVariable(variable);
        if(!root.isEmpty())candidates<<root+QStringLiteral("/Microsoft/Edge/Application/msedge.exe");
    }
    for(const char* variable:{"ProgramFiles","ProgramFiles(x86)","LOCALAPPDATA"}) {
        const QString root=qEnvironmentVariable(variable);
        if(!root.isEmpty())candidates<<root+QStringLiteral("/Google/Chrome/Application/chrome.exe");
    }
    for(const auto& path:candidates)if(QFileInfo(path).isFile())return QDir::toNativeSeparators(path);
    return {};
}
QStringList arguments(const QString& executable,const QString& url,const QString& window,const QString& profileDir) {
    QStringList args;
    args<<QStringLiteral("--user-data-dir=")+QDir::toNativeSeparators(profileDir)
        <<QStringLiteral("--no-first-run")<<QStringLiteral("--no-default-browser-check")
        <<QStringLiteral("--disable-session-crashed-bubble")
        <<QStringLiteral("--autoplay-policy=no-user-gesture-required");
    if(window==QLatin1String("kiosk")) {
        args<<QStringLiteral("--kiosk");
        if(QFileInfo(executable).fileName().compare(QStringLiteral("msedge.exe"),Qt::CaseInsensitive)==0)
            args<<QStringLiteral("--edge-kiosk-type=fullscreen");
        args<<url;
    } else {
        args<<QStringLiteral("--start-maximized")<<QStringLiteral("--app=")+url;
    }
    return args;
}
}
