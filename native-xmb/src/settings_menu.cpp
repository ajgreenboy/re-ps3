#include "shell.h"
#include "assets.h"
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
    if(key=="audio/volume") {QVector<Choice> result;for(int i=0;i<=100;i+=10)result.push_back({QString::number(i)+"%",i});return result;}
    if(key=="photo/interval")return {{"5 seconds",5000},{"10 seconds",10000},{"15 seconds",15000},{"30 seconds",30000}};
    if(key=="photo/fit")return {{"Fit",0},{"Fill",1}};
    if(key=="power/idleMinutes")return {{"Off",0},{"15 minutes",15},{"30 minutes",30},{"1 hour",60},{"2 hours",120}};
    return {{"Off",false},{"On",true}};
}
Entry option(QString id,QString title,QString detail,QString setting,Action action=Action::SettingChoices,QString value={}) {
    Entry entry{std::move(id),std::move(title),std::move(detail),"qgl/icontex","23n",action,std::move(value)};
    entry.setting=std::move(setting);return entry;
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
        page.entries={option("theme",tr("msg_theme","Theme"),tr("msg_theme_explanation",{}),{},Action::Unavailable),
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
        page.title=tr("msg_console_setting","System Settings");
        page.entries={option("name","System Name","Sets the name of this native XMB instance.","system/name",Action::EditName),
            option("information","System Information",{}, {},Action::Information),
            option("language","System Language","Additional original language resource tables are not implemented yet.",{},Action::Unavailable)};
    } else if(id=="sound") {
        page.title=tr("msg_sound_setting","Sound Settings");
        page.entries={option("volume","Volume","Sets the volume for media and menu sounds.","audio/volume"),
            option("mute","Mute","Mutes native XMB audio.","audio/muted"),
            option("keytone","Key Tone","Plays the original XMB sounds during navigation.","audio/keyTone")};
    } else if(id=="music" || id=="video") {
        page.title=id=="music"?"Music Settings":"Video Settings";
        page.entries={option("repeat","Repeat","Repeats the current media when playback reaches its end.","media/repeat"),
            option("volume","Volume","Sets native media playback volume.","audio/volume")};
    } else if(id=="photo") {
        page.title="Photo Settings";
        page.entries={option("interval","Slideshow Interval","Sets how long each photo remains on screen.","photo/interval"),
            option("fit","Display Mode","Fits the whole photo or fills the display area.","photo/fit"),
            option("repeat","Repeat","Restarts a slideshow after the final photo.","media/repeat")};
    } else if(id=="display") {
        page.title=tr("msg_display_setting","Display Settings");
        page.entries={option("fullscreen","Full Screen","Expands the native XMB window to fill the display.","display/fullscreen")};
    } else if(id=="power") {
        page.title=tr("msg_power_saving_setting","Power Save Settings");
        page.entries={option("idle","System Auto-Off","Closes native XMB after inactivity. Media playback keeps it awake; the host computer remains running.","power/idleMinutes"),
            option("controller","Controller Auto-Off","Controller hardware power management is not available through XInput.",{},Action::Unavailable)};
    } else if(id=="device") {
        page.title="Accessory Settings";
        page.entries={option("controller","Controller Settings","XInput D-pad, A and B control navigation when this window has focus.",{},Action::Information,
            "Controller Settings\n\nXInput controller 1\nD-pad: Navigate\nA: Enter\nB: Back\n\nKeyboard: Arrow keys, Enter, Backspace\nPage Up / Down: Move ten items"),
            option("bluetooth","Manage Bluetooth Devices","Native Bluetooth pairing is not implemented.",{},Action::Unavailable)};
    } else return;
    pages_.push_back(std::move(page));
}
void Shell::openChoices(const QString& key,const QString& title) {
    if(pages_.size()>=32)return;
    Page page;page.title=title;
    for(const auto& choice:choices(key,assets_)) {
        auto entry=option(key+"/"+QString::number(page.entries.size()),choice.title,{},key,Action::SetSetting);
        entry.option=choice.value;
        if(preferences_.value(key)==choice.value)page.selection=int(page.entries.size());
        page.entries.push_back(std::move(entry));
    }
    pages_.push_back(std::move(page));
}
QString Shell::valueText(const Entry& entry) const {
    if(entry.action==Action::SelectUser)return entry.value==profiles_.activeId()?QString(QChar(0x25cf)):QString{};
    if(entry.setting.isEmpty())return {};
    const auto value=preferences_.value(entry.setting);
    if(entry.action==Action::SetSetting)return value==entry.option?QString(QChar(0x25cf)):QString{};
    if(entry.setting=="system/name")return value.toString();
    for(const auto& choice:choices(entry.setting,assets_))if(choice.value==value)return choice.title;
    return {};
}
QString Shell::fontFamily()const{return assets_.fontFamily(preferences_.value("appearance/font").toInt());}
QString Shell::clockText()const {
    const auto zone=preferences_.value("clock/timeZone").toString();
    auto now=QDateTime::currentDateTime();
    if(zone!="System")now=now.toTimeZone(QTimeZone(zone.toUtf8()));
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
void Shell::setSystemName(const QString& name){preferences_.setValue("system/name",name.trimmed());emit changed();}
void Shell::setFullscreen(bool enabled){preferences_.setValue("display/fullscreen",enabled);emit changed();}
}
