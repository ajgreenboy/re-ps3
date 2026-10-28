#include "shell.h"
#include "settings_menu.h"
#include "assets.h"
#include "host.h"
#include "p3t.h"
#include "xmbml_view.h"
#include <cmath>
#include <QTimeZone>
#include <QDateTime>
#include <QImageReader>
#include <QFileInfo>
#include <QFile>
#include <QSysInfo>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace xmb {
namespace {
struct Choice {QString title;QVariant value;};
// The console's Date Format and Time Format options (sysconf_plugin msg_datetime_fmt_* and msg_time_fmt_*).
// The short forms feed the information-panel clock, the full forms the Date and Time screen. Fields are
// zero-padded like the firmware's own option labels and its reference frame (25/10); the hour is not padded,
// which is the US clock convention ("1:05 PM", not "01:05 PM").
QString clockDate(int format) {return QStringList{"dd/MM","MM/dd","yyyy/MM/dd"}.value(format,"MM/dd");}
QString clockFullDate(int format) {return QStringList{"dd/MM/yyyy","MM/dd/yyyy","yyyy/MM/dd"}.value(format,"MM/dd/yyyy");}
QString clockTime(int format) {return format?"h:mm AP":"HH:mm";}
QString clockFullTime(int format) {return format?"h:mm:ss AP":"HH:mm:ss";}
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
    if(key=="display/screensaverMinutes")return {{"After 5 Minutes",5},{"After 10 Minutes",10},{"After 20 Minutes",20},{"After 30 Minutes",30},{"Do Not Use",0}};
    if(key=="power/idleMinutes")return {{"Off",0},{"15 minutes",15},{"30 minutes",30},{"1 hour",60},{"2 hours",120}};
    return {{"Off",false},{"On",true}};
}
// One row of a settings list page. The row class is this build's: the glass settings bullet
// (qgl/icontex "23n"), which the view draws at the firmware's 18 px/39 px settings-row size. The
// firmware's brass wrench disc (tex_sett) is the same footprint but a different size on screen
// (40/56 through the item-glyph path), so D21's texture change needs the view.cpp size hunk and is
// not part of a row-set change (audit/SETTINGS-FIRMWARE.md D21).
Entry option(QString id,QString title,QString detail,QString setting,Action action=Action::SettingChoices,QString value={}) {
    Entry entry{std::move(id),std::move(title),std::move(detail),"qgl/icontex","23n",action,std::move(value)};
    entry.setting=std::move(setting);entry.themeIcon="icon_setting_item";return entry;
}
// --- The firmware row sets (Phase 11.1) -----------------------------------------------------------
// audit/SETTINGS-FIRMWARE.md 5.2-5.16 lists each page's rows from sysconf_plugin's own sub-pages and
// chart pages. A row is described once, by the resource keys the firmware uses for its label and its
// explanation, plus what this build really does with it. There is deliberately no "msg_"+id synthesis
// (7.4): a row whose label key is not named here has no label, and the row is not invented.
struct RowSpec {
    const char* id=nullptr;          // the row's stable native id (page dumps and logs)
    const char* titleKey=nullptr;    // sysconf_plugin msg_*
    const char* infoKey=nullptr;     // the row's *_explanation, when the RCO has one
    Action action=Action::Unavailable;
    const char* setting=nullptr;     // the Preferences key a SettingChoices/EditName row writes
    const char* value=nullptr;       // the Information payload (host:network, host:display, ...)
    const char* literalTitle=nullptr;   // last-resort label, and the whole label of a native-only row
    const char* literalDetail=nullptr;  // native-only explanation (never used for a firmware row)
};
Entry rowEntry(const RowSpec& spec,Assets& assets) {
    const auto text=[&assets](const char* key){return key?assets.text("sysconf_plugin",QString::fromLatin1(key)):QString{};};
    const auto title=text(spec.titleKey);
    const auto detail=text(spec.infoKey);
    const auto literalTitle=spec.literalTitle?QString::fromLatin1(spec.literalTitle):QString{};
    const auto literalDetail=spec.literalDetail?QString::fromLatin1(spec.literalDetail):QString{};
    Entry entry=option(QString::fromLatin1(spec.id),title.isEmpty()?literalTitle:title,
        detail.isEmpty()?literalDetail:detail,spec.setting?QString::fromLatin1(spec.setting):QString{},
        spec.action,spec.value?QString::fromLatin1(spec.value):QString{});
    // Where the label came from, so the entry dump says which rows are the firmware's and which are this
    // build's own (Entry::source, audit/MENUS-XMBML.md 4).
    if(!title.isEmpty())entry.source=QStringLiteral("rco");
    else entry.source=literalTitle.isEmpty()?QString{}:QStringLiteral("builtin");
    return entry;
}
// The one-level settings pages, in the order the evidence gives: section 5.2-5.16 for the rows, and the
// 2026-10-28 capture batch (section 9.2) for the measured top of Video, Music, Security and Remote Play.
// A row this build can act on keeps its native action and Preferences key; a firmware row it cannot act
// on is listed as Action::Unavailable with the RCO's own explanation, which is the "list-unavailable"
// disposition (MENUS-XMBML 4): the page is the console's row set, not a subset of it.
const QVector<RowSpec>* firmwareRows(const QString& id) {
    static const QVector<RowSpec> theme{
        {"theme","msg_theme","msg_theme_explanation",Action::SettingChoices,"appearance/theme",nullptr,"Theme"},
        {"color","msg_color","msg_color_explanation",Action::SettingChoices,"appearance/colour",nullptr,"Color"},
        {"background","msg_background","msg_background_explanation",Action::SettingChoices,"appearance/background",nullptr,"Background"},
        {"font","msg_font","msg_font_explanation",Action::SettingChoices,"appearance/font",nullptr,"Font"}};
    static const QVector<RowSpec> date{
        {"datetime","msg_datetime","msg_datetime_explanation",Action::Information,nullptr,nullptr,"Date and Time"},
        {"autodate","msg_automatic_datetime_setting","msg_automatic_datetime_setting_explanation",Action::Unavailable,nullptr,nullptr,"Set Automatically"},
        {"date_fmt","msg_date_fmt","msg_date_fmt_explanation",Action::SettingChoices,"clock/dateFormat",nullptr,"Date Format"},
        {"time_fmt","msg_time_fmt","msg_time_fmt_explanation",Action::SettingChoices,"clock/hourFormat",nullptr,"Time Format"},
        {"timezone","msg_timezone","msg_timezone_explanation",Action::SettingChoices,"clock/timeZone",nullptr,"Time Zone"},
        {"summertime","msg_summertime","msg_summertime_explanation",Action::Unavailable,nullptr,nullptr,"Daylight Saving"}};
    static const QVector<RowSpec> system{
        {"console_lang","msg_console_lang","msg_console_lang_explanation",Action::Unavailable,nullptr,nullptr,"System Language"},
        {"name","msg_ps3_name","msg_ps3_name_explanation",Action::EditName,"system/name",nullptr,"System Name"},
        {"disc_utility","msg_disc_utility","msg_disc_utility_explanation",Action::Unavailable,nullptr,nullptr,"Format Utility"},
        {"backup_utility","msg_backup_utility","msg_backup_utility_explanation",Action::Unavailable,nullptr,nullptr,"Backup Utility"},
        {"init_setting","msg_init_setting","msg_init_setting_explanation",Action::Unavailable,nullptr,nullptr,"Restore Default Settings"},
        {"init_console","msg_init_console","msg_init_console_explanation",Action::Unavailable,nullptr,nullptr,"Restore PS3 System"},
        {"ex_os_install","msg_ex_os_install","msg_ex_os_install_explanation",Action::Unavailable,nullptr,nullptr,"Install Other OS"},
        {"information","msg_console_info","msg_console_info_explanation",Action::Information,nullptr,nullptr,"System Information"},
        {"aboutps3","msg_aboutps3","msg_aboutps3_explanation",Action::Unavailable,nullptr,nullptr,"About PlayStation 3"}};
    static const QVector<RowSpec> sound{
        {"sound_output_setting","msg_sound_output_setting","msg_sound_output_setting_explanation",Action::Information,nullptr,"host:audio","Audio Output Settings"},
        {"audio_multi_output","msg_audio_multi_output","msg_audio_multi_output_explanation",Action::Unavailable,nullptr,nullptr,"Audio Multi-Output"},
        {"keytone","msg_sound_key","msg_sound_key_explanation",Action::SettingChoices,"audio/keyTone",nullptr,"Key Tone"}};
    static const QVector<RowSpec> display{
        {"display_output_setting","msg_display_output_setting","msg_display_output_setting_explanation",Action::Information,nullptr,"host:display","Video Output Settings"},
        {"screensaver","msg_screensaver","msg_screensaver_explanation",Action::SettingChoices,"display/screensaverMinutes",nullptr,"Screensaver"},
        {"cross_color_filter","msg_cross_color_filter","msg_cross_color_filter_explanation",Action::Unavailable,nullptr,nullptr,"Cross Color Reduction Filter"},
        {"rgb_full_range","msg_rgb_full_range_hdmi","msg_rgb_full_range_hdmi_explanation",Action::Unavailable,nullptr,nullptr,"RGB Full Range (HDMI)"},
        {"super_white","msg_ycbcr_super_white_hdmi","msg_ycbcr_super_white_hdmi_explanation",Action::Unavailable,nullptr,nullptr,"Y Pb/Cb Pr/Cr Super-White (HDMI)"},
        {"deep_color","msg_deep_color_output_hdmi","msg_deep_color_output_hdmi_explanation",Action::Unavailable,nullptr,nullptr,"Deep Color Output (HDMI)"}};
    static const QVector<RowSpec> security{
        {"pw_change","msg_pw_change","msg_pw_change_explanation",Action::Unavailable,nullptr,nullptr,"Change Password"},
        {"bd_pc","msg_bd_pc","msg_bd_pc_explanation",Action::Unavailable,nullptr,nullptr,"BD - Parental Control"},
        {"bd_dvd_pcregion","msg_bd_dvd_use_pcregion","msg_bd_dvd_use_pcregion_explanation",Action::Unavailable,nullptr,nullptr,"BD/DVD - Parental Control Region Code"},
        {"dvd_pc","msg_dvd_pc","msg_dvd_pc_explanation",Action::Unavailable,nullptr,nullptr,"DVD - Parental Control"},
        {"gameetc_pc","msg_gameetc_pc","msg_gameetc_pc_explanation",Action::Unavailable,nullptr,nullptr,"Parental Control"},
        {"pc_browser","msg_pc_browser","msg_pc_browser_explanation",Action::Unavailable,nullptr,nullptr,"Internet Browser Start Control"}};
    // Remote Play's five rows are the measured ones (capture batch 2026-10-28, SETTINGS 9.2); the
    // registration/list/custom-SSID rows open their own pages in the firmware and have no native action.
    static const QVector<RowSpec> remote{
        {"device_registration","msg_device_registration","msg_device_registration_explanation",Action::Unavailable,nullptr,nullptr,"Register Device"},
        {"device_registration_list","msg_device_registration_list","msg_device_registration_list_explanation",Action::Unavailable,nullptr,nullptr,"Registered Device List"},
        {"custom_connection_setting","msg_custom_connection_setting","msg_custom_connection_setting_explanation",Action::Unavailable,nullptr,nullptr,"Custom Connection Settings"},
        {"remote_start","msg_remote_start","msg_remote_start_explanation",Action::Unavailable,nullptr,nullptr,"Remote Start"},
        {"audio_output_device","msg_audio_output_device","msg_audio_output_device_explanation",Action::Unavailable,nullptr,nullptr,"Audio Output Device"}};
    // Video and Music: the measured top of each page (capture batch 2026-10-28, SETTINGS 9.2). Video's
    // rows below the seventh (the remaining DVD/HDD/upscaler rows) are still unmeasured, so they are not
    // invented here; Music stops at five rows in the still with no sixth row before the frame edge.
    static const QVector<RowSpec> video{
        {"bd_menu_lang","msg_bd_menu_lang","msg_bd_menu_lang_explanation",Action::Unavailable,nullptr,nullptr,"BD - Menu Language"},
        {"bd_audio_lang","msg_bd_audio_lang","msg_bd_audio_lang_explanation",Action::Unavailable,nullptr,nullptr,"BD - Audio Language"},
        {"bd_subtitle_lang","msg_bd_subtitle_lang","msg_bd_subtitle_lang_explanation",Action::Unavailable,nullptr,nullptr,"BD - Subtitle Language"},
        {"bd_internet_connection","msg_bd_internet_connection","msg_bd_internet_connection_explanation",Action::Unavailable,nullptr,nullptr,"BD - Internet Connection"},
        {"dvd_menu_lang","msg_dvd_menu_lang","msg_dvd_menu_lang_explanation",Action::Unavailable,nullptr,nullptr,"DVD - Menu Language"},
        {"dvd_audio_lang","msg_dvd_audio_lang","msg_dvd_audio_lang_explanation",Action::Unavailable,nullptr,nullptr,"DVD - Audio Language"},
        {"dvd_subtitle_lang","msg_dvd_subtitle_lang","msg_dvd_subtitle_lang_explanation",Action::Unavailable,nullptr,nullptr,"DVD - Subtitle Language"}};
    static const QVector<RowSpec> music{
        {"import_musiccd","msg_import_musiccd","msg_import_musiccd_explanation",Action::Unavailable,nullptr,nullptr,"Audio CD Import"},
        {"crossfade_play","msg_crossfade_play","msg_crossfade_play_explanation",Action::Unavailable,nullptr,nullptr,"Crossfade Playback"},
        {"output_frequency","msg_output_frequency","msg_output_frequency_explanation",Action::Unavailable,nullptr,nullptr,"Output Frequency"},
        {"bitmapping","msg_bitmapping_sacd","msg_bitmapping_sacd_explanation",Action::Unavailable,nullptr,nullptr,"Bitmapping"},
        {"dnormal","msg_music_dnormal","msg_music_dnormal_explanation",Action::Unavailable,nullptr,nullptr,"Dynamic Normalizer"}};
    static const QVector<RowSpec> print{
        {"printer_selection","msg_printer_selection","msg_printer_selection_explanation",Action::Unavailable,nullptr,nullptr,"Printer Selection"},
        {"printer_manager","msg_printer_manager","msg_printer_manager_explanation",Action::Unavailable,nullptr,nullptr,"Printer Manager"}};
    static const QVector<RowSpec> power{
        {"idle","msg_system_auto_power_off","msg_system_auto_power_off_explanation",Action::SettingChoices,"power/idleMinutes",nullptr,"System Auto-Off"},
        {"controller","msg_controller_auto_power_off","msg_controller_auto_power_off_explanation",Action::Unavailable,nullptr,nullptr,"Controller Auto-Off"}};
    static const QVector<RowSpec> chat{
        {"av_chat_start","msg_av_chat_start","msg_av_chat_start_explanation",Action::Unavailable,nullptr,nullptr,"Voice/Video Chat Start"},
        {"audio_device_setting","msg_audio_device_setting","msg_audio_device_setting_explanation",Action::Unavailable,nullptr,nullptr,"Audio Device Settings"},
        {"camera_device_setting","msg_camera_device_setting","msg_camera_device_setting_explanation",Action::Unavailable,nullptr,nullptr,"Camera Device Settings"}};
    if(id=="theme")return &theme;
    if(id=="date")return &date;
    if(id=="system")return &system;
    if(id=="sound")return &sound;
    if(id=="display")return &display;
    if(id=="security")return &security;
    if(id=="remote")return &remote;
    if(id=="print")return &print;
    if(id=="power")return &power;
    if(id=="chat")return &chat;
    if(id=="video")return &video;
    if(id=="music")return &music;
    return nullptr;
}
// Rows the firmware's page does not have. They stay only where this build's own feature would otherwise
// lose its only surface; each one is marked in the dump and in SETTINGS-FIRMWARE.md section 9.
const QVector<RowSpec>* nativeOnlyRows(const QString& id) {
    // D11: the firmware folds theme and wallpaper selection into the Theme and Background choice pages,
    // which this build cannot bind yet (the choice page class is Phase 11.3), so the two pickers stay as
    // native-only rows at the end of the page. Removing them would delete the only way to load a .p3t
    // theme or a wallpaper.
    static const QVector<RowSpec> theme{
        {"themefile",nullptr,nullptr,Action::Theme,nullptr,nullptr,"Select Theme",
            "Opens a PS3 theme file (.p3t): its background and icons replace the system's."},
        {"wallpaper",nullptr,nullptr,Action::Wallpaper,nullptr,nullptr,"Select Wallpaper",
            "Selects a local image for the XMB background."}};
    // D6/D7: the firmware's Video and Music pages are BD/DVD and audio-CD rows (listed above from the
    // measured batch); neither drive nor decoder exists in this build, so every one of them is an
    // explained refusal. These three are this build's own media settings and stay as native-only rows
    // at the end of the page rather than being dropped - they are the page's only working rows.
    static const QVector<RowSpec> media{
        {"repeat",nullptr,nullptr,Action::SettingChoices,"media/repeatMode",nullptr,"Repeat",
            "Stops at the end, repeats one file, or repeats the current folder queue."},
        {"shuffle",nullptr,nullptr,Action::SettingChoices,"media/shuffle",nullptr,"Shuffle",
            "Plays every file in a random order before starting another cycle."},
        {"volume",nullptr,nullptr,Action::SettingChoices,"audio/volume",nullptr,"Volume",
            "Sets native media playback volume."}};
    if(id=="theme")return &theme;
    if(id=="music" || id=="video")return &media;
    return nullptr;
}
// A page's rows, plus what the definition said and what was left out: one record the app logs, dumps and
// the audit can cite.
struct PageRows {
    QString id,title;
    QVector<Entry> entries;
    QStringList notes;      // "key: reason", one per refused row
    QStringList nativeOnly; // ids of the rows in this build and not in the firmware's page
    QJsonArray firmware;    // the definition's own rows (or the RCO sub-page set), row for row
};
// --- The three two-level pages: Game, Accessory, Network -------------------------------------------
// audit/SETTINGS-FIRMWARE.md 1.1/5.1/5.9/5.15: in the definition each of the three is a child=segment
// row whose page lists the rows of seg_<name> -> seg_*_fixed_upper|middle|lower (+ the USB-headset,
// keyboard-input and DLNA groups). resolveSegmentView() reads that from category_sysconf.xml; this table
// only says what this build does with each firmware row. A row with no binding is a listed refusal with
// the RCO's own explanation, which is what the console's conditional rows become on a PC.
PageRows segmentRows(const QString& id,Assets& assets) {
    static const QHash<QString,QString> views{{"game","seg_game"},{"device","seg_device"},{"network","seg_network"}};
    static const QHash<QString,QString> titles{{"game","msg_game_setting"},{"device","msg_pd_setting"},{"network","msg_net_setting"}};
    struct Binding { Action action; const char* setting; const char* value; };
    // The one firmware row of these pages this build can really perform: the network status list, which
    // is the host's own report (audit/SETTINGS-FIRMWARE.md 5.15 lists it first in the definition).
    static const QHash<QString,Binding> bindings{{"net_netlist",{Action::Information,nullptr,"host:network"}}};
    // Rows the definition binds in a group this build cannot reach: the wireless-headset row only exists
    // when a Sony wireless stereo headset is connected, and the kana group's keybind row only exists for
    // Japanese input. Both would be permanently dead rows, so they are left out and the reason logged.
    static const QHash<QString,QString> skipped{
        {"pd_usbhdset_setting","the row only exists when a Sony wireless stereo headset is connected"},
        {"pd_keybind","Japanese kana input only"}};
    PageRows data;data.id=id;
    const auto title=assets.text("explore_category_sysconf",titles.value(id));
    data.title=title.isEmpty()?id:title;
    const bool verbose=qEnvironmentVariableIsSet("XMB_XMBML_LOG") && qEnvironmentVariable("XMB_XMBML_LOG")!=QStringLiteral("0");
    XmbmlResolveOptions options;
    options.flashRoot=assets.root();
    // The page rows' labels are sysconf_plugin's (1985 entries); a key the category RCO also carries is
    // resolved from sysconf_plugin first, because that is the table the settings pages' labels live in.
    options.container=QStringLiteral("sysconf_plugin");
    const auto column=resolveSegmentView(QStringLiteral("category_sysconf"),views.value(id),
        [&assets](const QString& container,const QString& key){
            const auto value=assets.text(container,key);
            if(!value.isEmpty())return value;
            return assets.text("explore_category_sysconf",key);
        },options);
    if(!column.ok()) {
        qWarning().noquote()<<"settings:"<<id<<"segment page cannot be resolved:"<<column.error<<column.path;
        data.notes<<QStringLiteral("definition: ")+column.error;
        return data;
    }
    QSet<QString> seen;
    for(const auto& row:column.rows) {
        const auto key=row.itemKey.isEmpty()?row.queryKey:row.itemKey;
        if(const auto why=skipped.constFind(key);why!=skipped.cend()) {
            data.notes<<key+QStringLiteral(": ")+*why;
            if(verbose)qInfo().noquote()<<"settings:"<<id<<"skip"<<key<<"("<<*why<<")";
            continue;
        }
        if(seen.contains(key)) {
            // pd_keyboard_input is bound by both mutually exclusive keyboard-input groups; one row.
            data.notes<<key+QStringLiteral(": bound by two mutually exclusive groups, listed once");
            continue;
        }
        seen.insert(key);
        if(row.title.isEmpty()) {
            data.notes<<key+QStringLiteral(": no title in sysconf_plugin, left out");
            qWarning().noquote()<<"settings:"<<id<<"row"<<key<<"has no title";
            continue;
        }
        const auto binding=bindings.constFind(key);
        Entry entry=option(row.id,row.title,row.detail,
            binding==bindings.cend()?QString{}:QString::fromLatin1(binding->setting),
            binding==bindings.cend()?Action::Unavailable:binding->action,
            binding==bindings.cend()?QString{}:QString::fromLatin1(binding->value));
        entry.source=fieldSourceName(row.titleSource);
        data.firmware.push_back(QJsonObject{{"id",row.id},{"key",key},{"group",row.queryKey},
            {"title",row.title},{"titleRsc",row.titleRsc},{"infoRsc",row.infoRsc},{"iconRsc",row.iconRsc},
            {"action",int(entry.action)}});
        if(verbose)qInfo().noquote()<<"settings:"<<id<<"row"<<entry.id<<"group="+row.queryKey
            <<"title=\""+entry.title+"\""<<"icon="+row.iconRsc<<"action="+QString::number(int(entry.action));
        data.entries.push_back(std::move(entry));
    }
    qInfo().noquote()<<QStringLiteral("settings: %1 segment page resolved %2 rows from %3 "
        "(groups %4, sha256 %5): listed %6, left out %7, rco titles %8")
        .arg(id).arg(int(data.entries.size())).arg(QFileInfo(column.path).fileName()).arg(column.queries)
        .arg(column.sha256.left(12)).arg(int(data.entries.size())).arg(data.notes.size()).arg(column.rcoRows);
    return data;
}
PageRows pageRows(const QString& id,Assets& assets,const QString& dateTimeInfo) {
    if(id=="game"||id=="device"||id=="network")return segmentRows(id,assets);
    PageRows data;data.id=id;
    static const QHash<QString,QString> pageTitles{
        {"theme","msg_theme_settings"},{"date","msg_datetime_setting"},{"system","msg_console_setting"},
        {"sound","msg_sound_setting"},{"display","msg_display_setting"},{"security","msg_security_setting"},
        {"remote","msg_remoteplay_setting"},{"print","msg_printer_settings"},{"power","msg_power_saving_setting"},
        {"chat","msg_chat_setting"},{"video","msg_video_setting"},{"music","msg_music_setting"}};
    const auto* rows=firmwareRows(id);
    const auto* extra=nativeOnlyRows(id);
    // A page may carry only native-only rows (none does today) or only firmware rows; both are pages.
    if((!rows && !extra) || !pageTitles.contains(id))return data;
    const auto title=assets.text("sysconf_plugin",pageTitles.value(id));
    data.title=title.isEmpty()?id:title;
    if(rows)for(const auto& spec:*rows) {
        Entry entry=rowEntry(spec,assets);
        // The Date and Time row's own page carries the live clock readout (the firmware opens a set page
        // there; this build reports the host clock it follows - SETTINGS 5.7).
        if(id=="date" && entry.id=="datetime")entry.value=dateTimeInfo;
        data.firmware.push_back(QJsonObject{{"id",QString::fromLatin1(spec.id)},
            {"titleRsc",spec.titleKey?QString::fromLatin1(spec.titleKey):QString{}},
            {"infoRsc",spec.infoKey?QString::fromLatin1(spec.infoKey):QString{}},
            {"action",int(entry.action)}});
        data.entries.push_back(std::move(entry));
    }
    if(extra)for(const auto& spec:*extra) {
        Entry entry=rowEntry(spec,assets);
        data.nativeOnly<<entry.id;
        data.entries.push_back(std::move(entry));
    }
    qInfo().noquote()<<QStringLiteral("settings: %1 page from the RCO row set: %2 rows, %3 native-only")
        .arg(id).arg(int(data.entries.size())).arg(data.nativeOnly.size());
    return data;
}
// Verification hook (the same XMB_DUMP_ENTRIES the shell's catalogue/folder/store dumps use): every
// settings page opened in a scripted run writes its row list, so a capture can be checked without pixels.
void dumpSettingsPage(const PageRows& data) {
    if(!qEnvironmentVariableIsSet("XMB_DUMP_ENTRIES"))return;
    QJsonArray rows;
    for(const auto& entry:data.entries) {
        rows.push_back(QJsonObject{{"id",entry.id},{"title",entry.title},{"detail",entry.detail},
            {"action",int(entry.action)},{"setting",entry.setting},{"value",entry.value.left(80)},
            {"texture",entry.texture},{"source",entry.source},
            {"nativeOnly",data.nativeOnly.contains(entry.id)}});
    }
    QJsonObject object{{"page",data.id},{"title",data.title},{"count",int(data.entries.size())},
        {"rows",rows},{"firmware",data.firmware},
        {"skipped",QJsonArray::fromStringList(data.notes)}};
    QFile file(qEnvironmentVariable("XMB_DUMP_ENTRIES")+".settings-"+data.id+".json");
    if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(object).toJson());
    else qWarning().noquote()<<"settings: cannot write the"<<data.id<<"page dump";
}
} // namespace

