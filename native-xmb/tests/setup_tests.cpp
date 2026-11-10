#include "setup.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>

// First-run setup, the pure half (src/setup.cpp): setup.json, xmb-config.json round trips, the firmware check, the folder
// browser helpers and the pad button names. Build with -DXMB_SETUP_BUILD_TESTS=ON; exit code 0 = all pass.
static bool touch(const QString& path) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("x")==1;
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    int failures=0;
    const auto check=[&](bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);++failures;}};
    QTemporaryDir tmp;
    check(tmp.isValid(),"temporary directory");
    // setup.json
    const QString data=tmp.filePath("data");
    check(!xmb::setup::completed(data),"no setup.json is not completed");
    check(xmb::setup::markCompleted(data,false),"markCompleted writes");
    check(xmb::setup::completed(data),"completed after markCompleted");
    QFile bad(xmb::setup::statePath(data));
    check(bad.open(QIODevice::WriteOnly) && bad.write("{\"completed\":false}")>0,"rewrite as incomplete");
    bad.close();
    check(!xmb::setup::completed(data),"completed:false is not completed");
    QFile garbage(xmb::setup::statePath(data));
    check(garbage.open(QIODevice::WriteOnly) && garbage.write("not json")>0,"rewrite as garbage");
    garbage.close();
    check(!xmb::setup::completed(data),"garbage is not completed");
    // xmb-config.json keeps keys it does not own
    const QString config=tmp.filePath("xmb-config.json");
    check(xmb::setup::loadConfig(config).isEmpty(),"missing config is empty");
    QJsonObject object{{"vshModules","C:\\PS3\\vsh_module"},{"dataDir","C:\\PS3\\data"}};
    check(xmb::setup::saveConfig(config,object),"save config");
    auto loaded=xmb::setup::loadConfig(config);
    loaded.insert("devFlash","C:\\PS3\\dev_flash");loaded.insert("display",1);
    check(xmb::setup::saveConfig(config,loaded),"save config again");
    loaded=xmb::setup::loadConfig(config);
    check(loaded.value("vshModules").toString()=="C:\\PS3\\vsh_module" && loaded.value("dataDir").toString()=="C:\\PS3\\data"
          && loaded.value("devFlash").toString()=="C:\\PS3\\dev_flash" && loaded.value("display").toInt()==1,"config keeps old keys and adds new");
    // firmware check
    const QString flash=tmp.filePath("dev_flash");
    check(!xmb::setup::checkFirmware(flash).exists && !xmb::setup::checkFirmware(flash).ok,"missing folder");
    check(xmb::setup::checkFirmware(QString()).missing.size()==xmb::setup::requiredFirmwareFiles().size(),"empty path misses everything");
    QDir().mkpath(flash);
    auto partial=xmb::setup::checkFirmware(flash);
    check(partial.exists && !partial.ok && partial.missing.size()==xmb::setup::requiredFirmwareFiles().size(),"empty folder is incomplete");
    const auto required=xmb::setup::requiredFirmwareFiles();
    for(int i=0;i<required.size()-1;++i)touch(QDir(flash).filePath(required[i]));
    partial=xmb::setup::checkFirmware(flash);
    check(!partial.ok && partial.missing==QStringList{required.last()},"one file missing is named");
    touch(QDir(flash).filePath(required.last()));
    check(xmb::setup::checkFirmware(flash).ok,"complete folder passes");
    // browser helpers
    const QString tree=tmp.filePath("tree");
    QDir().mkpath(tree+"/b");QDir().mkpath(tree+"/A");QDir().mkpath(tree+"/c/inner");touch(tree+"/file.txt");
    const auto folders=xmb::setup::browseFolders(tree);
    check(folders.size()==3 && folders[0].endsWith("/A") && folders[1].endsWith("/b") && folders[2].endsWith("/c"),"folders sorted, files left out");
    check(!xmb::setup::browseRoots().isEmpty() && xmb::setup::browseFolders(QString())==xmb::setup::browseRoots(),"empty path lists drives");
    check(xmb::setup::parentOf(tree+"/c/inner")==QDir::cleanPath(tree+"/c"),"parent of a folder");
    check(xmb::setup::parentOf("C:/").isEmpty() && xmb::setup::parentOf("C:").isEmpty(),"a drive root's parent is the drive list");
    // pad names
    check(xmb::setup::buttonNames(0).isEmpty(),"no buttons");
    check(xmb::setup::buttonNames(0x1000)==QStringLiteral("Cross"),"cross");
    check(xmb::setup::buttonNames(0x1000|0x2000|0x0100)==QStringLiteral("Cross, Circle, L1"),"cross circle l1 in fixed order");
    check(xmb::setup::buttonNames(0x0400|0x0010|0x0020)==QStringLiteral("Start, Select, PS"),"start select ps");
    check(xmb::setup::axisText(0,0,0,0,0,0).isEmpty() && xmb::setup::axisText(.2f,-.3f,0,0,.1f,0).isEmpty(),"below the dead zone is nothing");
    check(xmb::setup::axisText(-.9f,-.9f,0,0,0,0)==QStringLiteral("Left stick up left"),"left stick diagonal");
    check(xmb::setup::axisText(0,0,.8f,0,1,.6f)==QStringLiteral("Right stick right, L2, R2"),"right stick and triggers");
    std::fprintf(stdout,failures?"setup tests: %d FAILED\n":"setup tests: all passed\n",failures);
    return failures?1:0;
}
