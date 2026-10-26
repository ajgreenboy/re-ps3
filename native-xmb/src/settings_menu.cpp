#include "shell.h"
#include "assets.h"
#include "host.h"
#include "p3t.h"
#include <cmath>
#include <QTimeZone>
#include <QDateTime>
#include <QImageReader>
#include <QFileInfo>
#include <QSysInfo>

namespace xmb {
namespace {
struct Choice {QString title;QVariant value;};
QVector<Choice> choices(const QString& key,Assets& assets) {
    const auto text=[&](const QString& id,const QString& fallback){return assets.text("sysconf_plugin",id,fallback);};
    if(key=="appearance/colour")return {{"Original",0},{"Blue",1},{"Purple",2},{"Red",3},{"Green",4},{"Silver",5}};
    // The theme chosen with Select Theme joins this list in openChoices (it is read from the user's .p3t).
    if(key=="appearance/theme")return {{text("msg_theme_default","Original"),QString{}}};
    if(key=="appearance/background")return {{text("msg_theme_default","Original"),0},{text("msg_theme_classic","Classic"),1},{"Wallpaper",2}};
    if(key=="appearance/font") {
        QVector<Choice> result;const auto families=assets.fontFamilies();
        for(int i=0;i<families.size();++i)result.push_back({i==0?text("msg_theme_default","Original"):families[i],i});
        return result;
    }
    if(key=="clock/dateFormat")return {{text("msg_datetime_fmt_dmy","DD/MM/YYYY"),0},{text("msg_datetime_fmt_mdy","MM/DD/YYYY"),1},{text("msg_datetime_fmt_ymd","YYYY/MM/DD"),2}};
    if(key=="clock/hourFormat")return {{text("msg_time_fmt_24","24-Hour Clock"),0},{text("msg_time_fmt_12","12-Hour Clock"),1}};
    if(key=="clock/timeZone") {
        static const QVector<Choice> zones=[] {
            QVector<Choice> result{{"System",QString("System")}};
            for(const auto& id:QTimeZone::availableTimeZoneIds())result.push_back({QString::fromUtf8(id),QString::fromUtf8(id)});
            return result;
        }();
        return zones;
    }
    if(key=="audio/volume" || key=="host/volume") {QVector<Choice> result;for(int i=0;i<=100;i+=10)result.push_back({QString::number(i)+"%",i});return result;}
    if(key=="photo/interval")return {{"5 seconds",5000},{"10 seconds",10000},{"15 seconds",15000},{"30 seconds",30000}};
    if(key=="photo/fit")return {{"Fit",0},{"Fill",1}};
    if(key=="media/repeatMode")return {{"Off",0},{"Repeat One",1},{"Repeat All",2}};
    if(key=="power/idleMinutes")return {{"Off",0},{"15 minutes",15},{"30 minutes",30},{"1 hour",60},{"2 hours",120}};
    return {{"Off",false},{"On",true}};
}
Entry option(QString id,QString title,QString detail,QString setting,Action action=Action::SettingChoices,QString value={}) {
    Entry entry{std::move(id),std::move(title),std::move(detail),"qgl/icontex","23n",action,std::move(value)};
    entry.setting=std::move(setting);entry.themeIcon="icon_setting_item";return entry;
}
}
void Shell::openSettings(const QString& id) {
    if(pages_.size()>=32)return;
    const auto tr=[&](const QString& key,const QString& fallback){return assets_.text("sysconf_plugin",key,fallback);};
    const auto setting=[&](const QString& name,const QString& title,const QString& key){
        return option(name,tr("msg_"+name,title),tr("msg_"+name+"_explanation",{}),key);
    };
    Page page;
    if(id=="theme") {
        page.title=tr("msg_theme_settings","Theme Settings");
        page.entries={option("theme",tr("msg_theme","Theme"),tr("msg_theme_explanation",{}),"appearance/theme"),
            option("themefile","Select Theme","Opens a PS3 theme file (.p3t): its background and icons replace the system's.",{},Action::Theme),
            setting("color","Color","appearance/colour"),setting("background","Background","appearance/background"),
            option("wallpaper","Select Wallpaper","Selects a local image for the XMB background.",{},Action::Wallpaper),
            setting("font","Font","appearance/font")};
    } else if(id=="date") {
        page.title=tr("msg_datetime_setting","Date and Time Settings");
        page.entries={option("datetime",tr("msg_datetime","Date and Time"),tr("msg_datetime_explanation",{}),{},Action::Information,
                "Date and Time\n\n"+QDateTime::currentDateTime().toString("dddd, d MMMM yyyy  HH:mm:ss t")+"\n\nThe clock follows the host system. Display format and time zone can be changed within XMB."),
            setting("date_fmt","Date Format","clock/dateFormat"),setting("time_fmt","Time Format","clock/hourFormat"),
            setting("timezone","Time Zone","clock/timeZone")};
    } else if(id=="system") {
        // Order and labels as the firmware's System Settings (reference video 95 s); native-only entries keep their action.
        const auto info=[&](const QString& key,const QString& fallback){return option(key,tr("msg_"+key,fallback),tr("msg_"+key+"_explanation",{}),{},Action::Unavailable);};
        page.title=tr("msg_console_setting","System Settings");
        page.entries={info("console_lang","System Language"),info("keyboardtype","Keyboard Type"),info("keyboard_input_mode","Keyboard Entry Method"),
            option("name","System Name","Sets the name of this native XMB instance.","system/name",Action::EditName),
            info("mediaserver_connection","Media Server Connection"),info("disc_utility","Format Utility"),info("backup_utility","Backup Utility"),
            info("init_setting","Restore Default Settings"),info("init_console","Restore PS3™ System"),info("ex_os_install","Install Other OS"),
            option("information",tr("msg_console_info","System Information"),tr("msg_console_info_explanation",{}),{},Action::Information),
            info("aboutps3","About PlayStation®3 (PS3™)")};
    } else if(id=="update") { // System Update (software_update_plugin)
        const auto su=[&](const QString& key,const QString& fallback){return assets_.text("software_update_plugin",key,fallback);};
        page.title=tr("msg_update","System Update");
        page.entries={option("net",su("msg_update_net","Update via Internet"),{},{},Action::Information,
                su("msg_update_net","Update via Internet")+"\n\nThe native XMB is updated with its own repository; there is no PS3 system software to download."),
            option("media",su("msg_update_media","Update via Storage Media"),{},{},Action::Unavailable)};
    } else if(id=="sound") {
        page.title=tr("msg_sound_setting","Sound Settings");
        page.entries={option("sound_output_setting",tr("msg_sound_output_setting","Audio Output Settings"),tr("msg_sound_output_setting_explanation",{}),{},Action::Information,"host:audio"),
            option("audio_multi_output",tr("msg_audio_multi_output","Audio Multi-Output"),tr("msg_audio_multi_output_explanation",{}),{},Action::Unavailable),
            option("system_volume","System Volume","Sets the master volume of this computer's playback device.","host/volume"),
            option("system_mute","Mute","Mutes this computer's playback device.","host/muted"),
            option("volume","XMB Volume","Sets the volume for media and menu sounds.","audio/volume"),
            option("xmb_mute","XMB Mute","Mutes native XMB audio only.","audio/muted"),
            option("keytone","Key Tone","Plays the current theme's menu sounds, with original XMB sounds as fallback.","audio/keyTone")};
    } else if(id=="music" || id=="video") {
        page.title=id=="music"?"Music Settings":"Video Settings";
        page.entries={option("repeat","Repeat","Stops at the end, repeats one file, or repeats the current folder queue.","media/repeatMode"),
            option("shuffle","Shuffle","Plays every file in a random order before starting another cycle.","media/shuffle"),
            option("volume","Volume","Sets native media playback volume.","audio/volume")};
    } else if(id=="photo") {
        page.title="Photo Settings";
        page.entries={option("interval","Slideshow Interval","Sets how long each photo remains on screen.","photo/interval"),
            option("fit","Display Mode","Fits the whole photo or fills the display area.","photo/fit"),
            option("repeat","Repeat","Restarts a slideshow after the final photo.","media/repeat")};
    } else if(id=="display") {
        page.title=tr("msg_display_setting","Display Settings");
        page.entries={option("display_output_setting",tr("msg_display_output_setting","Video Output Settings"),tr("msg_display_output_setting_explanation",{}),{},Action::Information,"host:display"),
            option("tvtype",tr("msg_tvtype","TV Type"),tr("msg_tvtype_explanation",{}),{},Action::Unavailable),
            option("fullscreen","Full Screen","Expands the native XMB window to fill the display.","display/fullscreen")};
    } else if(id=="power") {
        page.title=tr("msg_power_saving_setting","Power Save Settings");
        page.entries={option("idle","System Auto-Off","Closes native XMB after inactivity. Media playback keeps it awake; the host computer remains running.","power/idleMinutes"),
            option("controller","Controller Auto-Off","Controller hardware power management is not available through XInput.",{},Action::Unavailable)};
    } else if(id=="device") {
        page.title="Accessory Settings";
        page.entries={option("controller","Controller Settings","XInput D-pad, A and B control navigation when this window has focus.",{},Action::Information,
            "Controller Settings\n\nXInput controller 1\nD-pad: Navigate\nA: Enter\nB: Back\n\nKeyboard: Arrow keys, Enter, Backspace\nPage Up / Down: Move ten items"),
            option("bluetooth","Manage Bluetooth Devices","Native Bluetooth pairing is not implemented.",{},Action::Unavailable)};
    } else if(id=="messages") { // Friends > Message Box (category_friend.xml)
        const auto ft=[&](const QString& key,const QString& fallback){return assets_.text("explore_category_friend",key,fallback);};
        const auto none=ft("msg_error_no_msg","There are no messages.");
        page.title=ft("msg_msg_box","Message Box");
        auto received=option("received",ft("msg_received_box","Received"),{},{},Action::Information,none);received.container="explore_category_friend";received.texture="item_tex_InBox";received.themeIcon="icon_mbox_received";
        auto sent=option("sent",ft("msg_sent_box","Sent"),{},{},Action::Information,none);sent.container="explore_category_friend";sent.texture="item_tex_OutBox";sent.themeIcon="icon_mbox_sent";
        auto create=option("create",ft("msg_new_msg","Create Message"),{},{},Action::Unavailable);create.container="explore_category_friend";create.texture="item_tex_NewMessage";create.themeIcon="icon_mbox_create";
        page.entries={received,sent,create};
    } else if(id=="chat" || id=="security" || id=="remote" || id=="print" || id=="network" || id=="game") {
        // Labels and explanations are the firmware's own (sysconf_plugin); the features behind them are not native yet.
        const auto info=[&](const QString& key,const QString& fallback){return option(key,tr("msg_"+key,fallback),tr("msg_"+key+"_explanation",{}),{},Action::Unavailable);};
        static const QHash<QString,QPair<QString,QStringList>> pages{
            {"chat",{"msg_chat_setting",{"av_chat_start","audio_device_setting","camera_device_setting"}}},
            {"security",{"msg_security_setting",{"gameetc_pc"}}},
            {"remote",{"msg_remoteplay_setting",{"remoteplay_device_psp","remote_start"}}},
            {"print",{"msg_printer_settings",{"printer_selection","printer_manager"}}},
            {"network",{"msg_net_setting",{"internet_connect","internet_connect_setting","internet_connect_test","internetbrowser_setting"}}},
            {"game",{"msg_game_setting",{"game_other_feature"}}}};
        const auto& definition=pages[id];
        page.title=tr(definition.first,id);
        for(const auto& key:definition.second)page.entries.push_back(info(key,key));
        if(id=="network")page.entries.insert(2,option("netlist_current",tr("msg_netlist_current","Settings and Connection Status List"),
            tr("msg_netlist_current_explanation",{}),{},Action::Information,"host:network"));
    } else return;
    pages_.push_back(std::move(page));
}
void Shell::openChoices(const QString& key,const QString& title) {
    if(pages_.size()>=32)return;
    Page page;page.title=title;
    // host/ keys are read from the computer each time (another app may have changed them), never stored.
    const QVariant current=key=="host/volume"?QVariant(int(std::lround(host::masterVolume()/10.)*10)):
        key=="host/muted"?QVariant(host::masterMuted()):key=="media/repeatMode"?QVariant(mediaRepeatMode()):preferences_.value(key);
    for(const auto& choice:choices(key,assets_)) {
        auto entry=option(key+"/"+QString::number(page.entries.size()),choice.title,{},key,Action::SetSetting);
        entry.option=choice.value;
        if(current==choice.value)page.selection=int(page.entries.size());
        page.entries.push_back(std::move(entry));
    }
    if(key=="appearance/theme" && !theme_.path.isEmpty()) {
        auto entry=option(key+"/"+QString::number(page.entries.size()),theme_.name.isEmpty()?QFileInfo(theme_.path).completeBaseName():theme_.name,{},key,Action::SetSetting);
        entry.option=theme_.path;page.selection=int(page.entries.size());page.entries.push_back(std::move(entry));
    }
    pages_.push_back(std::move(page));
}
QString Shell::valueText(const Entry& entry) const {
    if(entry.setting=="media/repeatMode")return QStringList{"Off","Repeat One","Repeat All"}.value(mediaRepeatMode());
    if(entry.action==Action::SelectUser)return entry.value==profiles_.activeId()?QString(QChar(0x25cf)):QString{};
    if(entry.setting.isEmpty())return {};
    const auto value=preferences_.value(entry.setting);
    if(entry.action==Action::SetSetting)return value==entry.option?QString(QChar(0x25cf)):QString{};
    if(entry.setting=="system/name")return value.toString();
    if(entry.setting=="appearance/theme" && !theme_.path.isEmpty())return theme_.name.isEmpty()?QFileInfo(theme_.path).completeBaseName():theme_.name;
    for(const auto& choice:choices(entry.setting,assets_))if(choice.value==value)return choice.title;
    return {};
}
QString Shell::fontFamily()const{return assets_.fontFamily(preferences_.value("appearance/font").toInt());}
QDateTime Shell::clockDateTime()const {
    const auto zone=preferences_.value("clock/timeZone").toString();
    auto now=clockOverride_.isValid()?clockOverride_:QDateTime::currentDateTime();
    if(zone!="System")now=now.toTimeZone(QTimeZone(zone.toUtf8()));
    return now;
}
QString Shell::clockText()const {
    const auto now=clockDateTime();
    const QString date=QStringList{"d/M","M/d","yyyy/M/d"}.value(preferences_.value("clock/dateFormat").toInt(),"d/M");
    const QString time=preferences_.value("clock/hourFormat").toInt()?"h:mm AP":"HH:mm";
    return now.toString(date+"  "+time);
}
void Shell::chooseWallpaper(const QString& file) {
    if(file.isEmpty())return;
    QImageReader reader(file);
    if(!reader.canRead() || !reader.size().isValid()){message_="This image cannot be used as wallpaper.";emit changed();return;}
    preferences_.setValues({{"appearance/wallpaper",QFileInfo(file).absoluteFilePath()},{"appearance/background",2}});emit changed();
}
void Shell::chooseTheme(const QString& file) {
    if(file.isEmpty())return;
    QString error;
    if(P3t::load(file,&error).isNull()){message_=QFileInfo(file).fileName()+"\n\n"+error;emit changed();return;}
    // The theme's still background is shown through the wallpaper mode (View::loadWallpaper puts it before the user's own).
    preferences_.setValues({{"appearance/theme",QFileInfo(file).absoluteFilePath()},{"appearance/background",2}});emit changed();
}
void Shell::setSystemName(const QString& name){preferences_.setValue("system/name",name.trimmed());emit changed();}
void Shell::setFullscreen(bool enabled){preferences_.setValue("display/fullscreen",enabled);emit changed();}
}