// The Settings column, resolved from the firmware's own definition. See settings_menu.h for the contract;
// audit/SETTINGS-FIRMWARE.md 1.1-1.2 and 9 list what each row is and what was omitted.
QVector<Entry> settingsColumnEntries(const QVector<Entry>& builtin,Assets& assets) {
    QHash<QString,Entry> byId;
    for(const auto& entry:builtin)byId.insert(entry.id,entry);
    const bool verbose=qEnvironmentVariableIsSet("XMB_XMBML_LOG") && qEnvironmentVariable("XMB_XMBML_LOG")!=QStringLiteral("0");
    XmbmlResolveOptions options;
    options.flashRoot=assets.root();
    // The ids this build already uses for these pages (and for their theme icons), so every hunk that
    // addresses a settings row by id keeps working (MENUS-XMBML 4.2).
    options.idAliases={{"console","system"},{"bd_dvd","video"},{"date_time","date"}};
    QElapsedTimer timer;timer.start();
    const auto column=resolveCategoryColumn(QStringLiteral("category_sysconf"),
        [&assets](const QString& container,const QString& key){
            const auto value=assets.text(container,key);
            if(!value.isEmpty())return value;
            return assets.text("sysconf_plugin",key);
        },options);
    if(!column.ok()) {
        qWarning().noquote()<<"xmbml: settings column keeps its built-in rows:"<<column.error<<column.path;
        return {};
    }
    // The two rows a retail console does not show (SETTINGS 1.2, MENUS-XMBML 3.1): the console gates them
    // in the sysconf plugin - the Japan-only Edy Viewer and the QA Debug Settings - so this build omits
    // them and logs the reason for each.
    static const QHash<QString,QString> omitted{
        {"edy","the Japan-only Edy Viewer: the sysconf plugin gates it and this build has no page for it"},
        {"console_qa","Debug Settings: QA consoles only"}};
    QVector<Entry> rows;
    int skipped=0;
    for(const auto& row:column.rows) {
        const auto key=row.itemKey.isEmpty()?row.queryKey:row.itemKey;
        if(const auto why=omitted.constFind(key);why!=omitted.cend()) {
            qInfo().noquote()<<"xmbml: settings column omits"<<key<<"("<<*why<<")";
            ++skipped;continue;
        }
        if(row.title.isEmpty()) {
            qWarning().noquote()<<"xmbml: settings row"<<key<<"has no title (no title_rsc, no fallback)";
            ++skipped;continue;
        }
        const auto found=byId.constFind(row.id);
        Entry entry=found==byId.cend()?Entry{}:*found;
        entry.id=row.id;
        entry.title=row.title;
        // fo_lbl2_alpha=0 on the three child=segment folder rows: a folder is not a setting, so its row
        // carries no second label line. Every other row keeps the definition's info_rsc (D3).
        entry.detail=row.hasSecondLabel()?row.detail:QString{};
        // The definition's own icon (tex_* in explore_category_sysconf). The qgl/icontex normal-map
        // override that used to replace every settings icon is gone (D1).
        if(!row.icon.isEmpty())entry.texture=row.icon;
        entry.container=column.container;
        entry.source=fieldSourceName(row.titleSource);
        if(found==byId.cend()) {
            // A firmware row with no page in this build: listed and inert rather than dropped.
            entry.action=Action::Unavailable;entry.setting.clear();entry.value.clear();
            qWarning().noquote()<<"xmbml: settings row"<<row.id<<"has no native page; listed as inert";
        }
        if(verbose)qInfo().noquote()<<"xmbml: settings row"<<entry.id<<"key="+key
            <<"title=\""+entry.title+"\""<<"texture="+entry.texture<<"source="+entry.source
            <<"segment="+(row.segment()?QStringLiteral("true"):QStringLiteral("false"))
            <<"secondLabel="+(row.hasSecondLabel()?QStringLiteral("true"):QStringLiteral("false"))
            <<"barAction="+(row.barAction.isEmpty()?QStringLiteral("-"):row.barAction);
        rows.push_back(std::move(entry));
    }
    if(rows.isEmpty()) {
        qWarning().noquote()<<"xmbml: settings column has no resolvable row; keeping the built-in rows";
        return {};
    }
    qInfo().noquote()<<QStringLiteral("xmbml: settings column resolved %1 rows from %2 (queries %3, "
        "expanded %4, sha256 %5): listed %6, omitted %7, rco titles %8 in %9 ms")
        .arg(int(rows.size())).arg(QFileInfo(column.path).fileName()).arg(column.queries).arg(column.expanded)
        .arg(column.sha256.left(12)).arg(int(rows.size())).arg(skipped).arg(column.rcoRows).arg(timer.elapsed());
    return rows;
}

