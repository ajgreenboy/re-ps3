// First-run setup (PHASES-HTPC D8, audit/FIRST-RUN.md): a controller-only sequence for a fresh install, drawn as ordinary XMB
// pages (rows on the page stack, the shell's own Box and Page messages, the XMB keyboard for a typed path), so Cross / Circle /
// D-pad reach it exactly as they reach every other page and Back always goes back one step.
//
//   Welcome > Firmware (dev_flash) > Library folders > Display (only with two or more screens) > Controller > Start at Sign-in > Finish
//
// Every step has a "Skip This Step" row and a "Continue" row; both only move on, and nothing is written until a row that says it
// writes is chosen. The sequence runs on the first start (no <data>/setup.json with completed:true), with --first-run, and from
// Settings > System Settings > Run Setup Again. The pages are INFERRED; the strings marked TRACED come from the firmware's own
// Initial Setup (sysconf_plugin.rco: msg_initsetup "Initial Setup", msg_initsetup_set2 "Press the %e button to finish initial setup.").
#include "shell.h"
#include "assets.h"
#include "autostart.h"
#include "pad.h"
#include "setup.h"
#include "service_installer.h"
#include "firmware_fetcher.h"
#include "firmware_extract.h"
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QScreen>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace xmb {
namespace {
constexpr auto kFakePad="XMB_SETUP_FAKE_PAD";   // "<hex buttons>[,lx,ly,rx,ry,l2,r2]": a synthetic pad for scripted checks of the live test
Entry row(const QString& id,const QString& title,const QString& value={},const QString& detail={},const QString& shown={},bool enabled=true) {
    Entry entry;
    entry.id=id;entry.title=title;entry.container=QStringLiteral("qgl/icontex");entry.texture=QStringLiteral("23n");
    entry.action=enabled?Action::Setup:Action::Unavailable;entry.value=value;entry.detail=detail;entry.fieldValue=shown;
    entry.disabled=!enabled;entry.source=QStringLiteral("native");
    return entry;
}
QString nativePath(const QString& path) { return QDir::toNativeSeparators(path); }
QString libraryTitle(const QString& kind) {
    return kind==QLatin1String("photo")?QObject::tr("Photo Folder"):kind==QLatin1String("music")?QObject::tr("Music Folder"):QObject::tr("Video Folder");
}
}
void Shell::setSetupContext(const QString& devFlashRoot,int kioskDisplay) {
    setup_.devFlash=devFlashRoot;setup_.kioskDisplay=kioskDisplay;
    setup_.firmwareOk=setup::checkFirmware(devFlashRoot).ok;
}
bool Shell::setupActive() const { return setup_.active; }
QStringList Shell::setupOrder() const {
    QStringList order{QStringLiteral("welcome"),QStringLiteral("firmware"),QStringLiteral("library")};
    if(QGuiApplication::screens().size()>1)order<<QStringLiteral("display");
    order<<QStringLiteral("pad")<<QStringLiteral("autostart")<<QStringLiteral("services")<<QStringLiteral("finish");
    return order;
}
QString Shell::setupStepOfTopPage() const {
    if(!setup_.active || pages_.isEmpty() || pages_.last().entries.isEmpty())return {};
    const QString id=pages_.last().entries.first().id;
    if(!id.startsWith(QLatin1String("setup:")))return {};
    return id.section(QLatin1Char(':'),1,1);
}
void Shell::beginSetup(bool forced) {
    if(setup_.active)return;
    qInfo().noquote()<<QString("setup: starting (%1), dev_flash %2 (%3)").arg(forced?"forced":"first run",nativePath(setup_.devFlash),
        setup_.firmwareOk?"complete":"missing or incomplete");
    setup_.active=true;setup_.baseDepth=int(pages_.size());setup_.chosenFlash.clear();setup_.capturing=false;setup_.lastInput.clear();
    setup_.padList.clear();setup_.heldText.clear();
    if(!setup_.timer) {
        setup_.timer=new QTimer(this);setup_.timer->setInterval(100);
        connect(setup_.timer,&QTimer::timeout,this,&Shell::setupPadTick);
    }
    setup_.timer->start();
    setupPush(QStringLiteral("welcome"));
    emit changed();
}
void Shell::setupPush(const QString& step) {
    if(pages_.size()>=32)return;
    Page page;
    page.title=step==QLatin1String("welcome")?assets_.text("sysconf_plugin","msg_initsetup",tr("Initial Setup"))   // TRACED: sysconf_plugin msg_initsetup
        :step==QLatin1String("firmware")?tr("Firmware Folder"):step==QLatin1String("library")?tr("Library Folders")
        :step==QLatin1String("display")?tr("Display"):step==QLatin1String("pad")?tr("Controller")
        :step==QLatin1String("autostart")?tr("Start at Sign-in"):step==QLatin1String("services")?tr("Services"):tr("Setup Complete");
    page.entries=setupRows(step);
    // Focus the row the step is about: "Continue" when the step has nothing to choose, else its first choice.
    pages_.push_back(std::move(page));
    if(step==QLatin1String("finish"))pages_.last().selection=0;
    qInfo().noquote()<<"setup: step"<<step<<"rows"<<pages_.last().entries.size();
}
void Shell::setupNext() {
    const QString step=setupStepOfTopPage();
    const QStringList order=setupOrder();
    const int at=int(order.indexOf(step));
    if(at<0 || at+1>=order.size())return;
    setupPush(order[at+1]);
}
void Shell::setupRefresh() {
    if(setup_.active && !pages_.isEmpty() && !setupStepOfTopPage().isEmpty()) {
        auto& page=pages_.last();
        const QString step=setupStepOfTopPage();
        if(step==QLatin1String("browse")) {setupChooserList();return;}
        const int selection=page.selection;
        page.entries=setupRows(step);
        page.selection=std::clamp(selection,0,std::max(0,int(page.entries.size())-1));
    }
}
bool Shell::setupHandleBack() {
    if(!setup_.active || setupStepOfTopPage()!=QLatin1String("welcome"))return false;
    showConfirm(tr("Leave Setup"),tr("Leave setup now?\nWhat you have chosen so far is kept. You can run it again from Settings > System Settings > Run Setup Again."),
                [this]{setupFinish(true);});
    return true;
}
void Shell::setupFinish(bool skipped) {
    QString error;
    if(!setup::markCompleted(dataDirectory_,skipped,&error))qWarning().noquote()<<"setup: could not write setup.json:"<<error;
    qInfo().noquote()<<"setup:"<<(skipped?"left early (recorded as completed, skipped)":"finished");
    const bool restart=!setup_.chosenFlash.isEmpty() && setup_.chosenFlash!=setup_.devFlash;
    while(pages_.size()>setup_.baseDepth)pages_.removeLast();
    setup_.active=false;setup_.capturing=false;
    if(setup_.timer)setup_.timer->stop();
    clearMessage();
    emit changed();
    if(restart) {
        qInfo().noquote()<<"setup: a new dev_flash was chosen:"<<nativePath(setup_.chosenFlash)<<"- restarting to load it";
        emit setupRestartRequested();
    }
}
QVector<Entry> Shell::setupRows(const QString& step) {
    QVector<Entry> rows;
    const QString prefix=QStringLiteral("setup:")+step;
    const auto cont=[&](const QString& title=QString(),const QString& detail=QString()){
        return row(prefix+QStringLiteral(":next"),title.isEmpty()?tr("Continue"):title,QStringLiteral("next"),
                   detail.isEmpty()?tr("Goes to the next step. Circle goes back to the one before."):detail);
    };
    const auto skip=[&]{return row(prefix+QStringLiteral(":skip"),tr("Skip This Step"),QStringLiteral("next"),
                                   tr("Moves on without changing anything for this step."));};
    if(step==QLatin1String("welcome")) {
        rows<<row(prefix,tr("Set Up This System"),QStringLiteral("next"),
            tr("A few short steps: the firmware folder, your photo, music and video folders, the display, your controller and starting at sign-in. Every step can be skipped and Circle goes back."));
        rows<<row(prefix+QStringLiteral(":leave"),tr("Skip Setup"),QStringLiteral("leave"),
            tr("Leaves setup. You can run it again from Settings > System Settings > Run Setup Again."));
    } else if(step==QLatin1String("firmware")) {
        const QString root=!setup_.chosenFlash.isEmpty()?setup_.chosenFlash:setup_.devFlash;
        const auto check=setup::checkFirmware(root);
        if(check.ok) {
            rows<<row(prefix,tr("dev_flash Found"),QStringLiteral("why"),
                tr("%1 holds the firmware files the XMB reads: its icons, text and fonts. It stays where it is; nothing is copied or changed.").arg(nativePath(root)),
                setup_.chosenFlash.isEmpty()?tr("In use"):tr("Saved - restarts at the end"));
            rows<<cont();
            rows<<row(prefix+QStringLiteral(":browse"),tr("Choose a Different Folder"),QStringLiteral("fw:browse"),
                tr("Pick another dev_flash folder with the controller. It is saved to xmb-config.json next to the program and loaded at the next start."));
            rows<<row(prefix+QStringLiteral(":download"),tr("Download from Sony"),QStringLiteral("fw:download"),
                tr("Downloads the latest PS3 system software from Sony and unpacks it into a dev_flash folder in the XMB's data folder. Needs an internet connection and a few minutes."));
        } else {
            QString what;
            if(!check.exists)what=tr("No dev_flash folder is set (looked for %1).").arg(nativePath(root));
            else what=tr("%1 is missing: %2").arg(nativePath(root),check.missing.join(QStringLiteral(", ")));
            rows<<row(prefix,tr("dev_flash Not Found"),QStringLiteral("why"),
                what+QLatin1Char(' ')+tr("Without it the XMB has no icons, text or fonts. Cross explains what to look for."),tr("Needed"));
            for(const QString& found:setup::findFirmwareCandidates())
                rows<<row(prefix+QStringLiteral(":found:")+found,tr("Use %1").arg(nativePath(found)),QStringLiteral("fw:use|")+found,
                    tr("A complete dev_flash was found here. Cross uses it."));
            rows<<row(prefix+QStringLiteral(":browse"),tr("Browse for the Folder"),QStringLiteral("fw:browse"),
                tr("Pick the dev_flash folder with the controller."));
            rows<<row(prefix+QStringLiteral(":type"),tr("Type the Path"),QStringLiteral("fw:type"),
                tr("Opens the on-screen keyboard for a path such as D:\\PS3\\dev_flash."));
            rows<<row(prefix+QStringLiteral(":download"),tr("Download from Sony"),QStringLiteral("fw:download"),
                tr("Downloads the latest PS3 system software from Sony and unpacks it into a dev_flash folder in the XMB's data folder. Needs an internet connection and a few minutes."));
            rows<<cont(tr("Continue Without It"),tr("Goes on without firmware. You can come back with Settings > System Settings > Run Setup Again."));
        }
        if(!check.ok)rows<<skip();
    } else if(step==QLatin1String("library")) {
        const auto current=[&](const QString& kind){return preferences_.value(QStringLiteral("library/")+kind).toString();};
        const QString photo=setup::defaultLibraryFolder(QStringLiteral("photo")),music=setup::defaultLibraryFolder(QStringLiteral("music")),
                      video=setup::defaultLibraryFolder(QStringLiteral("video"));
        QStringList defaults;
        if(!photo.isEmpty())defaults<<tr("Photo: %1").arg(nativePath(photo));
        if(!music.isEmpty())defaults<<tr("Music: %1").arg(nativePath(music));
        if(!video.isEmpty())defaults<<tr("Video: %1").arg(nativePath(video));
        rows<<row(prefix,tr("Use the Windows Folders"),QStringLiteral("lib:defaults"),
            defaults.isEmpty()?tr("Windows names no Pictures, Music or Videos folder for this account."):
            tr("Sets the three library folders to your Windows Pictures, Music and Videos folders. %1").arg(defaults.join(QStringLiteral("; "))),
            {},!defaults.isEmpty());
        for(const char* kind:{"photo","music","video"}) {
            const QString path=current(QLatin1String(kind));
            rows<<row(prefix+QStringLiteral(":")+QLatin1String(kind),libraryTitle(QLatin1String(kind)),QStringLiteral("lib:browse|")+QLatin1String(kind),
                tr("The folder the %1 column lists. Cross picks it with the controller; the keyboard can type a path there too.")
                    .arg(QLatin1String(kind)==QLatin1String("photo")?tr("Photo"):QLatin1String(kind)==QLatin1String("music")?tr("Music"):tr("Video")),
                path.isEmpty()?tr("Not set"):nativePath(path));
        }
        rows<<cont()<<skip();
    } else if(step==QLatin1String("display")) {
        const auto config=setup::loadConfig(setup::configPath());
        const int saved=config.contains(QStringLiteral("display"))?config.value(QStringLiteral("display")).toInt(-1):setup_.kioskDisplay;
        const auto screens=QGuiApplication::screens();
        rows<<row(prefix,tr("Choose the Screen"),QStringLiteral("none"),
            tr("This is the screen the XMB takes over in appliance (kiosk) mode. Pick the TV. The choice is saved to xmb-config.json and used from the next start."),{},false);
        rows.first().action=Action::Setup;rows.first().disabled=false;
        for(int i=0;i<screens.size();++i) {
            QScreen* screen=screens[i];
            const auto geometry=screen->geometry();
            QString title=tr("Screen %1: %2 x %3").arg(i+1).arg(geometry.width()).arg(geometry.height());
            if(screen==QGuiApplication::primaryScreen())title+=tr(" (primary)");
            rows<<row(prefix+QStringLiteral(":screen:")+QString::number(i),title,QStringLiteral("disp:set|")+QString::number(i),
                tr("%1, %2 Hz. Cross makes this the XMB's screen.").arg(screen->name()).arg(int(screen->refreshRate()+.5)),
                i==saved?tr("Selected"):QString());
        }
        rows<<cont()<<skip();
    } else if(step==QLatin1String("pad")) {
        rows<<row(prefix,tr("Controllers"),QStringLiteral("pad:refresh"),
            tr("PlayStation controllers (DualShock 3/4, DualSense) and Xbox-layout controllers are read directly. Plug one in or pair it and it appears here."),
            setup_.padList.isEmpty()?tr("None found"):setup_.padList);
        rows<<row(prefix+QStringLiteral(":held"),tr("Held Now"),QStringLiteral("pad:refresh"),
            tr("The buttons a controller reports right now, live. Press and hold some."),setup_.heldText.isEmpty()?tr("Nothing"):setup_.heldText);
        rows<<row(prefix+QStringLiteral(":test"),tr("Test Every Button"),QStringLiteral("pad:test"),
            setup_.capturing?tr("Listening: every button and direction is shown under Last Input and nothing moves on. It stops after 3 seconds without a press.")
                            :tr("Cross starts the test: the pad stops navigating and shows each press. It ends by itself 3 seconds after the last one."),
            setup_.capturing?tr("Listening"):QString());
        rows<<row(prefix+QStringLiteral(":last"),tr("Last Input"),QStringLiteral("pad:refresh"),
            tr("The last button, direction or key the XMB received."),setup_.lastInput.isEmpty()?tr("None yet"):setup_.lastInput);
        rows<<cont()<<skip();
    } else if(step==QLatin1String("autostart")) {
        QString reason;
        const bool available=autostart::available(&reason);
        Entry entry=row(prefix,tr("Start at Sign-in"),QStringLiteral("auto:toggle"),
            tr("Starts the XMB full-screen with its crash watchdog when this Windows account signs in. Turns the scheduled task on or off for this account only; Windows automatic sign-in is a separate step you set yourself."),
            available?(autostart::enabled()?tr("On"):tr("Off")):tr("Unavailable"),available);
        if(!available)entry.detail=reason;
        rows<<entry<<cont()<<skip();
    } else if(step==QLatin1String("services")) {
        for(auto service:serviceAvailability()) {
            if(service.name==QLatin1String("Steam") && QFileInfo(QDir(steamRoot_).filePath(QStringLiteral("steam.exe"))).isFile())service.installed=true;
            Entry entry=row(prefix+QStringLiteral(":")+service.name,service.installed?service.name:tr("Get %1").arg(service.name),service.name,
                service.installed?tr("Installed"):tr("Download and run the official installer."),service.installed?tr("Installed"):QString());
            if(service.installed)entry.action=Action::Information;
            rows<<entry;
        }
        rows<<cont();
    } else if(step==QLatin1String("finish")) {
        const auto path=[&](const char* kind){const QString value=preferences_.value(QStringLiteral("library/")+QLatin1String(kind)).toString();return value.isEmpty()?tr("not set"):nativePath(value);};
        const QString root=!setup_.chosenFlash.isEmpty()?setup_.chosenFlash:setup_.devFlash;
        const bool restart=!setup_.chosenFlash.isEmpty() && setup_.chosenFlash!=setup_.devFlash;
        // TRACED: sysconf_plugin msg_initsetup_set2, "Press the %e button to finish initial setup." (%e is the enter-button icon).
        QString finish=assets_.text("sysconf_plugin","msg_initsetup_set2",tr("Press the %e button to finish initial setup."));
        finish.replace(QStringLiteral("%e"),QStringLiteral("Cross"));
        rows<<row(prefix,tr("Finish"),QStringLiteral("finish"),
            finish+(restart?QLatin1Char(' ')+tr("The XMB restarts once to load the new dev_flash."):QString()),restart?tr("Restarts"):QString());
        rows<<row(prefix+QStringLiteral(":sum-fw"),tr("dev_flash"),QStringLiteral("none"),{},setup::checkFirmware(root).ok?nativePath(root):tr("not set"),false);
        rows<<row(prefix+QStringLiteral(":sum-photo"),tr("Photo Folder"),QStringLiteral("none"),{},path("photo"),false);
        rows<<row(prefix+QStringLiteral(":sum-music"),tr("Music Folder"),QStringLiteral("none"),{},path("music"),false);
        rows<<row(prefix+QStringLiteral(":sum-video"),tr("Video Folder"),QStringLiteral("none"),{},path("video"),false);
        for(Entry& entry:rows)if(entry.action==Action::Unavailable){entry.action=Action::Setup;entry.disabled=false;}
    }
    return rows;
}
void Shell::activateSetup(const Entry& entry) {
    if(entry.id==QLatin1String("run_setup")) {beginSetup(true);return;}
    if(entry.id.startsWith(QLatin1String("setup:services:")) && entry.value!=QLatin1String("next")) {
        for(const auto& service:serviceAvailability())if(service.name==entry.value && !service.installed) {
            auto* watcher=new QFutureWatcher<QPair<QString,QString>>(this);
            connect(watcher,&QFutureWatcher<QPair<QString,QString>>::finished,this,[this,watcher,service]{
                const auto result=watcher->result();watcher->deleteLater();
                if(!result.second.isEmpty())showMessage(tr("Services"),tr("Could not download %1:\n%2").arg(service.name,result.second),MessageKind::Page);
                else if(qEnvironmentVariable("XMB_SERVICES_DRY_RUN")==QLatin1String("1"))qInfo().noquote()<<"would run"<<result.first;
                else if(!QDesktopServices::openUrl(QUrl::fromLocalFile(result.first)))showMessage(tr("Services"),tr("Could not launch the %1 installer.").arg(service.name),MessageKind::Page);
                emit changed();
            });
            watcher->setFuture(QtConcurrent::run([service]{QString path,error;downloadInstaller(service.installerUrl,service.name,&path,&error);return qMakePair(path,error);}));
            showMessage(tr("Services"),tr("Downloading the %1 installer.").arg(service.name),MessageKind::Page);
            emit changed();return;
        }
        return;
    }
    const QString command=entry.value.section(QLatin1Char('|'),0,0),argument=entry.value.section(QLatin1Char('|'),1);
    if(command==QLatin1String("next")) {setupNext();emit changed();return;}
    if(command==QLatin1String("leave")) {
        showConfirm(tr("Leave Setup"),tr("Leave setup now?\nWhat you have chosen so far is kept. You can run it again from Settings > System Settings > Run Setup Again."),
                    [this]{setupFinish(true);});
        emit changed();return;
    }
    if(command==QLatin1String("finish")) {setupFinish(false);return;}
    if(command==QLatin1String("why")) {
        showMessage(tr("dev_flash"),
            tr("dev_flash is the folder of PlayStation 3 system software files from your own console or your own RPCS3 installation. The XMB reads its icons, text, sounds and fonts from it. This program never downloads or includes it.\n\nLook for a folder named dev_flash that contains vsh, data and sys, for example in your RPCS3 folder. It must hold:\n%1")
                .arg(setup::requiredFirmwareFiles().join(QStringLiteral("\n"))),MessageKind::Page);
        emit changed();return;
    }
    if(command==QLatin1String("fw:use")) {setupUseFolder(argument);return;}
    if(command==QLatin1String("fw:browse")) {setupOpenChooser(QStringLiteral("firmware"),setup_.chosenFlash.isEmpty()?setup_.devFlash:setup_.chosenFlash);return;}
    if(command==QLatin1String("fw:type")) {
        setup_.browseKind=QStringLiteral("firmware");
        emit requestJellyfinFieldEdit(QStringLiteral("setup:path"),tr("dev_flash Folder"),setup_.devFlash,false);return;
    }
    if(command==QLatin1String("fw:download")) {setupDownloadFirmware();return;}
    if(command==QLatin1String("lib:defaults")) {
        for(const char* kind:{"photo","music","video"}) {
            const QString path=setup::defaultLibraryFolder(QLatin1String(kind));
            if(!path.isEmpty())preferences_.setValue(QStringLiteral("library/")+QLatin1String(kind),path);
        }
        qInfo()<<"setup: library folders set to the Windows folders";
        interactionAudio_.play(InteractionAudio::Cue::Accept);
        setupRefresh();emit changed();return;
    }
    if(command==QLatin1String("lib:browse")) {
        const QString current=preferences_.value(QStringLiteral("library/")+argument).toString();
        const QString start=!current.isEmpty()&&QFileInfo(current).isDir()?current:setup::defaultLibraryFolder(argument);
        setupOpenChooser(argument,start);return;
    }
    if(command==QLatin1String("pick")) {setupUseFolder(argument);return;}
    if(command==QLatin1String("cd")) {setup_.browsePath=argument;setupChooserList();emit changed();return;}
    if(command==QLatin1String("type")) {
        emit requestJellyfinFieldEdit(QStringLiteral("setup:path"),setup_.browseKind==QLatin1String("firmware")?tr("dev_flash Folder"):libraryTitle(setup_.browseKind),
                                      setup_.browsePath,false);return;
    }
    if(command==QLatin1String("disp:set")) {
        auto config=setup::loadConfig(setup::configPath());
        config.insert(QStringLiteral("display"),argument.toInt());
        QString error;
        if(setup::saveConfig(setup::configPath(),config,&error)) {
            qInfo().noquote()<<"setup: display"<<argument<<"saved to"<<nativePath(setup::configPath());
            interactionAudio_.play(InteractionAudio::Cue::Accept);
        } else showMessage(tr("Display"),tr("Could not save the choice to %1:\n%2").arg(nativePath(setup::configPath()),error),MessageKind::Page);
        setupRefresh();emit changed();return;
    }
    if(command==QLatin1String("pad:refresh")) {emit changed();return;}
    if(command==QLatin1String("pad:test")) {
        setup_.capturing=true;setup_.idle.restart();setup_.lastInput.clear();
        qInfo()<<"setup: pad test started (navigation is swallowed until 3 s without input)";
        setupRefresh();emit changed();return;
    }
    if(command==QLatin1String("auto:toggle")) {
        // The write runs PowerShell + the scheduler read-back, so it goes off the GUI thread (D3); the row
        // refresh and the result cue happen in the completion handler once the state is read back.
        const bool want=!autostart::enabled();
        autostart::setEnabledAsync(want,[this](bool ok,const QString& error){
            if(!ok)showMessage(tr("Start at Sign-in"),tr("It could not be changed:\n%1").arg(error),MessageKind::Page);
            else interactionAudio_.play(InteractionAudio::Cue::Accept);
            setupRefresh();emit changed();
        });
        return;
    }
    emit changed();
}
// ---- the folder chooser: a page of rows, one per sub-folder ----
void Shell::setupOpenChooser(const QString& kind,const QString& start) {
    setup_.browseKind=kind;
    setup_.browsePath=start.isEmpty()||!QFileInfo(start).isDir()?QString():QDir::cleanPath(start);
    Page page;
    page.title=kind==QLatin1String("firmware")?tr("Choose the dev_flash Folder"):tr("Choose the %1").arg(libraryTitle(kind));
    page.entries<<row(QStringLiteral("setup:browse"),tr("Use This Folder"),{},{});
    if(pages_.size()<32)pages_.push_back(std::move(page));
    setupChooserList();
    emit changed();
}
void Shell::setupChooserList() {
    if(pages_.isEmpty())return;
    auto& page=pages_.last();
    QVector<Entry> rows;
    const QString here=setup_.browsePath;
    const bool firmware=setup_.browseKind==QLatin1String("firmware");
    QString detail=here.isEmpty()?tr("Pick a drive below."):firmware?tr("Cross uses %1 as the dev_flash folder (it is checked first).").arg(nativePath(here)):tr("Cross uses %1.").arg(nativePath(here));
    rows<<row(QStringLiteral("setup:browse"),here.isEmpty()?tr("Drives"):tr("Use This Folder"),here.isEmpty()?QStringLiteral("none"):QStringLiteral("pick|")+here,detail,
              here.isEmpty()?QString():nativePath(here),!here.isEmpty());
    if(here.isEmpty()){rows.first().action=Action::Setup;rows.first().disabled=false;}
    if(!here.isEmpty())rows<<row(QStringLiteral("setup:browse-up"),tr("Up One Level"),QStringLiteral("cd|")+setup::parentOf(here),tr("Goes to the folder above."));
    rows<<row(QStringLiteral("setup:browse-type"),tr("Type a Path"),QStringLiteral("type"),tr("Opens the on-screen keyboard."));
    const int fixed=int(rows.size());
    for(const QString& folder:setup::browseFolders(here)) {
        const QString label=here.isEmpty()?nativePath(folder):QFileInfo(folder).fileName();
        rows<<row(QStringLiteral("setup:browse-dir:")+folder,label,QStringLiteral("cd|")+folder,tr("Opens %1.").arg(nativePath(folder)));
    }
    page.entries=rows;
    page.selection=int(rows.size())>fixed?fixed:0;   // land on the first folder when there is one, else on the row that uses this one
    if(here.isEmpty())page.selection=std::min(page.selection,int(rows.size())-1);
}
void Shell::setupUseFolder(const QString& folder) {
    const QString kind=setup_.browseKind.isEmpty()?QStringLiteral("firmware"):setup_.browseKind;
    if(folder.isEmpty())return;
    if(kind==QLatin1String("firmware")) {
        const auto check=setup::checkFirmware(folder);
        if(!check.ok) {
            showMessage(tr("This is not a dev_flash folder"),
                check.exists?tr("%1 is missing:\n%2").arg(nativePath(folder),check.missing.join(QStringLiteral("\n")))
                            :tr("%1 does not exist.").arg(nativePath(folder)),MessageKind::Page);
            emit changed();return;
        }
        auto config=setup::loadConfig(setup::configPath());
        config.insert(QStringLiteral("devFlash"),nativePath(folder));
        QString error;
        if(!setup::saveConfig(setup::configPath(),config,&error)) {
            showMessage(tr("dev_flash"),tr("Could not save the folder to %1:\n%2").arg(nativePath(setup::configPath()),error),MessageKind::Page);
            emit changed();return;
        }
        qInfo().noquote()<<"setup: dev_flash"<<nativePath(folder)<<"saved to"<<nativePath(setup::configPath());
        setup_.chosenFlash=QDir::cleanPath(folder);
        interactionAudio_.play(InteractionAudio::Cue::Accept);
    } else {
        if(!QFileInfo(folder).isDir()) {
            showMessage(tr("Folder"),tr("%1 does not exist.").arg(nativePath(folder)),MessageKind::Page);
            emit changed();return;
        }
        preferences_.setValue(QStringLiteral("library/")+kind,QDir::cleanPath(folder));
        qInfo().noquote()<<"setup: library/"<<kind<<"set to"<<nativePath(folder);
        interactionAudio_.play(InteractionAudio::Cue::Accept);
    }
    // Leave the chooser: the step page under it shows the new value.
    if(!pages_.isEmpty() && setupStepOfTopPage()==QLatin1String("browse"))pages_.removeLast();
    setupRefresh();emit changed();
}
void Shell::setupDownloadFirmware() {
    const QString dest=QDir::cleanPath(dataDirectory_+QStringLiteral("/dev_flash"));
    if(qEnvironmentVariable("XMB_FIRMWARE_DRY_RUN")==QLatin1String("1")) {
        qInfo().noquote()<<"would download the PS3 firmware and extract to"<<nativePath(dest);
        showMessage(tr("Firmware"),tr("Dry run: would download the PS3 system software from Sony and unpack it into %1.").arg(nativePath(dest)),MessageKind::Page);
        emit changed();return;
    }
    auto* watcher=new QFutureWatcher<QPair<QString,QString>>(this);
    connect(watcher,&QFutureWatcher<QPair<QString,QString>>::finished,this,[this,watcher,dest]{
        const auto result=watcher->result();watcher->deleteLater();
        if(!result.second.isEmpty()) {
            showMessage(tr("Firmware"),tr("Could not download and unpack the system software:\n%1").arg(result.second),MessageKind::Page);
            emit changed();return;
        }
        auto config=setup::loadConfig(setup::configPath());
        config.insert(QStringLiteral("devFlash"),nativePath(result.first));
        QString error;
        if(!setup::saveConfig(setup::configPath(),config,&error)) {
            showMessage(tr("Firmware"),tr("Downloaded and unpacked, but could not save the folder to %1:\n%2").arg(nativePath(setup::configPath()),error),MessageKind::Page);
            emit changed();return;
        }
        setup_.chosenFlash=QDir::cleanPath(result.first);
        qInfo().noquote()<<"setup: firmware downloaded and unpacked to"<<nativePath(result.first);
        interactionAudio_.play(InteractionAudio::Cue::Accept);
        showMessage(tr("Firmware"),tr("Downloaded and unpacked to %1. It loads at the next restart.").arg(nativePath(result.first)),MessageKind::Page);
        setupRefresh();emit changed();
    });
    watcher->setFuture(QtConcurrent::run([dest]{
        QString error;
        const auto release=xmb::latestFirmware(QStringLiteral("us"),&error);
        if(!error.isEmpty())return qMakePair(QString(),error);
        QString pup;
        if(!xmb::downloadPup(release.url,&pup,&error))return qMakePair(QString(),error);
        if(!xmb::extractPup(pup,dest,&error))return qMakePair(QString(),error);
        return qMakePair(dest,QString());
    }));
    showMessage(tr("Firmware"),tr("Downloading the latest PS3 system software from Sony and unpacking it. This takes a few minutes."),MessageKind::Page);
    emit changed();
}
void Shell::setupText(const QString& field,const QString& value) {
    Q_UNUSED(field)
    const QString path=QDir::cleanPath(value.trimmed());
    if(path.isEmpty() || !setup_.active)return;
    if(setupStepOfTopPage()==QLatin1String("browse")) {
        // Typed into a chooser: a folder that exists opens in it, one that does not says so.
        if(QFileInfo(path).isDir()){setup_.browsePath=path;setupChooserList();}
        else showMessage(tr("Folder"),tr("%1 does not exist.").arg(nativePath(path)),MessageKind::Page);
        emit changed();return;
    }
    // Typed on the Firmware page itself (no chooser): use it straight away.
    setup_.browseKind=QStringLiteral("firmware");
    setupUseFolder(path);
}
// ---- the pad test ----
bool Shell::setupCaptureInput(const QString& label) {
    if(!setup_.capturing)return false;
    setup_.lastInput=label;setup_.idle.restart();
    setupRefresh();emit changed();
    return true;
}
void Shell::setupPadTick() {
    if(!setup_.active || setupStepOfTopPage()!=QLatin1String("pad")) {setup_.capturing=false;return;}
    // Controllers: re-read at most every 2 s (an empty XInput slot is slow to probe).
    bool dirty=false;
    if(!setup_.padListAge.isValid() || setup_.padListAge.elapsed()>2000) {
        setup_.padListAge.restart();
        QStringList list;
        pad::Info info;
        if(pad::info(info)) {
            QString text=QStringLiteral("%1 (%2)").arg(pad::kindName(info.kind),pad::transportName(info.transport));
            if(info.batteryPercent>=0)text+=tr(", battery %1%").arg(info.batteryPercent);
            list<<text;
        }
        for(const auto& xinput:pad::xinputPads())list<<tr("Xbox-layout controller, slot %1, battery %2").arg(xinput.slot+1).arg(xinput.battery);
        if(qEnvironmentVariableIsSet(kFakePad))list<<tr("Test controller (synthetic, %1)").arg(QLatin1String(kFakePad));
        const QString text=list.join(QStringLiteral("; "));
        if(text!=setup_.padList){setup_.padList=text;dirty=true;}
    }
    // Held now: HID pad and XInput slots, merged; a synthetic state replaces both when XMB_SETUP_FAKE_PAD is set.
    quint16 buttons=0;float lx=0,ly=0,rx=0,ry=0,l2=0,r2=0;
    if(qEnvironmentVariableIsSet(kFakePad)) {
        const QStringList parts=qEnvironmentVariable(kFakePad).split(QLatin1Char(','));
        buttons=quint16(parts.value(0).toUInt(nullptr,16));
        lx=parts.value(1).toFloat();ly=parts.value(2).toFloat();rx=parts.value(3).toFloat();ry=parts.value(4).toFloat();l2=parts.value(5).toFloat();r2=parts.value(6).toFloat();
    } else {
        pad::State hid;pad::XInputState xinput;
        if(pad::latest(hid)){buttons|=hid.buttons;lx=hid.lx;ly=hid.ly;rx=hid.rx;ry=hid.ry;l2=hid.l2;r2=hid.r2;}
        if(pad::xinputLatest(xinput)){buttons|=xinput.buttons;lx=std::abs(xinput.lx)>std::abs(lx)?xinput.lx:lx;ly=std::abs(xinput.ly)>std::abs(ly)?xinput.ly:ly;
            rx=xinput.rx;ry=xinput.ry;l2=std::max(l2,xinput.l2);r2=std::max(r2,xinput.r2);}
    }
    QStringList held;
    if(const QString names=setup::buttonNames(buttons);!names.isEmpty())held<<names;
    if(const QString axes=setup::axisText(lx,ly,rx,ry,l2,r2);!axes.isEmpty())held<<axes;
    const QString heldText=held.join(QStringLiteral("; "));
    if(heldText!=setup_.heldText) {
        setup_.heldText=heldText;dirty=true;
        if(setup_.capturing && !heldText.isEmpty()){setup_.idle.restart();setup_.lastInput=heldText;}
    }
    if(setup_.capturing && setup_.idle.elapsed()>3000){setup_.capturing=false;qInfo()<<"setup: pad test ended (3 s without input)";dirty=true;}
    if(dirty){setupRefresh();emit changed();}
}
}  // namespace xmb
