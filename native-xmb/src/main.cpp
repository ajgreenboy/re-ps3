#include "view.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMainWindow>
#include <QSurfaceFormat>
#include <QSettings>
#include "catalog.h"
#include "rsx_program.h"
#include <QJsonDocument>
#include <QFile>
#include <QLoggingCategory>
#include <QPainter>
#include <QImageReader>
#include <QTimer>
#include <QTextLayout>
#include <QRawFont>
#include <QGlyphRun>

int main(int argc,char** argv) {
    QSurfaceFormat format;format.setVersion(3,3);format.setProfile(QSurfaceFormat::CoreProfile);format.setSwapInterval(1);format.setSamples(4);QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc,argv);app.setApplicationName("re-ps3-native");app.setApplicationVersion("0.2.0");QImageReader::setAllocationLimit(128);
    QCommandLineParser parser;parser.setApplicationDescription("Native C++ XMB recreation");parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"dev-flash","Local dev_flash asset directory","directory"});
    parser.addOption({"data-dir","Writable native application data directory","directory"});
    parser.addOption({"inventory","Write the local XMBML definition inventory and exit","json-file"});
    parser.addOption({"icon-sheet","Write a local contact sheet of original QGL normal maps and exit","image-file"});
    parser.addOption({"export-image","Export RCO images (container:name,...@directory) as local PNGs and exit","spec"});
    parser.addOption({"rco-images","Write the image names of every vsh/resource RCO to a local text file and exit","text-file"});
    parser.addOption({"month-sheet","Write a local contact sheet of the qgl/lines month_bg day/night textures and exit","image-file"});
    parser.addOption({"font-report","Write which firmware font renders each sample string's glyphs and exit","text-file"});
    parser.addOption({"dump-cg","Translate the original qgl/lines Cg programs to GLSL into a local folder and exit","directory"});
    parser.addOption({"measure-reference","Render the background offscreen, write statistics comparable to audit/VIDEO-REFERENCES.md and exit","text-file"});parser.process(app);
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
    if(parser.isSet("export-image")) { // local analysis: container:name[,container:name...] -> <dir>/<name>.png
        const auto spec=parser.value("export-image").split('@');
        if(spec.size()!=2){qCritical()<<"use --export-image container:name,...@directory";return 2;}
        QDir().mkpath(spec[1]);int failures=0;
        for(const auto& item:spec[0].split(',')) {
            const auto parts=item.split(':');if(parts.size()!=2){++failures;continue;}
            const auto& image=assets.image(parts[0],parts[1]);
            if(image.isNull() || !image.save(QDir(spec[1]).filePath(parts[1]+".png"))){qWarning()<<"cannot export"<<item;++failures;}
            else qInfo()<<"exported"<<item<<image.size();
        }
        return failures?1:0;
    }
    if(parser.isSet("rco-images")) { // local analysis: every image name in every vsh/resource RCO
        QFile out(parser.value("rco-images"));
        if(!out.open(QIODevice::WriteOnly|QIODevice::Text)){qCritical()<<out.errorString();return 2;}
        QTextStream stream(&out);
        for(const auto& file:QDir(QDir(parser.value("dev-flash")).filePath("vsh/resource")).entryInfoList({"*.rco"},QDir::Files,QDir::Name)) {
            QStringList names;
            try { names=assets.imageNames(file.completeBaseName()); } catch(const std::exception& e) { stream<<file.completeBaseName()<<": "<<e.what()<<"\n";continue; }
            names.sort();stream<<file.completeBaseName()<<": "<<names.join(' ')<<"\n";
        }
        return 0;
    }
    if(parser.isSet("month-sheet")) { // local analysis only: firmware-derived pixels, keep out of Git
        constexpr int scale=6;QImage sheet;QPainter painter;
        for(int m=1;m<=12;++m)for(int kind=0;kind<2;++kind) {
            const auto& image=assets.image("qgl/lines",QString("textures/month_bg/%1/%2.dds").arg(kind?"night":"rgb").arg(m,2,10,QChar('0')));
            if(image.isNull())continue;
            if(sheet.isNull()){sheet=QImage(image.width()*scale*2+30,(image.height()*scale+20)*12,QImage::Format_RGB32);sheet.fill(Qt::black);painter.begin(&sheet);painter.setPen(Qt::white);}
            const QRect target(kind*(image.width()*scale+30),(m-1)*(image.height()*scale+20),image.width()*scale,image.height()*scale);
            painter.drawImage(target,image);painter.drawText(target.adjusted(4,2,0,0),Qt::AlignLeft|Qt::AlignTop,QString("%1 %2").arg(m).arg(kind?"night":"day"));
        }
        if(painter.isActive())painter.end();
        if(sheet.isNull() || !sheet.save(parser.value("month-sheet"))){qCritical()<<"Cannot write month sheet";return 2;}
        return 0;
    }
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
    if(parser.isSet("font-report")) {
        QFile out(parser.value("font-report"));
        if(!out.open(QIODevice::WriteOnly|QIODevice::Text)){qCritical()<<out.errorString();return 2;}
        const QStringList samples{QString::fromUtf16(u"Saved Data Utility"),QString::fromUtf16(u"Paramètres réseau Ženska Ç ő ł"),
            QString::fromUtf16(u"○ ✕ △ □  ←↑→↓ ©™"),
            QString::fromUtf16(u"ゲーム 設定"),QString::fromUtf16(u"게임 설정"),QString::fromUtf16(u"游戏 设置")};
        bool windowsFallback=false;
        for(const auto& family:assets.fontFamilies()) {
            out.write(QString("== %1\n").arg(family).toUtf8());
            for(const auto& sample:samples) {
                QFont font(family);font.setPixelSize(28);
                QTextLayout layout(sample,font);layout.beginLayout();layout.createLine();layout.endLayout();
                QStringList runs;
                for(const auto& run:layout.glyphRuns()) {
                    const QString used=run.rawFont().familyName();runs<<QString("%1 (%2 glyphs)").arg(used).arg(run.glyphIndexes().size());
                    if(!used.startsWith("SCE-PS3"))windowsFallback=true;
                }
                out.write(QString("  %1  ->  %2\n").arg(sample,runs.join(" + ")).toUtf8());
            }
        }
        return windowsFallback?1:0;
    }
    if(parser.isSet("dump-cg")) {
        // Local analysis only: the folder receives firmware-derived shader text and must stay out of Git.
        int failures=0;
        for(const QString name:{"moyou/background","moyou/back_colours0","moyou/back_colours1","moyou/back_colours2","moyou/LinesController","moyou/LinesControllerQuin",
                                "moyou/LinesEdge","moyou/lines0","moyou/lines1","moyou/lines2","moyou/ffd_shader0","moyou/ffd_shader1","moyou/ffd_shader2","moyou/ffd_shader3",
                                "moyou/ffd_alpha_blend","glutils/Copy","glutils/GlareSource","glutils/GlareSourcePre","glutils/GlareSourceNoTone","glutils/Gaussian",
                                "glutils/ApplyDisplay","glutils/ToneApplyDisplay","glutils/AccGlare","glutils/DoubleGaussian","glutils/GaussBloomAndBlur","glutils/BlurAcc",
                                "glutils/BloomCone","glutils/ToneMap","glutils/ApplyGlare","particles/particles_quads","particles/particles_second"})
            for(const QString ext:{"vpo","fpo"}) {
                QByteArray bytes;
                try { bytes=assets.qglFile("lines","lib/"+name+"."+ext); } catch(const std::exception&) { continue; } // not every program has both stages
                if(bytes.isEmpty())continue;
                QString error;const auto program=xmb::CgProgram::load(bytes,&error);
                if(!program.valid()){qCritical()<<name<<ext<<error;++failures;continue;}
                QFile out(QDir(parser.value("dump-cg")).filePath(name.section('/',-1)+"."+ext+".glsl"));
                if(!QDir().mkpath(parser.value("dump-cg")) || !out.open(QIODevice::WriteOnly|QIODevice::Text)){++failures;continue;}
                QString header=QString("// %1.%2: %3 instructions\n").arg(name,ext).arg(program.instructionCount);
                for(const auto& p:program.parameters)header+=QString("// param %1 :%2 res=%3 idx=%4 slots=%5 defaults=%6\n").arg(p.name,p.semantic).arg(p.resource).arg(p.resourceIndex)
                    .arg([&]{QStringList l;for(int v:p.constantSlots)l<<QString::number(v);return l.join(',');}())
                    .arg([&]{QStringList l;for(float v:p.defaults)l<<QString::number(v);return l.join(',');}());
                for(int i=0;i<program.constants.size();++i){const auto& c=program.constants[i];header+=QString("// fc[%1] = %2 %3 %4 %5\n").arg(i).arg(c.x()).arg(c.y()).arg(c.z()).arg(c.w());}
                out.write((header+program.glsl).toUtf8());
            }
        return failures?1:0;
    }
    xmb::Shell shell(data,assets);
    QSettings windowSettings(QDir(data).filePath("settings.ini"),QSettings::IniFormat);
    QMainWindow window;window.setWindowTitle("re-ps3 — Native XMB");auto* view=new xmb::View(shell,assets,&window);window.setCentralWidget(view);window.resize(960,540);
    if(parser.isSet("measure-reference")) {
        window.show();
        QTimer::singleShot(1000,&app,[&]{
            const bool ok=view->measureReference(parser.value("measure-reference"));
            if(!ok)qCritical()<<"Background measurement failed";
            app.exit(ok?0:1);
        });
        return app.exec();
    }
    if(windowSettings.contains("window/geometry"))window.restoreGeometry(windowSettings.value("window/geometry").toByteArray());
    QObject::connect(&app,&QApplication::aboutToQuit,&window,[&]{windowSettings.setValue("window/geometry",window.saveGeometry());windowSettings.sync();});
    QObject::connect(&shell,&xmb::Shell::requestQuit,&app,&QApplication::quit);
    QObject::connect(&shell,&xmb::Shell::changed,&window,[&]{if(shell.fullscreen()!=window.isFullScreen()){if(shell.fullscreen())window.showFullScreen();else window.showNormal();}});
    if(shell.fullscreen())window.showFullScreen();else window.show();
    return app.exec();
}