void Shell::openSettings(const QString& id) {
    if(pages_.size()>=32)return;
    const auto tr=[&](const QString& key,const QString& fallback){return assets_.text("sysconf_plugin",key,fallback);};
    Page page;
    if(id=="update") { // System Update (software_update_plugin). Not in sysconf_plugin.rco: SETTINGS 5.16/U8.
        const auto su=[&](const QString& key,const QString& fallback){return assets_.text("software_update_plugin",key,fallback);};
        page.title=tr("msg_update","System Update");
        page.entries={option("net",su("msg_update_net","Update via Internet"),{},{},Action::Information,
                su("msg_update_net","Update via Internet")+"\n\nThe native XMB is updated with its own repository; there is no PS3 system software to download."),
            option("media",su("msg_update_media","Update via Storage Media"),{},{},Action::Unavailable)};
    } else if(id=="photo") { // This build's Photo Settings page: the sysconf inventory has no such page.
        page.title="Photo Settings";
        page.entries={option("interval","Slideshow Interval","Sets how long each photo remains on screen.","photo/interval"),
            option("fit","Display Mode","Fits the whole photo or fills the display area.","photo/fit"),
            option("repeat","Repeat","Restarts a slideshow after the final photo.","media/repeat")};
    } else if(id=="messages") { // Friends > Message Box (category_friend.xml)
        const auto ft=[&](const QString& key,const QString& fallback){return assets_.text("explore_category_friend",key,fallback);};
        const auto none=ft("msg_error_no_msg","There are no messages.");
        page.title=ft("msg_msg_box","Message Box");
        auto received=option("received",ft("msg_received_box","Received"),{},{},Action::Information,none);received.container="explore_category_friend";received.texture="item_tex_InBox";received.themeIcon="icon_mbox_received";
        auto sent=option("sent",ft("msg_sent_box","Sent"),{},{},Action::Information,none);sent.container="explore_category_friend";sent.texture="item_tex_OutBox";sent.themeIcon="icon_mbox_sent";
        auto create=option("create",ft("msg_new_msg","Create Message"),{},{},Action::Unavailable);create.container="explore_category_friend";create.texture="item_tex_NewMessage";create.themeIcon="icon_mbox_create";
        page.entries={received,sent,create};
    } else {
        // Every other settings page: the firmware's row set (SETTINGS 5.2-5.16), plus the three segment
        // pages whose rows come from seg_game/seg_device/seg_network.
        const auto dateTimeInfo=[this] {
            const auto value=clockDateTime().toString(clockFullDate(preferences_.value("clock/dateFormat").toInt())
                +" "+clockFullTime(preferences_.value("clock/hourFormat").toInt()));
            return QStringLiteral("Date and Time\n\n")+value+
                QStringLiteral("\n\nThe clock follows the host system. Display format and time zone can be changed within XMB.");
        };
        auto data=pageRows(id,assets_,dateTimeInfo());
        if(data.entries.isEmpty())return; // an id with no page: the caller's row opens nothing
        page.title=data.title;
        // Dump before the rows move into the page, so the file is what the page will show.
        dumpSettingsPage(data);
        page.entries=std::move(data.entries);
    }
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
    const int dateFormat=preferences_.value("clock/dateFormat").toInt();
    const int hourFormat=preferences_.value("clock/hourFormat").toInt();
    // One space between date and time: the reference frame's word gap is 14 px at this 27 px size, which is
    // a single space, not two (audit/TEXT-RENDERER.md section 5, audit/INFO-BAR.md).
    return now.toString(clockDate(dateFormat)+" "+clockTime(hourFormat));
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
