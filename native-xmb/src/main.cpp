#include "view.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMainWindow>
#include <QSurfaceFormat>
#include <QSettings>
#include "catalog.h"
#include <QJsonDocument>
#include <QFile>
#include <QLoggingCategory>
#include <QPainter>

int main(int argc,char** argv) {
    QSurfaceFormat format;format.setVersion(3,3);format.setProfile(QSurfaceFormat::CoreProfile);format.setSwapInterval(1);format.setSamples(4);QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc,argv);app.setApplicationName("re-ps3-native");app.setApplicationVersion("0.1.0");
    QCommandLineParser parser;parser.setApplicationDescription("Native C++ XMB recreation");parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"dev-flash","Local dev_flash asset directory","directory"});
    parser.addOption({"data-dir","Writable native application data directory","directory"});
    parser.addOption({"inventory","Write the local XMBML definition inventory and exit","json-file"});
    parser.addOption({"icon-sheet","Write a local contact sheet of original QGL normal maps and exit","image-file"});parser.process(app);
    if(!parser.isSet("dev-flash"))parser.showHelp(2);
    const QString data=parser.isSet("data-dir")?parser.value("data-dir"):QDir(QCoreApplication::applicationDirPath()).filePath("data");
    if(parser.isSet("inventory")) {
        const auto inventory=xmb::inventoryXmb(parser.value("dev-flash"));
        if(inventory.value("documentCount").toInt()==0){qCritical()<<"No local XMBML files found";return 2;}
        QFile output(parser.value("inventory"));
        if(!output.open(QIODevice::WriteOnly)){qCritical()<<output.errorString();return 2;}
        const auto bytes=QJsonDocument(inventory).toJson();
        if(output.write(bytes)!=bytes.size()){qCritical()<<output.errorString();return 2;}
        qInfo()<<"catalog inventory:"<<inventory.value("documentCount").toInt()<<"documents,"<<inventory.value("viewCount").toInt()<<"views,"<<inventory.value("itemCount").toInt()<<"items,"<<inventory.value("errorCount").toInt()<<"errors";
        return inventory.value("errorCount").toInt()?1:0;
    }
    xmb::Assets assets(parser.value("dev-flash"));
    if(parser.isSet("icon-sheet")) {
        QImage sheet(1400,1200,QImage::Format_RGB32);sheet.fill(QColor(45,40,25));QPainter painter(&sheet);
        painter.setPen(Qt::white);
        for(int i=1;i<=70;++i) {
            const auto name=QString::number(i)+"n";const auto& icon=assets.image("qgl/icontex",name);
            const int x=((i-1)%10)*140,y=((i-1)/10)*170;
            painter.drawImage(QRect(x+6,y+6,128,128),icon);painter.drawText(QRect(x,y+135,140,30),Qt::AlignCenter,name);
        }
        painter.end();
        if(!sheet.save(parser.value("icon-sheet"))){qCritical()<<"Cannot save icon sheet";return 2;}
        return assets.failedImages()?1:0;
    }
    xmb::Shell shell(data,assets);
    QSettings windowSettings(QDir(data).filePath("settings.ini"),QSettings::IniFormat);
    QMainWindow window;window.setWindowTitle("re-ps3 — Native XMB");window.setCentralWidget(new xmb::View(shell,assets,&window));window.resize(960,540);
    if(windowSettings.contains("window/geometry"))window.restoreGeometry(windowSettings.value("window/geometry").toByteArray());
    QObject::connect(&app,&QApplication::aboutToQuit,&window,[&]{windowSettings.setValue("window/geometry",window.saveGeometry());windowSettings.sync();});
    window.show();
    return app.exec();
}
